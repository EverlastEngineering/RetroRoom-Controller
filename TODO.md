# TODO.md

Curated list of pending work. Order matters where there are dependencies — do higher-numbered items only after lower-numbered ones complete, unless explicitly noted.

When an item is completed or rejected, log the disposition in `LOG.md`. Do not delete items silently.

> Policy: see [AGENT.md](AGENT.md).

---

## Phase 1 — Blockers

System does not function end-to-end without these. The hardware build currently works because it was flashed from a prior commit before the JSON-rewrite started; rebuilding from `main` will produce a non-functional binary until these are resolved.

1. **Consoles vector never populated.** `src/consoles.cpp::consoleDefinitions_init()` parses JSON but reads fields (`sensor`, `time`, `data[0..1]`) that don't exist in the input, never calls `addConsole()`. `___consoleDefinitions()` is dead code. Fix: wire JSON loading OR fall back to hardcoded `addConsole()` calls.
2. **`selectStack_init` mis-positions selector.** Calls `selectStack(howManyConsoles()+1)` — with empty consoles that resolves to `selectStack(1)` (zero clocks). Depends on #1.
3. **IR power-on fires on every boot.** `src/ircontrol.cpp::ir_control_init()` calls `discretePowerOn()`, sending a Sony power-on IR blast at every MCU reset. Gate behind a flag or move into an explicit user-triggered path.
4. **Touch events fire twice.** `touchDetected` is wired both as `EasyButton::onPressed` callback AND polled each loop via `touchSensor.isPressed()`. WebSocket broadcasts `touch: touched` twice per physical touch. Pick one path.
5. **Encoder double-ticked.** `encoder->tick()` runs in the ISR (`checkPosition`) and again in `rotaryEncoderTick()` loop. Drop the loop call.

## Phase 2 — Polish

Cleanups worth doing. Mostly safe to do in any order.

6. **Unbalanced `extern` for `currentConsoleIndex`.** Declared in `src/controls.h` but defined in `src/consoles.cpp` and not declared in `src/consoles.h`. Move the declaration to `consoles.h`.
7. **`flashLed()` doesn't flash.** `src/state.cpp` calls `analogWrite(MANUAL_OE_PIN, 127)` once and exits. LED holds at ~50% duty instead of blinking. Needs an oscillation loop or `millis()`-based toggle.
8. **`using namespace std;` in public header.** `src/main.h` pollutes the global namespace of every TU that includes it. Remove or scope to a single TU.
9. **No state persistence.** Selected console is lost on every reboot. Store last selection in EEPROM / NVS.
10. **WebSocket has no auth.** Any LAN client can send `ledOn` / `ledOff` / `flash` / `healthcheck`. Acceptable for home LAN but worth documenting or adding a shared-secret check.
11. **No rotary wraparound.** When hitting either end, rotation in that direction silently does nothing while the opposite direction works. Pick a wraparound behavior and make it consistent. (Note: currently masked by #1 because the consoles list is empty.)
12. **`lightSingle` flood pattern is unclear without context.** The 8-LED ring under the knob is rotation feedback, not selection feedback — document this so future contributors don't read it as broken. See LOG for the ring-vs-strip ADR.

## Phase 3 — Feature work

README backlog + items surfaced during the agent-infrastructure session.

13. **AGENT.md: add build/test quick reference.** Include PlatformIO build/upload commands, library deps, pin map summary.
14. **AGENT.md: add code style conventions.** Naming, file layout, `.clang-format` presence, defaults.
15. **AGENT.md: add architecture overview.** Brief "how the system works" section.
16. **Wire StackSelector output controls.** StackSelector hardware modules not yet driven by firmware.
17. **Add input selection to `Console` class.** Move IR input-set logic into a dedicated interface.
18. **Make console list editable from web UI.** Per `consoles.cpp` TODO and README backlog.
19. **Implement LED strip (separate pin from ring).** Ring is rotation feedback; strip above consoles should highlight the selected console via `led_position` / `led_width` on `Console`. See LOG for the ADR on the `lightStripSegment(int, int, CRGB)` primitive signature.
20. **Move ring off D3 (FLASH button pin).** D3 boot hazard acknowledged in `src/configuration.h`. Move in next perfboard revision.
21. **Pick color-keying strategy for the strip.** Hash-by-name vs adding a `CRGB color` field on `Console`. See LOG for context.
22. **Secret mode via `EasyButton::onSequence` for a light show.** README backlog.

## Phase 4 — Deferred

23. **Break StackSelector module into its own repo.** README backlog.
24. **Logo design for RetroRoom.** README backlog.
25. **Replace interrupt-on-CHANGE polling on touch sensor.** Current design uses both — pick one.
