# GlitchGlimmer LED Controller

**GlitchGlimmer** is a modular, highly extensible, sound-reactive LED controller platform for ESP32, designed for creative, high-performance lighting setups. It aims to serve as a reusable foundation for future projects, supporting advanced animations, responsive displays, multi-strip control, and sensor integration.

---

## Features

- **Sound-reactive LED animations** powered by FFT and volume analysis from a digital I2S microphone (INMP441).
- **Modular animation architecture** allowing easy registration and dynamic switching.
- **Hybrid animation controller** that intelligently swaps animations based on music dynamics and context.
- **Smart adaptive display system** using a widget-based layout (via `GridLayout`) for clear, beautiful visuals.
- **Multiple screen size support** with dynamic, constraint-based layout.
- **Cyberpunk-style color themes** with pulse/beat visual feedback.
- **Waveform visualization**, frequency bars (bass/mid/treble), BPM, power (loudness), and control state (manual/auto).
- **Rotary encoder input support** (planned): Twist to change values, press to switch setting, auto-return to overview.
- **Future-proofed for**:
    - Multiple LED strip output channels with independent animations.
    - Sensor inputs (e.g., motion, light, touch).
    - Wi-Fi / Bluetooth control (e.g., remote web UI).
    - Persistent settings and user profiles.

---

## Use Case and Intent

GlitchGlimmer is not just a one-off controller — it’s designed to become your **go-to LED control platform** for all kinds of embedded art and lighting projects. Whether you're building a rave booth, ambient wall art, motion-reactive hallway lights, or a DJ stage setup, this platform can scale with your creativity.

Key priorities driving the design:

- **Reusability:** All components are designed as modular building blocks.
- **Performance:** Memory-conscious layout and minimal allocations.
- **Aesthetic control:** Pixel-perfect display layout and beat-synced animations.
- **Developer friendliness:** Clean APIs, detailed debug logging, and expandability.

---

## Hardware Requirements

- **ESP32-WROOM** or similar
- **INMP441 I2S microphone** (wired to default I2S pins)
- **WS2812 / WS2815 / APA102** LED strips
- **Optional:** Rotary encoder with button (for future input support)
- **Optional:** OLED/TFT display (TFT_eSPI-compatible, e.g. ILI9341)

---

## Display System

Display layout uses a responsive, constraint-based `GridLayout` with modular widgets. Every widget (e.g., BPM, power bar, waveform) specifies its minimum size, and the layout fills available screen space optimally.

Widget types include:

- `AcronymValueWidget` – e.g. `BPM:120`, `PWR:73`
- `VerticalBarWidget` – BASS, MID, TREB, etc. with rotated labels
- `WaveformWidget` – Real-time audio waveform
- `ScrollingTextWidget` – Long mode names scroll automatically
- Future: `IconSettingWidget`, `AnimatedBeatWidget`, etc.

Color themes and beat-pulse animations are applied via a `ThemeManager`.

---

## Sound Analysis

Audio input is captured via I2S and analyzed using:

- **RMS Volume**
- **FFT spectrum (via ArduinoFFT)**
- **Bass/Mid/Treble energy bands**
- **Simple beat detection via volume change delta**
- **Smoothed Loudness metric (PWR)**

These values drive both animations and display widgets.

---

## Animation System

Animations are registered using the `AnimationRegistry` and can be selected manually or via the `HybridController`.

Animations receive full `AudioFeatures` every frame, including:

- `volume`, `bass`, `mid`, `treble`, `spectrum[]`
- `bpm`, `beatDetected`, `loudness`

They also support internal parameters such as:
- Current time
- Frame rate
- Brightness
- Optional metadata (color variation, etc.)

---

## HybridController: Smart Auto-Mode Switching

The **HybridController** is the brain of GlitchGlimmer’s auto mode. Inspired by VJ setups, adaptive music visualizers, and cognitive rhythm detection, it uses a blend of logic and randomness to keep animations dynamic and musically coherent.

### Switching Logic Principles:

- **Every N seconds**, consider switching (default 20s)
- **If a beat is detected**, and power is above threshold, switch chance increases
- **If user is in manual mode**, switching is locked
- **modeKeepReason** explains why a mode is being kept:
    - `"BeatMatch"` – animation matches current rhythm style
    - `"Cooldown"` – recently switched, waiting
    - `"Manual"` – manual mode lock
    - `"Init"` – first animation

### Future Plans:
- Assign tags or styles to animations (e.g., “calm”, “chaotic”, “bass-reactive”)
- Use spectrum fingerprinting or ML clustering for deeper switching logic

---

## Architecture Highlights

- **Widget-driven layout** (like mini HTML/CSS components)
- **Smart grid manager** that avoids overlap and resizes automatically
- **All logic is encapsulated** and modular (DisplayManager, AudioProcessor, HybridController)
- **Serial debug logging** can be toggled with `#define ENABLE_DEBUG`
- **Minimal dynamic memory use**, most state is persisted across frames

---

## Developer Notes

- All code is located under `fastLed-dj-booth/`
- Widgets in `widgets/`, animation logic in `animations/`, themes in `themes/`
- To create your own animation: inherit from `AnimationBase` and register via `registerAnimation(...)`
- To add a new display widget: inherit `Widget`, override `draw()`, and `getMinWidth/Height()`

---

## Example Usage

```cpp
layout.addWidget(new AcronymValueWidget("BPM", (int)features.bpm));
layout.addWidget(new VerticalBarWidget("BASS", features.bass, theme.accent1));
layout.addWidget(new WaveformWidget(features.waveform, NUM_SAMPLES));
layout.draw(tft);
