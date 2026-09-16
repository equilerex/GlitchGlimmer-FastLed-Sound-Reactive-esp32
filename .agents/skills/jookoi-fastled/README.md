# jookoi-fastled

A Claude Code skill for writing LED animations with [FastLED](https://github.com/FastLED/FastLED) that look good to a human eye, not just correct to a compiler.

## Who built this

Maintained by [Joosep Kõivistik](https://www.linkedin.com/in/jokoivi/), a frontend engineer based in Copenhagen who builds addressable LED installations and sound-reactive wearables on ESP32 as a hobby.

The skill was written by Claude (Anthropic) in a working session with Joosep, then reviewed and corrected against his own projects and the FastLED source tree. The aesthetic material is his: it is distilled from animation notes he had already written for his own projects and from the audio pipeline he built for GlitchGlimmer, his sound-reactive ESP32 device. Claude's contribution was structure, verification against upstream source, and the connective reasoning about why each technique works.

## Why it exists

Three problems it was built to solve.

**FastLED documents its API, not its aesthetics.** The library reference tells you `fill_rainbow` takes an initial hue and a delta. It does not tell you that `255 / NUM_LEDS` truncates to zero on a strip longer than 255 LEDs, that `deltahue` of 5 looks pleasant on 30 LEDs and like a smear on 300, or that full-saturation rainbows read as harsh. That knowledge lives in scattered forum posts and in people's heads.

**LLM-generated LED code is reliably ugly.** Asked for an effect, a model tends to produce something that compiles and runs and looks like a demo reel from 2013. Full white. Every LED at saturation 255. Six hues at once. `delay()` in the loop. None of those are API errors, so nothing catches them.

**Widely-copied FastLED idioms are stale or subtly wrong.** The hand-rolled `XY()` helper that every matrix tutorial ships, the four-argument `blur2d` that silently binds to a symbol you were supposed to define, unguarded pin literals, `fl::` prefixes on functions that were never in a namespace. Each one costs an afternoon to debug, and each one is in a tutorial somewhere.

## What is in it

`SKILL.md` holds the load-bearing rules (no `delay()`, guard your pins, cap power, check what already ships before writing an effect) plus a routing table to eight reference files:

| File | Covers |
|---|---|
| `core-loop.md` | Frame pacing, blocking vs non-blocking, brightness, power, FPS |
| `color.md` | Palettes, gradients, blending, gamma, color temperature |
| `motion.md` | Sine, beats, noise, easing, speed control, mapping |
| `patterns-1d.md` | Fades, chases, rainbows, fills, segments, blur |
| `matrix-2d.md` | Coordinate mapping, 2D noise, blur, drawing |
| `effects.md` | Shipped effects and audio detectors, indexed by name |
| `look-and-feel.md` | Why an effect reads as harsh, flat, or cheap, and what to do about it |
| `rhythm-and-audio.md` | Smoothing, attack/release, normalization, beat detection, phase, state changes |

## What it deliberately is not

- **Not a hardware guide.** Wiring, level shifting, power supply sizing, chip selection, and driver bring-up are separate problems with separate answers. The skill assumes the strip works.
- **Not tied to Joosep's private libraries.** The animation and audio code in GlitchGlimmer and his older Arduino controller informed the technique, but no API from either appears in the skill. Every identifier in it resolves against upstream FastLED, so it works for anyone.
- **Not a rulebook.** Animation design is exploratory. `look-and-feel.md` says so explicitly, because a checklist that produces generic output has failed at its own job.

## Verification

Every API name, signature, and default in the reference files was checked against the FastLED source tree at commit `df106dfe6a`, not recalled from training data. That pass found errors in both directions. Widely-copied idioms that do not compile include `blur1` (the function is `blur1d`), the hand-rolled `XY()` helper, and the four-argument `blur2d`. It also found claims that were wrong in the other direction, including several in earlier drafts of this skill itself: `getBassLevel()` and its siblings do exist on `fl::audio::Processor`, `scale8_video(255, 255)` returns 255 rather than 254, and the `fl::` prefix belongs on fewer functions than it first appears, since the `platforms/` math headers declare `namespace fl` while `lib8tion.h`, `noise.h`, and `hsv2rgb.h` declare no namespace at all.

## Install

```
/plugin marketplace add equilerex/jookoi-ai-market
/plugin install jookoi-dev@jookoi-ai-market
```

The skill triggers on any request involving LED effects, strips, matrices, or animation code, including review and debugging of existing sketches.

## License

UNLICENSED. Personal toolkit, shared in case it is useful.
