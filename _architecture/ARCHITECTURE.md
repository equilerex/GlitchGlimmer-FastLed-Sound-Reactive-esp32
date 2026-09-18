# Architecture

Why this repo is shaped this way. Static, so it changes when the structure, a module boundary or a load-bearing decision changes, and not when work moves. The working set is `TODO.md`.

## One source tree, three entry points

`src/` is the firmware, and it is also the audio analysis and the animation library. Three entry points sit beside each other and all three drive the same `LEDStripController`, `SceneDirector` and `animations/`.

| Entry point | Built by | Runs on |
|---|---|---|
| `src/main.ino` | PlatformIO, `[env:ttgo-t1]` | the board |
| `src/sim_main.cpp` | PlatformIO, `[env:native]` | the host, as test harness and frame dumper |
| `src/wasm_main.cpp` | `tools/build-wasm.sh` | the browser |

That shape is the point. The frame recording and the browser cannot drift from the firmware, because there is no second implementation of the analysis or the animations for them to drift from. The page is a renderer and an input source, not a port.

The WebAssembly surface is `extern "C"`. `EMSCRIPTEN_KEEPALIVE` alone keeps the name but not the linkage, so the page sees a mangled symbol and calls an undefined function.

## The device environment is the strict one

`[env:ttgo-t1]` compiles at `-std=gnu++11` and nothing else does, so it is the only build that catches a newer construct reaching device-reachable code. That is why the flag sits on that environment rather than on a shared base.

The build reaches outside the repo for nothing. All four libraries `src/` includes are named in `lib_deps`, where three of them used to arrive through a `lib_extra_dirs` absolute path into a global Arduino libraries directory that existed on one machine. TFT_eSPI needed the same treatment one level down, since its panel configuration normally lives in the library's own `User_Setup_Select.h`. `TFT_eSPI.h` tests `__has_include(<tft_setup.h>)` before it reads that file, so `include/tft_setup.h` carries the panel configuration and wins.

## The audio feature model

This is the part of the repo that has been wrong most often, and every correction has been the same correction. The rule behind all of them is worth stating once.

**A number a consumer compares against a threshold has to be normalised against a statistic of the input.** A microphone's absolute output is not knowable from here, and one fixed constant measured on one mic is 40 dB wrong on another, so any absolute threshold in a comparison is a claim about the input rather than about the audio. Everything a scene, layer or classifier tests is a fraction of a recent reference: `level`, `bassLevel`, `midLevel`, `trebleLevel`, `pixelLevel()` and `hsvLevel()`. The absolute fields survive because they are true measurements of the samples, and nothing compares them against anything.

`level` is the block's loudness over `levelEnv`, an asymmetric one-pole envelope of the block volume with separate attack and release, normalised against the measured noise floor as well as against the peak so silence reads zero by construction. The reference is the loudest the envelope has reached and it forgets over about twenty seconds, so a quiet passage stays quiet. The coefficients decide which question the value answers. Measured off a single 11.6 ms block it swings by a factor of two between one block and the next and answers "did something just hit" rather than "how loud is this", which is why attack sits below the timescale of a syllable (0.02 a block) and release above the timescale of a room (0.006). The reference rises at 0.05, because a reference that rises instantly lets one cough set the denominator for the next twenty seconds.

`dynamics` is the span the loudness covers while its window is open, as a fraction of the top of that span, taken from two remembered edges of the envelope that move out quickly and back in slowly. It is deliberately not a crest factor. A crest factor is a property of a waveform's shape and every shape a room produces sits near 0.65, so it separates nothing and cannot respond to gain at all. The classifier's cut points are 30 and 70 percent of the range it has itself measured, and the clause is dropped when that range is too narrow to split.

The three bands are plain shares of the spectrum, not rescaled against the largest of them. The rescale was tried and pinned the dominant band at exactly 1.0 by construction, and on this microphone the dominant band is mid for anything carrying a voice. Shares read low, around 0.05 for bass on real audio, so each band has a drive beside it: `bassLevel` and its two siblings ask the same 0..1 question of one band against its own recent peak, behind the same gate. One reference per band rather than one shared peak, because the bands do not peak together.

The noise floor rises only on a block that looks like noise, measured as spectral flatness, the geometric mean of the magnitudes over their arithmetic mean. A room's noise spreads across every bin and reads near 0.5, and anything carrying a pitch concentrates into a few bins and reads near zero, so only the first may raise the floor. The rise is bounded at 0.01, which is what stops a broadband mix walking the gate's threshold out of reach. Level cannot make that distinction, since a loud room and a loud compressed track are the same number.

A beat takes three conditions rather than one, because a level rise alone fires on every syllable: every syllable starts on a consonant and the block before it was quieter, which reported 140 to 200 BPM with nothing playing. The third condition is that the bass band under 200 Hz rose to 1.15 times its previous block, read on the raw magnitude rather than on its share, since a share of a bass-heavy block is already at the top of its range and cannot rise. It is read before the gate, because the gate opening is itself a rise. The tempo readout is the median of the last twelve intervals rather than the newest one, and it fades as a function of elapsed time rather than by a per-block coefficient, which made the speed of the fall depend on the loop rate.

Every reference here is a statistic of the input, which is what makes the features gain-independent and also what makes a change of input invisible to them. The analysis goes on measuring the new signal against the old one's peak, for about thirteen seconds. `AudioProcessor::resetTracking` drops all of it, exported as `gg_reset_analysis`, and every path in the page that changes the source calls it through one `selectSource`.

## Scenes and layers

`SceneDirector::attachState` has to have been called or the whole scene path early-returns and nothing is drawn. It builds the per-strip scene state, which carries the scene clock and the mood history.

Compositing is additive. A layer renders into a black buffer and that buffer is added to the base, with the layer's opacity as its share of the light. The earlier form blended with `nblend(..., 255)`, where 255 means replace rather than blend, so a layer's unlit pixels erased the base animation underneath and stacking layers made the animation disappear. The layer cap is enforced at insertion so that it matches what actually renders.

The scene clock compares the classified mood, not an instantaneous `level`. `level` is renormalised per block and churns about 11 percent a frame, so a delta test against the snapshot taken at scene start was crossed within a second or two of any audio and every scene changed at its minimum duration.

## Two colour sinks, two brightness curves

`hsv2rgb_rainbow` squares `CHSV`'s `val` before scaling the channels, `val = scale8_video(val, val)`, so `CHSV(h, s, v)` emits duty proportional to `v * v`. Brightness written through `val` therefore has a quadratic response and a `sqrt` curve applied on the way in cancels it exactly. Brightness written as a scale on `CRGB` has no such squaring. `AudioFeatures` carries a curve for each sink: `pixelLevel()` for the `CRGB` scale, and `hsvLevel()` for `CHSV`'s `val`, which supplies the other half of the exponent so both sinks land on the same emitted duty. Applying one curve to the wrong sink is how eight catalog animations rendered near black while every ratio-based check passed. A harness check measures the conversion itself, so a FastLED change that alters the squaring fails there instead of as a strip that has quietly gone dark.

## The harness

`sim/stubs/Arduino.h` gives the host a stub Arduino core, so `src/sim_main.cpp` drives the real `LEDStripController`, `SceneDirector`, `LayerManager`, `SceneRegistry` and every layer over a clock the harness owns.

Two things it structurally cannot see. `String` is aliased to `std::string`, whose small-string optimisation hides the per-frame allocation churn the device pays, so allocation counts from the harness exclude it. And a fixture's scale is not the input's scale: a synthetic `energy` in 0..1 against a device-scale spectrum running in the thousands satisfied a whole class of absolute constants that were fatal on real audio. A fixture is built at the measured scale, and an assertion about brightness names an absolute floor rather than a ratio, since a strip that is black at both ends of a ratio check passes it.

The loop has no throw path. Both history rings are fixed-capacity (`SnapshotRing`) rather than `std::deque`, so `addSnapshot` allocates zero times across a run and a failed `push_back` cannot reach `std::terminate`.

## The browser is a renderer, not a port

`web/` holds two views over one canvas. The recording view replays frames the harness writes with `--dump-frames`, so its audio timeline is fixed and nothing in it runs the FFT. The live view steps the WebAssembly module once per frame from the page and hands the microphone's samples to `AudioProcessor` unchanged, so the FFT, the feature extraction, the beat detector and the mood classifier are the firmware's own code.

The live view reads its state back out of the module every frame instead of rendering what it assumes, which is what turned "the animations keep blacking out" from an impression into a number. `?debug=1` traces every scene and mood change with its dwell time. `tools/build-wasm.sh --watch` rebuilds on save and stamps `web/live/build.json`, which an open page polls so that it reloads when the module it loaded has been replaced.

The page has no bundler and no dependency tree. It is plain scripts loaded by `index.html`, served by a dependency-free static server (`tools/serve-web.js`, `npm start`), which is what keeps it openable on a bare clone with nothing installed but Node. The server is not interchangeable with a plain file server: the module needs `application/wasm` to stream-compile, and nothing under `web/` may be cached, since the build replaces the module and the recordings under paths that are already in the browser's cache.

The only route from a real microphone to a measurement is the page's `Record` button, which writes what it fed the analyser to a `.f32` file that `--replay <file>` feeds back through the real `AudioProcessor`. The browser is the only place the microphone is reachable.

Tuning is live as well. Eight dials drive one indexed wasm pair covering the scene clock's minimum, ideal base and ideal span, and the mood's minimum hold, confirmation window, smoothing rate and two dynamics-window rates. The two window rates are per second rather than per frame, so the same window adapts at the same wall-clock speed on the device's 33 ms loop and on the page's per-frame loop.
