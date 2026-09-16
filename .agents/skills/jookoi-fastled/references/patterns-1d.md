# Patterns on a Strip

Two ways to write a 1D effect, and most confusion comes from mixing them.

**Stateless redraw** — clear the buffer, compute every pixel from scratch each frame, done. Easy to reason about, no hidden state, and correct by construction. Costs a full pass over the strip per frame.

**Incremental mutate** — leave the buffer alone and modify it in place: fade everything down a touch, add a new pixel, blur. Trail effects and particle systems work this way. Cheaper, and produces motion trails for free, but the buffer now carries state you have to reason about.

Neither is better. Unbounded trails that never fade are the symptom of picking the second without realising it.

## Fills

```cpp
fl::fill_solid(CRGB *arr, int num, const CRGB &color);
fl::fill_rainbow(CRGB *arr, int num, fl::u8 initialhue, fl::u8 deltahue = 5);
fl::fill_rainbow_circular(CRGB *arr, int num, fl::u8 initialhue, bool reversed = false);
fl::fill_gradient_RGB(CRGB *arr, u16 num, const CRGB &c1, const CRGB &c2);
fl::fill_gradient_RGB(CRGB *arr, u16 num, const CRGB &c1, const CRGB &c2, const CRGB &c3);
fl::fill_gradient(CRGB *arr, u16 num, CHSV c1, CHSV c2, TGradientDirectionCode d = SHORTEST_HUES);
```

`deltahue` in `fill_rainbow` is the hue step **per LED**, so strip length is part of the arithmetic: the same value on 30 LEDs and 300 LEDs produces completely different pictures. Since `deltahue` is a `u8`, the naive correction `255 / NUM_LEDS` truncates to **zero** on any strip longer than 255 LEDs — giving you a single flat color, not a rainbow. That truncation is a classic dead end.

To get exactly one spectrum across the strip regardless of length, use `fill_rainbow_circular(leds, NUM_LEDS, startHue)`, which derives the step from the array itself at 16-bit precision (`65535 / numToFill`, so it stays seamless even on very long strips). It is usually what people actually wanted from `fill_rainbow`.

One thing it fixes for you: `fill_rainbow_circular` hardcodes `sat = 240, val = 255`. There is no saturation parameter, so it cannot produce a pastel or dim rainbow — if the user wants one, write the loop yourself with `ColorFromPalette` or explicit `CHSV`.

To reason about what a given `deltahue` will produce: total hue span is `deltahue * NUM_LEDS`, and one full rainbow is 256. So `deltahue = 5` on 30 LEDs spans 150 (a bit over half a rainbow — pleasant), while the same value on 300 LEDs spans 1500 (~5.9 rainbows crammed in — a busy repeating smear, not a wash). This is why a rainbow that looks right on the bench looks wrong on the finished installation.

## Fades

```cpp
fl::fadeToBlackBy(CRGB *arr, u16 num, fl::u8 fadeBy);
fl::fadeLightBy(CRGB *arr, u16 num, fl::u8 fadeBy);
fl::nscale8(CRGB *arr, u16 num, fl::u8 scale);
leds[i].fadeToBlackBy(fadeBy);
```

`fadeToBlackBy(arr, n, f)` is `nscale8(arr, n, 255 - f)`.

`fadeLightBy` is **not** the same thing, despite being widely documented as an alias. `fadeToBlackBy` reaches black; `fadeLightBy` / `nscale8_video` is documented as guaranteed never to fade all the way to black. That difference is exactly what matters in a trail: with `fadeLightBy` you get a permanent dim residue, with `fadeToBlackBy` you get a clean background.

The trail idiom, in full:

```cpp
fadeToBlackBy(leds, NUM_LEDS, 40);     // everything dims a little
leds[pos] = fl::CRGB::White;           // the leading pixel is bright
```

For a clean frame, `FastLED.clear()` — repeated fading only asymptotes toward black and will leave faint residue on dim strips.

## Blur

```cpp
fl::blur1d(fl::span<CRGB> leds, fract8 amount);
```

A one-call softener. Blurring a field of noise or a hard-edged pattern is the fastest way to make procedural output look less synthetic — much cheaper than writing a smoothing pass, and it reads as glow on a diffused strip.

## Segments

`CRGBArray<N>` and `CRGBSet` (`CPixelView<CRGB>`) let you treat any sub-range as a strip:

```cpp
CRGBArray<NUM_LEDS> leds;
leds(0, 29).fill_rainbow(0, 8);          // first 30
leds(30, NUM_LEDS - 1).fadeToBlackBy(64);
leds(10, 20).fill_solid(CRGB::Blue);

leds.fadeToBlackBy(20);                   // whole array
leds(5, 15) = leds(20, 30);               // copy a range
```

This is the clean way to write effects on segmented installations — two strips treated differently, a gradient across zones — without index arithmetic scattered through the effect. `CRGBArray` converts to `fl::span<CRGB>`, so the free functions above accept it directly.

## Chases and scanners

The canonical chase is a fade plus a bright pixel, which is why it costs almost nothing:

```cpp
fadeToBlackBy(leds, NUM_LEDS, 64);

static uint8_t pos = 0;
leds[pos] = fl::CRGB::White;
pos = (pos + 1) % NUM_LEDS;
```

Two details decide whether this looks right. The `% NUM_LEDS` wrap is what makes it loop — without it the index walks off the end. And the fade amount controls trail length: small values give a long comet tail, values near 255 give a single sharp pixel.

For a bouncing scanner, keep a direction and reverse at the ends, or drive the position with a triangle wave instead and skip the direction state entirely.

## Rainbow, two ways

Procedural, cheap, uniform:

```cpp
fill_rainbow_circular(leds, NUM_LEDS, startHue++);
```

Palette-driven, tunable, and the better choice when the user will want to adjust it:

```cpp
for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = ColorFromPalette(RainbowColors_p, startHue + i * 3, 255, LINEARBLEND);
}
```

The palette version costs a lookup per pixel and buys you the ability to swap in `PartyColors_p` or a custom gradient without touching the loop. If the request is "a rainbow, and also make it match my brand colors", the palette is not optional.

## Compositing

```cpp
fl::nblend(CRGB &existing, const CRGB &overlay, fract8 amountOfOverlay);
fl::nblendPaletteTowardPalette(CRGBPalette16 &current, CRGBPalette16 &target, fl::u8 maxChanges);
```

`nblend` blends in place, which is what you want for layering a second effect over a first — draw the base, `nblend` the overlay at partial strength, and both contribute.

`nblendPaletteTowardPalette` moves a palette one step toward another, a bounded number of entries at a time. Called every frame, it produces a slow palette morph — the whole strip shifts mood over several seconds without any per-pixel blending cost. This is the idiomatic way to do "the colors slowly change", and it is far cheaper than interpolating every pixel toward a second palette.

## Reducing a pattern to fewer LEDs

For driving a short strip from a longer pattern, render into a small buffer and copy up, or sample the pattern at a stride. Scaling the *pattern* rather than the output is what preserves the look — scaling the output is just brightness.
