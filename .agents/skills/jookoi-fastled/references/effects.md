# Effects

## Check what already ships

This is the first thing to do and the thing most often skipped. FastLED ships a library of complete, tuned effects — the classic demos that most hand-written effects are reinventing badly.

```cpp
#include "fl/fx/1d/fire2012.h"
```

| Effect | Class | Notes |
|---|---|---|
| Fire | `fl::Fire2012` | `(numLeds, cooling=55, sparking=120, reverseDirection=false, palette=HeatColors_p)` |
| Pacifica | `fl::Pacifica` | Layered ocean waves |
| Twinkle | `fl::TwinkleFox` | Ships its own palettes: `Holly_p`, `Snow_p`, `Ice_p`, `RetroC9_p`, `RedWhite_p`, `BlueWhite_p`, `RedGreenWhite_p` |
| Cylon | `fl::Cylon` | Scanner |
| Pride | `fl::Pride2015` | Flowing rainbow, 2015 Pride flag |
| Noise wave | `fl::NoiseWave` | Noise-driven wave |
| Particles | `fl::Particles1d` | Physics-y particle system |
| Demo reel | `fl::DemoReel100` | Cycles through a set of effects every 10s |

2D, under `fl/fx/2d/`:

| Effect | Class |
|---|---|
| Animartrix (large family of visualizers) | `fl::Animartrix` |
| Noise + palette field | `fl::NoisePalette` |
| Luminova | `fl::Luminova` |
| Flow field | `fl::FlowField` |
| Wave | `fl::WaveFx` |
| Red square | `fl::RedSquare` |
| Blend two layers | `fl::Blend2d` |

If the user asks for one of these by name, the answer is to wire up the shipped class, not to write the algorithm. Writing a fresh fire effect when `fl::Fire2012` is one include away is wasted effort and rarely beats the shipped version.

The shipped effects are also parameterised rather than fixed. `Fire2012`'s palette argument is the lever for restyling it — pass a custom `DEFINE_GRADIENT_PALETTE` ramp that stops short of white for an ember look, or one that runs through blue and green for a "cold fire". Same simulation, entirely different effect, no new code. Before writing a variant from scratch, check whether a constructor argument already reaches it.

## Driving an effect

Every effect derives from `fl::Fx` and is driven through a `DrawContext`:

```cpp
struct DrawContext {
    fl::u32 now;                    // current time in ms
    fl::span<CRGB> leds;            // the buffer to write into
    u16 frame_time = 0;
    float speed = 1.0f;
    const AudioBatch *audio = nullptr;   // null when no audio
};
```

`draw(DrawContext)` is the only method an effect must implement. Everything else — pausing, fixed-frame-rate hints, naming — is optional.

```cpp
#include <FastLED.h>
#include "fl/fx/1d/fire2012.h"

#define NUM_LEDS 92
fl::CRGB leds[NUM_LEDS];

fl::Fire2012Ptr fire = fl::make_shared<fl::Fire2012>(NUM_LEDS, /*cooling=*/55, /*sparking=*/120, false);

void setup() {
    FastLED.addLeds<WS2812, FL_PIN_CLOCKLESS_1, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(128);
}

void loop() {
    fire->draw(fl::Fx::DrawContext(fl::millis(), leds));
    FastLED.show();
}
```

Effects are shared pointers — `fl::Fire2012Ptr`, `fl::AnimartrixPtr`, and so on, created with `fl::make_shared<>`. Passing the effect's own time in via `DrawContext(now, ...)` rather than letting it call `millis()` internally is what makes timing deterministic and testable.

## Switching between effects

`fl::FxEngine` owns a set of effects and transitions between them:

```cpp
fl::FxEngine fxEngine(NUM_LEDS);

void setup() {
    fxEngine.addFx(fire);
    fxEngine.addFx(animartrix);
}

void loop() {
    EVERY_N_SECONDS(10) { fxEngine.nextFx(500); }   // 500ms crossfade
    fxEngine.draw(fl::millis(), leds);
    FastLED.show();
}
```

`nextFx(transitionMs)` queues a blend into the next effect. This is the right structure for anything with more than one look — much better than a hand-rolled `switch` on an index, because the engine handles the transition.

## Writing a custom effect

When nothing shipped matches, subclass `Fx1d` or `Fx2d`:

```cpp
class MyEffect : public fl::Fx1d {
  public:
    MyEffect(fl::u16 numLeds) : Fx1d(numLeds) {}

    void draw(fl::Fx::DrawContext ctx) override {
        for (fl::u16 i = 0; i < ctx.leds.size(); i++) {
            ctx.leds[i] = ...;
        }
    }

    fl::string fxName() const override { return "MyEffect"; }
};
```

Writing it as an `Fx` subclass rather than raw code in `loop()` buys you `FxEngine` compatibility, transitions, and the audio pointer for free. It costs one class declaration. For anything beyond a throwaway, do it.

`Fx2d` holds an `XYMap` and exposes `xyMap(x, y)`, so a 2D effect writes `ctx.leds[xyMap(x,y)] = color` — see `references/matrix-2d.md`.

## Animartrix

Animartrix is a large collection of pre-built 2D visualizers — kaleidoscopes, lava, blobs, spirals, plasma. It is selected by an enum:

```cpp
#include "fl/fx/2d/animartrix.hpp"

fl::XYMap xyMap(WIDTH, HEIGHT, /*serpentine=*/true);
fl::Animartrix animartrix(xyMap, fl::AnimartrixAnim::POLAR_WAVES);
```

Options include `RGB_BLOBS`, `RGB_BLOBS2` through `5`, `POLAR_WAVES`, `SLOW_FADE`, `ZOOM`, `ZOOM2`, `HOT_BLOB`, `SPIRALUS2`, plus the kaleidoscope and lava families. See `examples/Animartrix/Animartrix.ino` for the dropdown-driven form that lets you browse them all.

Before building a bespoke plasma or kaleidoscope, browse that enum. The odds are good it already exists.

## Audio-reactive

The library has real audio analysis — not level detection, not an FFT you have to bolt on. Detectors live in `fl/audio/detector/`.

```cpp
// fl/audio/detector/beat.h
bool  isBeat() const;
float getBPM() const;
float getPhase() const;
float getConfidence() const;

// fl/audio/detector/multiband_beat_detector.h
float getBassEnergy() const;
float getMidEnergy() const;
float getTrebleEnergy() const;

// fl/audio/detector/equalizer.h
float getBass() const;
float getMid() const;
float getTreble() const;
float getVolume() const;
float getBin(int index) const;
float getDominantFreqHz() const;
```

Also available: `Downbeat`, `Backbeat`, `Note`, `DropDetector`, `BuildupDetector`, `ChordDetector`, `KeyDetector`, `MoodAnalyzer`, `DynamicsAnalyzer`, `EnergyAnalyzer`, `TempoAnalyzer`, `FrequencyBands`, `EqualizerDetector`.

Note the naming is not uniform — three of these drop the `Detector` suffix. Guess by reading the header, not by pattern.

If you're implementing detection yourself rather than using a shipped detector — a plain analog input, an envelope follower, a bare FFT — `references/rhythm-and-audio.md` has the mechanism: energy-ratio beat detection, refractory periods, attack/release envelopes, and normalization.

**Beware of `getBassLevel()` claims in both directions.** `getBassLevel()`, `getMidLevel()`, and `getTrebleLevel()` do exist, on `fl::audio::Processor` (`src/fl/audio/audio_processor.h:194-196`) and on `fl::audio::Reactive` (`src/fl/audio/audio_reactive.h:141-143`) — they are the coarse three-band split. `EqualizerDetector::getBass()` and friends are a different, finer API on the detector itself. A snippet using the level functions is not fabricated; a snippet using an invented name like `getSubBass()` is.

Effects receive audio through the draw context — `ctx.audio` is a `const AudioBatch*`, null when no audio is present. Guard for null before dereferencing:

```cpp
void draw(fl::Fx::DrawContext ctx) override {
    float bass = 0.0f;
    if (ctx.audio) { bass = ...; }     // else leave the effect in a non-audio state
}
```

An effect that divides by an audio level without a null check will crash the moment someone runs it without a microphone. Branch to a non-audio animation instead of guessing.

The detector outputs are `float` in normalized-ish ranges — multiply into your byte domain rather than assuming they are already 0-255.

## Combining what ships

The most interesting effects are usually shipped pieces arranged together: `fl::Blend2d` to layer a noise field over a base pattern, `fl::FxEngine` to crossfade between a fire and a kaleidoscope on a beat, `fl::ScaleUp` to render a small effect onto a larger matrix. Reach for composition before invention.
