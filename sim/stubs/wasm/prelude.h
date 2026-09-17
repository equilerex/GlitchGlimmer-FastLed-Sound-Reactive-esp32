#pragma once

// Force-included ahead of every translation unit in the browser build by
// tools/build-wasm.sh.
//
// On Emscripten FastLED selects its own wasm platform, and that platform's
// <Arduino.h> forwards to the same platforms/stub/Arduino.h the native harness
// uses. So the Arduino emulation is FastLED's here rather than sim/stubs:
// putting sim/stubs on the include path as well pulls two emulations into one
// translation unit and redefines Serial and digitalRead against each other.
// This file adds only what that emulation does not provide.

#include <Arduino.h>

// FastLED's wasm timing source calls FASTLED_WARN but includes no header that
// defines it, and works in FastLED's own build only because that build has a
// precompiled header carrying it. Included here for the same effect.
#include "fl/warn.h"

#include <cstdint>
#include <string>

// FastLED's emulation announces ARDUINO as 1, which is below the 100 threshold
// arduinoFFT uses to choose between Arduino.h and the pre-1.0 WProgram.h, so it
// asks for a header that no longer ships. 1.8.19 is the version the comment in
// that emulation names.
#undef ARDUINO
#define ARDUINO 10819

// SceneDirector returns Arduino's String by value. The device gets the real class
// from the ESP32 core; here it is the standard string.
using String = std::string;

// FastLED's stub declares constrain as a function template, which cannot deduce
// through the mixed-type calls the effect code makes, such as
// constrain(f.bass * 255, 50, 255) or constrain((uint8_t)wave, 15, 255). Arduino
// ships it as a macro for exactly that reason. Redefined here, after the template
// has been declared and behind the include guard that stops the header being read
// again, so it masks calls without mangling the declaration.
#undef constrain
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))

// sq() is another macro from the Arduino core, which the ESP32 build gets from
// its own Arduino.h. arduinoFFT uses it for its window weighting.
#define sq(x) ((x) * (x))

// FastLED's emulation has no ESP object and a browser has no such heap.
// LEDStripController's memory check compares these against MIN_FREE_HEAP, so they
// report a plausible fixed figure rather than zero, which would trip the warning
// on every frame.
struct SimEsp {
    uint32_t getFreeHeap() const;
    uint32_t getMinFreeHeap() const;
};

extern SimEsp ESP;
