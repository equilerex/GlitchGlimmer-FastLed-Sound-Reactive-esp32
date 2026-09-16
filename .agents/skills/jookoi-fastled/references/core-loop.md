# The Animation Loop

Everything an effect does happens between two `show()` calls. Get this structure right and most animation bugs disappear.

## Skeleton

```cpp
#include <FastLED.h>

#define NUM_LEDS 60
#ifndef PIN_DATA
#define PIN_DATA FL_PIN_CLOCKLESS_1
#endif

CRGB leds[NUM_LEDS];
uint32_t lastFrame = 0;          // file scope: survives across loop() calls

void setup() {
    FastLED.addLeds<WS2812, PIN_DATA, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(96);                              // cap it
    FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);        // cap it
}

void loop() {
    uint32_t now = millis();

    // advance state at a fixed rate, independent of loop speed
    if (now - lastFrame >= 16) {          // ~60 fps
        lastFrame = now;
        advance(now);
    }

    render();        // write leds[] from state
    FastLED.show();  // push
}
```

State lives at file scope. A local variable in `loop()` is recreated every frame, which is why a freshly written effect so often sits frozen — the animation advances one step and then resets, 60 times a second.

## Frame budget

Before choosing a frame rate, know what the wire costs. WS2812-family chipsets need ~30µs per LED (24 bits at 1.25µs/bit), and `show()` blocks for the whole transmission.

| LEDs | `show()` cost | Ceiling |
|---|---|---|
| 60 | ~1.8 ms | ~550 fps |
| 300 | ~9 ms | ~110 fps |
| 1000 | ~30 ms | ~33 fps |

So a 1000-LED strip cannot do 60fps on a single output no matter how fast the math is. Options: lower the target rate, split across multiple outputs (parallel transmission), or reduce LED count. On ESP32 the parallel-capable drivers exist for exactly this reason — but that is a build/driver concern, see `src/platforms/esp/32/ARCHITECTURE.md`.

Note the cost is per-`show()`, not per-LED-changed. Updating 3 LEDs and updating 1000 cost the same.

## Non-blocking timing

Use these instead of `delay()`. All are **global macros**, not `fl::`-namespaced.

```cpp
EVERY_N_MILLISECONDS(20) { ... }        // also spelled EVERY_N_MILLIS(20)
EVERY_N_SECONDS(5)       { ... }
EVERY_N_MILLISECONDS_I(timerName, 20) { ... }   // explicit name: multiple timers in one scope

// period computed at runtime - use this for speed that varies with input
EVERY_N_MILLISECONDS_DYNAMIC(periodFunc()) { ... }

// random period, re-rolled each fire - good for twinkle and sparkle
EVERY_N_MILLISECONDS_RANDOM(50, 400) { ... }
```

`_DYNAMIC` matters more than it looks. A common bug is wanting an effect whose rate follows a beat or a knob, then discovering the fixed `EVERY_N_MILLISECONDS` can't express it, then falling back to blocking code. Use `_DYNAMIC` with a function returning the current period.

For an effect that needs smooth sub-frame interpolation rather than discrete ticks, compare `millis()` directly and derive a continuous phase instead of using the macros at all.

## `delay()` vs `FastLED.delay()`

Plain `delay(ms)` is a busy sleep. The strip keeps showing the last frame, the loop does nothing, input is ignored. In an animation, it is a bug.

`FastLED.delay(ms)` is **not** a sleep. It repeatedly calls `show()` for the duration, which is how temporal dithering gets its extra bit of colour depth. It still blocks the loop, so it belongs only in sketches that deliberately show a static or slowly stepping image — a colour-calibration test pattern, a sensor readout. It has no place in a 60fps effect.

## Brightness

```cpp
FastLED.setBrightness(scale);        // 0-255, applied at output time
FastLED.setBrightness(255);          // full
```

Global brightness scales at `show()`, after the effect has written its colours. That ordering matters: dimming globally preserves the *relative* colour information the effect computed, whereas multiplying each pixel by a small factor inside the effect quantizes the colours and can crush subtle detail into identical values. Prefer `setBrightness()` for "make it dimmer"; use per-pixel scaling only when the *pattern itself* is the point (a fade, a distance falloff).

## Power limiting

```cpp
FastLED.setMaxPowerInVoltsAndMilliamps(5, 2000);   // 5V, 2A budget
FastLED.setMaxPowerInMilliWatts(10000);            // equivalent, wattage form
```

Call this in `setup()`, before the first `show()`. It does not measure current — it computes worst-case draw from LED count and chipset, then scales the output down until the estimate fits the budget. It is a static estimate, so it protects against the "full white on 300 LEDs" case rather than against a fault. It costs nothing at runtime and prevents the most common way a hobby project browns out.

Do not also try to compute the scaling yourself. Doing both double-dims.

## Frame rate and diagnostics

```cpp
FastLED.countFPS(25);      // must be called - enables FPS tracking over a 25-frame window
uint16_t fps = FastLED.getFPS();
```

`getFPS()` returns 0 until `countFPS()` has run. There is no `getFPSRaw()` in the library — if you see it in a snippet, the snippet is stale.

```cpp
FastLED.setMaxRefreshRate(100);              // throttle; call AFTER addLeds()
FastLED.setMaxRefreshRate(100, true);        // constrain: also suppresses the final partial show
```

Throttling is useful when an effect is deliberately running hot and you want to bound it — it makes the loop wait, so read the frame-budget table above first and prefer fixing the cost over throttling.

## Visual settings

```cpp
FastLED.setDither(BINARY_DITHER);            // default; trades a little temporal noise for colour depth
FastLED.setDither(DISABLE_DITHER);           // lower CPU, banding more visible on fades
FastLED.setCorrection(TypicalLEDStrip);      // colour correction per chipset
FastLED.setTemperature(Candle);              // shift the white point warmer/cooler
```

`setCorrection` and `setTemperature` are the cheap way to make two different strips look like one installation. Reach for them before writing custom per-channel scaling math.

These live on the `CFastLED` instance (`FastLED`), not as free functions — `fl::set_brightness()` is not the API.
