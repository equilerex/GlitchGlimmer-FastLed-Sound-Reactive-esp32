 
## 🎆 **FastLED Animation API – Common Methods & Tools**


FastLED doesn't have built-in named animations like WLED or NeoPixelFX, but it provides low-level tools to **build your own**.

Below is a list of **tools**, **functions**, and **concepts** you’ll use to create animations.

---

### 🧰 **Core Methods & Helpers**

| Function                                         | Purpose                                    |
| ------------------------------------------------ | ------------------------------------------ |
| `leds[i] = CRGB::Red;`                           | Set color directly                         |
| `leds[i] = CHSV(hue, sat, val);`                 | Set color using HSV for smooth transitions |
| `leds[i].fadeToBlackBy(x);`                      | Fade a pixel toward black                  |
| `fill_solid(leds, count, color);`                | Fill segment with solid color              |
| `fill_rainbow(leds, count, startHue, deltaHue);` | Auto rainbow fill                          |
| `blur1(leds, count, blurAmount);`                | Spatial blur across pixels                 |
| `fadeToBlackBy(leds, count, fadeAmount);`        | Fade entire strip                          |
| `nblend(leds[i], newColor, blendAmount);`        | Blend new color into current               |
| `EVERY_N_MILLISECONDS(30) {}`                    | Time-based looping                         |
| `beatsin8(BPM, low, high)`                       | Get a sinusoidal oscillation               |
| `inoise8(x, y)` / `inoise16()`                   | Perlin noise generation                    |
| `ColorFromPalette(palette, index, brightness)`   | Indexed palette lookup                     |

---

### 🌈 **Palette Tools**

| Function                          | Purpose                        |
| --------------------------------- | ------------------------------ |
| `CRGBPalette16`                   | A 16-color palette             |
| `CHSVPalette16`                   | HSV-based palette              |
| `currentPalette = PartyColors_p;` | Use built-in palette           |
| `setupPalette()`                  | Create a custom palette        |
| `ColorFromPalette()`              | Get color based on 0–255 index |

---

## ✨ Animation Techniques & Concepts

Creating great FastLED animations isn’t just code — it’s visual design. Here are **conceptual tools and design rules** for better animations.

---

### 🎨 **1. Use Hue Cycling for Visual Flow**

* Shift a global hue over time to create motion and color evolution.

  ```cpp
  static uint8_t gHue = 0;
  EVERY_N_MILLISECONDS(10) { gHue++; }
  ```

---

### 🌊 **2. Create Movement**

Simulate motion by:

* Shifting pixel values (manual scrolling)
* Using `beatsin8()` to pulse or bounce
* Applying `blur1()` or `fadeToBlackBy()` to leave trails

---

### 🔀 **3. Use Layers (if possible)**

Break effects into:

* **Background layer**: color wash, rainbow, fire
* **Motion layer**: moving dots, sparkles, waves
* **Highlight layer**: pulses on beat, sudden color shifts

Use blending like `nblend()` or `add()` to combine effects.

---

### 🎵 **4. Make It Audio/Music-Reactive**

If you have audio input:

* Change brightness based on volume
* Trigger sparkles or flashes on beat
* Shift speed or hue on detected bass hits

---

### 🧠 **5. Keep Animations Stateless When Possible**

Avoid complex state unless necessary. Instead:

* Derive values from `millis()` or `beatsin8()` to animate
* Use `EVERY_N_MILLISECONDS()` for timing control
* Use `gHue`, `phase`, `cycle`, `index` variables to drive motion

---

### 🎯 **6. General Animation Guidelines**

| Rule                    | Tip                                           |
| ----------------------- | --------------------------------------------- |
| ✅ Smooth motion         | Avoid sudden pixel jumps                      |
| ✅ Avoid full white      | It's jarring and power-hungry                 |
| ✅ Use fade/blend        | For visual smoothness                         |
| ✅ Limit color count     | 2–3 primary hues per effect is enough         |
| ✅ Vary brightness       | Don't make all pixels equally bright          |
| ✅ Create rhythm         | Pulses, builds, and releases are satisfying   |
| ✅ Keep CPU low          | No `delay()`, limit loops to <2ms/frame       |
| ⚠️ Avoid noise overload | Noise is beautiful, but mix it with structure |

---

## 🧪 Example Techniques

### ⚡ Sparkle Effect

```cpp
fadeToBlackBy(leds, NUM_LEDS, 40);
int pos = random16(NUM_LEDS);
leds[pos] += CHSV(gHue, 200, 255);
```

### 🌈 Rainbow Flow

```cpp
fill_rainbow(leds, NUM_LEDS, gHue, 7);
```

### 💫 Sine-Wave Motion Dot

```cpp
int pos = beatsin16(13, 0, NUM_LEDS - 1);
leds[pos] = CHSV(gHue, 255, 255);
```

### 🔥 Fire/Heat Map (using noise)

```cpp
for (int i = 0; i < NUM_LEDS; i++) {
  uint8_t heat = inoise8(i * 10, millis() / 3);
  leds[i] = ColorFromPalette(HeatColors_p, heat);
}
```

---

## ✅ Ideal Copilot Prompt Seed (Boilerplate for Code Suggestions)

> Write a FastLED animation for an ESP32 using a static `CRGB leds[NUM_LEDS]` array. Use `FastLED.addLeds<>()` in setup and update the display in `loop()` with `FastLED.show()`. Avoid using `delay()`. Use `EVERY_N_MILLISECONDS()` for timing, and animate using functions like `fill_rainbow()`, `fadeToBlackBy()`, `CHSV`, `ColorFromPalette()`, and `beatsin8()`. Use `gHue` to create smooth hue shifts over time. Avoid full white. Use trails, blending, and rhythm to make it visually pleasing. Keep the animation stateless where possible.

---
 
### 🔧 **Brightness & Power Management**

* `FastLED.setBrightness(x);` // 0–255
* `FastLED.setMaxPowerInVoltsAndMilliamps(5, 500);` // auto-dim if needed
* **Note**: Brightness affects `.show()`, not the raw buffer

---
