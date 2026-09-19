# Visualizer and UI Architecture

This document describes how the browser visualizer and the debugger/editor UI connect to the firmware codebase, how WebAssembly exports work, how LED paths are posed and sampled, and where state lives.

## System Overview

```
                 Audio (Mic or Demo Generator)
                               |
                               v
                       [web/live.js]
               (puts Float32 samples into heap)
                               |
                               v
                     [src/wasm_main.cpp]
                   calls AudioProcessor.cpp
            (FFT, Band Separation, Dynamic Ladder)
                               |
                               v
                     [LEDStripController]
                   +---------------------+
                   | SceneDirector       |
                   | LayerManager        |
                   | AnimationCatalog    |
                   +---------------------+
                               |
         +---------------------+---------------------+
         | (LED bytes: CRGB)                         | (Telemetries & Features)
         v                                           v
    [StripView.js]                             [live.js -> state.js]
(Sample spline, Camera,                     (level, dynamics, bpm, mood,
 Photometric passes, Multi-strip)            scene, active layers, etc.)
         |                                           |
         v                                           v
   <canvas id="view">                         Vue 3 UI Components
(Stage, Curved Strips)                       (Topbar, Cockpit, HUD)
```

## Directory & File Responsibilities

| File | Purpose | Key Exports & Data Structures |
|---|---|---|
| `src/wasm_main.cpp` | WebAssembly interface compiled via `tools/build-wasm.sh` | `gg_init`, `gg_step`, `gg_feature(index)`, `gg_scene_name`, `gg_mood_name`, `gg_layer_count`, `gg_active_layer_type`, `gg_leds0`, `gg_leds1` |
| `web/main.js` | Vue 3 root application | Connects Vue state with `hwStore`, handles mode switches (`live` vs `recording`), drawer visibility, and binding views |
| `web/state.js` | Single reactive source of truth for UI | `state.hw` (strip configurations, presets), `state.live` (scene, mood, audio levels, layers), `state.recording` |
| `web/live.js` | Web Audio loop & WebAssembly bridge | Manages `AudioContext`, submits mic/demo audio buffers into WASM, reads back telemetry every frame into `state.live`, manages capture recording |
| `web/render.js` | Top-level render abstraction | Dispatches to `StripView` (stage visualizer) and `Spectrum` canvas |
| `web/viz/StripView.js` | Photometric LED simulator | Canvas 2D multi-pass renderer: Substrate -> Glow/Halo -> Spill -> Core LEDs -> Film grain. Manages handle dragging on canvas |
| `web/viz/path.js` | Catmull-Rom spline calculations | Samples curve arcs, places discrete LEDs along path arc length, provides default curved poses and handle intersection checks |
| `web/viz/profiles.js` | LED hardware definitions | WS2812B (30/60/144 per meter), COB 480/m, fairy lights, bullet nodes. Pitch, die size, sigma diffusion parameters |
| `web/viz/hwStore.js` | Hardware settings persistence | Loads and stores visualizer configuration into `localStorage` (`gg.hw.v1`), binds StripView and BenchStrip to reactive state |
| `web/viz/BenchStrip.js` | Flat linear debugger strip | Straightened horizontal pixel-by-pixel view showing individual pixel index, color swatch, luma graph, and hover inspection |

## Core Principles & Invariants

1. **Firmware Authority Over Logic**:
   - The browser does not simulate audio analysis, beat detection, or scene selection in JS.
   - Everything runs through the C++ code compiled to WebAssembly.

2. **Arc-Length Density Scaling (Fit to Length)**:
   - When a strip has a physical count $N$ and the curve has total path length $L$:
   - In "fit to path" mode, the step between consecutive LEDs is $L / N$.
   - Changing the physical length or profile pitch changes the visual spacing and count density, but the strip always fills the entire curve from start to finish without leaving trailing gaps.

3. **Multi-Strip Layout on Stage**:
   - Strip 0 and Strip 1 default to an arching mirrored arrangement across the stage.
   - Strip 2 (if present) serves as a centered linear close-up strip.
   - Handles on any strip can be interactively dragged to reshape the curves.

4. **Telemetry & Feature Streaming**:
   - Audio features are read via indexed accessor `gg_feature(index)`:
     - 0: volume, 1: loudness, 2: peak, 3: bass, 4: mid, 5: treble
     - 7: dynamics, 8: bpm, 9: beatDetected, 10: level, 11: noiseFloor
     - 13: bassLevel, 14: midLevel, 15: trebleLevel
   - Layers and scene state are read via:
     - `gg_layer_count(strip)`
     - `gg_active_layer_type(strip, index)`
     - `gg_scene_elapsed_ms()`, `gg_scene_min_ms()`, `gg_scene_ideal_ms()`
