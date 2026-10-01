#include "SerialCmd.h"

#include <cstring>

namespace retroroom_core {

// The instruction screen, one blob rather than something assembled per
// line: it is printed once, on entering interactive mode, and the point
// of it is that someone can read it top to bottom and come away
// knowing the whole vocabulary.
const char* const kInstructions =
    "\n"
    "RetroRoom interactive mode. Heartbeats are off until a reboot.\n"
    "\n"
    "  ?              this list\n"
    "  STATUS         one line of live state\n"
    "  GET CONFIG     print the saved config\n"
    "  PUT CONFIG     paste a new config; end it with a line\n"
    "                 reading exactly CONFIG DONE\n"
    "  SETUP WIFI     prompts for the network name, then the password\n"
    "  RESET          erase everything and reboot\n"
    "\n"
    "Commands are case-insensitive. The password is shown as you type.\n";

namespace {

// Terse, and without an echo of what was typed. Garbage in the serial
// log is noise at best, and the usual caller is a person at a terminal
// who already knows what they typed.
constexpr const char* kErrUnknown = "err: unknown command (try ?)";
constexpr const char* kErrLineLong = "err: command too long";
constexpr const char* kErrConfigTooBig = "err: config exceeds 8192 bytes, not saved";
constexpr const char* kErrConfigStall = "err: paste stalled, aborted";
constexpr const char* kPromptSsid = "network name (SSID): ";
constexpr const char* kPromptPass = "password (visible): ";
// A CmdRequest carries one string, and two replies in one call means
// the second silently overwrites the first. So the complaint and the
// re-ask are one message.
constexpr const char* kErrNoSsid = "err: a network name is required; type it:";

void reply(CmdRequest* out, const char* text) {
	out->action = CmdAction::Reply;
	out->text = text;
}

bool isSpace(char c) {
	return c == ' ' || c == '\t';
}

// A line with nothing in it but whitespace. "Nothing the user typed",
// as opposed to "something we do not recognise" -- the two deserve
// different answers, and the most common accidental input on a line
// protocol is the first.
bool isBlank(const char* text) {
	while (*text != '\0') {
		if (!isSpace(*text)) {
			return false;
		}
		++text;
	}
	return true;
}

}  // namespace

// Start a session. This is the boot-time entry point, and the only
// thing that re-arms the `i` gate -- so it is *not* what the stall
// timeout uses. See resetToLine().
void SerialCmd::begin() {
	untouched_ = true;
	resetToLine();
}

// Back to listening for a command, keeping the fact that the host has
// already spoken. Separate from begin() because a paste that died
// mid-transfer is not a new session: the `i` that opens interactive
// mode is still spent, and letting a stray `i` re-open it from inside
// the recovery channel would be exactly the footgun the gate is for.
void SerialCmd::resetToLine() {
	state_ = State::Line;
	lineLen_ = 0;
	lineOverflow_ = false;
	configLen_ = 0;
	configOverflow_ = false;
	configStarted_ = false;
	configLastMs_ = 0;
	line_[0] = '\0';
	ssid_[0] = '\0';
	pass_[0] = '\0';
}

bool SerialCmd::isWord(const char* text, const char* word) {
	// Whitespace around the command is ignored, so a trailing space or
	// a tab from a terminal is not a different command. Whitespace
	// *inside* it is not ignored -- that is what separates the words.
	while (isSpace(*text)) {
		++text;
	}
	const std::size_t wordLen = std::strlen(word);
	for (std::size_t i = 0; i < wordLen; ++i) {
		char a = text[i];
		char b = word[i];
		// A NUL in `text` fails against any letter, so a string
		// shorter than the word returns false without ever walking off
		// the end of it.
		if (a >= 'A' && a <= 'Z') {
			a = static_cast<char>(a - 'A' + 'a');
		}
		if (b >= 'A' && b <= 'Z') {
			b = static_cast<char>(b - 'A' + 'a');
		}
		if (a != b) {
			return false;
		}
	}
	const char* tail = text + wordLen;
	while (isSpace(*tail)) {
		++tail;
	}
	return *tail == '\0';
}

bool SerialCmd::handleLine(CmdRequest* out) {
	if (isWord(line_, "?")) {
		reply(out, kInstructions);
		return true;
	}
	if (isWord(line_, "STATUS")) {
		out->action = CmdAction::Status;
		return true;
	}
	if (isWord(line_, "GET CONFIG")) {
		out->action = CmdAction::GetConfig;
		return true;
	}
	if (isWord(line_, "PUT CONFIG")) {
		// Everything after this is config until the terminator line.
		// The instruction screen goes out again, because the
		// terminator is what the person is about to have to type and
		// three lines up in a scrollback they have read past is not
		// where it will be found.
		configLen_ = 0;
		configOverflow_ = false;
		configStarted_ = false;
		state_ = State::Config;
		reply(out, kInstructions);
		return true;
	}
	if (isWord(line_, "SETUP WIFI")) {
		state_ = State::Ssid;
		ssid_[0] = '\0';
		pass_[0] = '\0';
		reply(out, kPromptSsid);
		return true;
	}
	if (isWord(line_, "RESET")) {
		out->action = CmdAction::Reset;
		return true;
	}
	// A blank line gets nothing at all. The most common accidental
	// input on a line protocol is an extra Enter -- or a line of
	// spaces, which is the same thing wearing a disguise -- and
	// answering it with a complaint trains people to ignore the port.
	if (isBlank(line_)) {
		return false;
	}
	reply(out, kErrUnknown);
	return true;
}

bool SerialCmd::handleConfigLine(CmdRequest* out) {
	// The terminator is a line that is exactly CONFIG DONE and nothing
	// else. That is not merely unlikely to collide with a pasted
	// config, it is impossible: every key and every string value in
	// JSON is quoted, so no bare token in a valid document can equal
	// this. A tagline of "CONFIG DONE" arrives as
	// `"tagline": "CONFIG DONE",` and does not match.
	if (isWord(line_, "CONFIG DONE")) {
		out->action = CmdAction::PutConfig;
		out->body = config_;
		out->bodyLength = configLen_;
		out->truncated = configOverflow_;
		// NUL-terminate, so a caller that treats the body as a C
		// string sees the document and not whatever the previous
		// paste left behind. The copy above leaves room for this: the
		// bound is one short of the cap precisely so the terminator
		// has a byte to land in.
		config_[configLen_] = '\0';
		// Only the length is cleared, not the bytes. The shell has to
		// have finished with the body by the time the next byte
		// arrives -- it always has, because every action completes
		// inside the call that produced it -- and clearing the length
		// means a stale pointer can never be mistaken for a fresh
		// request while the buffer stays immediately reusable.
		configLen_ = 0;
		configOverflow_ = false;
		state_ = State::Line;
		return true;
	}
	// An over-long paste keeps draining to the terminator rather than
	// aborting on the offending line. Aborting would leave the parser
	// in Config state, and the operator's next command would be eaten
	// as though it were more config.
	if (!configOverflow_) {
		const std::size_t n = std::strlen(line_);
		// +1 for the newline put back between lines.
		if (configLen_ + n + 1 < kMaxConfigBytes) {
			std::memcpy(config_ + configLen_, line_, n);
			configLen_ += n;
			config_[configLen_++] = '\n';
		} else {
			configOverflow_ = true;
		}
	}
	return false;
}

bool SerialCmd::feed(char c, unsigned long nowMs, CmdRequest* out) {
	// The `i` that opens interactive mode is honoured only as the very
	// first byte the host sends. It lives here rather than in the
	// shell so the rule is testable, and so that a config pasted over a
	// port whose session was never opened cannot turn the heartbeat
	// off on a device whose only way back is a reboot.
	if (untouched_) {
		untouched_ = false;
		if (c == 'i' || c == 'I') {
			out->action = CmdAction::EnterInteractive;
			out->text = kInstructions;
			return true;
		}
	}

	if (state_ == State::Ssid || state_ == State::Pass) {
		if (c == '\r') {
			// Dropped on arrival, so a CRLF terminal never leaves a
			// stray carriage return inside the value it typed.
			return false;
		}
		if (c != '\n') {
			if (lineLen_ < kMaxLineBytes) {
				line_[lineLen_++] = c;
			} else {
				// Too long to be an SSID or a password. Drop what has
				// been collected and stay in this state, so the value
				// is typed again rather than silently truncated into
				// one that will not connect.
				lineLen_ = 0;
				reply(out, kErrLineLong);
			}
			return false;
		}
		line_[lineLen_] = '\0';
		lineLen_ = 0;
		if (state_ == State::Ssid) {
			// Trimmed, because a trailing space is invisible at a
			// prompt and produces a network that will not join. The
			// interior is left alone -- a name may legitimately
			// contain one, and the prompt makes the cost of that low.
			char* s = line_;
			while (isSpace(*s)) {
				++s;
			}
			char* e = s + std::strlen(s);
			while (e > s && isSpace(e[-1])) {
				--e;
			}
			*e = '\0';
			if (*s == '\0') {
				// An empty name is not a network. Ask again rather
				// than writing credentials that cannot possibly join.
				// One message, not two: a CmdRequest carries one
				// string, and two replies in one call means the
				// second silently overwrites the first.
				reply(out, kErrNoSsid);
				return true;
			}
			std::strcpy(ssid_, s);
			state_ = State::Pass;
			reply(out, kPromptPass);
			return true;
		}
		// The password is taken exactly as typed. No trimming of any
		// kind: a password ending in a space is a password ending in
		// a space, and stripping it is the kind of bug that costs an
		// afternoon. The carriage return was already dropped above.
		std::strcpy(pass_, line_);
		out->action = CmdAction::SetWifi;
		out->ssid = ssid_;
		out->pass = pass_;
		state_ = State::Line;
		return true;
	}

	if (state_ == State::Config) {
		if (c == '\n') {
			// The stall clock restarts on every completed line, so it
			// measures a paste that has *stopped* rather than one being
			// read slowly off a disk.
			configStarted_ = true;
			configLastMs_ = nowMs;
			// `lineOverflow_` is per-line state and is cleared here
			// rather than left set. Leaving it set is the wedge this
			// file exists to prevent: the terminator would be dropped
			// on the floor like so much other noise, the paste would
			// never end, and the operator's next command would be
			// swallowed as config.
			const bool overflowed = lineOverflow_;
			line_[lineLen_] = '\0';
			lineLen_ = 0;
			lineOverflow_ = false;
			if (overflowed) {
				// The line itself is junk, so it is not interpreted --
				// the tail of an over-long line is not a command
				// anybody typed. The paste is already doomed; all that
				// is left is to find the terminator.
				configOverflow_ = true;
				return false;
			}
			return handleConfigLine(out);
		}
		if (c == '\r') {
			// Dropped rather than stored, so a config pasted from
			// Windows lands on flash byte-identical to the same config
			// pasted from Unix. The newline is re-added per line by
			// handleConfigLine().
			return false;
		}
		if (lineOverflow_) {
			return false;  // draining to the terminator
		}
		if (lineLen_ >= kMaxLineBytes) {
			// One line longer than any command can be, so the whole
			// paste is junk rather than this line alone.
			lineOverflow_ = true;
			configOverflow_ = true;
			return false;
		}
		line_[lineLen_++] = c;
		return false;
	}

	// Line state.
	if (c == '\n') {
		if (lineOverflow_) {
			lineOverflow_ = false;
			lineLen_ = 0;
			reply(out, kErrLineLong);
			return true;
		}
		line_[lineLen_] = '\0';
		lineLen_ = 0;
		return handleLine(out);
	}
	if (c == '\r') {
		return false;
	}
	if (lineOverflow_) {
		return false;  // draining to the newline
	}
	if (lineLen_ >= kMaxLineBytes) {
		lineOverflow_ = true;
		return false;
	}
	line_[lineLen_++] = c;
	return false;
}

bool SerialCmd::poll(unsigned long nowMs, CmdRequest* out) {
	// The clock starts on the first line of the paste, not when
	// PUT CONFIG was typed: a person who has issued the command and
	// then gone to find the file has not stalled, and disconnecting
	// them for it would be absurd.
	if (state_ != State::Config || !configStarted_) {
		return false;
	}
	if (nowMs - configLastMs_ < kLineStallMs) {
		return false;
	}
	// The paste stopped arriving. Say which kind of problem it was --
	// one that was going to be too big anyway, or one that merely
	// died -- and go back to listening. A wedged parser is the single
	// failure this channel cannot afford, on the one device that
	// needed it.
	reply(out, configOverflow_ ? kErrConfigTooBig : kErrConfigStall);
	resetToLine();
	return true;
}

}  // namespace retroroom_core