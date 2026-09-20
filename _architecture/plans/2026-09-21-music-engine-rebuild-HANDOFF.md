# music-engine-rebuild — handoff

Session: 2026-09-21. Status: ready to build steps 0 to 3. Step 4 is gated and must not be started.

For an implementer picking this up cold, including a smaller model. The design and the reasoning are in `2026-09-21-music-engine-rebuild.md` — this file does not repeat them. It covers what to read, in what order to work, how to build and check, and the traps that have already cost this project time.

## Read before touching anything

In this order. Stop when you can answer "what does `classifyMood()` throw away".

1. `2026-09-21-music-engine-rebuild.md` — the plan. Its "Specifications the build needs" section has the concrete values, algorithms and structs. Work from that section, not from the prose above it.
2. `src/audio/AudioFeatures.h` — 193 lines, nearly all comments, and the comments are the record of what previous tuning cost. Read them, do not skim them.
3. `src/scenes/MoodHistory.h:432-500` — `ladderRankFrom` and `classifyMood`. This is the collapse the rebuild removes.

Do **not** read the whole catalog or the animations. Nothing in steps 0 to 3 touches them.

## Build and run

**Three builds, over one shared set of sources.** The native harness and the web emulator are separate builds; neither produces the other. `tools/build-wasm.sh` needs `pio run -e native` to have run once, but only to populate `.pio/libdeps/native/FastLED` and `arduinoFFT` — a dependency fetch, not a harness build. All three compile the same `src/audio/AudioProcessor.cpp`, `src/scenes/*.cpp` and `src/animations/*.cpp`, so analysis behaviour is shared and tuning in the browser tunes the device.

PlatformIO covers two of them.

```bash
pio run -e native
```

```bash
pio run -e esp32-s3-devkitc-1
```

The native environment builds `sim_main.cpp` into a harness that runs every check. Run it after every step:

```bash
.pio/build/native/program
```

Replay a real microphone capture through the real analyser:

```bash
.pio/build/native/program --replay path/to/capture.f32
```

The third is the web emulator, and it is where the owner does the tuning. Needs Emscripten on `PATH`:

```bash
tools/build-wasm.sh --watch
```

It rebuilds on save and an open page reloads itself, so the loop is edit, save, look. Serve the page with:

```bash
node tools/serve-web.js
```

**Environment trap, and it fails silently.** The native environment needs the WinGet WinLibs mingw64 `bin` directory on `PATH` — both to build and again to run the resulting binary. Without it the binary exits 1 with no output, which reads as a failing check rather than a missing compiler. If `program` exits 1 and prints nothing, this is why.

## Order of work

Do not reorder. Each step's check gates the next.

**First, before step 0: make the structural signals visible, on all three surfaces.** They are currently absent from every readout, and the web emulator is the surface the owner tunes on, so a value missing there cannot be tuned. Add `buildup`, `descent`, `dropDetected`, `teaseDetected`, `anomaly`, `gateGain` and `spectralFlatness` to:

- `gg_feature()` at `src/wasm_main.cpp:179` — an indexed switch, cases 0 to 15 today — **and** the HUD list at `web/live.js:232-259`. The index is the contract between them, so they change together. This is the one that matters most.
- `kTracked` at `src/sim_main.cpp:2723` — the native replay summary.

**Also reset them in `gg_reset_analysis()` at `src/wasm_main.cpp:117`.** It exists because switching input source used to leave the classifier holding smoothed values built from the previous source, which read as a mood stuck from the last input. Every coordinate EMA, trend follower and confidence counter added later has exactly this defect. Switching between test inputs *is* what tuning consists of, so missing this makes browser tuning quietly untrustworthy.

**Then, also before step 0.** Add `--trace <file.csv>` to `sim_main.cpp`: one row per frame, frame index plus every tracked value plus the mood. Step 3's equivalence check is a diff of two of these and has no other mechanism.

**Step 0.** Ring size and the block timestamp. Take the cheap option — raise `dma_buf_count` 8 to 32. Do **not** move capture to the second core; that option is in the plan as a fallback only.

**Step 1.** The coordinate block. Exists alongside the mood enum. Changes no downstream behaviour.

**Step 2.** Events out of the enum into their own channel.

**Step 3.** `classifyMood()` becomes a function of the new state and still returns `MoodType`.

**Step 4. Stop.** Selection, animation region metadata, hysteresis and layers are gated on research that has not happened. Three planning passes have already drifted into this step early. If steps 0 to 3 are done and checks pass, the handoff ends — report back rather than continuing.

## How to know a step worked

Two loops, different jobs. The **web emulator** answers "does it look and feel right" — judged by eye, and that is the point rather than a weaker substitute for a measurement. The **native harness** answers "did I break something" and cannot tell you whether anything looks good. Expect most of the time in the first, and run the second before every commit.

The device microphone will differ from the browser's. That is a later tuning pass, not a reason to tune on hardware now.

- **Every step:** `.pio/build/native/program` passes every check, unchanged. A check that needs editing to pass is a signal you changed behaviour, not that the check is stale.
- **Every step:** the emulator still runs and the HUD still shows every value it showed before.
- **Step 0:** a synthetic tone of known tempo reads the same bpm at 30 FPS and at 120 FPS.
- **Steps 1 and 3:** `--trace` output is identical before and after, or if a full diff proves awkward, the replay summary's mood percentages, change count and dwell distribution are unchanged. Any difference means the coordinate layer lost information.
- **Coordinate jitter:** the replay summary already prints a `churn%` column — the mean frame-to-frame change as a fraction of the value's own range. Above roughly 10% a value is flickering. Use this rather than adding another measurement.

## Traps

Each of these has already caused a real bug here.

- **Never compare a band or level against an absolute constant.** They are measured against rolling references for a reason: every absolute constant was a claim about one microphone, and the bass-driven animations rendered near black on real audio while passing every check. `AudioFeatures::updateBandLevels` is the pattern to copy.
- **`pixelLevel()` and `hsvLevel()` are not interchangeable.** FastLED's `hsv2rgb_rainbow` squares `val` on the way out. Handing `pixelLevel` to a CHSV val cancels exactly and renders black with nothing to indicate it went wrong.
- **New fields default to a value that fails safe.** `gateGain` defaults to `1.0f`, not `0.0f`, because the harness builds `AudioFeatures` by hand in fixtures and a producer that forgets a field must get normal behaviour rather than a dead strip. Every coordinate needs the same treatment.
- **Confidence 0 is not value 0.** A coordinate that cannot be estimated must say so. `bpm` is exactly 0 on every beatless passage and `ladderRankFrom` carries a special case to stop that reading as slow music — do not recreate that bug in new code.
- **Keep the bass gate on tempo and pulse.** The beat detector requires a bass rise as well as a level rise because every syllable of speech is an onset; without it the tempo followed voices at 140 to 200 BPM with nothing playing. Broadband spectral flux feeds *activity* only.
- **New audio code must be header-only.** `build_src_filter` in `platformio.ini` and `repo_src` in `tools/build-wasm.sh:168` *both* glob `scenes/*.cpp` and `animations/*.cpp` while listing `audio/AudioProcessor.cpp` explicitly. A new `.cpp` under `src/audio/` is silently dropped from the native harness *and* the web emulator, and appears to work until the device link fails. Header-only is the reliable answer; remembering two file lists is the fragile one.
- **Anything with memory must reset in `gg_reset_analysis()`.** See the first task under "Order of work".
- **Tune only on real microphone captures.** The synthetic demo signal is a fake input and has misled tuning before.

## Not yours to decide

- Whether tease survives as an event, whether descent is an event or a trend, whether the current drop detector is good enough. Phase 2 questions. Build the channel to carry them either way.
- Any threshold that encodes a musical claim. Where the plan gives a starting value it is marked inherited or chosen-for-shape. Tune those on replay; do not invent new ones.
- The animation catalog, scene selection, layers.

## Commits

Do not run `git commit` or `git push`. The owner does all commits.
