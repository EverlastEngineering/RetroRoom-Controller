#ifndef RR_SERIALCMD_H
#define RR_SERIALCMD_H

// The USB-serial configuration channel: the shell half.
//
// The protocol itself is a pure state machine in lib/SerialCmd, which
// is testable on the host and holds no I/O. This is the part that owns
// Serial, LittleFS and the reboot -- the things that need a device to
// run.
//
// Why it exists at all: /consoles.json used to be writable only over
// HTTP, so a cabinet configured with `network.disable`, or one with a
// dead CYW43, could not be reconfigured by anybody. The cable that is
// already plugged in is the channel that works in every state.
//
// Call serialcmd_init() once from setup(), after consoleDefinitions()
// and network_init() -- the auto-entry decision needs to know whether
// there is a config on flash and whether there is a network. Then call
// serialcmd_loop() from loop().

// Why this file is not called serialcmd.h, which is the obvious name
// and the one that broke first:
//
//   The protocol library's header is lib/SerialCmd/src/SerialCmd.h.
//   On any case-insensitive filesystem -- macOS, Windows -- a file
//   named serialcmd.h and one named SerialCmd.h are the same file to
//   the compiler. `#include <SerialCmd.h>` from inside
//   src/serialcmd.cpp resolved to src/serialcmd.h, the shell's own
//   header, and #pragma once then skipped the real one. The symptom
//   was a cascade of "retroroom_core::SerialCmd does not name a type"
//   with no missing-include error anywhere, because as far as the
//   compiler was concerned the include had succeeded.
//
// So the two halves must not differ only by case. serialconfig.h is
// not otherwise an obvious name, which is the point.
//
// The function names below stay serialcmd_* because that is what the
// channel is called in the protocol and in every comment about it, and
// a grep for "serialcmd" should find all of it.

#include <Arduino.h>

// Bring the channel up and print the one-line hint. Does not open the
// session; see serialcmd_maybeAutoEnter().
void serialcmd_init();

// Pump. Cheap when nothing is connected: one poll() and, if the CDC
// buffer has anything in it, the work of consuming it.
void serialcmd_loop();

// True once the interactive session is open -- either because the host
// pressed `i`, or because serialcmd_maybeAutoEnter() decided the
// cabinet needed it. There is no way to close it except a reboot.
//
// Consulted by the heartbeat, which stops while this is true. The
// reason is not tidiness: someone reading a pasted config and typing a
// password should not have a line of telemetry landing in the middle of
// each one.
bool serialcmd_isInteractive();

// Open the session unprompted if the cabinet is not in a state where
// the web API would be usable -- no config on flash, the radio switched
// off, or the SoftAP up because there are no credentials. Idempotent,
// and a no-op once the session is open.
//
// Called from two places, and the two are not interchangeable:
//   - end of setup(), which covers every state that is already decided
//     by then: not configured, radio disabled, SoftAP for want of
//     credentials.
//   - the SoftAP fallback in network.cpp, which covers a join that
//     timed out. Deliberately not decided at setup() time: a cabinet
//     with valid credentials whose router is merely slow is *on its way*
//     to being fine, and opening a session on it would be a guess. By
//     the time the fallback fires, the guess has been settled.
void serialcmd_maybeAutoEnter();

#endif  // RR_SERIALCMD_H
