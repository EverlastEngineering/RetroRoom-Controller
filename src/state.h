#ifndef RR_STATE_H
#define RR_STATE_H

// For uint32_t below. This header is included by main.h, which is
// included first thing by every translation unit in the firmware, so
// there was never a reason for it to need one until now -- and needing
// one now is not a reason to go looking for a different header.
#include <cstdint>

extern bool flash;
extern int statusLedActive;

// how often the loop() iteration count, reported in the heartbeat as
// `loop=<n>/s`.
//
// It is here because it is the number nobody can otherwise get: the
// rotary selector is polled, so the loop rate IS the button's sampling
// rate. A knob press shorter than one iteration is a press that never
// happened, and the whole failure looks like a broken gesture rather
// than a slow loop. The heartbeat has been printing uptime for a while;
// this puts the loop period next to it so the two can be compared.
extern uint32_t loopTickCount;

void setLed(int state);
void ledOff();
void ledOn();
void flashLed();
// Per-loop pump for the on-board LED blink state-machine. No-op when
// `flash` is false; 1 Hz toggle (500 ms on / 500 ms off) while it's
// true. Called from main.cpp::loop() -- main.cpp never touches the
// LED pin directly, this is the only writer to LED_BUILTIN while
// flashing.
void flashLedTick();
// Per-loop pump for the liveness heartbeat over Serial. Prints
// "Heartbeat: uptime=N selected=N idx=I total=T mode=sta|ap" once
// every kHeartbeatIntervalMs (default 2000 ms). The prefix is
// stable so agent-script/pio-upload-monitor.sh can filter it out
// of build logs (default behaviour) while --keep-heartbeats surfaces
// it for liveness debugging. The harness and operators can also
// grep on `^Heartbeat:` to confirm the firmware main loop is still
// ticking even when WiFi is wedged -- which is the whole point:
// before this existed, a radio-stuck device looked identical to a
// crashed device from the host's perspective.
void heartbeatTick();
#endif