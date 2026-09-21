# Backlog — logged, not yet scoped
<!-- Unordered. Promote to next-steps.md when an item gets a real slot. See AGENTS.md. -->

<!-- Status is one of: OPEN | DESIGNED | BLOCKED | MOVED | DROPPED
     BLOCKED names what it waits on. MOVED says where. DROPPED says why.
     Status never appears in the heading. Headings are undated topic titles. -->

## Inert macros in Config.h

Status: OPEN

The five macros this item named are dealt with: NOISE_THRESHOLD, MAX_AUDIO_LEVEL, LOUDNESS_SMOOTHING and FFT_SMOOTHING are deleted, and GAIN_SMOOTHING is `0.85f` and is now the only statement of that number rather than a `0.92f` reading against the `0.85f` that ran. What remains is the same defect seven more times. CHANNEL_COUNT, BITS_PER_SAMPLE, FFT_BANDS, FFT_MAX_SCALE, BAR_HEIGHT_MAX, MIN_SWITCH_INTERVAL and ENABLE_WEB_UI have zero uses outside Config.h. They stay, by the owner's decision on 2026-09-18. None of the seven traces to a caller or to a commit that removed one, so deleting it would discard the only record of an intent that may still be wanted. Each goes only once what it was for has been worked out, which is why FFT_BANDS and BAR_HEIGHT_MAX (display work that may still be wanted) and ENABLE_WEB_UI (a switch rather than a measurement) are named here rather than removed. MIN_BEAT_INTERVAL is 250 and is used, at AudioProcessor.cpp:477. The beat gates inside the layers have the same problem and may simply never fire, since BassShockwaveLayer wants beat detected and bass above 0.8 against a fixed divisor of 100.0 that nothing calibrates.

## Injected layers would never expire
 
Status: DROPPED
 
Resolved 2026-09-21. `SceneDirector::maybeInjectReactiveLayer` has been wired into `LEDStripController::update(stripIndex, audio, now)`. Injected layers now pass finite durations: 2500ms for drop highlights, 3500ms for energy surges, 3000ms for buildup swells, 450ms for beat accents, and 1200ms for energy flares. Base scenes were pruned in `SceneRegistry::layersForIntensity` so low-intensity scenes have 0 layers and moderate scenes have 1-2 layers, leaving active slots for reactive layers to enter and expire cleanly.

## The heap gate has a floor that stops everything

Status: OPEN

main.ino:48 skips controller.update() entirely whenever free heap drops below 20KB, so the <10KB branch in MainController.cpp:298 is unreachable and the system's actual low-memory behaviour is a bare delay loop, not the LED-only fallback that branch describes. Now that the leak is fixed, confirm the heap never approaches that floor and decide whether the gate should degrade (drop display, keep audio and LEDs) rather than stall.

## Per-pixel transcendental math in layers

Status: OPEN

`src/animations/visual-layers/VisualLayers.h:328` runs `exp(-pow(...))` inside a per-LED loop, and `:295` and `:178` call `sin` per LED. Float arguments promote to software double and ESP32 has no hardware double FPU, so the cost scales with LED count: invisible on a 10-LED strip, dominant on a 300-LED one. This was latent rather than live until recently, because the whole scene path early-returned and none of it executed. Now that layers render it is real, and the earlier estimate was made against code that did not run, so measure frame time before optimising.

## Dead code and uninstantiated classes

Status: OPEN

LayerPool is never instantiated anywhere, so its getByType() returning std::vector<Entry> by value is latent rather than live. AlienSquirtTrailLayer is unused. WormholeVortexLayer and CentroidColorFlowLayer are no longer in this list: both are reachable now that the twelve concrete `LayerType` values exist, so the phase wraps in them are live code rather than hygiene. SettingIconWidget is declared and owned at DisplayManager.h:67 but never constructed, and ScrollingTextWidget at Widget.h:218 is never instantiated.

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

## Structural mood thresholds are unvalidated

Status: OPEN

TEASE is the fuzziest of the structural detectors and its definition is a first cut: three triggers (a drop inside twelve seconds, a hush with the level low and dynamics or buildup rising, a high-variance mid-mean level ring) with any one of them sufficient, and no hold of its own. WEIRD needs two of three, and the centroid condition depends on a span floor that no real recording has exercised. DESCENT and TEASE genuinely overlap, because a drop is always followed by a fall, and TEASE is placed first so the aftermath of an event is named by the event, which is a judgement rather than a measurement. None of it can be settled without a 60-120 s capture of a real track: the only capture is 2.3 s, shorter than every window these use. Blocked on that recording rather than on design.

## Which layer set belongs on which intensity band

Status: BLOCKED

Blocked on a 60-120 s capture and a look at the live page. `SceneDefinition::layerTypes` is chosen by intensity band now instead of being the same two layers on every scene, which is what lets two scenes at different intensities composite differently. Which accent suits which band was decided by reading the layers rather than by looking at a strip, and ten of the twelve concrete layer types are reachable without any scene naming them, because choosing between alternatives that share a role is a visual judgement that cannot be made from the code. The sets themselves are a first cut: a quiet band gets a drone and a slow arc, the groove gets a streak and a beat pop, the peak gets a base with impact layers, and every one of those choices is a guess about what reads well at that intensity.

## Visualiser follow-ups left after the rebuild

Status: OPEN

Small, unordered, none blocking. Each was raised in review during the ten-task rebuild and deliberately deferred rather than fixed.

`package.json` has no `"type": "module"`, so `npm test` prints `MODULE_TYPELESS_PACKAGE_JSON` on every run. Adding it changes how every `tools/*.js` script is parsed, and none of those are tested, which is why it stayed out of a plan that was not touching them.

`Spectrum.paint` in `web/render.js` hardcodes two things about its container: `cssHeight = 48`, written back as an inline style on every resize, and `cssWidth = parentElement.clientWidth - 16`, which assumes the padding around it. The stylesheet was changed to 48px to stop it lying, but the real fix is the canvas not deciding its own size.

`hitHandle` in `web/viz/path.js` uses `d <= bestDistance`, so an exact distance tie resolves to the highest-index handle. Unreachable in practice at the current handle spacing, and untested.

Two weak spots in the test suite rather than in the code: the `fitScale` test is a round-trip identity, so it catches inversions and stray factors but cannot tell consistent implementations apart, and no test exercises `placePixels` with a pitch near its 0.25 px floor.

Nothing exercises `pointAt` and `placePixels` against the sharper presets. That was noted when `wrap` and `drape` were the only two; there are now ten, and `corner` has the tightest turn of them.

## Pixel counts agree by convention, not by construction

Status: OPEN

The browser visualiser learns its pixel counts twice: live from the WASM engine, which reads `LED_0_NUM` and `LED_1_NUM` in `src/config/Config.h`, and for recordings from `leds0`/`leds1` in `web/data/manifest.json`. Both say `[100, 10]` today. Nothing enforces that they keep agreeing.

The bench ruler is built once from whichever mode binds first and reused across mode switches, so a divergence would show as a bench that silently mis-renders rather than as an error. `bindRenderer` in `web/main.js` now rebuilds the bench when a newly bound view reports different counts, which contains the symptom but not the cause.

The cause is that `manifest.json` is generated by `tools/dump-frames.js` from a firmware build, so the two are in step only as long as nobody hand-edits the manifest or replays a recording made against a different `Config.h`. A generated stamp the page could compare would close it.

## Mood vs events revamp

Status: OPEN

Replace the single mood label with independent music coordinates, trends, structural states and events, chosen by research and real captures rather than by patching the current classifier. Animation metadata becomes regions in that space; selection uses multidimensional compatibility plus hysteresis. Phases and open questions in plans/2026-09-21-music-model-for-lighting-research.md, problem statement in plans/2026-09-21-mood-vs-events-revamp.md.

## Decide what Level should mean

Status: OPEN

level is the envelope over the loudest envelope of the last ~20 s, so any steady passage reads 70 to 100 percent regardless of how loud or slow the song is. A floor at eight times the noise floor now stops near-silence reading loud, but steady music still sits high. Options: (1) a much longer reference memory plus a curve such as level squared, so a slow song after a loud one reads low and the first track of a session still reads high; (2) an absolute anchor at a typical loud-music RMS, which needs per-microphone calibration; (3) leave level relative, drive from intensity, activity and weight, and relabel the meter. Recommendation: 1. Changing it moves every level threshold and the ladder edges, so re-run the harness and check on real audio.

## Silence gate looks too lenient near the noise floor

Status: OPEN

A reading of volume 0.0005 against a noise floor of 0.0003 showed the gate open and level at 91 percent. The gate opens at noiseFloor * 1.5 + 0.0005 (AudioProcessor.cpp around line 371), and the hangover holds it 350 ms, so a signal about 1.7 times the floor should not stay open. Not investigated. Also: a tonal synthetic signal never raises the noise floor above 0 in the harness, because the floor only rises on blocks with spectral flatness above NOISE_FLAT_MIN, so no harness check reaches the floor-relative guard on level.
