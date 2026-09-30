# Pin Map — RetroRoom-Controller

> **Source of truth.** This file is the single, authoritative pin
> assignment table for the RetroRoom-Controller firmware on the
> Raspberry Pi Pico 2 W. Hardware is wired from this table.
>
> **Contract:** every pin listed below has exactly one matching
> `#define` in `src/configuration.h`, and `configuration.h` never
> claims a pin that isn't listed here. If the two disagree, **the code
> is wrong** — fix the code, not this table.
>
> If you need a pin that isn't in the table, or a pin here has an
> ambiguous or conflicting role, that's a **blocking question for the
> maintainer**: the answer changes the physical wiring, so it can't be
> inferred from the source. Raise it before writing code.
>
> Per-role documentation lives inline in `src/configuration.h` as
> `#define` comments.

```
GP0  UART0 TX (USB-CDC)               — reserved by USB
GP1  UART0 RX (USB-CDC)               — reserved by USB
GP2  (free)                           ┐ free GPIO
GP3  (free)                           ┘
GP4  I2C0 SDA  LCD_I2C_SDA_PIN         ┐ 16x2 I2C LCD (HD44780 + PCF8574
GP5  I2C0 SCL  LCD_I2C_SCL_PIN         ┘ backpack) — NOT free
GP6  NEXT_CONSOLE_BTN                 ┐ hardware next/prev buttons
GP7  PREV_CONSOLE_BTN                 ┘ (active-low, INPUT_PULLUP)
GP8  ARM                              ┐
GP9  CYCLE                            │ StackSelector + OE
GP10 ENABLE                           │ (contiguous 4-pin cluster)
GP11 MANUAL_OE                        ┘
GP12 TOUCH_SENSOR_PIN                 - capacitive touch, wired to the
                                        rotary encoder's ground body
GP13 ROTARY_SELECTOR                  ┐
GP14 ROTARY_PIN_IN1                   │ rotary encoder
GP15 ROTARY_PIN_IN2                   ┘
GP16 (free) SPI0 MISO                 ┐
GP17 (free) SPI0 SS                   │ SPI0 block — 4 contiguous,
GP18 (free) SPI0 SCK                  │ fully free
GP19 (free) SPI0 MOSI                 ┘
GP20 LED_RING_DATA_PIN (FastLED ring)          ┐
GP21 SELECTED_CONSOLE_LED_STRING_DATA │ output pins. The string on
                                      ┘ this pin is ~2 m of WS2812B at
                                       1.5 LEDs/inch, so **about 118
                                       LEDs**, and it is NOT cut to
                                       size -- there is spare string at
                                       the far end. Recorded here
                                       because it is a hardware fact
                                       and the firmware cannot know
                                       it: the buffer is built for
                                       512 (retroroom_core::
                                       kLedStripCapacity) and the
                                       config's `led.totalLeds` says
                                       how many are really there. The
                                       parser refuses anything above
                                       the build.
GP22 IR_CONTROL_PIN                   ┘
GP23 (free)                           ┐
GP24 (free)                           │ free GPIO — the CYW43 radio does
GP25 (free)                           ┘ NOT occupy these on the Pico 2 W
GP26 (free) I2C1 SDA / ADC0           ┐
GP27 (free) I2C1 SCL / ADC1           │ I2C1 is here (not GP2/GP3);
GP28 (free) ADC2                      ┘ all three also on the ADC
GP29 Vsys                             — internal / reserved
GP30 RUN                              — reserved (reset)
GP64 LED_BUILTIN                      — on-board status LED
```

### Mux notes

Several pins above are shared with peripherals the board exposes. They
are safe to use as plain GPIO as long as the corresponding peripheral
stays off:

| Our pin | Shared peripheral |
|---|---|
| GP8 / GP9 | UART2 TX / RX |
| GP12–GP15 | SPI1 (SS / SCK / MOSI / MISO) — **SPI1 is not available** |

SPI0 (GP16–GP19) is untouched and remains fully available.
