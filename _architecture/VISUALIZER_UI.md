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
| `src/wasm_main.cpp` | WebAssembly interface compiled via `tools/build-wasm.sh` | `gg_init`, `gg_step`, `gg_feature(index)`, `gg_scene_name`, `gg_scene_count`, `gg_scene_name_by_index`, `gg_scene_mood_by_index`, `gg_scene_role_by_index`, `gg_scene_intensity_by_index`, `gg_lock_scene`, `gg_unlock_scene`, `gg_locked_scene`, `gg_current_scene_index`, `gg_mood_name`, `gg_layer_count`, `gg_active_layer_type`, `gg_leds0`, `gg_leds1` |
| `web/main.js` | Vue 3 root application | Connects Vue state with `hwStore`, handles mode switches (`live` vs `recording`), drawer visibility, and binding views |
| `web/state.js` | Single reactive source of truth for UI | `state.hw` (strip configurations, presets), `state.live` (scene, mood, audio levels, layers), `state.recording` |
| `web/live.js` | Web Audio loop & WebAssembly bridge | Manages `AudioContext`, submits mic/demo audio buffers into WASM, reads back telemetry every frame into `state.live`, manages capture recording |
| `web/render.js` | Top-level render abstraction | Dispatches to `StripView` (stage visualizer) and `Spectrum` canvas |
| `web/viz/StripView.js` | Photometric LED simulator | Canvas 2D multi-pass renderer: Substrate -> Environment/Diffuse (room glow size) -> Glow/Halo -> Spill -> Core LEDs -> Film grain. Manages drag-to-redraw paths and visible LED guides on canvas |
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
     - 16: buildup and 17: descent are displacements (level minus its 10 s follower) while confirmed and 0 otherwise, not 0..1 scores. 18: dropDetected (one frame), 19: teaseDetected, 20: anomaly, 21: gateGain, 22: spectralFlatness, 23 to 46: the 8 coordinates as value, confidence, trend, 47 to 51: clock and beat phase.
     - 52 to 81: structural episodes, six fields per signal in the order buildup, descent, drop window, tease, anomaly. The fields are state (0 idle, 1 arming, 2 active, 3 fading), episodeId, elapsedMs, lastDurationMs, sinceEndMs and lastEndReason. The signal is `(index - 52) / 6` and the field `(index - 52) % 6`.
     - 82: displacement (signed, before any threshold), 83: arming (0..1 through the hold), 84: dropConfidence (provisional), 85: dropConfirmed.
   - The episode event ring is drained by seq with `gg_event_newest_seq()`, `gg_event_oldest_seq()`, `gg_event_load(seq)` and `gg_event_field(0..6)` (signal, kind, reason, confirmed, atMs, durationMs, value). Seq starts at 1 and never goes backwards, not even across `gg_reset_analysis()`, so the page keeps the newest seq it has read and never repeats or loses a record still in the ring. `atMs` is sample time and restarts on a source change. `web/live.js` `drainEpisodeEvents` only formats records.
   - The four episode windows are tuning entries 11 to 14: section window, tease window, drop safety bound, drop hold fraction.
   - Layers and scene state are read via:
     - `gg_layer_count(strip)`
     - `gg_active_layer_type(strip, index)`
     - `gg_scene_elapsed_ms()`, `gg_scene_min_ms()`, `gg_scene_ideal_ms()`

## Telemetry layout and serving

- `tools/serve-web.js` serves `web/`. `dist/` is a commit-time bundle and is served only with `--dist` or when `web/` is absent.
- No reading is hidden and nothing is an accordion. Top to bottom: health chips, Structure card beside Live Event Stream, Level and Tempo, Scene clock (built by `buildState` into `#clock-host`), spectrum canvas, Frequency drives, Music coordinates (fat value bar over a 2 px confidence strip: green from 0.8, amber from 0.5, red below, with trend and confidence numbers), then the state groups in two balanced CSS columns. The old Structure rows and the 8D State group are gone because the Structure card and the coordinate cells carry the same data. Copy diagnostics, Copy snapshot and Record sit in the header. Chips and pills carry a fixed min-width so a changing number does not shift the layout.
- The right panel has a `LED` / `Tuning` tab (`state.hwTab`). `buildState` appends the tuning sliders to `#tuning-host`.
