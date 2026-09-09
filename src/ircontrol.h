#ifndef IRCONTROL_H
#define IRCONTROL_H

#include <Arduino.h>
#include "configuration.h"

#if defined(ESP8266)
// Legacy implementation: crankyoldgit/IRremoteESP8266 + <IRsend.h>.
// On Pico_base (no ESP, no wireless), the IR blaster is out of scope for
// tonight's smoke build. The whole ircontrol.{h,cpp} compiles to nothing
// there. The IRremote v4.x migration lands in a follow-up commit and the
// guard will move to `#ifdef HAS_IR`.
#include <IRremoteESP8266.h>
#include <IRsend.h>

extern void ir_control_init();
extern void sendSonyPower();
extern void setInput(int inputHexCode);
void discretePowerOn();
void sendHexCode(int inputHexCode);
#endif

#endif