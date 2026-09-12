#ifndef IRCONTROL_H
#define IRCONTROL_H

#include <Arduino.h>
#include "configuration.h"

// Note: <IRremote.hpp> is intentionally NOT included here. It pulls in
// the z3t0/IRremote library's globals (IrSender, IRrecv, timer helpers,
// LED-feedback state) which are non-inline definitions in the library's
// own sources. Including the header from any TU other than src/ircontrol.cpp
// causes linker multiple-definition errors (one of every IRrecv/IRsend
// method per TU). src/ircontrol.cpp is the single consumer and the
// only TU that includes <IRremote.hpp>.
//
// The functions below are plain C-linkage wrappers around the IRsend
// class instance, exposed to the rest of the firmware without dragging
// the library header along.

#if !defined(HAS_IR)
// Stub mode: HAS_IR not defined. The functions are no-ops and don't
// touch the IR library at all.
#endif

// Initialize the IR sender. Safe to call multiple times -- the
// underlying IrSender.begin() guards against re-init.
void ir_control_init();

// Drive the TV input. inputHexCode is a 12-bit Sony SIRC value
// (matches the format used in src/json/base.json), e.g. 0xA90 for
// Sony power-cycle. The high 5 bits are the SIRC address, the low 7
// bits are the command. See the comment block at the top of
// src/ircontrol.cpp for the bit-layout.
void setInput(int inputHexCode);

// Backward-compat helpers from the legacy crankyoldgit implementation.
// Not currently called by selectConsole() but kept so the legacy
// sendSonyPower button-action, if re-wired later, has something to
// bind to. Both are no-ops in the current build.
void sendSonyPower();
void discretePowerOn();

#endif
