#ifndef RR_STATE_H
#define RR_STATE_H

extern bool flash;
extern int statusLedActive;

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