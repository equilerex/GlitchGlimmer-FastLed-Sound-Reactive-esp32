# !work in progress, not functional.!

# GlitchGlimmer Design & Automation Architecture

## ✨ Overview
GlitchGlimmer is a modular, audio-reactive **light show controller** for ESP32, designed for **fully automated** operation at parties, raves, DJ sets, or home events. either integrated into your outfit, your dj table or what ever fits the mood.
It combines **audio analysis**, **mood classification**, **scene management**, and **layer-based visual effects** to create a **dynamic, non-repetitive, music-synced light experience** — **without manual intervention**.

This document explores the inner workings, design principles, and practical use cases for GlitchGlimmer, making it an ideal project for DJs, event spaces, festivals, or anyone who wants vibrant, automated visual flair.

---


## Features
- ‐std=gnu++11 on Arduino-ESP32 3.0+.


- Multiple LED strip output channels with randomized animations.
- **Sound-Reactive Animations**: Utilizes FFT and volume analysis from a digital I2S microphone (e.g., INMP441) to drive LED animations.
- **Modular Animation Architecture**: Easily register and dynamically switch between animations.
- **Automated Animation Controller**: Intelligently swaps animations based on music dynamics and context.  Includes pulse/beat visual feedback with waveform visualization, frequency bars (bass/mid/treble), BPM, loudness, and control state indicators.
 
- **Smart adaptive display system** using a widget-based layout (via `GridLayout`) for clear, beautiful visuals.
- **Multiple screen size support** with dynamic, constraint-based layout.
 
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
 
 
 
 
## 🧬 Architecture Diagram

```
+------------------+      +---------------------+      +-------------------+
| Audio Analyzer    | ---> | Mood History Buffer  | ---> | Scene Director    |
+------------------+      +---------------------+      +---------+---------+
                                                            |
                                                            v
+--------------------------------+      +-------------------------------+
| Scene Definition (Catalog)     | ---> | LED Strip Controller (per strip) |
|  - Base Animation              |      |  - Run Base Animation           |
|  - Suggested Layer Types       |      |  - Inject Temporary Layers      |
+--------------------------------+      +-------------------------------+
```

---


## 🛠️ Design Principles

- **Layered Visual System:**  
  Every visual is a "layer" that can be added and removed dynamically.
  - *Base layer:* A core mood-driven animation
  - *Reactive layers:* Quick overlays (beat pop, energy flows, mood arcs)

- **Mood-Driven Scene Switching:**  
  Scenes are not switched randomly — they respond to the **feeling** of the music based on energy, tempo, and dynamics.

- **Memory Conscious Layer Pool:**  
  Layers are **pre-allocated** and **recycled** to avoid ESP32 heap fragmentation or memory leaks.

- **Smooth Lifecycle Management:**  
  Base animations **run indefinitely** until the scene changes.
  Temporary layers **fade out** automatically after a duration.

---

## 🧠 Audio Features Tracked

| Feature        | Description                              |
|----------------|------------------------------------------|
| Volume         | Overall sound pressure                  |
| Loudness       | Dynamic range of audio                  |
| Peak           | Max peak volume for quick hits           |
| Bass / Mid / Treble | Band energy levels                 |
| BPM Estimate   | Approximate beats per minute             |
| Beat Detection | Detected beat pulses                    |
| Spectrum Centroid | Center of gravity for frequencies    |
| Dominant Band  | Most powerful frequency band             |
| Dynamics       | Audio energy variation over time         |
| Signal Presence| Is there a signal (vs. silence)          |

---

## 🎛️ Scene Lifecycle Example

1. **Startup:**  
   → Default mood: *Calm*  
   → Pick a slow-moving tunnel animation

2. **Music Picks Up:**  
   → Mood shifts to *Energetic*  
   → Scene switch: faster pulse storm animation

3. **Drop Hits:**  
   → Beat detected + energy spike  
   → Temporary flash layers injected across strips

4. **Calm Breakdown:**  
   → Mood drops to *Floaty*  
   → Scene switch: slow neon waveform animation

---

## 🧑‍💻 Design Principles

- **Modular & Extensible**: New animations and layers can be added by registering a class in the catalog.
- **Low Coupling**: Audio, mood, animation, and rendering are separate concerns.
- **Predictable Switching**: Scene changes respect timing and require meaningful mood shifts.
- **Dynamic but Intentional**: Not every beat triggers chaos—logic curates the show.
- **Customizable**: Change strip layouts, layer behaviors, animation parameters.

---


## ✨ Why GlitchGlimmer?

✅ Fully automated party lighting system  
✅ Mood-aware visual performance  
✅ Modular, expandable, clean codebase  
✅ Great for DJs, musicians, artists, hackers  
✅ Designed for **ESP32** + **FastLED** optimized performance

---

## 📜 TODOs & Future Work
- Rotary encoder support
- Wi-Fi remote control panel
- User-configurable animation presets
- Dynamic per-strip scene control
- Mood prediction smoothing

---

# 📂 Folders

| Folder | Purpose |
|--------|---------|
| src/   | Core project source code (everything important) |
| lib/   | Optional libraries (can be ignored) |
| include/ | Settings headers and platform defines |
| test/  | Testing files (not yet fully active) |

---


Happy glitching! ✨
