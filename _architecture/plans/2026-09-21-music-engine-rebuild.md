# music-engine-rebuild

Session: 2026-09-21. Status: designed, not started. Prerequisite work identified and independent of the open research.

## Context

The mood system works but is not intentional. One `MoodType` label is asked to carry both the character of a passage and the events inside it, bpm is a weak intensity signal because most dance music holds one tempo, and in practice most steady music pins to the top rung. The owner's direction, unchanged through three passes and restated because every previous plan drifted off it: **independent coordinates, not ranks and not ordered labels; animation metadata describing where in that space an animation works; selection by multidimensional compatibility with hysteresis; events separate from character.**

The problem statement is in `2026-09-21-mood-vs-events-revamp.md`. The durable music model is `docs/music-research/MODEL.md`. This file is the engineering plan for the current iteration only.

### The finding this plan is built on

Reading the current engine changes the shape of the work. **The signals already exist. The classifier is what destroys them.**

`AudioFeatures` (`src/audio/AudioFeatures.h`) already carries, as independent fields: `level`, `bassLevel` / `midLevel` / `trebleLevel`, `spectrumCentroid`, `spectralFlatness`, `dynamics`, `bpm`, `gateGain`, `buildup`, `descent`, `dropDetected`, `teaseDetected`, `anomaly`. That is most of a coordinate system already, and the band and level fields are measured **against rolling references rather than constants**, which is exactly the property a coordinate needs to survive a change of microphone or room. That work is done, and the comments record what it cost.

Then `MoodHistory::classifyMood()` (`src/scenes/MoodHistory.h:484`) flattens all of it into one enum: a five-rung ladder on `level`, nudged by `bpm` and `dynamics`, with the structural moods overriding by priority. Everything downstream — `SceneRegistry::pickSceneByMood()`, `animationMood()`, the per-animation `intensity` float — sees only that enum.

So this is not a DSP rebuild. The detectors are mostly fine and some are good. **The rebuild is the removal of a collapse**, plus a new consumer contract on the other side of it. That makes it far cheaper than it looks, and it puts most of the risk in the selection policy rather than in the audio.

### What this plan may and may not decide yet

Phase 2 of the research — real captures, marked by ear, compared against signals — has not run. That constrains the order rather than blocking it:

- **Safe to build now:** anything that *preserves* information rather than deciding on it. The coordinate layer is a refactor that stops throwing data away; it commits to no thresholds and makes no musical claims.
- **Not safe yet:** the selection policy, the compatibility metric, the animation region metadata, and any decision about which concepts are worth representing. Those need evidence and belong to phase 3 and after.

Where a step needs a value it cannot justify, it takes the current tuned constant and marks it inherited, not chosen.

## Build order

### 0. Signal path, first and independent of everything

Cannot be invalidated by any research outcome, and makes every rate-based coordinate trustworthy.

- Attach a sample-clock timestamp to each analysed block. `AudioFeatures` carries no time, so nothing downstream can know the gap between two blocks.
- Stop the LED frame stealing the audio hop. The I2S DMA ring is 8 x 64 samples, about 11.6 ms (`src/audio/AudioProcessor.cpp:28-29`), drained once per loop pass, and the 33 ms render blocks that loop. Adjacent blocks are separated by a variable, unrecorded gap. Either enlarge the ring, or move capture to the second core — which is the reason the S3 is the right board.
- Everything rate-shaped depends on a known hop: onset rate, flux, pulse strength, tempo, beat phase, and the "progressively shorter note values" build mechanism.

**Done when:** a replayed capture reports a constant hop, and a synthetic tone of known tempo reads the same bpm at 30 FPS and at 120 FPS. Until that holds, any tempo or activity tuning is measuring the render loop.

### 1. The coordinate block, alongside the mood enum rather than replacing it

Introduce a `MusicState` that the analyser fills and the classifier reads. Nothing downstream changes yet.

Each coordinate gets its **own** smoothing window. The one well-supported finding in the research is that different aspects need different memory lengths (TenseMusic, A), so a single global constant is known-wrong.

| Coordinate | Built from | Status |
|---|---|---|
| intensity | `level` against its rolling reference | exists |
| activity | onset rate and flux per band | **new**, needs step 0 |
| brightness | `spectrumCentroid`, relative | exists, needs relative framing |
| weight | `bassLevel` against its reference | exists |
| pulse | periodicity strength of the onset envelope | **new**, needs step 0 |
| tempo | `bpm` | exists, the one long-lived signal |
| texture | `spectralFlatness`, tone against noise | exists, unused downstream |
| presence | `gateGain`, plus a no-music state | exists |

Each carries a **confidence**, because a coordinate that cannot be estimated right now must be able to say so rather than emit a default that reads as a real value. `bpm` is the cautionary example that already exists: it is exactly 0 on every beatless passage, and `ladderRankFrom` has to special-case `bpm > 1.0f` to stop that reading as slow music.

Each also carries a **trend** over its own window — rising, falling or flat, with a magnitude. Trends are where "a build is coming" and "we just came down" live, and they are per coordinate because loudness can fall while density rises.

**Do not collapse. Do not rank. Do not order.** A coordinate is a number with a confidence and a trend, and nothing in this block knows what a mood is.

**Done when:** replaying a capture produces the traces plus confidences, and `classifyMood()` still returns exactly what it returned before, bit for bit, on the same input. That equivalence is the safety property of this step.

### 2. Events, separate, with latency and confidence

Keep the existing detectors. Change what they produce.

`dropDetected`, `teaseDetected`, `buildup`, `descent` and `anomaly` already exist and already encode real structural information. Promote them out of the mood enum into an event and structural-state channel read independently of the coordinates, so **a drop can occur inside any character** — the owner's stated requirement, and the thing the priority chain in `classifyMood()` makes impossible today.

Each event carries a confidence and the latency at which it was recognised. The latency budget is real and generous: a few hundred ms is imperceptible on a strip in a crowd, so a detector may buffer and confirm rather than fire on the first frame. Late and correct beats instant and wrong.

Bias every detector toward firing. A flash that should not have happened reads as part of the show; a drop the strip sleeps through is the failure everyone notices. This is the opposite of what the accuracy-oriented literature optimises for, and it is correct here.

**Not decided here:** whether tease survives as its own event, whether descent is an event or only a trend, and whether the current drop detector is good enough. Those are phase 2 questions. The channel is built to carry them either way.

### 3. Strangler: derive the old enum from the new state

`MoodHistory::classifyMood()` becomes a function of `MusicState` instead of `MoodSnapshot`, and keeps returning `MoodType`.

This is the step that makes the rebuild survivable. The 37 animations, `pickSceneByMood`, `animationMood`, the scene registry and every existing check keep working against an enum that is now *derived* rather than *primary*. The old behaviour becomes one possible consumer of the new model rather than the model itself, and it can be deleted later without a flag day.

**Done when:** the check suite passes unchanged, and replaying every capture gives the same mood sequence as before. If it does not, the coordinate layer lost information and the fault is in step 1.

### 4. Selection against the coordinates — gated on phase 2

Not before. This is the step that needs evidence, and it is where the previous two planning passes went wrong by reaching for it early.

- Animation metadata describing the **region** of the coordinate space an animation works in, replacing the single `MoodType mood` plus `float intensity` pair in `AnimationCatalog.h:58-60`.
- Selection by multidimensional compatibility against that region.
- Hysteresis, the hard part and the main risk: too little and the strip thrashes between animations on coordinate noise, too much and it sits still through a genuine change. Likely needs a dwell floor, a change threshold that scales with how far the state moved, and an event override that cuts through both.
- Layers currently read the animation's `intensity` value alone, so two animations at similar intensity composite alike and identity comes almost entirely from the base animation. Once coordinates exist, layer choice should read them directly.

**Prerequisite, still unchecked:** how many of the 37 animations are visually distinct rather than recolours, and which honour speed, palette and spread. That decides whether a few animations get driven continuously by the coordinates or whether many discrete ones get selected between — a different design, not a parameter.

## Specifications the build needs

Added after a readiness pass found the plan above too abstract to hand to an implementer without it inventing answers. Everything here is a starting point chosen to be cheap and reversible, not a researched value. Inherited constants are named as such.

### Step 0: enlarge the ring, do not move cores yet

The plan offered two options. Take the cheap one first. Raise `dma_buf_count` from 8 to 32 in `src/audio/AudioProcessor.cpp:28`, which buys about 46 ms of ring against a 33 ms render frame. Moving capture to the second core means a FreeRTOS task, a shared buffer and synchronisation, and it is a much larger change to make on a board that has not been flashed with a microphone yet. Do it only if the larger ring still drops blocks.

The timestamp is the part that is not optional. Add a sample counter to `AudioProcessor` that increments by the block size on every accepted block, and carry it on `AudioFeatures` as a frame index plus a derived `dtSeconds` since the previous block. Everything rate-shaped divides by that instead of assuming 33 ms.

### Step 1: what a coordinate actually is

```
struct Coord {
    float value;       // 0..1, or a named unit for tempo
    float confidence;  // 0..1, see below
    float trend;       // signed, units of value per second
};
```

- **value** stays relative to a rolling reference, never to an absolute constant. Reuse the existing pattern in `AudioFeatures::updateBandLevels`, which is the shape the rest of the codebase already trusts.
- **confidence** is *not* a quality score. It is "can this be estimated right now at all", 0 when the input it needs is absent and 1 when it is fully present. Concretely: gate-derived for everything (a coordinate measured through a closed gate has no confidence), plus for tempo the count of usable beat intervals over `BEAT_BPM_WINDOW`, and for pulse the same. A coordinate at confidence 0 must never be read as a value of 0 — that conflation is the bug `ladderRankFrom` already works around for bpm.
- **trend** is the difference between a fast and a slow EMA of the same value, divided by the elapsed time. Not a fitted slope. Two EMAs per coordinate is cheap, needs no history buffer, and is what the device can afford.

**Starting windows.** No researched values exist for this material, so these are chosen for shape and to be tuned on replay. Fast EMA then slow EMA, in seconds:

| Coordinate | fast | slow | note |
|---|---|---|---|
| intensity | 0.3 | 3 | TenseMusic fitted 3/3 for loudness (A, classical) |
| activity | 0.5 | 5 | |
| brightness | 0.3 | 3 | |
| weight | 0.3 | 3 | |
| pulse | 2 | 10 | slow because pulse is a property of a passage |
| tempo | — | — | already has its own hold and fade, leave them |
| texture | 1 | 8 | |
| presence | — | — | the gate's own ramp, already tuned |

`BUILDUP_TAU_SEC` is 10.0f and is the existing slow-mean constant for buildup and descent. Where a coordinate wants a slow window, prefer that value over inventing a neighbour.

### Step 1: the two new coordinates, specified

Both are the hardest new code in the plan and both were one table cell. Specify them or they get invented badly.

**activity, from spectral flux.** Half-wave rectified spectral flux: per block, sum over bins of `max(0, mag[k] - prevMag[k])`, using the `spectrum` array already computed. Normalise against a rolling reference exactly as `level` is. Onset rate is that flux crossing an adaptive threshold — the running median of recent flux times a factor — with a refractory period of `MIN_BEAT_INTERVAL` (250 ms, inherited).

**Warning that is load-bearing:** the existing beat detector is deliberately gated on a *bass* rise, because every syllable of speech is an onset and without that gate the tempo followed voices at 140-200 BPM with nothing playing. Broadband flux reintroduces exactly that failure. Activity is allowed to respond to speech — it is genuine activity — but **tempo and pulse must keep the bass gate**. Do not unify them.

**pulse, from interval consistency rather than autocorrelation.** Autocorrelating an onset envelope is the textbook answer and is too expensive here. The beat detector already keeps `beatIntervals`, a ring of the last `BEAT_BPM_WINDOW` (12) intervals, and already takes their median. Pulse strength is the *consistency* of that ring: one minus the median absolute deviation over the median, clamped to 0..1. A machine-steady four-on-the-floor gives intervals that barely vary and reads near 1; an irregular or beatless passage gives a scattered ring and reads near 0. Confidence is how full the ring is. This reuses state that already exists and costs almost nothing.

### Observability is the first deliverable, and it spans three surfaces

Revised after the owner confirmed the **web emulator is the primary tuning surface** — it is how the current system was tuned to feel decent, and it is how the revamp will be. That makes a coordinate invisible in the browser untunable, which outranks everything else in this section.

The structural fields are currently absent from **every** readout. All three need the same seven fields added — `buildup`, `descent`, `dropDetected`, `teaseDetected`, `anomaly`, `gateGain`, `spectralFlatness` — and later the coordinates too:

1. **`gg_feature()` (`src/wasm_main.cpp:179`)** — an indexed switch exposing cases 0 to 15, plus the HUD list in `web/live.js:232-259`. This is the one that matters most. Extend both together; the index is the contract between them.
2. **`kTracked` (`src/sim_main.cpp:2723`)** — the native replay summary. Level, bands, energy, dynamics, bpm, centroid, noiseFloor and nothing structural.
3. **`--trace <file.csv>`**, which does not exist yet — one row per frame, frame index plus every value plus the mood. Step 3's equivalence check is a diff of two of these and has no other mechanism. Also what phase 2's comparison against ear-marks needs, so it pays for itself twice.

**Reset the new state in `gg_reset_analysis()` (`src/wasm_main.cpp:117`).** It exists because switching input source leaves the classifier's smoothed values, adaptive window and snapshot history built from whatever was playing before, which showed up as a mood stuck from the previous source. Every coordinate EMA, trend follower and confidence counter has exactly this defect and must be reset there. Missing this makes browser tuning quietly untrustworthy in the one workflow that matters, because switching between test inputs is what tuning *is*.

### Step 3: what the equivalence check can actually be

The plan asserted bit-for-bit equivalence as its safety property without a mechanism, and there is none: `replayCapture` prints summary statistics, not a per-frame trace. The `--trace` mode above is that mechanism, and equivalence becomes a diff of two CSVs.

If a full per-frame diff proves awkward, the weaker but usable fallback already exists in the summary: the mood percentages, the change count and the dwell distribution together form a fingerprint that a lossy refactor will disturb.

### Where the new code lives, and one trap

`MusicState` belongs in `src/audio/`, filled by `AudioProcessor` at the end of `analyzeAudio()` and read by `MoodHistory`. Header-only is simplest and avoids the trap below.

**The trap, and it is in both build systems.** `build_src_filter` in `platformio.ini` and `repo_src` in `tools/build-wasm.sh:168` *both* glob `scenes/*.cpp` and `animations/*.cpp` while listing `audio/AudioProcessor.cpp` **explicitly**. A new `.cpp` under `src/audio/` is silently excluded from the native harness *and* from the web emulator, and appears to work until the device link fails. **Keep new audio code header-only.** That is the reliable answer; adding the file to two lists in the same commit is the fragile one.

**How the two builds relate**, since it is easy to assume one produces the other. They are separate builds over shared source. `tools/build-wasm.sh` compiles its own list with `em++` into `web/live/` and never touches `sim_main.cpp`; it requires `pio run -e native` to have run once only to populate `.pio/libdeps/native/FastLED` and `arduinoFFT`, which is a dependency fetch and not a harness build. Both compile the same `AudioProcessor.cpp`, `scenes/*.cpp` and `animations/*.cpp`, so **analysis behaviour is genuinely shared and tuning in the browser tunes the device**. Only the readout surfaces differ: `gg_feature` for the page, `kTracked` for the harness. Extend both or the same work has to be done twice.

The existing `churn%` column in the replay output is already the jitter measurement the risks section asks for. Use it rather than adding another.

## What must survive the rebuild

Hard-won, easy to destroy while refactoring. The comments in `AudioFeatures.h` record what each cost to find.

- **Rolling references instead of constants.** `level` and the three `bandLevel`s are measured against their own recent peak. Every animation constant compared against an absolute band was a claim about one microphone, and the bass-driven animations rendered near black on real audio while passing every check. A coordinate system must not reintroduce absolute thresholds.
- **The two gamma sinks.** `pixelLevel()` and `hsvLevel()` are not interchangeable — FastLED's `hsv2rgb_rainbow` squares `val` on the way out, so handing `pixelLevel` to a CHSV val cancels exactly and renders black with nothing to show it went wrong.
- **Defaults that fail safe.** `gateGain` defaults to 1.0f, not 0.0f, because the harness builds `AudioFeatures` by hand and a producer that forgets a field must get normal behaviour rather than a dead strip. Every new coordinate needs the same treatment.
- **The noise floor and gate behaviour**, recently fixed and easy to regress.

## Verification

Two loops with different jobs. Neither replaces the other.

**The web emulator is the tuning loop** — does it look and feel right. `tools/build-wasm.sh --watch` rebuilds on save and the open page reloads itself, so the loop is edit, save, look. This is how the current system was tuned and how the revamp will be, so a coordinate that is not on the HUD is not tunable. Judgement here is by eye and is the point, not a weaker substitute for a measurement.

**The native harness is the correctness gate** — did I break something. Checks, replay statistics, the equivalence diff. It cannot tell you whether anything looks good.

Expect to spend most of the time in the first and to run the second before every commit. The device microphone will differ from the browser's and that is a later tuning pass, not a reason to tune on the device now.

The harness should be extended rather than replaced.

- `src/sim_main.cpp --replay <file.f32>` runs a capture through the real analyser at the device's 33 ms step. The equivalence checks in steps 1 and 3 run here.
- The existing check suite asserts the mood partition is total by sweeping the input space through `classifyForTest`. The coordinate layer needs the analogous property: no input produces a coordinate outside its range, or a confidence that lies.
- **Tune only on real microphone captures.** The synthetic demo signal is not a substitute and has misled tuning before.

## Risks

- **Hysteresis is the real difficulty**, not the audio. Thrash and stasis are both failure modes and the gap between them may be narrow. Budget for tuning it against replay, and accept that it may need an event override that bypasses it entirely.
- **Coordinate noise is unknown** until real captures are replayed. Some coordinates may be too jittery to select on without heavy smoothing, and heavy smoothing may make them too slow to be expressive. Per-coordinate windows exist partly for this.
- **Step 1 losing information silently.** The bit-for-bit equivalence check is the guard, and it is worth the effort of building.
- **Scope creep back into implementation-first.** Step 4 is gated for a reason. Three planning passes have now drifted into it early.

## Implementation deviations
