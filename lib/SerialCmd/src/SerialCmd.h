#pragma once

// The USB-serial configuration channel, as a pure state machine.
//
// Why this exists
// ---------------
// The only way to write /consoles.json was HTTP. That is fine right
// up until there is no HTTP: `network.disable` leaves the radio off, a
// dead CYW43 never brings the SoftAP up, and a USB reflash does not
// help -- picotool writes the sketch region, LittleFS is a separate
// partition, and nothing else writes it. A cabinet configured to have
// no network was a cabinet nobody could reconfigure.
//
// So the recovery channel is the cable that is already plugged in.
// This file is the half of that which can be tested: it turns a byte
// stream into "here is what the host is asking for", and knows nothing
// about Arduino, LittleFS, WiFi or the console config itself.
// lib/CabinetMenu is the same shape of problem, solved the same way.
//
// The other half is src/serialcmd.cpp, which feeds this from Serial,
// performs the file I/O, and writes the replies.
//
// Who this is for
// ---------------
// A person holding a USB cable and a laptop, and the scripts in
// agent-script/ that do the same thing while developing the firmware.
// Not the end user, who gets the web UI. That is why the vocabulary is
// words -- GET CONFIG, PUT CONFIG -- rather than paths and byte counts,
// and why a pasted config is framed by a CONFIG DONE line rather than
// a length the caller has to work out before pasting anything.
//
// The interaction model, in one paragraph
// --------------------------------------
// Line-terminated, case-insensitive, and there is no way out but a
// reboot. An unrecognised line produces one short line of output with
// no echo of the input, and the command list prints only for `?` -- so
// a stream of garbage produces one terse line per line of garbage and
// never a help dump. A blank line produces silence, because pressing
// Enter twice is the most common accidental input and it is not an
// error. There is no prompt string, because the instruction screen is
// printed once on entry and a prompt would only fight with it.

#include <cstddef>
#include <cstdint>

namespace retroroom_core {

// The instruction screen, exported rather than file-private because a
// shell that opens the session on its own initiative -- a cabinet with
// no config and no network -- has nothing else to print. Duplicating
// the text over there would be two copies of the vocabulary, and two
// copies is how a client and a device start disagreeing about what
// commands exist.
extern const char* const kInstructions;

// Bounds, all in one place, and all here to stop a host -- a stuck
// terminal, a curious person, a script with a bug -- from taking the
// cabinet down.
//
//   kMaxConfigBytes  matches the HTTP upload cap in the store layer, so
//                    the two channels accept exactly the same inputs.
//                    A paste over the cap is refused with a clear error
//                    rather than truncated into a confusing parse
//                    error, and nothing reaches the disk either way.
//   kMaxLineBytes    the longest legal command is 11 characters
//                    ("SETUP WIFI"), so anything longer is junk and is
//                    discarded without being buffered.
//   kLineStallMs     how long a partially-received paste may stall
//                    before the channel gives up and goes back to
//                    listening. Without it a host that dies mid-paste
//                    leaves the parser collecting lines that will never
//                    arrive, and the recovery channel is wedged on the
//                    one device that needed it.
constexpr std::size_t kMaxConfigBytes = 8 * 1024;
constexpr std::size_t kMaxLineBytes = 64;
constexpr unsigned long kLineStallMs = 5000UL;

// What the shell has been asked to do. The core decides *which* of
// these; only the shell can carry them out.
enum class CmdAction {
	None,              // nothing to do. A blank line, or bytes consumed.
	Reply,             // write `text` to the port.
	EnterInteractive,  // the host pressed `i`; `text` is the instruction
	                    // screen and the shell should turn the heartbeat
	                    // off for good.
	Status,            // the shell answers with one line of live state.
	GetConfig,         // read the live config and send it back, as text.
	PutConfig,         // `body`/`bodyLength` hold a complete pasted config.
	SetWifi,           // `ssid` and `pass` hold what SETUP WIFI collected.
	Reset,             // wipe everything and reboot.
	Reboot,            // change nothing on flash, just restart.
};

// One unit of work. `text`, `ssid` and `pass` point at storage owned
// by this object; `body` points at the config buffer and is valid only
// until the next call. That lifetime is the price of a fixed 8 KB
// buffer and no allocation, and it costs the shell nothing: every
// action completes inside the call that produced it, so nothing is
// still reading by the time the next byte arrives.
struct CmdRequest {
	CmdAction action = CmdAction::None;
	// One string, not a list. Two replies in one call would mean the
	// second silently overwriting the first, which is a bug this file
	// had once.
	const char* text = nullptr;
	const char* ssid = nullptr;
	const char* pass = nullptr;
	const char* body = nullptr;
	std::size_t bodyLength = 0;
	// True when a PutConfig's body is a *truncated* paste rather than
	// the whole thing. The shell must refuse it: the bytes are valid
	// UTF-8 and stop cleanly at the cap, so a document cut in half
	// still reaches the validator, and the operator would be told
	// about a parse error when what actually happened is that their
	// paste was too big. Carried on the request rather than queried
	// separately so the flag cannot be read after the state has moved
	// on.
	bool truncated = false;
};

// The state machine. One per port.
class SerialCmd {
  public:
	// Forget any partial transfer and go back to listening for a line.
	void begin();

	// True until anything at all has been received. Only the very
	// first byte the host sends can be the `i` that opens interactive
	// mode, so a stray `i` inside a pasted config cannot turn the
	// heartbeat off on a device whose only way back is a reboot.
	bool untouched() const { return untouched_; }

	// The shell opened the session itself, so the `i` key is spent.
	// For auto-entry: the cabinet was already in a state that needed
	// this channel, and a stray `i` in a later paste must not print
	// the instruction screen a second time and look like a reply to
	// whatever the operator just sent.
	void markSessionOpen() { untouched_ = false; }

	// Feed one byte. Returns true when `out` has been filled in with
	// something to do; false for bytes that are merely part of a line,
	// which is the common case and the cheap one.
	//
	// `nowMs` is millis() and is used for one thing only: restarting
	// the paste-stall clock on each completed line. It is a parameter
	// rather than a call to millis() so this file stays free of
	// Arduino, which is the whole reason it is a separate library.
	bool feed(char c, unsigned long nowMs, CmdRequest* out);

	// Called once per loop iteration. Exists for one reason: the stall
	// timeout on a paste that stopped arriving.
	bool poll(unsigned long nowMs, CmdRequest* out);

	// True while a pasted config is being collected.
	bool receivingConfig() const { return state_ == State::Config; }

  private:
	enum class State {
		Line,    // collecting a command line
		Config,  // collecting a pasted config, ended by CONFIG DONE
		Ssid,    // SETUP WIFI: waiting for the network name
		Pass,    // SETUP WIFI: waiting for the password
	};

	// Back to listening, keeping the fact that the host has already
	// spoken. begin() is the boot-time entry point and re-arms the `i`
	// gate; this is what the stall timeout uses, because a paste that
	// died mid-transfer is not a new session.
	void resetToLine();

	// Consume a complete command line and produce whatever it asked
	// for. False when there is nothing to say about it.
	bool handleLine(CmdRequest* out);
	// One line of a pasted config. False when it was the terminator,
	// which the caller turns into the completed PUT.
	bool handleConfigLine(CmdRequest* out);
	// `text`, ignoring case and any whitespace around it, equals
	// `word`.
	static bool isWord(const char* text, const char* word);

	State state_ = State::Line;

	// Set by the first byte the host sends, and the gate on the `i`
	// that opens interactive mode. Anything arriving clears it, so a
	// paste can never open the session.
	bool untouched_ = true;

	char line_[kMaxLineBytes + 1] = {0};
	std::size_t lineLen_ = 0;
	// Set when a line overran the buffer, so the bytes are drained to
	// the next newline and reported once rather than being parsed as
	// the tail of a command.
	bool lineOverflow_ = false;

	// The pasted config, reassembled line by line. Newlines go back
	// between lines because that is what a pasted file had; a
	// carriage return is dropped, so a config pasted from Windows
	// stores the same bytes as one pasted from anywhere else.
	char config_[kMaxConfigBytes] = {0};
	std::size_t configLen_ = 0;
	// Set when the paste outgrew the cap, so collection keeps draining
	// to the terminator without growing. A 40 KB paste must not be
	// able to cost 40 KB of RAM, and it must not be able to be saved
	// truncated either.
	bool configOverflow_ = false;
	// millis() of the last line seen, and whether any line has actually
	// arrived -- the clock starts when the paste starts, not when
	// PUT CONFIG was typed, or a person who goes to find the file
	// would be disconnected for it.
	unsigned long configLastMs_ = 0;
	bool configStarted_ = false;

	// SETUP WIFI's two answers. Capped at the line size, which is far
	// more than an SSID and comfortably more than any password anyone
	// types; a longer one is refused rather than silently truncated
	// into a wrong password.
	char ssid_[kMaxLineBytes + 1] = {0};
	char pass_[kMaxLineBytes + 1] = {0};
};

}  // namespace retroroom_core
