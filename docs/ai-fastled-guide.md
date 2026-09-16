### FastLED Animation Guidelines
***Intro**:
> You're coding an ESP32 LED animation using FastLED. `leds[]` is a statically declared CRGB array. You can't change its size at runtime. Use `FastLED.addLeds<>()` in `setup()`, then animate in `loop()` using `EVERY_N_MILLISECONDS()`. Call `FastLED.show()` to push changes. Avoid `delay()`. Don't call `FastLED.clear()` unless followed by new pixel values and `.show()`. Use `CHSV()` for hue-based color, or `ColorFromPalette()` for smoother effects. Never reassign the `leds` array. Prefer global buffers and low-CPU drawing.

**Core Principles**:
- Use stateless design: Derive values from `millis()` or `beatsin8()` for timing.
  - Smooth transitions: Employ `fadeToBlackBy()`, `nblend()`, `blur1d()` to avoid abrupt changes.
  - Power management: Limit brightness, avoid full white; use palettes for efficiency.
  - Reusability: Modularize effects (e.g., layers for background/motion).
  - Timing: Use `EVERY_N_MILLISECONDS` for non-blocking loops.
  - Testing: Start with small `NUM_LEDS`; simulate in tools like FastLED Animator.
  - Community Tips: Read code from GitHub examples; join r/FastLED for feedback.

**Best Practices**:
- Avoid `delay()`; use timing macros.
  - Palettes for color harmony: `CRGBPalette16` or built-ins like `HeatColors_p`.
  - Noise for organic effects: `inoise8()` for fire/waves.
  - Layers: Combine effects with `nblend()` (background + foreground).
  - Reusable Helpers: Extract common functions (e.g., glitter, twinkle).

### Cheat Sheet: Useful API Parts
**Setup**:
- `FastLED.addLeds<CHIPSET, DATA_PIN>(leds, NUM_LEDS);` – Add strip.
  - `FastLED.setBrightness(val);` – Global brightness (0-255).
  - `FastLED.show();` – Update LEDs.

**Color Setting**:
- `leds[i] = CRGB(r, g, b);` – Set RGB.
  - `leds[i] = CHSV(h, s, v);` – Set HSV (hue 0-255, sat/val 0-255).
  - `fill_solid(leds, NUM_LEDS, color);` – Fill all.

**Animations/Effects**:
- `fill_rainbow(leds, NUM_LEDS, hue, delta);` – Rainbow fill.
  - `fadeToBlackBy(leds, NUM_LEDS, fadeAmt);` – Fade (0-255).
  - `nblend(leds[i], newColor, amt);` – Blend colors (0-255).
  - `blur1d(leds, NUM_LEDS, blurAmt);` – Blur strip.
  - `addGlitter(chance);` – Random white sparkles.

**Timing/Utilities**:
- `EVERY_N_MILLISECONDS(ms) { code; }` – Timed execution.
  - `beatsin8(bpm, min, max);` – Sine wave value.
  - `inoise8(x, y);` – Perlin noise.
  - `ColorFromPalette(palette, index, bright);` – Palette lookup.

**Common Patterns**:
- Moving dot: `leds[pos] = color; pos = beatsin8(bpm, 0, NUM_LEDS-1);`.
  - Fade trails: `fadeToBlackBy(leds, NUM_LEDS, 20);`.

### Most Loved Community Utilities
- **Glitter Helper**: Add random white flashes (from r/FastLED): `void addGlitter(fract8 chance) { if (random8() < chance) leds[random16(NUM_LEDS)] += CRGB::White; }`.
  - **Twinkle Effect**: Sparse random lights (from Arduino Forum): `if (random8() < density) leds[random16(NUM_LEDS)] = CHSV(random8(), 200, 255); fadeToBlackBy(leds, NUM_LEDS, fade);`.
  - **Fire Simulation**: Perlin noise flames (from FastLED GitHub examples): Use `HeatColors_p` palette with `inoise8` for heat map.
  - **Palette Blending**: Smooth color transitions (from Tweaking4All): `blendPalette(targetPalette, currentPalette, blendAmt);`.
  - **FastLED Animator**: Open-source simulator for prototyping animations (GitHub: FastLED-Animator).
  - **Reusable Layers**: Function to combine effects (from MakerForums): `void layerBlend(CRGB* base, CRGB* overlay, uint8_t amt) { nblend(base, overlay, NUM_LEDS, amt); }`.
  - **Chess & Pygame Integrations**: For game-based animations (from Arduino Libraries).
  - **YouTube Tutorials**: Democode series for reusable patterns (e.g., breathing, waves).

From GitHub/FastLED wiki, Reddit r/FastLED, Arduino Forum, Tweaking4All.


---

### ⚠️ Gotchas / Must-Knows

| Issue                                     | Explanation                                                                 |
| ----------------------------------------- | --------------------------------------------------------------------------- |
| `leds[]` must be static                   | You **cannot realloc** or recreate `leds[]` after `FastLED.addLeds()`       |
| `.clear()` + `.show()` removes all pixels | Make sure you **update buffer again** afterward or animation appears frozen |
| No dynamic length change                  | You must re-flash to change strip length                                    |
| Global buffer required                    | If inside a class, `CRGB* leds` must point to a static/global buffer        |
| `.show()` must be called explicitly       | FastLED doesn’t auto-flush changes                                          |
| Watch for blocking delays                 | Use `EVERY_N_` macros or `millis()` timing, **not** `delay()` in animations |
| Power draw limits                         | Especially important for large strips — auto-dim with `setMaxPower...`      |
| No multi-strip built-in                   | You must manage each strip's `CRGB[]` and `.addLeds()` call manually        |

---

### 🧪 **Debug Helpers**

```cpp
Serial.print(leds[0].r);  // Inspect color components
```

---

### 🔄 **Palette Support**

* Define:

  ```cpp
  CRGBPalette16 palette = PartyColors_p;
  ```

  * Use:

    ```cpp
    leds[i] = ColorFromPalette(palette, index, brightness);
    ```

---

* **Useful macros**:

    * `EVERY_N_MILLISECONDS(ms) { }`
    * `EVERY_N_SECONDS(s) { }`

  * **Noise, sine, palette helpers**:

    ```cpp
    uint8_t noise = inoise8(x, y);  // Perlin noise
    uint8_t sine = beatsin8(BPM, low, high);
    ```

  * **HSV support**:

    ```cpp
    leds[i] = CHSV(hue, sat, val);
    ```

---