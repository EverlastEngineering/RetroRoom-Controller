// IR blaster implementation for the Pico port. Uses z3t0/IRremote@^4.x
// via the global IrSender instance. The legacy crankyoldgit/IRremoteESP8266
// implementation (the ESP-only IRsend class + IRsend.sendSony(uint32_t,
// int, int) raw-bit call) is gone as of session/merge-pico-json commit
// c6005c7.
//
// Sony SIRC bit layout (12-bit, the format RetroRoom uses):
//
// The 12-bit value packs (MSB first):
//   - SIRC command:  7 bits   (bits 11..5 of the 12-bit value)
//   - SIRC address:  5 bits   (bits  4..0 of the 12-bit value)
//
// Worked example from the legacy implementation (see git history for
// the full comment block):
//   0xa90 = 0b1010_1001_0000
//                ^^^ ^^^ ^^^ ^
//                |   |   |   +-- SIRC bit 0 = 0 (low bit of address = 1)
//                |   +------+-- SIRC command = 0b0010101 = 0x15 = 21 decimal
//                +----------+ SIRC address  = 0b10000  = 0x10 = 16 (= 1 in SIRC 1-based)
//
// Translation to v4 IrSender::sendSony(uint16_t address, uint8_t command,
// int_fast8_t aNumberOfRepeats) is direct:
//   - address = (inputHexCode >> 7) & 0x1F   // 5 bits (SIRC format)
//   - command = (inputHexCode >> 0) & 0x7F   // 7 bits (low byte, low 7)
//   - repeats = 2                            // matches the legacy cr=2
//
// Example: 0xa90 -> address = 0x10, command = 0x10, repeats = 2

#include "ircontrol.h"

#if !defined(HAS_IR)
// Stub implementations when IR is disabled. Keeps the link clean
// without pulling in the z3t0/IRremote globals.
static bool ir_initialized = false;
void ir_control_init() {}
void setInput(int inputHexCode) {
    (void)inputHexCode;
    Serial.print("ircontrol: HAS_IR not set, skipping setInput(0x");
    Serial.print(inputHexCode, HEX);
    Serial.println(")");
}
void sendSonyPower() {}
void discretePowerOn() {}

#else

#include <IRremote.hpp>

static bool ir_initialized = false;

void ir_control_init() {
    if (ir_initialized) return;
    // Standard v4 init: send pin + default feedback LED pin. To disable
    // the on-board feedback LED blip on every send, define
    // NO_LED_SEND_FEEDBACK_CODE in lib_deps (the project-level PlatformIO
    // lib_extra_options); for now we accept the default behavior since
    // the on-board blue LED on GP25 is currently the only feedback LED
    // and it's not used by the firmware for anything else.
    IrSender.begin(IR_CONTROL_PIN, USE_DEFAULT_FEEDBACK_LED_PIN);
    ir_initialized = true;
    Serial.print("IR sender initialized on GP");
    Serial.println(IR_CONTROL_PIN);
}

void setInput(int inputHexCode) {
    if (!ir_initialized) ir_control_init();
    // Decode the 12-bit SIRC value: high 5 bits are SIRC address,
    // low 7 bits are SIRC command. See header comment for the bit
    // layout.
    uint16_t address = (inputHexCode >> 7) & 0x1F;
    uint8_t command  = (inputHexCode >> 0) & 0x7F;
    Serial.print("IR send: SIRC addr=");
    Serial.print(address);
    Serial.print(" cmd=0x");
    Serial.print(command, HEX);
    Serial.print(" (from 0x");
    Serial.print(inputHexCode, HEX);
    Serial.println(")");
    IrSender.sendSony(address, command, 2);
}

void sendSonyPower() {
    // Sony TV power-cycle code. The legacy implementation used the
    // 12-bit SIRC value 0xA90 which decoded to address=0x10 command=0x10;
    // the v4 API wants them split out.
    setInput(0xA90);
}

void discretePowerOn() {
    // 0x750 = 0b0111_0101_0000 -> address=0x0E, command=0x50
    setInput(0x750);
}

#endif  // HAS_IR
