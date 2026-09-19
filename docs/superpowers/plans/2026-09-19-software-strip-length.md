# Software Strip Length Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make browser-configured software strip length and pitch drive the shared WASM animation/library geometry without changing physical board capacity.

**Architecture:** Separate fixed buffer capacity from runtime active length in `LEDStrip`. Device builds initialize active length from their compile-time counts; WASM uses larger simulation buffers and runtime setters. The browser stores per-strip length/pitch/profile state, sends it through WASM, and draws the same active geometry with a wrapped path and camera zoom.

**Tech Stack:** C++11 PlatformIO/FastLED, Emscripten WASM exports, Vue 3 CDN bindings, native ES modules, Node `node:test`.

**Spec:** `docs/superpowers/specs/2026-09-19-software-strip-length-design.md`

## Global Constraints

- Device-reachable code compiles as `gnu++11`; run `pio run` before claiming device-reachable work complete.
- Physical `LED_*_NUM` values and board pin assignments remain unchanged.
- Animation continuity remains emergent through `fuses(profile)`; no per-type lit-path branch.
- Pixel count is software geometry in WASM/browser, not a user-controlled physical board allocation.
- Keep the repository's existing no-build browser architecture and Node 22 test command.

### Task 1: Define runtime strip geometry

**Files:**
- Modify: `src/core/LEDStripController.h`
- Modify: `src/scenes/LayerManager.h`, `src/scenes/LayerManager.cpp`
- Test: `test/` native harness checks in `src/sim_main.cpp`

**Interfaces:**
- Add `LEDStrip::capacity()` and `LEDStrip::length` as distinct concepts.
- Add `LEDStrip::setSoftwareLength(int)` that clamps to `[0, capacity]` and updates the layer manager count.
- Keep `init(capacity, buffer)` as the physical/device setup API.

- [ ] Write a failing native/harness assertion that an active length smaller than capacity is passed to an animation and layer manager while the buffer remains capacity-sized.
- [ ] Run the focused native harness and confirm it fails because no runtime setter exists.
- [ ] Implement the setter and make `update()` pass the active length to animations/layers.
- [ ] Run the focused harness and then the full native harness; confirm the new checks pass without allocations per frame.

### Task 2: Add WASM runtime geometry exports

**Files:**
- Modify: `src/wasm_main.cpp`
- Modify: `src/config/Config.h` only if a WASM-specific capacity macro is required without changing device values.
- Test: `test/` WASM export smoke test or the existing WASM build checks.

**Interfaces:**
- Export `gg_strip_capacity(int)`, `gg_strip_length(int)`, `gg_set_strip_length(int, int)`, and `gg_strip_pitch_mm(int)` or the equivalent exact C ABI chosen in implementation.
- WASM buffers must hold the declared simulation capacity; device globals and FastLED hardware registrations must remain unchanged.

- [ ] Add an export smoke test or harness assertion for get/set length round trips and invalid-index clamping.
- [ ] Run it against the current build and confirm it fails before exports/buffers exist.
- [ ] Add WASM-only simulation capacity and route the setter into `LEDStrip::setSoftwareLength`.
- [ ] Build the device target and the WASM target; verify the device still uses its existing physical counts.

### Task 3: Make the pure browser geometry support length, pitch, and wrapping

**Files:**
- Modify: `web/viz/path.js`
- Modify: `web/viz/profiles.js`
- Test: `test/viz/path.test.js`, `test/viz/profiles.test.js`

**Interfaces:**
- Add pure helpers for `deriveSoftwareCount(lengthM, pitchMm, capacity)` and pitch/length clamping.
- Extend path sampling to append wrapped segments without producing non-finite coordinates.
- Keep `placePixels` arc-length based and return active-count pixels.
- Add profile visual defaults without adding casing branches to the renderer.

- [ ] Write failing tests for 20 m derivation, manual pitch overrides, capacity clamping, and a wrapped path whose samples remain finite.
- [ ] Run the focused Node tests and confirm the new tests fail.
- [ ] Implement the helpers and wrapped path representation.
- [ ] Add tests for every preset and the sharpest wrap/corner cases; run all browser tests.

### Task 4: Persist and bind per-strip software settings

**Files:**
- Modify: `web/viz/hwStore.js`
- Modify: `web/state.js`, `web/main.js`
- Test: `test/viz/hwStore.test.js`

**Interfaces:**
- Store `lengthM`, `pitchMm`, manual-override flags, zoom, and profile visual defaults per strip.
- On profile selection, apply profile defaults only to fields not manually overridden.
- `bindView`/`syncView` must update both the browser renderer and WASM runtime geometry.

- [ ] Write failing persistence/default tests for per-strip length, pitch, zoom, and profile defaults.
- [ ] Run focused tests and confirm failure.
- [ ] Implement validated persistence, migration from the current `stageWidthM` state, and shared sync callbacks.
- [ ] Run all Node tests and confirm settings survive reload/mode switches.

### Task 5: Replace stage controls and render the software strip

**Files:**
- Modify: `web/index.html`, `web/style.css`
- Modify: `web/viz/StripView.js`, `web/live.js`, `web/app.js`
- Test: browser/manual verification plus pure geometry tests

**Interfaces:**
- Replace stage-width controls with strip length, pitch override, and zoom controls.
- Keep the small strip as the fixed close-up reference view; apply length controls to the large strip.
- Make handles appear on hover and support more than four editable handles without corrupting pointer capture.
- Clip wrapped geometry to the canvas and keep layout slots stable.

- [ ] Add failing markup/logic checks for length and pitch controls and the removal of stage-width terminology.
- [ ] Run the browser test suite before implementation and confirm the checks fail.
- [ ] Implement controls, hover handles, zoom transform, and wrapped rendering.
- [ ] Verify profile changes, length changes, pitch changes, zoom, mode switches, and the footer/cockpit stability manually through the served page.

### Task 6: Full verification and written trail

**Files:**
- Modify: `_architecture/TODO.md`
- Modify: `web/viz/CONTEXT.md`, `_architecture/VISUALIZER_UI.md`

- [ ] Run `npm test` and record the result.
- [ ] Run `pio run` and record the device result.
- [ ] Run the native harness and WASM build/smoke checks.
- [ ] Run `git diff --check`.
- [ ] Update the project trail with the capacity-versus-active-length rule and any remaining visual limitations.

### Deferred stretch goal: browser animation editing

Do not include this in the runtime geometry implementation. Later, investigate a browser-editable animation layer that consumes the same runtime strip geometry and frame contract. Compare a small JS/DSL animation API against compiling selected C++ animation code in-browser; the current task only needs to preserve a clean boundary where either can provide per-frame LED data.
