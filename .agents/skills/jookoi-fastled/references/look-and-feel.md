# Making it look good

Everything else in this skill is about making an effect *work*. This file is about making it *worth watching* — and it is deliberately not a checklist.

Aesthetic rules for LED work are descriptive, not prescriptive. They describe a design language that tends to read well, the way music theory describes why a progression lands without telling you what to write. The actual method is: try something, look at it, change one thing, try again. Most good effects were found by accident while fiddling. If you generate a sketch that satisfies every guideline below and looks generic, the guidelines have failed you, not the reverse.

So read this as a set of dials and what they do — then turn them.

## The one hard constraint: eyes aren't linear

This is the only non-negotiable fact in the file, and it's the usual root cause when an effect reads as harsh, muddy, or "cheap" despite the code being correct.

An LED's PWM duty cycle is linear in light output. Human brightness perception is roughly logarithmic. So 50% duty does **not** look half as bright — it looks around 73% as bright. The practical consequences:

- Two colors blended 50/50 in linear space land at a perceptual midpoint that looks wrong. `fl::nblend` blends in linear RGB. `fl::blend_oklab` blends perceptually and looks more like what you'd expect. `fl::ColorFromPalette` on a gradient has the same issue.
- Dimming a strip via `FastLED.setBrightness()` does not dim perception proportionally. Brightness at 32 does not look 1/8 as bright; it looks far dimmer than that. Sweeping brightness linearly through time reads as a fast collapse followed by a long slow crawl.
- `scale8(x, v)` scales linearly (`i * v / 255`, truncated) — correct for *data* (fading values, working in the middle of a computation). `scale8_video(x, v)` rounds each result up unless the input is zero, which pushes low values higher and approximates a video-style gamma. When a fade looks like it lingers then dumps to black, that's a linear scale where a video scale belonged.

If one thing in this file is worth internalizing as a rule of thumb: **reason about the middle of the range.** Effects that look bad usually look bad in the midtones, because that's where the linear/non-linear gap is widest. Both ends (off, max) look the same either way.

## Brightness is the strongest axis

Hue gets the attention, but brightness is what actually shapes an effect. A strip breathing through one hue reads better than a strip cycling six hues at constant brightness. Two colors at different brightnesses look designed; the same two at equal brightness look like a swatch.

Consequence worth exploiting: brightness variation is nearly free. Scaling an existing pattern's value costs one multiply and transforms how it reads. Effects that feel flat are usually tonally flat, not chromatically boring.

## Hue: how many, and which

The useful constraint is fewer hues than feels natural at first — **two or three per effect** covers most good work. Six hues is a rainbow, and a rainbow is a specific effect, not a general palette.

Relationships that read intentionally, in rough order of how forgiving they are:

- **Analogous** (neighbors on the wheel — cyan→blue→purple). Hard to make ugly. Good default for backgrounds and washes.
- **Complementary** (opposite — orange/blue, purple/yellow). High energy, easy to overdo. Works best when one side is small (an accent) and the other is the field.
- **Triadic / spread** — more interesting, more fragile. Wants a dominant hue with the others clearly subordinate.

**Saturation matters more than hue count.** Setting saturation to 255 everywhere is the single most common reason an effect looks harsh rather than rich — full saturation across a whole strip reads as a wall of primary color, and adjacent hues at max saturation vibrate against each other. Pull most of the strip toward 150–200 saturation and reserve 255 for small, transient highlights. The eye then has somewhere to rest.

Prefer `CHSV` over raw `CRGB` when you want to explore: hue, saturation, and brightness become independent dials, which is what makes fiddling productive.

## Motion

Speed has a perceptual ceiling that is much lower than the frame rate. At 60 fps almost any smooth motion is technically rendered — but past a certain pixels-per-second, a moving feature stops reading as *motion* and starts reading as *flicker*, because the eye can't track it. Slower than you think is almost always better. A dot crossing 300 LEDs in 2 seconds will read as movement; the same dot in 0.2 seconds reads as a strobe.

**Trails are motion blur.** `fadeToBlackBy(leds, n, amount)` instead of clearing each frame is the difference between a dot teleporting and a comet. This is one of the highest ratio techniques in the whole library — one line, transforms perceived smoothness, and it works on nearly any moving feature. `blur1d()` does the spatial version of the same thing.

**Linear motion reads as mechanical.** Anything driven by a raw ramp or a `+1` per frame accelerates instantly and stops dead. The library ships easing shapes ready to use: `triwave8` (linear triangle), `quadwave8` (eased, slow at the ends), `cubicwave8` (stronger ease). For motion that should feel physical, use one of these as the position curve instead of a straight accumulator. `beatsin8` / `beatsin16` give a sinusoid — the smoothest ease, and they take a BPM directly, which ties motion to musical time for free.

**Don't move everything at once.** A still background with one moving element reads as more alive than a field where every pixel is in motion. Motion needs a static referent.

## Rhythm

The reason pulses and builds are satisfying is contrast, not the pulse itself. An effect at constant intensity has no rhythm no matter how complex the pattern; an effect that alternates between *still* and *moving*, or *dim* and *bright*, has rhythm even if the pattern is trivial.

The shape that works: **build → hit → release → quiet.** Tension accumulates, resolves, and the release needs a moment of low intensity after it or the next build has nowhere to go. Effects built on a plain repeating pulse (`beatsin8(60, 0, 255)` forever) tend to feel mechanical for exactly this reason — every moment is the same distance from every other.

Practical levers: modulate something other than brightness on the beat (speed, hue, blur amount), and vary the *interval* — a pattern that hits every 2 seconds is a metronome, one that hits at 2s, then 1.5s, then 3s has a groove. `EVERY_N_MILLISECONDS` with a changing argument, or `beatsin8` at a tempo that drifts, gets you there.

## Layers

Effects that feel rich are usually two or three simple things stacked, not one complicated thing. Three roles cover most of it:

- **Field** — the base wash: a dim palette gradient, a slow noise field, a breathing background. Usually low brightness and low saturation. Sets color and mood.
- **Motion** — the thing that moves: dots, waves, particles, a scanner. This is what the eye tracks.
- **Accent** — brief, small, bright: sparkles on a beat, a flash, glitter. Present only sometimes, and at the highest brightness in the composition.

Combine with `fl::nblend(existing, overlay, amount)` per pixel, the array form `nblend(CRGB* existing, const CRGB* overlay, u16 count, fract8 amount)`, or `|=` / `+=` for additive (which naturally clips to white — sometimes what you want, often not).

The Accent layer is the one people skip and the one that most changes the result. A field plus motion is a nice effect; adding sparse bright events on top of it makes the same two layers feel like a system.

**Darkness is a resource.** Effects compete for the eye, and a strip that is lit everywhere has no focal point. Leaving 60–80% of pixels near-black most of the time costs nothing and makes the lit pixels legible. Sparse beats dense.

## Noise needs structure

Noise (`inoise8`, `inoise16`) is the fastest route to organic-looking output and the fastest route to mush. Left to fill a strip on its own it produces a field with no readable form — it moves, but nothing about it invites the eye.

Noise works when something gives it structure: quantize it through a narrow palette (`ColorFromPalette` with a handful of entries, not a full rainbow), threshold it so most of the field is off and only peaks show, or use it to *modulate* something that already has shape (a wave's amplitude, a dot's brightness) instead of as the primary color source.

## Two things that reliably look bad

**Full white.** It's jarring at any size, it clips all hue information, and it's the single worst case for power draw — a 300-LED strip at full white is roughly 18A. If you want "bright," use high brightness in one or two channels (amber, ice blue) rather than all three.

**Strobing.** Beyond looking cheap, full-field high-contrast flashing is a genuine photosensitivity hazard. The general accessibility threshold is 3 flashes per second — stay under it for anything that flashes the whole strip between extremes. Small-area sparkles, trails, and partial-field changes are fine at much higher rates because they don't drive the whole visual field.

## Make it tunable

The workflow here is fiddling, so the output should be easy to fiddle with. Put the expressive parameters in named file-scope constants at the top of the sketch rather than burying them in the draw code:

```cpp
uint8_t  kHueBase     = 140;   // field hue
uint8_t  kSat         = 180;   // field saturation - deliberately not 255
uint8_t  kTrailFade   = 20;    // lower = longer comet tails
uint8_t  kSpeed       = 2;     // hue advance per tick
uint8_t  kAccentRate  = 30;    // sparkle probability per frame
```

This costs a few lines and turns the sketch into something the user can actually explore instead of a fixed artifact. When you hand over an effect, say which dials matter most and roughly what range is interesting — that's more useful than a polished single configuration.

Consider also that the "correct" answer is usually not one answer. If two parameter settings are genuinely different in character (slow and drifting vs fast and jittery), it's reasonable to expose both and let the user pick.
