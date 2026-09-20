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

> Renderer internals (profiles, looks, glow, surfaces, paint order) live in `web/viz/CONTEXT.md`;
> page layout, state and the drawer live in `web/CONTEXT.md`. They are not repeated here.

## Directory & File Responsibilities

| File | Purpose | Key Exports & Data Structures |
|---|---|---|
| `src/wasm_main.cpp` | WebAssembly interface compiled via `tools/build-wasm.sh` | `gg_init`, `gg_step`, `gg_feature(index)`, `gg_scene_name`, `gg_mood_name`, `gg_layer_count`, `gg_active_layer_type`, `gg_leds0`, `gg_leds1` |
| `web/main.js` | Vue 3 root application | Connects Vue state with `hwStore`, handles mode switches (`live` vs `recording`), drawer visibility, and binding views |
| `web/state.js` | Single reactive source of truth for UI | `state.hw` (strip configurations, presets), `state.live` (scene, mood, audio levels, layers), `state.recording` |
| `web/live.js` | Web Audio loop & WebAssembly bridge | Manages `AudioContext`, submits mic/demo audio buffers into WASM, reads back telemetry every frame into `state.live`, manages capture recording |
| `web/render.js` | Top-level render abstraction | Dispatches to `StripView` (stage visualizer) and `Spectrum` canvas |
| `web/viz/StripView.js` | Photometric LED simulator | Canvas 2D multi-pass renderer: Substrate -> Glow/Halo -> Spill -> Core LEDs -> Film grain. Manages drag-to-redraw paths and visible LED guides on canvas |
| `web/viz/path.js` | Catmull-Rom spline calculations | Samples preset and freehand paths, places discrete LEDs along path arc length, and provides default calibration poses |
| `web/viz/profiles.js` | LED hardware definitions | WS2812B (30/60/144 per meter), COB 480/m, fairy lights, bullet nodes. Pitch, die size, sigma diffusion parameters |
| `web/viz/hwStore.js` | Hardware settings persistence | Loads and stores visualizer configuration into `localStorage` (`gg.hw.v2`), binds StripView and BenchStrip to reactive state |
| `web/viz/BenchStrip.js` | Flat linear debugger strip | Straightened horizontal pixel-by-pixel view showing individual pixel index, color swatch, luma graph, and hover inspection |

## Core Principles & Invariants

1. **Firmware Authority Over Logic**:
   - The browser does not simulate audio analysis, beat detection, or scene selection in JS.
   - Everything runs through the C++ code compiled to WebAssembly.

2. **Arc-Length Density Scaling (Fit to Length)**:
   - When a strip has a physical count $N$ and the curve has total path length $L$:
   - In "fit to path" mode, the step between consecutive LEDs is $L / N$.
   - The drawn path is the physical strip length. The selected profile pitch controls LED cadence; drawing faster or slower cannot change spacing. Zoom is a display transform only.

3. **Multi-Strip Layout on Stage**:
   - Strips default to separated horizontal calibration lines across the stage.
   - The active strip can be redrawn directly by dragging across the stage. A new drag replaces its previous path.
   - A drag replaces the active path. On release, its measured length becomes the software strip length and the count is recalculated from the selected pitch. The renderer uses fixed pitch placement while the path is being drawn.

4. **Telemetry & Feature Streaming**:
   - Audio features are read via indexed accessor `gg_feature(index)`:
     - 0: volume, 1: loudness, 2: peak, 3: bass, 4: mid, 5: treble
     - 7: dynamics, 8: bpm, 9: beatDetected, 10: level, 11: noiseFloor
     - 13: bassLevel, 14: midLevel, 15: trebleLevel
   - Layers and scene state are read via:
     - `gg_layer_count(strip)`
     - `gg_active_layer_type(strip, index)`
     - `gg_scene_elapsed_ms()`, `gg_scene_min_ms()`, `gg_scene_ideal_ms()`
