#pragma once

// Definitions FastLED's generated native translation units need before they
// select the stub platform. This deliberately does not include Arduino.h:
// sim_main.cpp includes Windows headers first, and Arduino's INPUT macro would
// corrupt WinUser.h if it arrived through a compiler-wide preinclude.
#include <chrono>
#include <cstdint>
#include <thread>

typedef volatile uint32_t RoReg;
typedef volatile uint32_t RwReg;

#ifndef digitalPinToBitMask
#define digitalPinToBitMask(P) (0)
#endif
#ifndef digitalPinToPort
#define digitalPinToPort(P) ((P) / 32)
#endif
#ifndef portOutputRegister
#define portOutputRegister(P) (0)
#endif
#ifndef portInputRegister
#define portInputRegister(P) (0)
#endif
