# Handoff: Animation System & Selection Engine (Step 4)

**Context:** The music engine rebuild (Steps 0–3), gain-invariant autocorrelation rhythm tracking, silence gate stabilization, and real audio song sampling for browser demo mode are complete. All test suites and compiler targets are green in the uncommitted worktree.

## System status at handoff

- **Native test harness (`npm run native`)**: 364 of 364 checks passed at handoff (418 now, see below). Zero allocations in steady-state analysis.
- **Frontend test suite (`npm test`)**: 32 of 32 unit tests passed.
- **WebAssembly build (`npm run wasm`)**: Compiled cleanly with Emscripten 6.0.9 into `web/live/glitchglimmer.wasm`.
- **Device build (`pio run -e esp32s3`)**: Compiled cleanly under `-std=gnu++11` (Flash 25.2%, RAM 9.1%).
- **Browser visualizer (`npm start`)**:
  - Full 8D MusicState coordinates (`intensity`, `activity`, `brightness`, `weight`, `pulse`, `tempo`, `texture`, `presence`) visible on HUD with confidence, trend, and hop timing.
  - Live beat phase progress ring (0..100%), confidence locking, and median BPM.
  - Real audio demo playback with bundled tracks (`EDM Beat 128 BPM`, `Jazzy Percussion`), speaker audio with mute toggle, and custom local audio file loader.

## What is ready for the animation engine

The audio pipeline now produces rich, normalized, gain-invariant musical state every frame (`AudioFeatures` / `MusicState`):

1. **Rhythm & Phase (`f.beatPhase`, `f.beatConfidence`, `f.currentBPM`, `f.beatDetected`)**:
   - `f.beatPhase`: Fractional phase from 0.0 to 1.0 through the current beat cycle, continuous and phase-locked to audio onsets via PLL.
   - `f.beatConfidence`: Trust score (0.0 to 1.0) from autocorrelation peak prominence.
   - Animations no longer need to guess timing using freewheeling `beatsin8()` or jittery frame-to-frame beat flags; they can lock rotations, pulses, and sweeps directly to `f.beatPhase`.

2. **8D Music Coordinates (`f.musicState.coords[...]`)**:
   - `intensity`: Dynamic perceived loudness (RMS follower vs rolling peak reference, 0..1).
   - `activity`: Event density / onset frequency from positive spectral flux (0..1).
   - `brightness`: Timbral center of mass from spectral centroid (0..1).
   - `weight`: Low-frequency bass/sub energy proportion (0..1).
   - `pulse`: Metric regularity from inter-beat interval consistency fused with autocorrelation (0..1).
   - `tempo`: Normalized speed coordinate (0 = 60 BPM, 0.5 = 120 BPM, 1.0 = 180+ BPM).
   - `texture`: Harmonicity vs noise from spectral flatness (0..1).
   - `presence`: Acoustic gate state slewed through attack/release (0..1).
   - Each coordinate has a corresponding `conf` (0..1) and `trend` (-1..+1 / sec).

3. **Structural Events (`f.dropDetected`, `f.teaseDetected`, `f.buildup`, `f.descent`, `f.anomaly`)**:
   - Clear structural signals for triggering breakdowns, drop flashes, build-up sweeps, or anomaly glitches.

## Progress since this handoff (same day, later session)

Everything below is uncommitted. Native harness 418 of 418, `npm test` 32 of 32, `npm run wasm` and `pio run -e esp32s3` build. None of it has run on hardware, and the selector has not been watched on live audio: an automated browser tab is `hidden` and throttled, so that check needs a real foreground tab at `?source=demo`.

Done from the roadmap:
1. Profiling. `src/animations/AnimationProfile.h` holds a 7-axis target per animation and `profileDistance`. Values are first estimates. Design and rejected alternatives: `plans/decisions/001-select-scenes-by-distance-in-music-space.md`.
2. Rhythm integration. `src/animations/BeatClock.h`. Heartbeat, Beat Scanner, Gentle Pulse Wave, Color Slam and Neon Beat Tunnel run on it. Rising Tension, Strobe Pulse and Pop Fade follow `f.buildup` through `TensionRamp`. Lava Cyber Storm, Space Wizards, Playa Chaos and Hybrid follow coordinates through `HoldLatch` and `HoldSelect` instead of a mode timer. Roadmap names `GentlePulseWave`, `BeatScanner` and `NeonBeatTunnel`; a `BpmWavePulse` was named there but does not exist.
3. Selection. `SceneRegistry::pickSceneByMusic`, `sceneDistance`, a recent-scene ring in `SceneState`, and a margin plus dwell in `SceneDirector::update`. The ladder stays as the fallback for hand-built snapshots.
4. Events. `maybeInjectReactiveLayer` answers a drop and a buildup directly, with cooldown members on the director.
5. Ports. Eight animations from `D:/repos/Serenity/digital-rgb-led-universal-controller/src/animations/themes` are in `src/animations/ThemeAnimations.h`, each driven by the music. 44 animations are registered.

Also changed: the browser panel (missing methods and CSS; `Mood State` is now `Structure`), a floor on `level`'s reference (`LEVEL_REF_MIN_OVER_NOISE`), and per-animation seeding of FastLED's generator in the harness.

Left, in order of value:
1. Watch the selector on live audio and tune the profiles. Nothing else can be judged until this is done.
2. Port the remaining themes: Liquid Dream, Dreamwave Aurora, Fire Tribe Wonderland, Cosmic Chaos, Cosmic Beast of Many Moods, Trippy Hippie Wonderland, both Plasma Effects, Lava Lamp 2, Three Sin, Two Sin nPsy, Rainbow with Glitter. They were dismissed once as not sound reactive; the eight already ported show the pattern for making them so (speed from tempo, density from activity, pulse from `BeatClock`, brightness from `hsvLevel()`).
3. Give Alien Breath, Bass Pulse Storm, Twilight Ripple, Aurora and Neon Flow a coordinate input, and replace the `CRGB temp[n]` stack array in `MultiLayeredHybrid` with a member buffer.
4. Decide what `level` should mean. See `BACKLOG.md`.
5. Layer composition (roadmap item 4) is untouched: how base animations combine with overlay layers is still additive as before.

Two things that cost time this session. The harness shares FastLED's random generator across animations, so an animation that draws from it changed whether Noise Wave and Pacifica lit until each got its own seed. And a check for near-silence reading low could not be written, because the tonal harness signals never form a noise floor.

## Next steps (Step 4 roadmap)

1. **Animation Metadata & Musical Profiling**:
   - Catalog each animation in `src/animations/AnimationCatalog.cpp` with target coordinates (e.g. ideal region in the 8D space) or suitability tags (e.g. `RHYTHMIC`, `AMBIENT_TEXTURE`, `BASS_HEAVY`, `HIGH_ENERGY`, `MELODIC`).
   - Identify which animations are background beds vs overlay reactive layers.

2. **Rhythm Integration in Animations**:
   - Update rhythmic animations (e.g. `GentlePulseWaveAnimation`, `BpmWavePulseAnimation`, `BeatScannerAnimation`, `NeonBeatTunnelAnimation`) to use `f.beatPhase` and `f.beatConfidence` when confidence is high, falling back to internal clocks during beatless passages.

3. **Selection Engine & Scene Architecture**:
   - Re-architect `SceneRegistry` and the scene picker.
   - Replace the legacy 1D 5-rung mood ladder (`Chill` -> `Groove` -> `Energetic`, etc.) with multi-dimensional distance/affinity matching.
   - Implement hysteresis (dwell times, mood/scene hold rules) so scenes don't rapidly flicker across musical boundaries.
   - Wire event triggers: direct drop reaction (e.g. firing `DROP` flash or temporary scene override).

4. **Composition & Layering**:
   - Coordinate how background base animations combine with reactive transient overlay layers (additive, alpha, or masking).

## Non-negotiable rules for the next session

- **Never run `git commit` or `git push`** — the owner commits directly.
- **`-std=gnu++11` device compatibility**: Device build (`pio run -e esp32s3`) must compile cleanly. Avoid C++14/17 features (auto return types, generic lambdas, `std::make_unique`).
- **Zero steady-state allocations**: No `new`, `malloc`, or dynamically resizing containers in per-frame rendering or audio paths.
- **Run the test harness (`npm run native`) and frontend tests (`npm test`)** to verify no regressions after changes.
