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

`VisualLayers.h:298` runs `exp(-pow(...))` inside a per-LED loop, and `:265` and `:161` call `sin` per LED. Float arguments promote to software double and ESP32 has no hardware double FPU, so the cost scales with LED count: invisible on a 10-LED strip, dominant on a 300-LED one. This was latent rather than live until recently, because the whole scene path early-returned and none of it executed. Now that layers render it is real, and the earlier estimate was made against code that did not run, so measure frame time before optimising.

## Dead code and uninstantiated classes

Status: OPEN

LayerPool is never instantiated anywhere, so its getByType() returning std::vector<Entry> by value is latent rather than live. AlienSquirtTrailLayer is unused, as are WormholeVortexLayer and CentroidColorFlowLayer, which is why the phase wraps added to those two are hygiene rather than a live fix. SettingIconWidget is declared and owned at DisplayManager.h:67 but never constructed, and ScrollingTextWidget at Widget.h:218 is never instantiated.

## Verbose ESP-IDF logging left on

Status: OPEN

platformio.ini sets -DCORE_DEBUG_LEVEL=5, which is verbose. It costs cycles and floods the serial console during timing work.

## MainController holds a second, dead SceneDirector

Status: OPEN

MainController.cpp constructs a SceneDirector with its own SceneRegistry and never attaches a SceneState, so every call on it early-returns. The display scene name now reads from the live director inside LEDStripController instead. Deleting this removes a redundant registry and a heap allocation. It is live code: the device build now includes `src/core/MainController.cpp`, so the dead director is compiled and constructed on every boot.

## History depth is now a fixed 60KB of heap

Status: OPEN

`AudioHistory` is a fixed-capacity ring of 1500 `AudioSnapshot`s, so it holds 60000 bytes for the life of the program, allocated once at boot. The harness measures it as filling to that cap. The deepest live consumer is `MoodMemoryArcLayer`, which reads the last 10, and nothing else reads it at all. The capacity is carried over from the `std::deque` it replaced, where the same 1500 cost nothing up front. Shrinking it is a behaviour change rather than a cleanup, which is why it is not in the current work.

## No LICENSE, so the repo is all-rights-reserved by default

Status: OPEN

There is no LICENSE file. That leaves the repository all-rights-reserved by default, which is the opposite of what an open README implies, and it is a choice rather than a defect. Decide the license and add the file, or state plainly that the code is not licensed for reuse. `package.json` deliberately carries no `license` field until this is settled.

## Two AI-era FastLED guides in docs/ are unreferenced

Status: OPEN

`docs/_Fastled-animation-guidelines.md` and `docs/ai-fastled-guide.md` are AI-era guides that nothing in the repo references, and the vendored `.agents/skills/jookoi-fastled/` supersedes them for the same purpose. Two separate problems. The guidelines file still claims FastLED has no built-in named animations, which is false and is the era's defect class in prose rather than in code. And it carries the `_` private-layer prefix while being committed, which reads the prefix as a filename convention rather than as the privacy switch it is. Decide per file: correct it, delete it, or fold whatever is still true into the vendored skill.

## AI_ASSIST_INSTRUCTIONS.MD targets a gitignored IDE directory

Status: OPEN

`AI_ASSIST_INSTRUCTIONS.MD` at the root is a prompt file for an IntelliJ assistant. Nothing in the repo reads it, it targets `.idea/`, which is now gitignored, and its only mention anywhere is the generated `graphify-out/GRAPH_REPORT.md`. Delete it unless an IDE still points at it.
