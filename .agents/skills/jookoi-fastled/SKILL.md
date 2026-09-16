---
name: jookoi-fastled
description: Write LED animations and effects with the FastLED library - the animation loop, color and palette work, motion primitives, 1D and 2D matrix patterns, and complete effect recipes like fire, twinkle, breathing, and chases, plus what makes an effect actually read well to a human eye. Use this whenever the task involves making LEDs move, fade, pulse, shimmer, or react - including requests phrased as "make my strip do X", "I want a fire effect", "animate my matrix", "rainbow that speeds up with the beat", "why does my effect look harsh or flat", or any sketch touching WS2812/NeoPixel/APA102/SK9822 output. Also use it when reviewing, debugging, or tuning existing LED effect code, since several widely-copied FastLED idioms (the XY() helper, blur2d's short form, unguarded pin literals, fl:: on functions that are global) are stale or subtly wrong.
---

# jookoi-fastled

How to write LED effects that look good and run at a stable frame rate.

This skill is about what the pixels do. Wiring, level shifting, power supply sizing, chip selection, and driver bring-up are different problems with their own answers — this skill assumes the strip works and focuses on making it do something worth watching.

## Check what already ships, first

FastLED ships complete, tuned implementations of the effects people most often ask for — fire, Pacifica, twinkle, Cylon, pride, particles, plus a large family of 2D visualizers called Animartrix. They live in `fl/fx/1d/` and `fl/fx/2d/` as classes (`fl::Fire2012`, `fl::Pacifica`, `fl::TwinkleFox`, `fl::Animartrix`, …), and real audio analysis lives in `fl/audio/detector/`.

So when someone asks for a fire effect, the answer is usually to wire up `fl::Fire2012`, not to write a fire simulation. A hand-written version will be longer, run slower, and rarely look better than the shipped one. **Look up the effect name in `references/effects.md` before writing any named effect from scratch** — this is the highest-leverage step in the whole skill, and the one most often skipped.

Writing a custom effect is still right for anything genuinely novel, or when the shipped effect's parameters can't reach what the user wants. That's what the rest of this skill is for.

## The load-bearing idea

An LED effect is three things composed: **state** that persists across frames, **time** read non-blockingly, and **color** written into a buffer that gets pushed out. Almost every effect is a different arrangement of those three.

```cpp
// The shape every effect follows. Note what is NOT here: no delay().
void loop() {
    uint32_t now = millis();

    // 1. read time, derive a phase
    // 2. update state
    // 3. write color into leds[]
    // 4. push it out
    FastLED.show();
}
```

`FastLED.show()` is the only thing that touches the wire. Everything before it is arithmetic on an array in RAM. This matters because it means expensive math is cheap if it happens before `show()` — the cost that actually hurts is a `show()` you can't afford at your frame rate, or a `delay()` that blocks the loop.

## Non-negotiables

These are the errors that show up in nearly every hand-written FastLED sketch. Each one costs an afternoon to debug, which is why they're up front.

**Never leave a pin as a bare literal.** Not `#define DATA_PIN 5`, not `addLeds<WS2812, 5, GRB>`. An unguarded literal is a guess about hardware the code can't see, and it silently overrides whatever the platform declared.

What to write instead depends on whether you actually know the wiring:

- **You don't know the pin** (the common case) — consume the platform macro, so the board definition decides:
  ```cpp
  #ifndef PIN_DATA
  #define PIN_DATA FL_PIN_CLOCKLESS_1     // platform declares it; sketch/build flag still overrides
  #endif
  FastLED.addLeds<WS2812, PIN_DATA, GRB>(leds, NUM_LEDS);
  ```
  For clocked chipsets use `FL_PIN_SPI_DATA_1` / `FL_PIN_SPI_CLOCK_1`.

- **The user has told you their pin** — use their number, still behind a guard, so a build flag can override it. `FL_PIN_CLOCKLESS_1` is a per-platform default (it resolves to GPIO 4 on ESP32), which is not the user's wiring and would be wrong here. A guarded literal is correct:
  ```cpp
  #ifndef PIN_DATA
  #define PIN_DATA 5      // user's wiring; -DPIN_DATA=n still overrides
  #endif
  ```

The failure to avoid is the *unguarded* literal, not the literal itself. Full rule: `agents/docs/cpp-standards.md` → "Default Example Pins".

**Prefer `fl::` for types and functions, but know the exceptions.** `fl::` is the canonical namespace. `CRGB` and `CHSV` still resolve globally through compatibility aliases, and `src/crgb.h` asks new code to use `fl::CRGB` exclusively. However — these are **global, not namespaced**, and writing `fl::random8` will not compile:

- `random8`, `random16`, `random16_set_seed`, `random16_add_entropy`
- the `EVERY_N_*` macros
- `map()` (Arduino's), `map8()`, `lerp8by8()`

**Never `delay()` inside an effect.** `delay()` freezes the loop: no input, no new frame, no state advance. `FastLED.delay(ms)` is different — it repeatedly calls `show()` to drive dithering — but it still blocks, so it belongs only in a deliberately static sketch. For animation, use `EVERY_N_MILLISECONDS` or compare `millis()` yourself. See `references/core-loop.md`.

**Cap brightness and power.** A 300-LED strip at full white draws ~18A. `FastLED.setBrightness()` and `FastLED.setMaxPowerInVoltsAndMilliamps()` are not optional polish; they're the difference between a working installation and a browned-out microcontroller. Details in `references/core-loop.md`.

**Matrix effects: there is no `XY()` function.** It is not in the library. `examples/XYMatrix/XYMatrix.ino` hand-rolls its own, which is why the idiom spread — but every sketch writing its own `XY()` is duplicated work and a different bug. Use `fl::XYMap`, and see `references/matrix-2d.md` before writing any 2D indexing.

## Where to go next

Read the reference that matches the effect, not all of them.

| You're building | Read |
|---|---|
| Anything — frame pacing, blocking vs non-blocking, brightness, power, FPS | `references/core-loop.md` |
| Colors, palettes, gradients, blending, gamma, color temperature | `references/color.md` |
| Movement over time: sine, beats, noise, easing, speed control | `references/motion.md` |
| Reacting to music or a sensor: smoothing, attack/release, normalization, beat detection, phase, state changes | `references/rhythm-and-audio.md` |
| Strip patterns: fades, chases, rainbows, fills, segments, blur | `references/patterns-1d.md` |
| Matrices and panels: coordinate mapping, 2D noise, blur, drawing shapes | `references/matrix-2d.md` |
| A specific named effect: fire, twinkle, breathing, waves, audio-reactive — **check here first** | `references/effects.md` |
| Making something look good — brightness, hue discipline, motion, rhythm, layering, why an effect reads as harsh or flat | `references/look-and-feel.md` |

## How to work

**Compose from primitives before inventing.** The library already has fades (`fadeToBlackBy`), blurs (`blur1d`), palettes (`ColorFromPalette`), oscillators (`beatsin8`), and noise (`inoise8`). A "new" effect is almost always two of these arranged differently. Reaching for these first produces shorter code that runs faster and behaves predictably.

**Persist state in file scope, not locals.** A `static` or file-scope variable survives across `loop()` calls; a local re-initializes every frame, so the animation restarts 60 times a second. This is the single most common cause of "my effect doesn't move".

**Check the effect looks right, not just that it compiles.** Compilation proves nothing about whether the animation reads well. If you can't run it on hardware, say so plainly rather than presenting untested output as working. Reasoning about the arithmetic — what happens at frame 0, at the wrap point, at `NUM_LEDS`, when `NUM_LEDS` is 1 or 0 — catches most of what hardware would.

**Treat the look as a design problem, not a spec.** Effects that read well are found by trying things, not by satisfying a list. `references/look-and-feel.md` is a vocabulary of dials — brightness as the strongest axis, how many hues, motion speed ceilings, trails, rhythm as contrast, layering, why noise turns to mush — not a set of requirements. Use it to reason about *why* something looks flat or harsh, then make a choice. If asked for something pleasant without further direction, pick a clear intention (calm and drifting, or tense and rhythmic) and commit to it rather than averaging several.

**Make the output tunable.** Put the expressive parameters in named file-scope constants at the top (`kHueBase`, `kSat`, `kTrailFade`, `kSpeed`), not buried in draw code. The user's next step is always to fiddle, and a sketch with visible dials is far more useful than a polished fixed one. Say which dials matter and what range is interesting.

**Reactive effects need a filter chain, not a direct wire.** Mapping a sensor or audio value straight into a pixel value produces a strobe. Smoothing, asymmetric attack/release, and normalization against a rolling peak and floor are what make a reactive effect look composed rather than twitchy; `references/rhythm-and-audio.md` covers the chain from raw signal to a control value that reads well at 60 fps.

**When the user is on ESP32 specifically**, the animation code is identical to any other target. The differences live in build and pin configuration, which belong to `agents/docs/build-system.md` and the `esp32-arch-review` skill, not here.
