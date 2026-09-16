# Color

FastLED has two pixel types and a pile of blending helpers. The helpers have sharp edges — argument order, which default applies, which one saturates — and most color bugs trace back to guessing instead of checking.

## Two pixel types

```cpp
fl::CRGB rgb(255, 0, 0);         // stored form; 3 bytes; what leds[] holds
fl::CHSV hsv(0, 255, 255);       // working form; hue/sat/val; what you animate
```

`CHSV` is `fl::hsv8` under an alias. Animating hue is far easier than animating RGB — a hue rotation is one addition, whereas the RGB equivalent is a conversion plus interpolation that can pass through muddy intermediate colors. **Animate in HSV, store in RGB.** If you find yourself interpolating two `CRGB` values to make a color transition, stop and ask whether the intent was a hue sweep.

Conversion is implicit and costs a lookup:

```cpp
fl::CRGB c = fl::CHSV(160, 255, 255);      // implicit
fl::CRGB c = hsv2rgb_rainbow(hsv);         // explicit, same thing
fl::CRGB c = hsv2rgb_spectrum(hsv);        // alternate mapping, wider greens
fl::CHSV back = rgb2hsv_approximate(c);
```

`hsv2rgb_rainbow` is the default and what the implicit conversion uses. It is not a physically correct HSV→RGB; it is tuned to look good on LEDs. Don't "fix" it.

Named colors are an enum inside the type, full HTML set:

```cpp
leds[i] = fl::CRGB::Red;
leds[i] = fl::CRGB::Aqua;
```

## Blending

```cpp
fl::CRGB fl::blend(const CRGB &p1, const CRGB &p2, fract8 amountOfP2);
```

**The amount is `p2`'s weight, not `p1`'s.** `blend(red, blue, 255)` is blue. This is the opposite of what most people assume, and it is the single most common color bug in hand-written FastLED code. The member form reads more clearly:

```cpp
fl::CRGB result = red.lerp8(blue, amount);   // amount is blue's weight - same semantics
```

The `CHSV` overload takes a direction code, which decides which way around the hue wheel it interpolates:

```cpp
fl::blend(hsv1, hsv2, amount, SHORTEST_HUES);   // default
fl::blend(hsv1, hsv2, amount, LONGEST_HUES);
fl::blend(hsv1, hsv2, amount, FORWARD_HUES);
```

For perceptual blending — where the midpoint of red→green should look like a plausible color rather than olive — use the newer opt-in:

```cpp
fl::CRGB fl::blend_oklab(a, b, amountOfB);
```

Plain `blend()` in RGB is what most effects want and is cheaper. Reach for `blend_oklab` when a gradient between two saturated colors looks dirty.

Byte-level helpers. The arithmetic ones are `fl::`; the interpolation ones are global:

```cpp
fl::blend8(a, b, amountOfB);           // single channel
lerp8by8(a, b, frac);                  // frac 0-255, global
fl::qadd8(i, j);                       // saturating add, clamps at 255
fl::qsub8(i, j);                       // saturating subtract, floors at 0
fl::avg8(i, j);
```

The rule of thumb: anything declared in `lib8tion.h` itself (`map8`, `lerp8by8`, the wave shapes) is global, while the `platforms/` headers (`scale8`, `math8`, `trig8`) declare `namespace fl` and are re-exported to global scope by `using` declarations. So both spellings compile for most of these — but only one is right, and the global ones cannot be written `fl::`.

`qadd8` is what you want when accumulating brightness — plain `+` wraps, and a wrapped channel is the classic "why did my fade explode into white". Note that `leds[i] += CRGB(...)` does saturate at the pixel level, so convenience operators are safe.

## Scaling and fading

```cpp
fl::u8 fl::scale8(fl::u8 i, fract8 scale);       // i * (scale + 1) / 256, truncating
fl::u8 fl::scale8_video(fl::u8 i, fract8 scale); // same, but rounds up when both are nonzero
fl::nscale8(CRGB *leds, fl::u16 num, fl::u8 scale);
fl::fadeToBlackBy(CRGB *leds, fl::u16 num, fl::u8 fadeBy);
leds[i].fadeToBlackBy(fadeBy);
```

`fadeToBlackBy(leds, n, f)` is literally `nscale8(leds, n, 255 - f)`. It is the workhorse of trail effects: draw new pixels, then fade the whole buffer slightly, and motion trails appear for free.

`scale8` vs `scale8_video`: `scale8` truncates (`scale8(128,128) == 64`), `scale8_video` adds one to every nonzero result (`== 65`). Both preserve full white. The video variant is one step brighter across the whole ramp, which is why it reads as less muddy at low brightness. Prefer `scale8` when the value is data you'll keep computing with, and `scale8_video` when the value is going straight to the LEDs.

`fadeToBlackBy` never reaches exactly zero on its own — repeated multiplication asymptotes. For a trail you want that. For a true clear, call `FastLED.clear()`.

## Palettes

A palette is a 256-entry lookup that maps a byte to a color. Effects then animate the *index*, which is why palettes pair so well with oscillators and noise.

```cpp
CRGBPalette16 myPal = CRGBPalette16(CRGB::Black, CRGB::Red, CRGB::Yellow, CRGB::White);
CRGBPalette16 heat  = HeatColors_p;
```

Built-ins that exist: `CloudColors_p`, `LavaColors_p`, `OceanColors_p`, `ForestColors_p`, `RainbowColors_p`, `RainbowStripeColors_p` (also accepted as `RainbowStripesColors_p`), `PartyColors_p`, `HeatColors_p`.

Custom gradients use a macro whose entries are `index, r, g, b`:

```cpp
DEFINE_GRADIENT_PALETTE( sunset_gp ) {
      0,   0,   0,   0,     // black
    128, 255,  80,   0,     // orange
    255, 255, 200, 120      // pale gold
};
CRGBPalette16 sunset = sunset_gp;
```

**The last entry must be at index 255.** The library enforces this now with a 256-entry cutoff, so a gradient that stops at 200 silently truncates rather than wrapping.

Lookup:

```cpp
fl::CRGB fl::ColorFromPalette(const CRGBPalette16 &pal, fl::u8 index,
                              fl::u8 brightness = 255,
                              TBlendType blendType = LINEARBLEND);
```

Two things bite here.

**The default blend mode differs by palette width.** `CRGBPalette16` and 32-bit/progmem variants default to `LINEARBLEND`, which interpolates between the 16 entries. `CRGBPalette256` defaults to `NOBLEND`, because a 256-entry palette needs no interpolation. Pass the mode explicitly if you're switching palette widths and want identical output.

```cpp
NOBLEND              // 0 - snap to nearest entry
BLEND / LINEARBLEND  // 1 - interpolate (same value, two names)
LINEARBLEND_NOWRAP   // 2
```

**The `brightness` argument is a scale, not a threshold.** `ColorFromPalette(pal, idx, 0)` returns black. It multiplies after lookup. Passing a varying brightness is a clean way to make one palette produce both bright and dim pixels from the same index.

For smoother gradients than 8-bit output allows:

```cpp
fl::ColorFromPaletteExtended(pal, fl::u16 index, brightness, blendType);
fl::CRGB16 fl::ColorFromPaletteHD(pal, ...);     // 16-bit per channel
```

## Making colors look right

Three knobs, in the order you should reach for them:

```cpp
FastLED.setCorrection(TypicalLEDStrip);   // per-chipset channel correction
FastLED.setTemperature(fl::CRGB(255, 170, 90));   // warm the white point
FastLED.setDither(BINARY_DITHER);         // extra perceived depth on fades
```

LEDs are not linear. A mathematically even fade from white to black looks like it holds bright then drops off a cliff. If a fade looks wrong, the fix is usually gamma or dithering rather than the fade arithmetic. `setDither` is the cheap answer; hand-rolled `dim8_video`-style curves are the expensive one. Try correction and dithering before writing a custom transfer function.
