# Backlog — logged, not yet scoped
<!-- Unordered. Promote to next-steps.md when an item gets a real slot. See AGENTS.md. -->

<!-- Status is one of: OPEN | DESIGNED | BLOCKED | MOVED | DROPPED
     BLOCKED names what it waits on. MOVED says where. DROPPED says why.
     Status never appears in the heading. Headings are undated topic titles. -->

## Inert audio tuning macros in Config.h

Status: OPEN

BEAT_THRESHOLD, MIN_BEAT_INTERVAL, GAIN_SMOOTHING, LOUDNESS_SMOOTHING, NOISE_THRESHOLD, FFT_SMOOTHING and MAX_AUDIO_LEVEL have zero uses outside Config.h. The live values are hardcoded instead (0.85f in AudioProcessor.h:23, 250 inline in the beat path). MIN_BEAT_INTERVAL declares 300 while the code uses 250, so the documented knob and the real one disagree. Deferred until the loop structure is fixed, because tuning sensitivity against a broken frame budget measures the wrong thing. The beat gates inside the layers have the same problem and may simply never fire, since BassShockwaveLayer wants beat detected and bass above 0.8 against a fixed divisor of 100.0 that nothing calibrates.

## Injected layers would never expire

Status: OPEN

SceneDirector::maybeInjectReactiveLayer (SceneDirector.h:82) is never called, so its MAX_LAYERS cap is dead. If it is ever wired up, all three of its addLayerByType calls pass no duration, and LayerManager treats durMs == 0 as "live until cleared" — which only happens on scene change now, not on a timer. Accent layers would accumulate for the whole scene instead of decaying on their own. Give injected layers a real duration before enabling this path.

## The heap gate has a floor that stops everything

Status: OPEN

main.ino:48 skips controller.update() entirely whenever free heap drops below 20KB, so the <10KB branch in MainController.cpp:298 is unreachable and the system's actual low-memory behaviour is a bare delay loop, not the LED-only fallback that branch describes. Now that the leak is fixed, confirm the heap never approaches that floor and decide whether the gate should degrade (drop display, keep audio and LEDs) rather than stall.

## Per-pixel transcendental math in layers

Status: OPEN

VisualLayers.h:256 runs 'exp(-pow(...))' inside a per-LED loop. VisualLayers.h:231 and :127 call sin per LED. float arguments promote to software double, and ESP32 has no hardware double FPU. The cost scales with LED count, so it is invisible on a 10-LED strip and dominant on a 300-LED one.

## The mood classifier compares a magnitude sum against 0..1 thresholds

Status: OPEN

`MoodHistory::classifyMood` and `SceneRegistry::pickSceneByMood` both test `m.energy > 0.8f`, `> 0.6f`, `< 0.3f` and `> 0.4f`. On device `AudioFeatures::energy` is the raw sum of 255 FFT magnitudes, so it runs in the hundreds to thousands and the first two tests are always true. The effect is that CALM is unreachable, scenes preferring CALM or FLOATY are only ever picked by the mood-blind fallback, and the mood display is effectively reporting `dynamics` alone. The host harness does not show this because its scripted `energy` is in the 0..1 range, which is precisely the mismatch. Fixing it means deciding on a scale first, either normalising `energy` at the source or writing the thresholds against the real range, so it is not a mechanical change. Same class as the inert tuning macros item above.

## Dead code and uninstantiated classes

Status: OPEN

LayerPool is never instantiated anywhere, so its getByType() returning std::vector<Entry> by value is latent rather than live. AlienSquirtTrailLayer is unused, as are WormholeVortexLayer and CentroidColorFlowLayer, which is why the phase wraps added to those two are hygiene rather than a live fix. MoodReactiveAnimation.h does not compile (mood.centroid and moodHistory.latest() do not exist) and is excluded from AnimationCatalog.h, so it silently rots. SettingIconWidget is declared in DisplayManager.h:67 but never constructed. ScrollingTextWidget is never instantiated.

## Verbose ESP-IDF logging left on

Status: OPEN

platformio.ini sets -DCORE_DEBUG_LEVEL=5, which is verbose. It costs cycles and floods the serial console during timing work.

## MainController holds a second, dead SceneDirector

Status: OPEN

MainController.cpp constructs a SceneDirector with its own SceneRegistry and never attaches a SceneState, so every call on it early-returns. The display scene name now reads from the live director inside LEDStripController instead. Deleting this removes a redundant registry and a heap allocation. It is live code: the device build now includes `src/core/MainController.cpp`, so the dead director is compiled and constructed on every boot.

## Layer pixel cost is live for the first time

Status: OPEN

The per-pixel exp, pow and sin calls in VisualLayers.h never executed, because the whole scene path was dead. Now that layers actually render, that cost is real. Measure frame time before optimising, since the earlier estimate was made against code that did not run.

## History depth is now a fixed 60KB of heap

Status: OPEN

`AudioHistory` is a fixed-capacity ring of 1500 `AudioSnapshot`s, so it holds 60000 bytes for the life of the program, allocated once at boot. The harness measures it as filling to that cap. The deepest live consumer is `MoodMemoryArcLayer`, which reads the last 10, and nothing else reads it at all. The capacity is carried over from the `std::deque` it replaced, where the same 1500 cost nothing up front. Shrinking it is a behaviour change rather than a cleanup, which is why it is not in the current work.
