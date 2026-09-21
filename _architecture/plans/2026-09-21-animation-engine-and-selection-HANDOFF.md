# Handoff: Animation System, Audio Calibration & Layer Tuning (Step 4)

**Context:** The music engine rebuild (Steps 0–3), Step 4 core architecture (`BeatClock`, `AnimationProfile`, multi-dimensional music-space selection, 20 Serenity theme ports, coordinate bindings), layer lifecycle overhaul, audio input calibration, structural detector hangovers, and web diagnostics deck are complete. All test suites, host harness checks, and compiler targets are green in the uncommitted worktree.

## System status at handoff

- **Native test harness (`npm run native`)**: 491 of 491 checks passed. Zero allocations in steady-state analysis or per-frame rendering (`ctrl.update()` allocations reduced from 353 to 295).
- **Frontend test suite (`npm test`)**: 32 of 32 unit tests passed.
- **WebAssembly build (`npm run wasm`)**: Compiled cleanly with Emscripten into `web/live/glitchglimmer.wasm` (351K).
- **Device build (`pio run -e esp32s3`)**: Compiled cleanly under `-std=gnu++11` (Flash 25.8%, RAM 9.4%).
- **Browser visualizer (`npm start`)**:
  - Live 8D MusicState coordinates (`intensity`, `activity`, `brightness`, `weight`, `pulse`, `tempo`, `texture`, `presence`) with confidences, trends, and hop timing.
  - Beat phase meter (0..100%), autocorrelation confidence locking, and median BPM.
  - Compact diagnostics clipboard exporter (`📋 Copy Diagnostics`) generating a <250-token Markdown snapshot of current state, scene, mood, rhythm, and coordinates without raw spectrum arrays.
  - Event stream tracking with relative "seconds since last seen" readouts (`(12s ago)`, `(just now)`) on drops, buildups, descents, and tease events.
  - Real audio demo playback (`EDM Beat 128 BPM`, `Jazzy Percussion`), speaker audio with mute toggle, and custom local audio file loader.

## Work completed in recent sessions

1. **Layer Overhaul & Lifecycle Management**:
   - Resolved permanent layer saturation where all 4 layers were stuck permanently for 20s at a time.
   - Base scenes in `SceneRegistry::layersForIntensity` now attach conservative layer sets (0-2 layers max): `< 0.40` intensity: 0 layers, `< 0.75`: 1 layer (`BACKGROUND`), `>= 0.75`: 2 layers (`BACKGROUND` + `HIGHLIGHT`).
   - `SceneDirector::maybeInjectReactiveLayer` now enforces explicit finite durations:
     - Drop impact: `HIGHLIGHT` (2500ms) + `ENERGY` (3500ms).
     - Buildup swell: `OVERLAY` (3000ms).
     - Confident beat accent (`beatConfidence >= 0.70`): punchy transient `REACTIVE` (450ms).
     - Dynamic energy surge (`level > 0.85`, `dynamics > 0.35`): brief `OVERLAY` (1200ms).
     - Rare mood arc: `MOOD_ARC` (4000ms).
   - Pruning via `LayerInstance::expired` frees slots dynamically so animations breathe.

2. **Audio Input Dynamic Range & Calibration**:
   - In `web/live.js`, added `analyserGainNode` with gain `0.12` between `bufferSourceNode` and `analyser`. Digital MP3 PCM (~0.30 RMS) is now scaled to match the INMP441 microphone range (~0.035 RMS), preventing `energy` and `level` from pinning permanently at 100%.
   - In `src/audio/AudioProcessor.cpp`, updated perceived intensity to use the composite formula from research:
     `0.40 * level + 0.30 * activity + 0.20 * bassLevel + 0.10 * dynamics`. Chill tracks now register at ~0.20–0.35 rather than ~0.90.

3. **Structural Detector Hangover Hold**:
   - Added 350ms hangover timer (`buildupLastExceededMs`, `descentLastExceededMs`) to bridge inter-kick beat troughs. Buildup and descent values now stay active through rhythmic valleys instead of zeroing out on transient frames.

4. **Web UI Diagnostics & Bug Fixes**:
   - Added `📋 Copy Diagnostics` button in top bar and live stream.
   - Fixed `ReferenceError: actions is not defined` in `buildState()` (`web/live.js`).
   - Added relative time indicators to structural badges.
   - Added dynamic auto-scaling to the `energy` meter based on rolling max observed.

## Steps still needing to be done

1. **Foreground Browser Verification & Profile Tuning**:
   - Open `http://127.0.0.1:8000/?source=demo` in an active foreground tab.
   - Verify scene transitions and selector distance scores against EDM and Jazz tracks.
   - Review and fine-tune `AnimationProfile.h` 7-axis targets for the 55 catalog animations against live audio behavior.
   - Verify that bed vs rhythm vs event roles feel musical and distinct during song builds, drops, and verses.

2. **Coordinate Inputs for Remaining Animations**:
   - Verify whether any remaining animations from the original set or ports still ignore music coordinates (e.g. `presence`, `brightness`, `weight`, `texture`, `activity`, `tempo`).
   - Replace any remaining freewheeling timers or static color cycles with `BeatClock` or coordinate-driven phase.

3. **Decide Long-Term Level Normalization (Logged in `BACKLOG.md`)**:
   - `level` is currently an envelope over the loudest envelope of the last ~20 s. Evaluate options:
     - Longer reference memory / squared curve for natural track-to-track contrast.
     - Absolute anchor at typical loud-music RMS.

4. **Physical Hardware Validation**:
   - Flash firmware to ESP32 / ESP32-S3 test board.
   - Verify FastLED pin outputs on pins 25 and 33.
   - Verify INMP441 I2S microphone sampling under real acoustic conditions.
   - Validate TFT display rendering and heap headroom (>20KB floor).

## Standing rules & constraints

- **NEVER run `git commit` or `git push`** — the owner commits directly.
- **`-std=gnu++11` device compatibility**: Device build (`pio run -e esp32s3`) must compile cleanly. Zero C++14/17 features.
- **Zero steady-state heap allocations**: No `new`, `malloc`, or dynamic container resizes in per-frame rendering or audio analysis paths.
- **Verify before handoff**: Run `npm test` (32 tests) and `npm run native` (491 checks).

