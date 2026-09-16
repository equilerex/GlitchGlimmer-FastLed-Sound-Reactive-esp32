# TODO
<!-- Live working set. `jookoi-paper-trail flush` archives it and resets it. See AGENTS.md. -->

## Context

ESP32 sound-reactive LED firmware (PlatformIO, board `ttgo-t1`, FastLED for LEDs on pins 25/33, TFT_eSPI on 18/19/5/16/23, INMP441 I2S mic on 26/27/32). The codebase was largely AI-generated before current models, and inherited the defects of that era.

`plans/2026-09-16-ai-era-defect-audit.md` holds the audit. `plans/2026-09-16-verification-harness-and-what-it-found.md` holds the current measured state and supersedes parts of the audit, so read it first if you only read one.

The correction matters and is worth stating plainly. The audit called the per-frame layer leak the crash, but `SceneDirector::attachState` had no call sites, so `state` was always null, every director call early-returned, `getActiveScene()` returned null, and the per-strip block that built those layers never ran. The leak was unreachable, and with it the whole scene path. Nothing was drawn at all. So the leak is not the crash cause, and whatever crashed on hardware was not this revision.

The user's real symptom, layers stacking until the animation disappears, came from the compositor instead. Every layer renders additively into a black buffer, and `renderLayers` blended that buffer over the base with `nblend(..., 255)`, where 255 means replace rather than blend. Each layer's unlit pixels therefore erased the base animation. Compositing is now additive with opacity as the layer's share of the light.

Wiring the state also uncovered a crash that had been unreachable for the same reason. `animationCatalog` is sized `AnimationType::COUNT`, which is 9, but only 8 entries were initialised and `NONE` is 0. Every lookup reads one position early, and the last slot was left value-initialised with an empty `std::function` that aborts on call. Any scene built from `ALIEN_PULSE` would have called it. This is the one defect found so far that plausibly explains a crash on hardware, and it only became reachable once scenes actually ran.

Where it stands now. Both environments build and the harness runs 101 checks green. `pio run -e ttgo-t1` succeeds at RAM 8.2 percent and flash 32.6 percent at `-std=gnu++11`. `pio run -e native -t exec` drives the real controller over a test-owned clock, sweeps all eight catalog animations at two lengths and all eight reachable layer factories, and soaks the four accumulator sites for 20000 frames each.

The loop no longer has a throw path. Both history buffers were `std::deque`, which took and returned a 456-byte and a 480-byte node about fifteen times a second, and a failed `push_back` was the one way `loop()` could reach `std::terminate`. They are now `SnapshotRing`, a fixed-capacity ring in `src/audio/SnapshotRing.h`, and `AudioHistoryTracker::addSnapshot` allocates zero times across a whole run. `AudioHistory` is 60000 bytes of heap taken once at boot, which is more than anything reads; `BACKLOG.md` carries the shrink.

Nine further defects are fixed, among them self-transitioning scenes, C++20 designated initializers under `gnu++11`, a function-local static scratch buffer shared across strips, and six unwrapped float phase accumulators. Two defects surfaced while fixing those and are recorded rather than fixed: `WormholeVortexLayer` and `CentroidColorFlowLayer` are never instantiated anywhere, and the mood classifier's thresholds are written against a 0..1 range while the device `energy` field is a raw FFT magnitude sum in the thousands. Nothing has run on hardware, and that is the open item. It is also worth repeating that the harness's allocation counts exclude `String`, because `sim/stubs/Arduino.h` aliases it to `std::string` and small-string optimisation hides the churn the device would pay.

## Checklist

- [x] Build graphify knowledge graph (`graphify-out/`)
- [x] Audit animations and layer lifecycle
- [x] Audit audio and mic pipeline
- [x] Audit display and blocking paths
- [x] Set up paper trail
- [x] Write the fix plan from the audit findings
- [x] Fix the layer leak — layers rebuilt on scene change, not per frame (`LEDStripController.h`, `LayerManager.cpp`)
- [x] Fix the display cost — no per-frame `fillScreen`, no 4-widget cap, no internal 500ms clear (`DisplayManager.cpp`, `GridLayout.h`)
- [x] Fix the double ownership of widgets (`GridLayout` is now non-owning)
- [x] Restore CPU to 240 MHz (`src/main.ino`)
- [x] Fix `ArduinoFFT<float>` and drain the I2S ring every pass (`AudioProcessor.cpp`, `MainController.cpp`)
- [x] Assign `AudioFeatures::average` and `dynamics` — both were computed then dropped, leaving them permanently 0
- [x] Instantiate `SceneState` and attach it — without this the entire scene path was dead code
- [x] Make layer compositing additive, so a layer can no longer erase the base it sits on (`LayerManager.cpp`)
- [x] Enforce the layer cap at insertion, matching what actually renders — render capped at 3 while update ran all of them
- [x] Drop the outer fade in `LEDStripController::update` — it reached only strip 0 and decayed it twice per frame
- [x] Give scenes a field + accent preset and an accent opacity for the shockwave (`SceneRegistry.cpp`, `VisualLayers.h`)
- [x] Point the display's scene name at the live director instead of a second, unstateful one (`MainController.cpp`)
- [x] Align `animationCatalog` with `AnimationType` — `NONE = 0` left every lookup one slot early and the trailing empty `create` aborts when called (`AnimationCatalog.cpp`, `SceneRegistry.cpp`)
- [x] Build it (`pio run`) and confirm it compiles — both environments build, `[env:ttgo-t1]` at RAM 8.3% and flash 32.7%, and `[env:native]`
- [x] Add a host harness for the scene and layer lifecycle — real controller, stub Arduino core, a clock the test owns, both strips drawn to the terminal (`sim/stubs/Arduino.h`, `src/sim_main.cpp`)
- [x] Run the harness green — 14 of 14 checks across 3000 simulated frames, with the leak measured rather than argued
- [x] Narrow `lib_ldf_mode` back to the default once `lib_compat_mode = off` carried the load — native build 46 s to 21 s
- [x] Remove the loop's throw path — both `std::deque` histories to a fixed-capacity `SnapshotRing`, so `addSnapshot` allocates zero times per run (`src/audio/SnapshotRing.h`, `AudioHistoryTracker.h`, `MoodHistory.h`)
- [x] Drop the per-frame `String` work — cache the scene name on transition, compute the widget's digit count arithmetically (`MainController.cpp`, `Widget.h`)
- [x] Remove the misplaced `try`/`catch` around non-throwing calls and add a backstop `catch` in `loop()` (`LayerManager.cpp`, `main.ino`)
- [x] Fix the verified defects — self-transitioning scenes, dead `SceneDefinition::activeScene`, C++20 designated initializers under `gnu++11`, the function-local static layer buffer, the `neonFlow` brightness underflow, the negative `uint8_t` argument in `AlienPulse`, six unwrapped phase accumulators, zero-length guards in all eight catalog animations, and a heap monitor with no call sites
- [x] Sweep every animation and layer — eight catalog animations at n=100 and n=8, eight layer factories, device-scale audio with a populated spectrum and waveform, and a per-animation runaway budget
- [x] Soak the accumulators — 20000 frames per site, asserting each stays inside one period via a test seam, since precision exhaustion itself is not reachable in a harness
- [x] Set `default_envs` and document both build commands, stating that neither needs a board attached (`platformio.ini`)
- [ ] Flash and confirm on hardware: strips animate at all, no crash under scene changes, stable heap, screen free of flicker, mic responsive
- [ ] Write `ARCHITECTURE.md` — deferred until the loop is confirmed working on hardware
