#include "serialconfig.h"

#include <LittleFS.h>
#include <ArduinoJson.h>
#include <cstring>
#include <string>

#include <SerialCmd.h>
#include <ConsoleConfig.h>

#include "consoleconfig_store.h"
#include "consoles.h"
#include "network.h"

// ---------- module state ----------

namespace {

// The protocol. ~9 KB of fixed buffers, so it is a file-static rather
// than something constructed per call.
retroroom_core::SerialCmd parser;

// Open once and never closed. See serialcmd_isInteractive() for why
// that is the shape.
bool interactive = false;

// Long enough for the host's write to drain and the operator to see
// "ok" before the lights go out. The same shape as the pending-reboot
// deadlines in network.cpp, and for the same reason: a write is
// asynchronous with respect to the wire, and restarting from inside the
// handler truncates the last thing the operator was told.
constexpr unsigned long kRebootDelayMs = 1500;

// millis() at which a finished PUT or RESET should restart the board.
unsigned long rebootAtMs = 0;

// Write `text` and make sure it ended up newline-terminated. The
// instruction screen brings its own trailing newline; everything else
// does not, and a reply that runs into the next line of output is
// worse than a stray blank line.
void reply(const char* text) {
	Serial.print(text);
	const std::size_t n = std::strlen(text);
	if (n == 0 || text[n - 1] != '\n') {
		Serial.println();
	}
}

// One line of live state. The heartbeat is off in this mode, so STATUS
// is the only way to see any of it -- which is the point of having it.
void printStatus() {
	Serial.print("status: consoles=");
	Serial.print(HowManyConsoles());
	Serial.print(" idx=");
	Serial.print(currentConsoleIndex);
	Serial.print(" config=");
	Serial.print(consoleConfigIsUploaded() ? "uploaded" : "shipped-default");
	Serial.print(" net=");
	if (network_disabled()) {
		Serial.print("off");
	} else {
		Serial.print(network_inStaMode() ? "sta" : "ap");
	}
	Serial.print(" uptime=");
	Serial.print((unsigned long)millis() / 1000UL);
	Serial.println("s");
}

// Hand the host whatever the cabinet is running, length-prefixed.
//
// Both cases send a config. When nothing is on flash that is the
// built-in default rather than a comment saying there is nothing: a
// caller asking "what is this set to?" gets a real answer, and gets
// something they can edit and PUT back as a starting point. The header
// says which of the two it is, so a client can tell an operator's own
// config from the factory one without parsing the body.
void sendConfig() {
	retroroom_store::ConsoleConfigSource cfg;
	if (!retroroom_store::loadConsoleConfigOrDefault(cfg)) {
		reply("err: no config on flash and no built-in default");
		return;
	}
	const char* origin = cfg.fromFlash
	                         ? "from flash"
	                         : "built-in default, nothing on flash";
	// Length-prefixed, then raw bytes. A config contains newlines, so
	// it cannot be framed by them, and a client reading to a
	// terminator would otherwise have to know the document's shape.
	Serial.print("# config ");
	Serial.print((unsigned long)cfg.json.size());
	Serial.print(" bytes (");
	Serial.print(origin);
	Serial.println("):");
	Serial.write(reinterpret_cast<const uint8_t*>(cfg.json.data()),
	             cfg.json.size());
	Serial.println();
	reply("ok");
}

// Validate, save, and queue a restart. The same three steps the HTTP
// POST handler takes, in the same order, for the same reasons -- most
// of all validate-before-write, so a bad paste never reaches flash and
// the two rolling backups are never rotated by a document nobody can
// parse.
void acceptConfig(const char* body, std::size_t length) {
	if (!retroroom_store::ensureMounted()) {
		reply("err: LittleFS mount failed; nothing saved");
		return;
	}
	const std::string payload(body, length);
	retroroom_core::LoadResult parsed;
	if (!retroroom_core::validateConsoleConfigJson(payload, parsed)) {
		Serial.print("err: rejected: ");
		Serial.println(parsed.error.c_str());
		reply("err: nothing saved; the running config is unchanged");
		return;
	}
	Serial.print("# accepted: ");
	Serial.print(parsed.consoles.size());
	Serial.print(" consoles, ");
	Serial.print(parsed.irCodes.size());
	Serial.println(" IR codes");
	const retroroom_store::SaveResult saved =
	    retroroom_store::saveConsoleConfigWithBackups(payload);
	if (saved != retroroom_store::SaveResult::Ok) {
		reply("err: LittleFS write failed; retry the PUT");
		return;
	}
	// The stored index names a slot in the array the config just
	// replaced, so carrying it across would point at whichever console
	// lands at that offset in the new list. Wiping means the reboot
	// below lands on console 0.
	retroroom_store::clearLastSelectedConsole();
	reply("ok -- saved, rebooting");
	rebootAtMs = millis() + kRebootDelayMs;
}

// Wipe everything and queue a restart. One call, shared with the HTTP
// /factory-reset handler, so the two cannot disagree about what a
// factory reset removes.
void acceptReset() {
	if (!retroroom_store::wipeEverything()) {
		reply("err: LittleFS unavailable or a file survived; nothing is confirmed wiped");
		return;
	}
	reply("ok -- erased, rebooting into setup");
	rebootAtMs = millis() + kRebootDelayMs;
}

// Save the credentials SETUP WIFI collected.
//
// Written through ArduinoJson rather than by hand. A passphrase
// containing a quote or a backslash is entirely ordinary, and this is
// the one place the escaping has to be right; the reader in
// network.cpp is ArduinoJson, so the writer should be too. (The HTTP
// /setup form has been doing exactly this, which is why the two paths
// cannot produce different files for the same credentials.)
void acceptWifi(const char* ssid, const char* pass) {
	if (!LittleFS.begin()) {
		reply("err: LittleFS mount failed; nothing saved");
		return;
	}
	StaticJsonDocument<256> doc;
	doc["ssid"] = ssid;
	doc["pass"] = pass;
	File f = LittleFS.open("/wifi.json", "w");
	if (!f) {
		reply("err: could not open /wifi.json for writing");
		return;
	}
	serializeJson(doc, f);
	f.close();
	reply("ok -- saved, rebooting");
	rebootAtMs = millis() + kRebootDelayMs;
}

// Carry out whatever the parser asked for. One action per call, which
// is all CmdRequest can carry and is why the parser can never lose a
// message by trying to report two at once.
void act(const retroroom_core::CmdRequest& r) {
	switch (r.action) {
	case retroroom_core::CmdAction::None:
		break;
	case retroroom_core::CmdAction::Reply:
		reply(r.text);
		break;
	case retroroom_core::CmdAction::EnterInteractive:
		// The host asked, and the screen is already in r.text. Note
		// the parser has *not* been told the session is open -- the
		// host spent the `i` by sending it, which is the whole
		// mechanism. Only auto-entry has to say so explicitly.
		interactive = true;
		Serial.print(r.text);
		break;
	case retroroom_core::CmdAction::Status:
		printStatus();
		break;
	case retroroom_core::CmdAction::GetConfig:
		sendConfig();
		break;
	case retroroom_core::CmdAction::PutConfig:
		// A truncated paste is refused rather than saved. The bytes
		// stop cleanly at the cap, so they are valid UTF-8 that ends
		// mid-document, and without this the operator would be told
		// about a parse error for what is really a paste that was too
		// big.
		if (r.truncated) {
			reply("err: that config was longer than 8 KB; nothing saved");
			break;
		}
		acceptConfig(r.body, r.bodyLength);
		break;
	case retroroom_core::CmdAction::SetWifi:
		acceptWifi(r.ssid, r.pass);
		break;
	case retroroom_core::CmdAction::Reset:
		acceptReset();
		break;
	case retroroom_core::CmdAction::Reboot:
		// Deliberately not "save first". The operator has a menu row
		// called Save and a `save` verb does not exist here; a REBOOT
		// that silently wrote pending changes to flash would be a
		// second, invisible way to commit them. The LCD menu makes you
		// press Save, and the consequence of forgetting is a lost
		// change, which is the recoverable direction.
		reply("ok -- rebooting, nothing saved");
		rebootAtMs = millis() + kRebootDelayMs;
		break;
	}
}

}  // namespace

bool serialcmd_isInteractive() {
	return interactive;
}

void serialcmd_maybeAutoEnter() {
	if (interactive) {
		return;
	}
	// "The web API would not be usable", said as the three things that
	// make it unusable rather than as a list of happy paths:
	//
	//   - No config on flash. The cabinet is running what we shipped,
	//     and there is nothing to POST a replacement to.
	//   - The radio is switched off. Same, and more so.
	//   - The SoftAP is up for want of credentials. /consoles.json is
	//     not even routed there -- onNotFound redirects everything to
	//     /setup -- so the web API genuinely cannot reach the config,
	//     whatever the operator types at 192.168.4.1.
	//
	// Asked as "is the network unreachable" and deliberately NOT as
	// "is it not in STA mode": at this point in the boot a cabinet
	// with good credentials whose router is slow has not joined yet,
	// and the second question would open the session on a healthy
	// device every single boot. The first question knows the
	// difference between "has not managed" and "has not finished
	// trying".
	//
	// A cabinet that is configured and joined stays out of it and
	// keeps its heartbeat: there is nothing wrong with it, and opening
	// a session unprompted on a healthy device is noise.
	if (consoleConfigIsUploaded() && !network_isUnreachable()) {
		return;
	}
	interactive = true;
	// The `i` key is spent. The session is already open, and a stray
	// `i` inside a pasted config must not print the screen a second
	// time and look like a second command.
	parser.markSessionOpen();
	Serial.print(retroroom_core::kInstructions);
}

void serialcmd_init() {
	parser.begin();
	interactive = false;
	rebootAtMs = 0;
	// One line, unconditionally, including on a healthy cabinet.
	//
	// The heartbeat carries the same hint and that is not enough on its
	// own: agent-script/pio-upload-monitor.sh strips ^Heartbeat: lines
	// by default, so the only person who would find this through the
	// repo's own tooling is one running with --keep-heartbeats. A
	// recovery channel nobody knows exists is not a recovery channel.
	Serial.println("serial: press i for interactive mode and instructions");
	// A cabinet already in a state that needs the channel gets it
	// without the keypress.
	serialcmd_maybeAutoEnter();
}

void serialcmd_loop() {
	// Drain the CDC buffer before doing anything slow.
	//
	// This is the one place a paste can be lost, and it is worth being
	// explicit about why. A terminal paste arrives as a burst and the
	// device's receive buffer is small, so anything that stalls the loop
	// while bytes are still arriving is bytes being dropped. One
	// dropped byte inside a JSON document produces a document that
	// fails to parse -- a loud, recoverable failure, not a silent one,
	// which is the only reason this is acceptable at all.
	//
	// So: feed everything available, and deal with any request
	// afterwards. A PUT commits to flash, a few milliseconds, and it
	// happens when the buffer is already empty. The next loop
	// iteration picks the rest of the paste back up.
	retroroom_core::CmdRequest pending;
	bool havePending = false;
	while (Serial.available() > 0) {
		const int c = Serial.read();
		if (c < 0) {
			break;
		}
		if (parser.feed(static_cast<char>(c), millis(), &pending)) {
			havePending = true;
			// Stop consuming. The action may want milliseconds, and
			// whatever is queued behind it can wait in the buffer for
			// a tick rather than being dropped on the floor.
			break;
		}
	}
	if (havePending) {
		act(pending);
		return;
	}

	// The paste-stall timeout, and only that. It fires on a config
	// that stopped arriving; every other part of the parser is
	// edge-driven and needs no clock.
	if (parser.poll(millis(), &pending)) {
		act(pending);
		return;
	}

	if (rebootAtMs && (long)(millis() - rebootAtMs) >= 0) {
		rebootAtMs = 0;
		Serial.println("rebooting");
		Serial.flush();
		rp2040.restart();
	}
}
