# GlitchGlimmer Design & Automation Architecture

## ✨ Overview
GlitchGlimmer is more than just a sound-reactive LED controller. It's a fully modular, intelligent lighting system designed for immersive party and music experiences. It reacts to music in real time, shifting moods and visual behaviors based on detailed audio analysis, dynamically layering effects like a live VJ.

This document explores the inner workings, design principles, and practical use cases for GlitchGlimmer, making it an ideal project for DJs, event spaces, festivals, or anyone who wants vibrant, automated visual flair.

---


## Features

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
  +----------------+      +-----------------------+       +------------------+
  | Audio analyzer | ---> |  Mood+sound History   | --->  | Scene Director   |    
  +----------------+      +-----------------------+       +------------------+
                                                         |
                          +----------------------------+ |
                          |  Scene Definition          | |
                          |  + Base Animation          | |
                          |  + Preferred Moods         | |
                          |  + Suggested Layers        |<+
                          +----------------------------+
                                  |
                        +----------------------+
                        |  LED Strip Controller |
                        |  + Animation          |
                        |  + LayerManager       |
                        +----------------------+
```

---

## 🚀 Key Concepts

### Audio Analysis (AudioFeatures)
The audio engine provides real-time FFT-based audio metrics:

- `volume`, 
- `loudness`
- `peak`
- `average`
- `bass`
- `mid`
- `treble`
- `dynamics` (difference between peak & average)
- `spectrumCentroid`, 
- `dominantBand`
- `frequency`
- `energy` (normalized loudness & dynamics)
- `beatDetected`, 
- `bassHits`, 
- `BPM`
- `signalPresence`, 
- `noiseFloor`

### Sound History
Uses a moving window of audio snapshots of AudioFeatures for the animations to use.



### Mood History
Uses a moving window of stabilized "mood" snapshots aggregated from audio features to classify and track mood evolution over time:

- `MoodType`: CALM, ENERGETIC, INTENSE, FLOATY
- `MoodInfo`: includes BPM, energy, dynamics, beat presence

It helps smooth out momentary spikes and detect mood shifts, guiding long-form animation changes.

### Scene System

SceneRegistry
consisting of "scenes" 
A `SceneDefinition` includes: 
- `baseAnimation`: Base animation animation for a strip - will shift between a set of base animations
- `preferredMoods`: moods this scene is suited for
- `layerTypes`: types of additional overlays that work well for this animation

### Scene Director
The conductor of the system. It:

1. Picks a new scene when a significant mood shift is detected
2. Manages duration of scenes (min time, ideal time)
3. Injects reactive layers (beat pops, energy flashes, ambient flows)
4. Tracks current scene per strip

It uses both real-time audio and historical mood to ensure visual cohesion.

### Layer System
Each LED strip has a `LayerManager` which stacks temporary visual effects:

- Reactive: Beat flashes, energy pulses, sparkles
- Overlay: Subtle continuous effects (scribbles, fog, trails)
- Highlight: Dominant band strobes, flickers, shockwaves
- Mood Arc: Visual themes tied to current mood (arcs, color flows)
- Background: Low-opacity ambiance, waveform flow

Layers are drawn on top of the base animation, and expire over time.

---

## 🔄 Lifecycle of Animation Switching

1. **Initialization**:
    - Load all animations and visual layers via catalog.
    - Attach mood & audio history trackers.

2. **Audio Input & Analysis**:
    - Microphone feeds samples, analyzed to produce `AudioFeatures`.
    - History updated every frame (~25 FPS).

3. **Mood Classification**:
    - MoodSnapshot added to MoodHistory.
    - Determines current & predicted mood.

4. **Scene Transition Check**:
    - Compare new mood to current scene's mood.
    - If energy or BPM has shifted significantly, and min duration passed:
        - Pick next scene matching predicted mood.
        - Load new base animation.

5. **Reactive Layer Injection**:
    - Beat, BPM, and energy events trigger layers.
    - Limits prevent overload (e.g., 1 layer per type).

6. **Rendering**:
    - Base animation updated.
    - LayerManager renders active layers on top.

---

## 🧑‍💻 Design Principles

- **Modular & Extensible**: New animations and layers can be added by registering a class in the catalog.
- **Low Coupling**: Audio, mood, animation, and rendering are separate concerns.
- **Predictable Switching**: Scene changes respect timing and require meaningful mood shifts.
- **Dynamic but Intentional**: Not every beat triggers chaos—logic curates the show.
- **Customizable**: Change strip layouts, layer behaviors, animation parameters.

---

## 🎉 Why It's Perfect for Automated Party Lighting

- **Hands-free Mood Matching**: GlitchGlimmer listens to the music and reacts naturally.
- **VJ-Inspired Visuals**: Layering lets you mix a calm tunnel with beat pops or fog trails.
- **Smart Transitions**: Doesn’t switch scenes jarringly—it waits for musical moments.
- **Easy to Extend**: Developers can build their own animations or layers.
- **Beautiful with Zero Tuning**: Defaults work great out of the box.

---

## 📄 TODO / Future Plans
- Persistent settings & config via JSON
- Rotary encoder input
- Web dashboard for remote control
- Scene scripting and timeline playback
- Light show presets and auto-recording
  



Happy glitching! ✨