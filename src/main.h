#ifndef MAIN_H
#define MAIN_H

#include "configuration.h"
#include "controls.h"
#include "state.h"
#include "network.h"
#include "lighting.h"
#include "ledstring.h"
#include "Console.h"
#if defined(HAS_IR)
#include "ircontrol.h"
#endif
#include "stackselector.h"
#include "consoles.h"

void consoleDefinitions();

#endif