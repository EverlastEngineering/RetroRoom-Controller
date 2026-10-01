// Host-side Unity tests for the USB-serial configuration protocol in
// lib/SerialCmd.
//
// The protocol is the recovery channel for a cabinet that has been
// configured to have no network, and for one whose radio is dead. That
// makes "the parser wedges" the worst possible bug in the firmware, and
// it is a bug that only shows up under the exact circumstances where
// nothing else works -- so it is worth testing hard here rather than
// discovering it at a bench with a laptop and no way to reach the box.
//
// Run with:
//   pio test -d . -e test_native

#include <SerialCmd.h>
#include <unity.h>

#include <cstring>
#include <string>

using retroroom_core::CmdAction;
using retroroom_core::CmdRequest;
using retroroom_core::SerialCmd;

void setUp(void) {}
void tearDown(void) {}

// The parser is ~9 KB of fixed buffers, so it lives in static storage
// rather than on the stack.
static SerialCmd cmd;
static CmdRequest req;

// Feed a whole line and report what came back. Returns true if the
// line produced a request.
static bool typeLine(SerialCmd& c, const char* line, CmdRequest* out,
                     unsigned long nowMs = 1000) {
	for (const char* p = line; *p; ++p) {
		if (c.feed(*p, nowMs, out)) {
			return true;
		}
	}
	return c.feed('\n', nowMs, out);
}

// True when `text` contains `needle` -- the instruction screen is one
// blob, so tests care that it *mentions* something rather than that it
// equals something.
static bool mentions(const char* text, const char* needle) {
	return text != nullptr && std::strstr(text, needle) != nullptr;
}

// ---------------------------------------------------------------------
// Entering interactive mode
// ---------------------------------------------------------------------

// The `i` is the whole entry point to the recovery channel, and it is
// only honoured on the very first byte. The second half matters: the
// heartbeat is the device's only liveness signal and the only way out
// of interactive mode is a reboot, so a stray `i` in a pasted config
// must not be able to do it.
void test_i_on_the_first_byte_enters_interactive_mode(void) {
	cmd.begin();
	TEST_ASSERT_TRUE(cmd.untouched());
	TEST_ASSERT_TRUE(cmd.feed('i', 0, &req));
	TEST_ASSERT_EQUAL(CmdAction::EnterInteractive, req.action);
	TEST_ASSERT_TRUE_MESSAGE(mentions(req.text, "CONFIG DONE"),
	                         "the entry screen must name the terminator");
	TEST_ASSERT_FALSE(cmd.untouched());
}

void test_uppercase_i_also_enters(void) {
	cmd.begin();
	TEST_ASSERT_TRUE(cmd.feed('I', 0, &req));
	TEST_ASSERT_EQUAL(CmdAction::EnterInteractive, req.action);
}

void test_i_anywhere_but_the_start_is_just_a_character(void) {
	cmd.begin();
	// A blank line first, so the first byte is not `i`.
	TEST_ASSERT_FALSE(typeLine(cmd, "", &req));
	TEST_ASSERT_FALSE(cmd.untouched());
	// Now an `i` -- and it is inside a command, not the first byte.
	TEST_ASSERT_FALSE(cmd.feed('i', 0, &req));
}

// The specific disaster: a config whose contents include a line that is
// just `i`. Because the session was never opened, that must be inert.
void test_a_stray_i_inside_a_paste_cannot_enter_interactive_mode(void) {
	cmd.begin();
	TEST_ASSERT_TRUE(typeLine(cmd, "PUT CONFIG", &req));
	for (const char* p = "i"; *p; ++p) {
		TEST_ASSERT_FALSE(cmd.feed(*p, 0, &req));
	}
	TEST_ASSERT_FALSE(cmd.feed('\n', 0, &req));
	// The paste is still being collected, which is the point.
	TEST_ASSERT_TRUE(cmd.receivingConfig());
}

// ---------------------------------------------------------------------
// The command vocabulary
// ---------------------------------------------------------------------

void test_the_documented_commands_are_recognised(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);  // open the session

	TEST_ASSERT_TRUE(typeLine(cmd, "STATUS", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);

	TEST_ASSERT_TRUE(typeLine(cmd, "GET CONFIG", &req));
	TEST_ASSERT_EQUAL(CmdAction::GetConfig, req.action);

	TEST_ASSERT_TRUE(typeLine(cmd, "RESET", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reset, req.action);
}

// Case-insensitivity is a promise made on the instruction screen, so it
// is pinned here rather than left to whoever types at the terminal.
void test_commands_are_case_insensitive(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "get config", &req));
	TEST_ASSERT_EQUAL(CmdAction::GetConfig, req.action);
	TEST_ASSERT_TRUE(typeLine(cmd, "GeT cOnFiG", &req));
	TEST_ASSERT_EQUAL(CmdAction::GetConfig, req.action);
	TEST_ASSERT_TRUE(typeLine(cmd, "status", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);
}

// A terminal that sends a tab, or a line with trailing blanks, must not
// be talking about a different command.
void test_surrounding_whitespace_is_ignored(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "  GET CONFIG   ", &req));
	TEST_ASSERT_EQUAL(CmdAction::GetConfig, req.action);
	TEST_ASSERT_TRUE(typeLine(cmd, "\tSTATUS\t", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);
}

// The anti-flood property: garbage produces one terse line, never the
// help screen. A device spewing its command list at someone who is
// holding down a key is unusable.
void test_an_unknown_command_is_one_terse_line_with_no_echo(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "FLARGLE", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE_MESSAGE(std::strstr(req.text, "unknown") != nullptr,
	                         req.text);
	// The help listing must NOT be what an unknown command produces.
	TEST_ASSERT_FALSE_MESSAGE(mentions(req.text, "GET CONFIG"),
	                          "an error must not dump the whole vocabulary");
	// And no echo of what was typed.
	TEST_ASSERT_TRUE_MESSAGE(std::strstr(req.text, "FLARGLE") == nullptr,
	                          "the input is not echoed back");
}

// The most common accidental input on a line protocol, and it is not an
// error. Answering it with a complaint trains people to ignore the port.
void test_a_blank_line_says_nothing(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_FALSE(typeLine(cmd, "", &req));
	TEST_ASSERT_FALSE(typeLine(cmd, "   ", &req));
	TEST_ASSERT_FALSE(typeLine(cmd, "\t", &req));
}

// Help is on demand, and it is the only thing that produces the full
// listing.
void test_question_mark_prints_the_instructions(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "?", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(mentions(req.text, "GET CONFIG"));
	TEST_ASSERT_TRUE(mentions(req.text, "PUT CONFIG"));
	TEST_ASSERT_TRUE(mentions(req.text, "SETUP WIFI"));
	TEST_ASSERT_TRUE(mentions(req.text, "RESET"));
	TEST_ASSERT_TRUE(mentions(req.text, "STATUS"));
	TEST_ASSERT_TRUE_MESSAGE(mentions(req.text, "CONFIG DONE"),
	                         "the terminator has to be findable");
}

// A line longer than any command is junk. It must be reported once and
// not interpreted -- otherwise the tail of a long paste becomes a
// command nobody asked for.
void test_an_over_long_line_is_reported_once_and_discarded(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	const char* huge =
	    "GET CONFIG GET CONFIG GET CONFIG GET CONFIG GET CONFIG "
	    "GET CONFIG GET CONFIG GET CONFIG GET CONFIG GET CONFIG";
	int replies = 0;
	for (const char* p = huge; *p; ++p) {
		if (cmd.feed(*p, 0, &req)) {
			++replies;
		}
	}
	// The report comes at the newline, not at the byte that overran:
	// the operator only cares that the line was too long, and the
	// tail of it is not something worth interpreting.
	if (cmd.feed('\n', 0, &req)) {
		++replies;
		TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
		TEST_ASSERT_TRUE(std::strstr(req.text, "too long") != nullptr);
	}
	TEST_ASSERT_EQUAL_MESSAGE(1, replies, "exactly once, at the newline");
	// And the parser is usable afterwards.
	TEST_ASSERT_TRUE(typeLine(cmd, "STATUS", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);
}

// ---------------------------------------------------------------------
// SETUP WIFI
// ---------------------------------------------------------------------

// Guided, not arguments-on-one-line, because a password containing a
// space would be split into two fields.
void test_setup_wifi_prompts_for_the_name_then_the_password(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);

	TEST_ASSERT_TRUE(typeLine(cmd, "SETUP WIFI", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(mentions(req.text, "SSID"));

	TEST_ASSERT_TRUE(typeLine(cmd, "My Network", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(mentions(req.text, "password"));

	TEST_ASSERT_TRUE(typeLine(cmd, "hunter2", &req));
	TEST_ASSERT_EQUAL(CmdAction::SetWifi, req.action);
	TEST_ASSERT_EQUAL_STRING("My Network", req.ssid);
	TEST_ASSERT_EQUAL_STRING("hunter2", req.pass);
}

// A password is taken exactly as typed. Stripping a trailing space is
// the kind of quiet correction that costs an afternoon, because the
// symptom is a router that refuses a password the user can see is
// correct.
void test_a_password_is_not_trimmed(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "SETUP WIFI", &req);
	typeLine(cmd, "net", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "pass with spaces  ", &req));
	TEST_ASSERT_EQUAL(CmdAction::SetWifi, req.action);
	TEST_ASSERT_EQUAL_STRING("pass with spaces  ", req.pass);
}

// An SSID *is* trimmed, because a trailing space there is invisible at
// the prompt and produces a network that will not join. The interior
// is untouched, because a name may legitimately contain a space.
void test_an_ssid_is_trimmed_but_only_at_the_ends(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "SETUP WIFI", &req);
	typeLine(cmd, "  My Network  ", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "pw", &req));
	TEST_ASSERT_EQUAL(CmdAction::SetWifi, req.action);
	TEST_ASSERT_EQUAL_STRING("My Network", req.ssid);
}

// An empty name is not a network. Ask again rather than writing
// credentials that cannot possibly join.
void test_an_empty_ssid_is_refused_and_asked_again(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "SETUP WIFI", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "   ", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(std::strstr(req.text, "network name is required") != nullptr);
	// Still waiting for one, and still waiting for a password after.
	TEST_ASSERT_TRUE(typeLine(cmd, "RealNet", &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(mentions(req.text, "password"));
	TEST_ASSERT_TRUE(typeLine(cmd, "pw", &req));
	TEST_ASSERT_EQUAL(CmdAction::SetWifi, req.action);
	TEST_ASSERT_EQUAL_STRING("RealNet", req.ssid);
}

// A CRLF terminal must not leave a carriage return inside the value.
void test_crlf_leaves_no_carriage_return_in_the_values(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "SETUP WIFI\r", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "Net\r", &req));
	TEST_ASSERT_TRUE(typeLine(cmd, "pw\r", &req));
	TEST_ASSERT_EQUAL(CmdAction::SetWifi, req.action);
	TEST_ASSERT_EQUAL_STRING("Net", req.ssid);
	TEST_ASSERT_EQUAL_STRING("pw", req.pass);
}

// ---------------------------------------------------------------------
// PUT CONFIG
// ---------------------------------------------------------------------

// The whole point of the terminator: a config is pasted in as-is and
// the terminator says where it stopped.
void test_a_pasted_config_ends_at_the_config_done_line(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "PUT CONFIG", &req));
	TEST_ASSERT_TRUE(cmd.receivingConfig());

	// Nothing is emitted while the paste is arriving -- a multi-line
	// paste must not produce a screenful of output.
	typeLine(cmd, "{", &req);
	typeLine(cmd, "  \"irCodes\": { \"Video\": \"0x430\" }", &req);
	typeLine(cmd, "}", &req);
	TEST_ASSERT_TRUE(cmd.receivingConfig());

	TEST_ASSERT_TRUE(typeLine(cmd, "CONFIG DONE", &req));
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_FALSE(cmd.receivingConfig());
	TEST_ASSERT_EQUAL_STRING("{\n  \"irCodes\": { \"Video\": \"0x430\" }\n}\n",
	                         req.body);
}

// The user's reasoning, checked: an exact whole-line match cannot
// collide with anything inside a JSON document, because every key and
// every string value is quoted. A tagline of "CONFIG DONE" is therefore
// safe -- and this is what makes it safe rather than merely unlikely.
void test_a_tagline_reading_config_done_does_not_end_the_paste(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	typeLine(cmd, "    \"tagline\": \"CONFIG DONE\",", &req);
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_TRUE_MESSAGE(
	    std::strstr(req.body, "\"tagline\": \"CONFIG DONE\",") != nullptr,
	    req.body);
}

// A config pasted from Windows has carriage returns on every line. They
// are dropped so what lands on flash is byte-identical to the same
// config pasted from Unix.
void test_carriage_returns_are_dropped_from_a_pasted_config(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	typeLine(cmd, "{\r", &req);
	typeLine(cmd, "  \"a\": 1\r", &req);
	typeLine(cmd, "}\r", &req);
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_EQUAL_STRING("{\n  \"a\": 1\n}\n", req.body);
	TEST_ASSERT_TRUE(std::strchr(req.body, '\r') == nullptr);
}

// The case the user does not care about but which must not be allowed
// to become a silent truncation: a paste over the cap is refused, and
// the refusal says so rather than letting a broken document reach the
// disk.
void test_an_over_sized_paste_is_refused_and_nothing_is_saved(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	// Push well past the cap in lines that are each a legal length.
	const std::string chunk(50, 'x');
	for (int i = 0; i < 250; ++i) {
		TEST_ASSERT_FALSE(typeLine(cmd, chunk.c_str(), &req));
	}
	TEST_ASSERT_TRUE(typeLine(cmd, "CONFIG DONE", &req));
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	// The bytes handed over stop at the cap rather than running past
	// it -- and the request says so, because a document cut in half
	// is still valid UTF-8 that ends cleanly, and without the flag the
	// shell would take it to the validator and report a parse error
	// for what is really "your paste was too big".
	TEST_ASSERT_TRUE(req.truncated);
	TEST_ASSERT_TRUE_MESSAGE(req.bodyLength <= retroroom_core::kMaxConfigBytes,
	                         "the buffer must stop at the cap");
}

// An over-sized paste must not be able to eat memory either. The parser
// keeps draining to the terminator, so the operator's next command is
// not eaten as config -- which is the failure that would wedge the one
// channel this exists to provide.
void test_the_parser_recovers_after_an_over_sized_paste(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	const std::string chunk(50, 'x');
	for (int i = 0; i < 250; ++i) {
		typeLine(cmd, chunk.c_str(), &req);
	}
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_TRUE(typeLine(cmd, "STATUS", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);
}

// A host that dies mid-paste must not leave the parser collecting lines
// that will never arrive. This is the single most important test in the
// file: a wedged parser on a device that needed the serial channel is
// the failure this whole exercise exists to prevent.
void test_a_paste_that_stalls_is_abandoned_and_the_parser_recovers(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	typeLine(cmd, "{\"a\": 1}", &req, 1000);
	TEST_ASSERT_TRUE(cmd.receivingConfig());

	// Not yet -- the timeout has not elapsed.
	TEST_ASSERT_FALSE(cmd.poll(1000 + retroroom_core::kLineStallMs - 1, &req));
	TEST_ASSERT_TRUE(cmd.receivingConfig());

	TEST_ASSERT_TRUE(cmd.poll(1000 + retroroom_core::kLineStallMs, &req));
	TEST_ASSERT_EQUAL(CmdAction::Reply, req.action);
	TEST_ASSERT_TRUE(std::strstr(req.text, "stalled") != nullptr);
	TEST_ASSERT_FALSE(cmd.receivingConfig());

	// And the channel works again.
	TEST_ASSERT_TRUE(typeLine(cmd, "STATUS", &req));
	TEST_ASSERT_EQUAL(CmdAction::Status, req.action);
}

// The clock starts when the paste starts, not when PUT CONFIG was
// typed. A person who issues the command and then goes to find the file
// has not stalled, and being disconnected for it would be absurd.
void test_the_stall_clock_does_not_start_at_the_command(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req, 1000);
	// Ten times the timeout with no lines at all, and nothing has been
	// received to stall on.
	for (unsigned long t = 2000; t < 12000; t += 1000) {
		TEST_ASSERT_FALSE(cmd.poll(t, &req));
		TEST_ASSERT_TRUE(cmd.receivingConfig());
	}
}

// A long line inside a paste is junk in the paste, not a command, and
// it does not get to end the transfer early.
void test_a_long_line_inside_a_paste_does_not_end_it(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	const std::string chunk(200, 'x');
	typeLine(cmd, chunk.c_str(), &req);
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_TRUE_MESSAGE(req.bodyLength < 100,
	                         "the over-long line was dropped, not stored");
}

// The buffer is handed over by pointer and the length is cleared, so a
// stale pointer can never be mistaken for a fresh request.
void test_the_config_buffer_is_reusable_after_a_put(void) {
	cmd.begin();
	typeLine(cmd, "i", &req);
	typeLine(cmd, "PUT CONFIG", &req);
	typeLine(cmd, "first", &req);
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_EQUAL_STRING("first\n", req.body);

	TEST_ASSERT_TRUE(typeLine(cmd, "PUT CONFIG", &req));
	typeLine(cmd, "second", &req);
	typeLine(cmd, "CONFIG DONE", &req);
	TEST_ASSERT_EQUAL(CmdAction::PutConfig, req.action);
	TEST_ASSERT_EQUAL_STRING("second\n", req.body);
}

int main(int argc, char** argv) {
	(void)argc;
	(void)argv;
	RUN_TEST(test_i_on_the_first_byte_enters_interactive_mode);
	RUN_TEST(test_uppercase_i_also_enters);
	RUN_TEST(test_i_anywhere_but_the_start_is_just_a_character);
	RUN_TEST(test_a_stray_i_inside_a_paste_cannot_enter_interactive_mode);
	RUN_TEST(test_the_documented_commands_are_recognised);
	RUN_TEST(test_commands_are_case_insensitive);
	RUN_TEST(test_surrounding_whitespace_is_ignored);
	RUN_TEST(test_an_unknown_command_is_one_terse_line_with_no_echo);
	RUN_TEST(test_a_blank_line_says_nothing);
	RUN_TEST(test_question_mark_prints_the_instructions);
	RUN_TEST(test_an_over_long_line_is_reported_once_and_discarded);
	RUN_TEST(test_setup_wifi_prompts_for_the_name_then_the_password);
	RUN_TEST(test_a_password_is_not_trimmed);
	RUN_TEST(test_an_ssid_is_trimmed_but_only_at_the_ends);
	RUN_TEST(test_an_empty_ssid_is_refused_and_asked_again);
	RUN_TEST(test_crlf_leaves_no_carriage_return_in_the_values);
	RUN_TEST(test_a_pasted_config_ends_at_the_config_done_line);
	RUN_TEST(test_a_tagline_reading_config_done_does_not_end_the_paste);
	RUN_TEST(test_carriage_returns_are_dropped_from_a_pasted_config);
	RUN_TEST(test_an_over_sized_paste_is_refused_and_nothing_is_saved);
	RUN_TEST(test_the_parser_recovers_after_an_over_sized_paste);
	RUN_TEST(test_a_paste_that_stalls_is_abandoned_and_the_parser_recovers);
	RUN_TEST(test_the_stall_clock_does_not_start_at_the_command);
	RUN_TEST(test_a_long_line_inside_a_paste_does_not_end_it);
	RUN_TEST(test_the_config_buffer_is_reusable_after_a_put);
	return UNITY_END();
}
