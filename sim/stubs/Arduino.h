#pragma once

// Minimal Arduino core stand-in for the host harness under sim/. It provides only
// the surface the LED, scene and animation code touches, so those sources compile
// on a desktop. The device build never sees this file: it is on the include path
// for the native environment only.
//
// FastLED is compiled through its own stub platform here, because led_sysdefs.h
// selects it for __x86_64__. That platform's .cpp files are all inert unless
// FASTLED_STUB_IMPL or FASTLED_USE_STUB_ARDUINO is defined, and the stub headers
// are the only thing that defines FASTLED_STUB_IMPL -- which the .cpp files never
// include. So FastLED contributes no millis(), no Serial and no delay(), and the
// definitions below are the only ones in the program. That is deliberate: it is
// what lets the harness own the clock.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

using std::abs;
using std::max;
using std::min;

// -----------------------------------------------------------------------------
//  Core version and the macros arduinoFFT expects
// -----------------------------------------------------------------------------
// FastLED's stub announces ARDUINO as 1. arduinoFFT picks its Arduino header with
// `#ifdef ARDUINO / #if ARDUINO >= 100`, so both "undefined" and "1" send it to
// the pre-1.0 WProgram.h, which no longer ships. 1.8.19 is the version FastLED's
// own emulation names in the comment beside its define.
#ifndef ARDUINO
#define ARDUINO 10819
#endif

// An Arduino core macro, used by arduinoFFT for its window weighting. A macro
// rather than a function for the same reason constrain below is one.
#ifndef sq
#define sq(x) ((x) * (x))
#endif

// -----------------------------------------------------------------------------
//  Clock. The harness advances it; everything else only reads it.
// -----------------------------------------------------------------------------
unsigned long& simNow();
void          simAdvance(unsigned long ms);

// FastLED's stub sysdefs declare these inside extern "C" with exactly these
// signatures. They are declared (not defined) here so every translation unit sees
// a compatible declaration, and defined once in sim_main.cpp.
extern "C" {
uint32_t millis(void);
uint32_t micros(void);
void     delay(int ms);
void     yield(void);
void     pinMode(uint8_t pin, uint8_t mode);
}

// -----------------------------------------------------------------------------
//  Types and helpers
// -----------------------------------------------------------------------------
// Several headers declare String members. std::string covers every operation they
// perform on it: construction from a literal, assignment, concatenation, c_str().
using String = std::string;

typedef unsigned char byte;
typedef volatile uint32_t RoReg;
typedef volatile uint32_t RwReg;

// Arduino's flash-string macro. There is no PROGMEM on the host.
#ifdef F
#undef F
#endif
#define F(x) x

// led_sysdefs_stub_generic.h defines INPUT/OUTPUT/INPUT_PULLUP and then #errors
// unless they are 0/1/2, so these values are not free choices. All five are the
// values Arduino itself uses.
#ifndef HIGH
#define HIGH 1
#endif
#ifndef LOW
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#endif
#ifndef OUTPUT
#define OUTPUT 1
#endif
#ifndef INPUT_PULLUP
#define INPUT_PULLUP 2
#endif

// Digital I/O. FastLED's sensors/ unit calls these, and pinMode is declared by
// led_sysdefs_stub_generic.h rather than here.
inline void digitalWrite(int, int) {}
inline int  digitalRead(int) { return LOW; }
inline void analogWrite(int, int) {}
inline int  analogRead(int) { return 0; }

// Arduino's own random, distinct from FastLED's random8/random16.
long random(long howbig);
long random(long howsmall, long howbig);
void randomSeed(unsigned long seed);

// A macro, deliberately, because that is what Arduino ships. A template could not
// deduce through the mixed-type calls the effect code makes, such as
// constrain(f.bass * 255, 50, 255) or constrain((uint8_t)wave, 15, 255).
#ifndef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

inline long map(long x, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return outMin;
    return (x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
}

// -----------------------------------------------------------------------------
//  Serial. Writes to stdout so a failed assertion is visible in context.
// -----------------------------------------------------------------------------
struct SimSerial {
    void begin(unsigned long) {}
    void flush() { std::fflush(stdout); }

    void write(const char* s) { std::fputs(s, stdout); }

    void print(const char* v)  { std::fputs(v, stdout); }
    void print(const String& v) { std::fputs(v.c_str(), stdout); }
    void print(char v)         { std::fputc(v, stdout); }
    void print(int v)          { std::printf("%d", v); }
    void print(unsigned v)     { std::printf("%u", v); }
    void print(long v)         { std::printf("%ld", v); }
    void print(unsigned long v){ std::printf("%lu", v); }
    void print(double v)       { std::printf("%g", v); }
    void print(double v, int digits) { std::printf("%.*f", digits, v); }
    void print(float v)        { std::printf("%g", v); }
    void print(float v, int digits) { std::printf("%.*f", digits, double(v)); }
    void print(bool v)         { std::fputs(v ? "1" : "0", stdout); }

    void println()             { std::fputc('\n', stdout); }
    template <typename T> void println(T v) { print(v); println(); }
    template <typename T> void println(T v, int digits) { print(v, digits); println(); }
};

extern SimSerial Serial;

// -----------------------------------------------------------------------------
//  ESP. getFreeHeap() is the loop's health gate and Debug.h's low-memory
//  report, getMinFreeHeap() is Debug.h's heap dump.
// -----------------------------------------------------------------------------
struct SimEsp {
    uint32_t getFreeHeap() const;
    uint32_t getMinFreeHeap() const;
};

extern SimEsp ESP;
