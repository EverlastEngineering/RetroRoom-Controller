#ifndef IRCONTROL_H
#define IRCONTROL_H

#include <Arduino.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "configuration.h"

extern void ir_control_init();
extern void sendSonyPower();

#endif