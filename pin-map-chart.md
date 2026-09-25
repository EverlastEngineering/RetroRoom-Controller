# Pin Map — RetroRoom-Controller

> **Source of truth.** This file is the single, authoritative pin
> assignment table for the RetroRoom-Controller firmware on the
> Raspberry Pi Pico / Pico-W / Pico 2 W. `src/configuration.h`'s
> `#define`s MUST match this table; any change to one MUST be
> reflected in the other in the same commit.
>
> Older narrative docs (`pico-pin-mapping.md`, `pin-map-plan.md`)
> were removed on 2026-09-24 — see
> `todo/done/2026-09-24_pin-map-consolidation.md` for the deletion
> commit. Per-role documentation lives inline in `src/configuration.h`
> as `#define` comments.

```
GP0  UART0 TX (USB-CDC)               — reserved by USB
GP1  UART0 RX (USB-CDC)               — reserved by USB
GP2  (free)                           ┐ I2C1 primary (SDA/SCL) — fully free
GP3  (free)                           ┘
GP4  (free)                           ┐ I2C0 primary (SDA/SCL) — fully free; 16x2 LCD lives here
GP5  (free)                           ┘
GP6  NEXT_CONSOLE_BTN                 ┐ user buttons (adjacent, active-low)
GP7  PREV_CONSOLE_BTN                 ┘
GP8  ARM                              ┐
GP9  CYCLE                            │ StackSelector + OE (contiguous 4-pin cluster)
GP10 ENABLE                           │
GP11 MANUAL_OE                        ┘
GP12 TOUCH_SENSOR_PIN                 - capacitive touch (connected to rotary encoder ground body)
GP13 ROTARY_SELECTOR                  ┐ rotary encoder
GP14 ROTARY_PIN_IN1                   │ 
GP15 ROTARY_PIN_IN2                   ┘
GP16 (free)                           ┐
GP17 (free)                           │ SPI0 block (4 contiguous, fully free)
GP18 (free)                           │
GP19 (free)                           ┘
GP20 DATA_PIN (FastLED ring)          ┐ output pins
GP21 SELECTED_CONSOLE_LED_STRING_DATA │
GP22 IR_CONTROL_PIN                   ┘ 
GP23-25 (CYW43 / LED_BUILTIN)         — internal / reserved
GP26 (free)                           ┐
GP27 (free)                           │ ADC0/1/2 (3 contiguous, all free)
GP28 (free)                           ┘
GP29 (Vsys)                           — internal / reserved
GP30 RUN                              — reserved (reset)
```