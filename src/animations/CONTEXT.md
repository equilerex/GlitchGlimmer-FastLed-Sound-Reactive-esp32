# CONTEXT — animations
updated: 2026-09-18

<!-- All four sections stay even when briefly empty — an absent section is
     indistinguishable from an omission.

     Does not go in: anything the code plainly shows; API or parameter docs (those
     belong in code); changelog or commit history (git has it); general framework or
     language behaviour (the model has it).
     Test: if removing a line would not slow a newcomer down, cut it. -->

## What this is

Catalog base animations. Each is an `Animation` that owns the strip for a scene. Compositor accents live in `visual-layers/`, not here.

## Why it's built this way

A catalog entry is a full-strip field. A layer is an accent composited on top. Mixing the two in one folder made a listing look like one library with two naming conventions.

## Gotchas

FastLED 3.10.3 ships 1D effects at `fx/1d/`, not `fl/fx/1d/`. `pacifica.h`, `pride2015.h` and `twinklefox.h` define members out of line with no `inline`. Include those wrappers from `AnimationCatalog.cpp` only. `fire2012.h` and `noisewave.h` are inline in the class and would survive a wider include, but they still stay off `AnimationCatalog.h` so the next ODR-unsafe fx cannot sneak back in.

Time models differ per class. Pacifica uses `DrawContext.now` for its delta, so the wrapper feeds a clock that starts at first update. NoiseWave arms `start_time` from `context.now` and then times with `millis()`, so the wrapper must pass wall `millis()` or the first frame jumps by boot time. Fire2012 ignores time.

Shipped fire uses `HeatColors_p`, which ends in white. The catalog fire passes an amber ramp into the constructor instead of rewriting the simulation.

Do not drive these through FastLED's `fl::audio::` detectors. This repo's feature model is `AudioFeatures`, and a second analysis would disagree with the classifier. Scale the finished buffer with `pixelLevel()`.

## Don't

Do not include a shipped-fx wrapper from a header. Do not put `VisualLayer` subclasses back in this folder. Do not wrap `Pride2015` as a groove base: it is a full-spectrum rainbow. Do not wrap `DemoReel100`: it cycles looks, which is a scene director's job. Do not copy FastLED headers into `src/fx/` to paper over ODR.
