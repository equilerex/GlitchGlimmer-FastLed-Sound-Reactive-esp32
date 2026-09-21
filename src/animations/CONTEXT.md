# CONTEXT — animations
updated: 2026-09-21

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

## Selection and beat inputs

`AnimationProfile.h` holds one 7-axis target per animation in music space (intensity, activity, brightness, weight, pulse, tempo, texture). `kAny` leaves an axis out of the distance. The values are first estimates and are meant to be moved with the visualiser open on real audio. It is parallel to `AnimationCatalog.cpp`, which still carries the older single `intensity` and `preferredTempo` that `pickSceneByMood` uses for hand-built snapshots. Adding an animation means a row in both tables and an enum value; the profile table's `static_assert` catches a missing row.

`BeatClock.h` is what an animation draws beat-locked motion from. Read `f.beatPhase` raw and a sweep teleports on every relock and stalls when the tracker loses the beat. The clock free-runs at a fallback bpm and is pulled toward `beatPhase` in proportion to `beatConfidence`. Motion whose rate changes every frame (the riser and strobe) uses its own accumulator, because a phase multiplied by a changing factor jumps. `TensionRamp` follows `f.buildup`; `HoldLatch` and `HoldSelect` replace mode timers with a coordinate, a dead band and a minimum hold.

`ThemeAnimations.h` groups the small ports from the Serenity themes folder. Each was a free-running effect and now takes speed, density and pulse from the music.

## Gotchas

FastLED 3.10.3 ships 1D effects at `fx/1d/`, not `fl/fx/1d/`. `pacifica.h`, `pride2015.h` and `twinklefox.h` define members out of line with no `inline`. Include those wrappers from `AnimationCatalog.cpp` only. `fire2012.h` and `noisewave.h` are inline in the class and would survive a wider include, but they still stay off `AnimationCatalog.h` so the next ODR-unsafe fx cannot sneak back in.

Time models differ per class. Pacifica uses `DrawContext.now` for its delta, so the wrapper feeds a clock that starts at first update. NoiseWave arms `start_time` from `context.now` and then times with `millis()`, so the wrapper must pass wall `millis()` or the first frame jumps by boot time. Fire2012 ignores time.

Shipped fire uses `HeatColors_p`, which ends in white. The catalog fire passes an amber ramp into the constructor instead of rewriting the simulation.

Do not drive these through FastLED's `fl::audio::` detectors. This repo's feature model is `AudioFeatures`, and a second analysis would disagree with the classifier. Scale the finished buffer with `pixelLevel()`.

## Don't

Do not flip an animation's mode on a wall-clock timer: the scene selector owns variety, and a self-cycling animation fights it. Do not read `f.beatPhase` directly for motion; use `BeatClock`.

Do not flip an animation's mode on a wall-clock timer: the scene selector owns variety, and a self-cycling animation fights it. Do not read `f.beatPhase` directly for motion; use `BeatClock`.

Do not include a shipped-fx wrapper from a header. Do not put `VisualLayer` subclasses back in this folder. Do not wrap `Pride2015` as a groove base: it is a full-spectrum rainbow. Do not wrap `DemoReel100`: it cycles looks, which is a scene director's job. Do not copy FastLED headers into `src/fx/` to paper over ODR.
