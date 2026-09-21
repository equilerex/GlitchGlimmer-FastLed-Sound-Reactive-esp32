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

`level` is the block's loudness over `levelEnv`, an asymmetric one-pole envelope of the block volume with separate attack and release, normalised against the measured noise floor as well as against the peak so silence reads zero by construction. The reference is the loudest the envelope has reached and it forgets over about twenty seconds, so a quiet passage stays quiet. The coefficients decide which question the value answers. Measured off a single 11.6 ms block it swings by a factor of two between one block and the next and answers "did something just hit" rather than "how loud is this", which is why attack sits below the timescale of a syllable (0.02 a block) and release above the timescale of a room (0.006). The reference rises at 0.05, because a reference that rises instantly lets one cough set the denominator for the next twenty seconds. The reference is floored at eight times the noise floor (`LEVEL_REF_MIN_OVER_NOISE`), because a reference that follows room tone lets a signal barely above the room read as most of it: 0.0005 against a floor of 0.0003 read 91 percent. That is a multiple of the measured floor rather than a constant, so it scales with the microphone's gain. What it does not do is make a steady song read low. `level` is a fraction of the loudest recent thing, so any steady passage sits near its own peak; whether it should instead mean absolute loudness is an open decision logged in `BACKLOG.md`.

`dynamics` is the span the loudness covers while its window is open, as a fraction of the top of that span, taken from two remembered edges of the envelope that move out quickly and back in slowly. It is deliberately not a crest factor. A crest factor is a property of a waveform's shape and every shape a room produces sits near 0.65, so it separates nothing and cannot respond to gain at all. The classifier's cut points are 30 and 70 percent of the range it has itself measured, and the clause is dropped when that range is too narrow to split.

The three bands are plain shares of the spectrum, not rescaled against the largest of them. The rescale was tried and pinned the dominant band at exactly 1.0 by construction, and on this microphone the dominant band is mid for anything carrying a voice. Shares read low, around 0.05 for bass on real audio, so each band has a drive beside it: `bassLevel` and its two siblings ask the same 0..1 question of one band against its own recent peak, behind the same gate. One reference per band rather than one shared peak, because the bands do not peak together.

The noise floor rises only on a block that looks like noise, measured as spectral flatness, the geometric mean of the magnitudes over their arithmetic mean. A room's noise spreads across every bin and reads near 0.5, and anything carrying a pitch concentrates into a few bins and reads near zero, so only the first may raise the floor. The rise is bounded at 0.01, which is what stops a broadband mix walking the gate's threshold out of reach. Level cannot make that distinction, since a loud room and a loud compressed track are the same number.

A beat takes three conditions rather than one, because a level rise alone fires on every syllable: every syllable starts on a consonant and the block before it was quieter, which reported 140 to 200 BPM with nothing playing. The third condition is that the bass band under 200 Hz rose to 1.15 times its previous block, read on the raw magnitude rather than on its share, since a share of a bass-heavy block is already at the top of its range and cannot rise. It is read before the gate, because the gate opening is itself a rise. The tempo readout is the median of the last twelve intervals rather than the newest one, and it fades as a function of elapsed time rather than by a per-block coefficient, which made the speed of the fall depend on the loop rate.

Every reference here is a statistic of the input, which is what makes the features gain-independent and also what makes a change of input invisible to them. The analysis goes on measuring the new signal against the old one's peak, for about thirteen seconds. `AudioProcessor::resetTracking` drops all of it, exported as `gg_reset_analysis`, and every path in the page that changes the source calls it through one `selectSource`.

## Musical events and musical states

Read `docs/music-research/MODEL.md` before touching the structural detectors, the layers that answer them, or the animations that follow them. It holds the concepts, and this section holds the rules that follow from them for this code.

- **A concept is what a listener hears, and a detector is one hypothesis about it.** "Bass left and came back" is a candidate drop detector, not the definition of a drop. Do not rewrite a concept to match whatever the code currently measures.
- **A buildup is a state that unfolds.** It can be confirmed only while it runs, so its start is stamped when the hold began and reported at confirmation.
- **"The drop" is two things.** The onset is a point event, the arrival that releases a buildup. Preparation raises the confidence that it is a drop, and is not required: the buildup may have been missed or the drop may be abrupt. The onset opens a drop window, the payoff section that follows, which lasts as long as the music stays in it and ends when the music stops behaving like a payoff or another section takes over. An arrival not followed by a sustained payoff is an impact. A timeout may exist as a safety backstop but it is never the definition of the window.
- **A detector is a hypothesis, including the ones that open and end a window.** The first implementation may use a cheap stand-in such as loudness against a plateau, and it stays behind a replaceable function. A payoff section is not defined by loudness and survives short dips. Its continuation is meant to be judged from intensity, bass weight, activity, pulse, spectral character and structural change.
- **Three lifetimes stay separate.** The onset (one instant), the drop window (the music's), and a layer's own attack, hold and decay (the visual design's). Do not derive one from another, and never add analyser state only to give a visual layer something to live for.
- **Layers follow episodes, or run one-shot envelopes.** A layer for a section or overlay (buildup, descent, drop window, tease, anomaly) is bound to the analyser's episode, appears when it opens and fades when it ends. A layer for an onset is a one-shot with its own envelope.
- **A tease is a buildup whose expected arrival is withheld.** An actual drop resolves it. It is recognised in retrospect and its reliability is low.
- **A breakdown is a sustained stripping-back**, distinct from a drop and from a buildup. `descent` is the closest detector the code has, not the concept.
- **Section events, and the decision that they have ended, live in the firmware.** The page displays them and sends tuning values, and never decides when an episode starts or ends. The plan is `plans/2026-09-21-structural-event-lifecycle.md`.

## Scenes and layers

`SceneDirector::attachState` has to have been called or the whole scene path early-returns and nothing is drawn. It builds the per-strip scene state, which carries the scene clock and the mood history.

Compositing is additive. A layer renders into a black buffer and that buffer is added to the base, with the layer's opacity as its share of the light. The earlier form blended with `nblend(..., 255)`, where 255 means replace rather than blend, so a layer's unlit pixels erased the base animation underneath and stacking layers made the animation disappear. The layer cap is enforced at insertion so that it matches what actually renders, and it is the only bound on a scene's layer set.

A scene's set is chosen by the intensity band its animation sits in rather than being the same two layers on every scene, so two scenes at different intensities do not composite identically. `LayerType` has two kinds for the same reason: eight roles, which name a compositing job without naming an implementation, and twelve concrete values, which name one of the alternatives written for a role. Half the layer library was unreachable before the second kind existed, because the factory map was one class per role and nothing else. The classes themselves live in `src/animations/visual-layers/`, not beside the catalog bases, so a folder listing is a role listing.

The scene clock compares the classified mood, not an instantaneous `level`. `level` is renormalised per block and churns about 11 percent a frame, so a delta test against the snapshot taken at scene start was crossed within a second or two of any audio and every scene changed at its minimum duration. That is the path for hand-built snapshots. Live audio carries the music coordinates and takes the selection path in the next section.

## Scene selection by music

Scenes are ranked by weighted distance between the music's coordinates and each animation's profile, not by a loudness ladder. `AnimationProfile.h` holds seven targets per animation, `kAny` for an axis it ignores, and `SceneRegistry::pickSceneByMusic` returns the nearest scene that is not running. Each axis counts in proportion to the analyser's confidence in it, floored so an unsettled coordinate still counts a little, and intensity counts double. The reasoning and the rejected alternatives are in `plans/decisions/001-select-scenes-by-distance-in-music-space.md`.

Choosing is separate from switching. `SceneDirector::update` switches on a structural event after a short dwell, or when a rival beats the running scene by a margin for a sustained time once the scene's minimum has passed. Without the margin two scenes at similar distance trade places every frame. A scene just left carries a small penalty, newest strongest, so two near-equal scenes do not become a loop of two looks.

Animations that need a beat draw from `BeatClock`, not from `f.beatPhase` directly. The tracker's phase jumps on relock and is meaningless when confidence is low, so the clock free-runs at a fallback tempo and is pulled toward the tracked phase in proportion to confidence. An animation must not flip its own mode on a wall-clock timer: variety belongs to the selector, and a self-cycling animation fights it. Where a look depends on the music, a coordinate with a dead band and a minimum hold (`HoldLatch`, `HoldSelect`) replaces the timer.

Structural events reach the layers separately. `maybeInjectReactiveLayer` answers a drop with highlight and energy layers and a buildup with an overlay, each behind a cooldown, and accents on beats with a chance that rises when the beat is locked.

## The mood classifier

`MoodHistory::classifyMood` is a total partition rather than a set of independent tests. `level` picks one of five rungs through four ordered edges, and `bpm` and `dynamics` may each move the result by one rung before it is clamped back onto the ladder. Nothing can fall outside it.

That shape is the fix for the defect it replaced. Four overlapping booleans left four bands with no mood at all, the widest being every level between 0.30 and 0.40, so about a third of the input range classified as nothing. The nothing reached `SceneRegistry::pickSceneByMood`, whose fallback drew a scene uniformly at random, while `moodToString` rendered it as "Calm". The strip changed scene at random and the display said calm. A gate is what opens a dead band: `level > 0.6 && bpm > 100` ANDs one away, so level 0.5 was unreachable whatever was playing. A nudge of at most one rung cannot remove a band, and an ordered chain with `else` cannot leave a gap.

`bpm` is guarded above zero where it nudges down, because `bpm` is exactly 0 whenever no tempo is known and a bare `bpm < 80` would read every beatless passage as slow. `dynamics` nudges only while the range it has measured is wider than `DYN_MIN_SPAN`, so a signal with no variation is not split along its own noise. Both cuts are read from the class rather than hardcoded by whoever draws them.

There is deliberately no step limiter. The ladder is an ordering used for matching, never a path the classifier walks one rung at a time, because a track may go from floaty straight to intense and that is correct. What reads as a jarring change is answered by selection instead of by pacing. `pickSceneByMood` scores every scene on the distance between its `AnimationMeta::intensity` and the mood's target, breaks ties on tempo, and drops the running scene while another candidate remains. `random()` is not on the selection path at all.

A rung's target is the midpoint of its band, derived from the same four edges the classifier partitions on, which is what makes the two views one scale. Hand-picked instead, the targets were ordered so that FLOATY resolved to the brighter scene and CALM to the dimmer one, so the ladder ran backwards across its two quietest rungs and CALM to DANCY was a step several times the others. A catalog that grows cannot reorder them while they are derived. A structural mood is not on the ladder and takes a position of its own, which may coincide with a rung's scene: six of those against an eight-entry catalog have no choice, and a structural mood is meant to be answered by a scene tagged for it, so its target is the fallback.

A structural mood (`SILENT`, `TEASE`, `BUILDUP`, `DESCENT`, `DROP`, `WEIRD`) claims a scene tagged for it through `SceneDefinition::structuralMoods` before the intensity comparison. A ladder mood in that list would be worse than inert, since each tagged scene would win its own mood outright and the intensity axis would never be consulted. The branch is still empty, because no catalog entry is tagged for a structural mood yet, so all six currently fall through to intensity.

Classification runs in two stages, and the structural moods answer first. A passage can be structurally a drop at any rung, so the ladder cannot decide these and the rung is only consulted afterwards as a gate. Among the structural conditions the order is by how tightly each is tied to something that has just happened: `SILENT` is the gate, `DROP` is an event, `TEASE` is anchored to a drop inside a twelve second window, `BUILDUP` and `DESCENT` are standing measurements of a movement, and `WEIRD` is the loosest with two of three conditions. `TEASE` sits ahead of `DESCENT` because a drop is always followed by a fall, so the two genuinely overlap and the aftermath of an event is named by the event.

The detectors do not run while the gate is climbing, and the follower re-seeds when they resume. The gate is an exponential approach to one, so opening it from silence is a rise across the whole range of `level`, and read as a level it is a movement of nearly the full span. A follower seeded from a gated block starts at zero and every detector reports the gate's own opening: a `BUILDUP` by the letter of the rule, and a `DROP` once the three bands come up behind it. `GATE_SETTLED` is set by what it has to leave behind, since the climb remaining above it has to stay under both `BUILDUP_LEVEL` and `DROP_SCALE`.

`DROP`'s four conditions are chosen so that no beat can satisfy them. Breadth is the load-bearing one: a kick is low end and nothing else, which is what `BEAT_BASS_RISE` already exploits to tell a rhythm from a voice, and it cannot light all three bands against their own recent peaks at once. The preceding-quiet condition encodes that a drop follows a breakdown rather than arriving mid-chorus. No structural detector carries a cooldown, because a cooldown removes inputs from ever being reported and a removed input is a dead band; the mood's own confirmation and minimum hold supply the pacing instead.

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
