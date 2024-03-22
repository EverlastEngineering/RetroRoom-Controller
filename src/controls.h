#ifndef CONTROLS_H
#define CONTROLS_H

/* Rotary Encoder Library */
#include <Arduino.h>
#include <RotaryEncoder.h>
#include "configuration.h"
#include "Console.h"

/* EasyButton Libary used for rotary encoder selector and capacitive touch sensor in digital mode */
#include <EasyButton.h>

#include <vector>

extern std::vector <Console> consoles; 

extern void controls_init();
extern void rotaryEncoderTick();
extern int currentConsoleIndex;

void rotarySelectorPressed();
void sequenceElapsed();
void rotarySelectorISR();
void touchSensorISR();
void touchDetected();
void addConsole(Console console);
int HowManyConsoles();
Console CurrentConsole();

#endif