#ifndef LIGHTING_H
#define LIGHTING_H

#define FASTLED_INTERNAL //to get rid of the pragma messages from FastLED
#include <FastLED.h>

#include "configuration.h"

extern void lighting_init();
extern void lightSingle(int led);
extern void ringLEDNext();
extern void ringLEDPrevious();
extern void lightRing(bool lit);

#endif