# Motion

Effects move because something varies over time. The library gives you three families of oscillator — beats, trig, and noise — and they cover nearly every case. Picking the right family is most of the design work.

## Beats

Beats are time-driven oscillators. You give them a rate in BPM and they hand back a value. They need no state of your own, which makes them the cheapest way to add periodic motion.

```cpp
fl::u8  fl::beatsin8(accum88 bpm, fl::u8 lowest = 0,  fl::u8 highest = 255,
                     fl::u32 timebase = 0, fl::u8 phase_offset = 0);
fl::u16 fl::beatsin16(accum88 bpm, fl::u16 lowest = 0, fl::u16 highest = 65535,
                      fl::u32 timebase = 0, fl::u16 phase_offset = 0);
fl::u16 fl::beat88(accum88 bpm88, fl::u32 timebase = 0);   // sawtooth
fl::u16 fl::beat16(accum88 bpm,  fl::u32 timebase = 0);
fl::u8  fl::beat8(accum88 bpm,  fl::u32 timebase = 0);
```

`bpm` is an `accum88` — 8 integer bits, 8 fractional — so you can pass a fractional rate and, more usefully, compute one at runtime. An effect whose speed follows a knob or a beat is just `beatsin8(speed)` where `speed` is a variable.

The `phase_offset` argument is newer than most example code. It lets two oscillators at the same rate run out of step without a timebase trick:

```cpp
fl::u8 a = fl::beatsin8(20, 0, 255, 0, 0);     // in phase
fl::u8 b = fl::beatsin8(20, 0, 255, 0, 128);   // exact half-cycle behind
```

Four-argument calls still compile, so old code works — but when you see a hand-rolled offset using `millis()` math, `phase_offset` replaces it.

`timebase` shifts the whole oscillator's clock. Passing `0` uses the current time.

## Trig

```cpp
fl::u8  fl::sin8(fl::u8 theta);    // theta 0-255 is one full cycle; returns 128-centered
fl::i16 fl::sin16(fl::u16 theta);  // 0-65535 is one cycle; note the signed return
fl::u8  fl::cos8(fl::u8 theta);
fl::i16 fl::cos16(fl::u16 theta);

// other wave shapes, same 0-255 angle convention (global, not fl::)
fl::u8  triwave8(fl::u8 theta);     // triangle
fl::u8  quadwave8(fl::u8 theta);    // smoother, quadratic
fl::u8  cubicwave8(fl::u8 theta);   // smoother still
```

A triangle or cubic wave is a different *feel* rather than a different rate — `triwave8` gives a mechanical sweep, `cubicwave8` a soft one. Reaching for a wave shape you didn't consider is often the fix for "the motion looks robotic", and it costs nothing because the shapes are pure lookup tables.

Note the return types: `sin16`/`cos16` return **`int16_t`**, signed, centred on zero. Code declaring them `uint16_t` is wrong and will produce surprising wrap behaviour near the zero crossing.

Fixed-point angle, no radians, no floats. The full circle is the *range* of the input type — 256 for `sin8`, 65536 for `sin16` — which is why effects index them with a byte phase.

`sin8` returns 0-255 with 128 as zero crossing. To oscillate around a center value:

```cpp
uint8_t centre = 128, amp = 64;
uint8_t v = centre + ((int16_t)fl::sin8(phase) - 128) * amp / 128;
```

Prefer `beatsin8` when the oscillation has a BPM. Reach for raw `sin8` when the phase is driven by something else — a pixel's position, a noise value, an audio level. Using `sin8` where `beatsin8` fits means reimplementing rate tracking by hand.

## Noise

Noise is the family for anything organic: fire, clouds, water, breathing, drift. It returns smoothly varying pseudo-random values — neighbouring inputs give neighbouring outputs, which is what separates it from `random8`.

```cpp
fl::u8  inoise8(fl::u16 x);
fl::u8  inoise8(fl::u16 x, fl::u16 y);
fl::u8  inoise8(fl::u16 x, fl::u16 y, fl::u16 z);
fl::u16 inoise16(fl::u32 x, ...);
fl::u16 snoise16(fl::u32 x, ...);     // simplex variant
```

Coordinates are **8.8 fixed point**, so the units are not pixels. A scale factor of roughly 100-200 times the pixel index is the usual starting range:

```cpp
leds[i] = fl::CHSV(hue, 255, inoise8(i * 60, millis() / 20));
```

That 60 is the whole tuning knob. Smaller values stretch the noise features out — smoother, more gradual sweeps. Larger values compress them — choppier, more turbulent. If an effect looks like static, the scale is too large. If it looks like a flat wash, too small.

The extra axes are free spatial dimensions, and `millis()` fed into a z or w axis is the standard way to animate noise over time:

```cpp
uint8_t n = inoise8(x * 80, y * 80, millis() / 30);   // 2D field, evolving
```

There is no `noise16_1`, `noise16_2`, or `noise16_3` in this library. Older snippets reference them; they do not exist. Use `inoise8` / `inoise16` / `snoise16`, or the fill helpers:

```cpp
fill_noise8(CRGB *leds, int num, fl::u8 octaves, fl::u16 x, int scale,
            fl::u8 hue_octaves, fl::u16 hue_x, int hue_scale, fl::u16 time);
fill_2dnoise8(CRGB *leds, int width, int height, bool serpentine, ...);
```

These fill a whole strip or matrix from noise in one call, including a separately-noised hue channel — which is how you get fire whose color varies as much as its brightness.

## Mapping and ranges

```cpp
fl::u8  map8(fl::u8 in, fl::u8 rangeStart, fl::u8 rangeEnd);      // global
long    map(long, long, long, long, long);                        // Arduino's, global
fl::map_range<T,U>(value, in_min, in_max, out_min, out_max);      // typed, fl::
fl::map_range_clamped<T,U>(...);                                  // typed + clamped
```

Arduino's `map()` is integer-only and unclamped — a value outside the input range produces an output outside the output range, silently and sometimes wrapping. `map_range_clamped` is the modern form and is what you want at a system boundary where the input might be out of spec. Use `map8` for byte-to-byte.

## Random

```cpp
random8();              // 0-255
random8(limit);         // 0..limit-1
random8(min, limit);    // min..limit-1
random16();
random16_set_seed(s);
random16_add_entropy(e);
```

These are **global — do not write `fl::random8`**, it will not compile.

`random8` is not noise: consecutive calls jump anywhere in range, which is right for sparkle and wrong for anything that should drift. Reach for `random8` when you want a pixel to pop; reach for `inoise8` when you want the strip to breathe.

Seed once in `setup()` for reproducible effects, or add entropy from an unconnected analog pin if you want a different look each boot. Without seeding, the sequence restarts identically every power cycle — which surprises people testing a "random" effect.

## Easing

There is no easing library. Three approaches, in order of how often they're the right answer:

**Interpolate with a scaled fraction.** For a transition between two states, keep a 0-255 progress value and lerp:

```cpp
fl::CRGB c = fl::blend(a, b, progress);      // progress is b's weight
uint8_t v  = lerp8by8(from, to, progress);   // lerp8by8 is global
```

**Shape the progress curve with `sin8`.** A sine applied to a linear progress gives ease-in-out for free, because it starts and ends slow:

```cpp
uint8_t eased = fl::sin8(progress / 2 + 64);   // 0..255 in, S-curve out
```

Actually a cleaner statement of the same trick: `ease = sin8(p/4)` maps p from 0-255 to a half-sine. Either way, no floats and no lookup table.

**Accumulate toward a target.** For drift-toward-value behaviour, move a fixed fraction of the remaining distance each frame:

```cpp
current += (target - current) / 8;    // ints fine; exponential approach, never quite arrives
```

That last one is the honest version of "smoothly move toward X" — it decelerates as it closes, which is what reads as smooth. It also never exactly arrives, so add a deadzone if exactness matters.
