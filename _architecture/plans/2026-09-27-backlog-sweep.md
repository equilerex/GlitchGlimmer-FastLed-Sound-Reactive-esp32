# Backlog sweep: cleanup, memory health, web polish, gate, defect sweep, tease

Session: 27-09-2026. Status: in progress. Phases 0, 1 and 3 done, rest not started.

## Context

This plan covers every open `TODO.md` and `BACKLOG.md` item that can move without the owner's hardware, ears or a long microphone capture. The owner-gated items stay out: the 60 to 120 s real capture, flashing the board, enabling Pages, the `dropConfidence` weights, the meaning of `level`, and the license. Anything here that turns out to need one of those stops at that point and says so. It does not tune around the gap.

Preconditions. The uncommitted drop-detector work (10 modified files plus the `tools/*.py` evaluation scripts) is committed or deliberately parked first. Every phase below starts from a clean tree so each one can be reviewed as its own diff.

Read before starting: `_architecture/ARCHITECTURE.md` (audio pipeline, compositor, brightness curves), `docs/music-research/MODEL.md` before Phase 8, and `web/viz/CONTEXT.md` before Phase 3.

## Decisions made in this session

- **Tease is two concepts.** The owner's definition (2026-09-21) becomes the detected `TEASE`: the established tune continues at steady tempo while something odd is mixed in, or the sound cuts in and out. `MODEL.md` §11, "withheld release", stays as research and remains annotation-only and undetected. `MODEL.md` gains a new concept for the detected one, and §11 gets a line saying the `TEASE` label no longer means it.
- **Heap gate gets requirements, not a patch.** The owner called the current gate a low-effort MVP. Phase 2 defines what the firmware must do under memory pressure and builds one guard to that spec.
- **The three AI-era docs stay untouched.** `AI_ASSIST_INSTRUCTIONS.MD`, `docs/_Fastled-animation-guidelines.md` and `docs/ai-fastled-guide.md` are out of scope by owner decision.
- **Inert `Config.h` macros stay.** Already decided by the owner on 2026-09-18. They are not reopened here.

## Verified state at planning time

Facts checked against the code on 27-09-2026, so the executor does not re-derive them:

- `CORE_DEBUG_LEVEL` is already `0` in `platformio.ini`. The "Verbose ESP-IDF logging" backlog item is stale.
- The heap threshold is a bare literal in four places: `src/main.ino:94` (20 KB, stalls `controller.update()`), `src/core/MainController.cpp:294` and `:334` (20 KB, gates audio capture), and `:308` (10 KB, LED-only branch, unreachable behind the first). The `:308` branch also `Serial.println`s on every frame it runs.
- `MainController` still builds its own `SceneRegistry` and `SceneDirector` (`MainController.cpp:111-127`, `begin()` at `:181`). The live director is inside `LEDStripController`.
- `AudioHistory` is `SnapshotRing<AudioSnapshot, 1500>` (`src/audio/AudioSnapshot.h:25`). The only reader is `MoodMemoryArcLayer` (`VisualLayers.h:442-448`, last 10 entries). `src/sim_main.cpp:597-603` asserts the peak equals 1500.
- Per-pixel math in `VisualLayers.h`: `:199` `sin(phase + millis() / 200.0)` (double literal, and `millis()` inside the loop), `:316` `sin(...)`, `:349` `exp(-pow(..., 2))`.
- Dead code: `src/scenes/LayerPool.h`, `src/animations/visual-layers/AlienSquirtTrailLayer.h`, `SettingIconWidget` in `DisplayManager.h`, `ScrollingTextWidget` in `Widget.h`. `src/scenes/CONTEXT.md` and `src/animations/visual-layers/CONTEXT.md` mention them.
- Silence gate (`AudioProcessor.cpp:401-412`): it closes when `volume <= noiseFloor * 1.5 + 0.0005` for 350 ms. At the reported floor of 0.0003 the close threshold is 0.00095, so a volume of 0.0005 should close it. The report and the code disagree, so the bug is in what was displayed or in which values feed the comparison. It is not in the threshold arithmetic.
- The current tease detector (`AudioProcessor.cpp` around `:1130-1159`) ORs three triggers: `postDrop` (12 s after a drop), `hush` and `fakeOut` (level variance). None of them matches the owner's definition.
- Ground truth: `web/audio/local/ground_truth.json` (demo, 58 events) and `jazz_ground_truth.json` (8 events) have drops, buildups, payoffs, pauses, aftermaths and sections, and **no tease labels**. `web/audio/local/` is gitignored.
- Web: `package.json` has no `"type"`. `web/render.js:67-72` hardcodes `clientWidth - 16` and `cssHeight = 48`. `web/viz/path.js:254` ties with `<=`. `npm test` had 1 failure at last record: `hardware settings use physical scale by default` in `test/viz/hwStore.test.js`.

## Global constraints

- Device build is `-std=gnu++11`. Run `pio run -e esp32s3` after every phase that touches `src/`. Native passing proves nothing about the device.
- Every behaviour change in `src/audio` or `src/scenes` gets a native harness check that fails before the change and passes after it.
- No tuning against the synthetic signal. The `demo.f32` and `jazz.f32` replays are real music but did not come through the microphone, so they can validate *detection logic and relative shape*, not absolute gain constants. Any constant that encodes microphone gain waits for the real capture and gets logged as such.
- No commits by the agent. Each phase ends at a review point with the tree green, and the owner commits.
- A change that makes `ARCHITECTURE.md`, a `CONTEXT.md` or `MODEL.md` wrong is fixed in the same phase.

## Phase 0: baseline

Run and record the counts in this plan's deviations section: `npm run native`, `pio run -e esp32s3` (flash and RAM percentages), `npm test`, `npm run wasm`. Every later phase compares against these. If the `hwStore` test still fails, it goes to Phase 3 first.

## Phase 1: firmware cleanup

Low risk and mechanical. Each step leaves the harness count unchanged unless noted.

1. **Dead `SceneDirector` in `MainController`.** Remove the `sceneRegistry` and `sceneDirector` members, their construction, `begin()` and deletion. Check whether `moodHistory` in `MainController` has any other consumer. If it existed only for that director, it goes too. Confirm that the display scene name still reads from `LEDStripController`.
2. **Dead classes.** Delete `LayerPool.h` and `AlienSquirtTrailLayer.h`. Remove the `SettingIconWidget` member from `DisplayManager` and the `ScrollingTextWidget` class, after grepping the `.cpp` files for construction. Fix both `CONTEXT.md` files that name them.
3. **Per-pixel math.** Hoist `millis()` and anything else that does not change per pixel out of the loops. Replace `sin`/`exp`/`pow` on floats with `sinf`/`expf` and `x * x`, and turn double literals into `f` literals. Then sweep all of `src/animations/` for the same pattern (double literals and libm calls inside per-LED loops) and fix it everywhere. Verify with `npm run frames` before and after. Frames should match within float rounding, and any larger visual difference is a bug in the change.
4. **Shrink `AudioHistory`.** The capacity becomes a named constant in `Config.h`, sized to the deepest reader plus headroom (32, reasoned in a comment). Update the harness assertion in `sim_main.cpp:597-603` to the new cap. This frees about 58 KB of heap, which feeds Phase 2's numbers.
5. **Backlog hygiene.** Mark "Verbose ESP-IDF logging" DROPPED as already fixed. Mark the Phase 1 items DROPPED as done, with the date.

Review point: `pio run -e esp32s3` flash and RAM compared with Phase 0, harness green.

## Phase 2: memory health, requirements and guard

### Requirements

- **R1. The gate is a safety net, not a mode.** After `setup()`, steady state allocates nothing per frame. The harness already counts allocations. A non-zero steady-state count is the bug, and the gate only covers what the harness cannot see (`String`, which `sim/stubs/Arduino.h` aliases to `std::string`, and device-only paths such as TFT_eSPI and I2S).
- **R2. Shed load in priority order.** Audio capture and analysis come first, because it allocates nothing and the LEDs are meaningless without it. LED render and output come second, as the product. Display third. Input polling and periodic logging last. Today the 20 KB gate stops audio first, which is the opposite order.
- **R3. Judge by the right quantity.** Total free heap hides fragmentation. The guard reads both `ESP.getFreeHeap()` and the largest free block (`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`), and each state is entered on whichever is worse.
- **R4. Hysteresis.** Each state is left only above its entry threshold plus a margin, so the guard cannot flap at 30 fps.
- **R5. Observable, once.** A state transition logs one line with free heap, largest block and the minimum ever seen (`ESP.getMinFreeHeap()`). The status screen shows the state when it is not `OK`. Nothing logs per frame.
- **R6. Bounded failure.** If `CRITICAL` holds for `HEAP_CRITICAL_RESTART_MS`, the firmware calls `ESP.restart()`. A few seconds dark is better than an unbounded frozen wearable. Before restarting it writes the reason to the log.
- **R7. One source of truth.** All thresholds live in `Config.h` with the reasoning. No heap literal remains anywhere else. `main.ino` and `MainController` both ask the guard.
- **R8. Testable on the host.** The guard is a pure state machine: `(freeHeap, largestBlock, nowMs) -> {OK, DEGRADED, CRITICAL}` plus a restart request. The harness drives it with fake readings: entry, exit with hysteresis, fragmentation-only entry, and a restart after the timeout and not before.

### States

| State | Entered when | Runs | Skips |
|---|---|---|---|
| `OK` | above `HEAP_DEGRADED_EXIT` | everything | nothing |
| `DEGRADED` | below `HEAP_DEGRADED_ENTER` | audio, LEDs | display, inputs, periodic logging |
| `CRITICAL` | below `HEAP_CRITICAL_ENTER` | audio, LEDs at reduced brightness, no layer injection | everything else. Restart after timeout |

Initial values are 24 KB and 32 KB exit for degraded, 12 KB for critical, and 10 s before restart. They are provisional, and the `Config.h` comment says so. The hardware checklist item gains a step: log boot heap, steady-state minimum and largest block over 10 minutes of music, then set the thresholds from those numbers.

### Build

New `src/core/MemoryGuard.h` (header-only, C++11, no Arduino calls inside the state machine, readings passed in). `main.ino` feeds it and drops `isMemoryHealthy()`. `MainController::update` branches on its state and removes the literals at `:294`, `:308` and `:334`. Harness checks as in R8. `ARCHITECTURE.md` gets a short "Memory pressure" section with R2, R6 and R7. The backlog item "The heap gate has a floor that stops everything" is marked DROPPED as superseded by this.

Review point: device build, and grep shows no remaining `* 1024` heap literal outside `Config.h`.

## Phase 3: web polish

1. **`hwStore` test failure.** Find out whether the test or the default is wrong, from the store's own intent and `web/viz/CONTEXT.md`. Fix the side that is wrong.
2. **`MODULE_TYPELESS_PACKAGE_JSON`.** `tools/*.js` are CommonJS (`require`), so a root `"type": "module"` would break them. Add `web/package.json` containing `{"type": "module"}`, which covers the modules the tests import, and add `test/package.json` if the tests themselves are ESM. Exclude `web/package.json` from `dist/` in `tools/build-dist.js`. The root stays untyped.
3. **Spectrum sizing.** `Spectrum.paint` reads the canvas's own laid-out size (`getBoundingClientRect` on the canvas, `ResizeObserver` for changes), and the stylesheet owns width and height. Remove the `- 16` and the inline height write.
4. **`hitHandle` tie.** Change to `<` so the lower index wins a tie, deterministically. Add a test with two handles at equal distance.
5. **Test gaps.** Give `fitScale` a test with hand-computed expected numbers, not a round-trip. Test `placePixels` at a pitch just above the 0.25 px floor. Run `pointAt` and `placePixels` over all ten presets, checking monotonic arc length and no NaN, with `corner` as the tight case.
6. **Pixel counts by construction.** Where the manifest is generated (find the writer, `tools/dump-frames.js` or the harness), stamp it with the `LED_0_NUM`/`LED_1_NUM` the binary compiled with. On a mode switch the page compares manifest counts with the WASM counts and shows a visible mismatch notice instead of silently rebuilding the bench. Add a unit test for the comparison. Mark the backlog item DROPPED as done.

Review point: `npm test` fully green, and `npm run build:dist` produces no unexpected `dist/` churn.

## Phase 4: browser verification pass

Serve with `npm start` and use a foreground tab through the browser tools. First run `npm run wasm`, since the SDK is pinned in `.tools/emsdk`, so the "Emscripten not on PATH" note in `TODO.md` is obsolete.

- Structure card renders with episodes open and closing (`?source=demo`, the EDM track through a buildup and drop).
- Density layout: scrollHeight equal to clientHeight at 1080p with the event stream populated. Then 1101 px width and phone width (390 px), with no horizontal scroll.
- Strip geometry: length, pitch and zoom controls, wrap presets, hover handles. Zoom must not change software count or physical length.
- Selector: scene changes across the demo track follow the distance scores, with no scene flapping inside the hysteresis window.

This phase only observes. Each defect becomes a backlog entry with a screenshot reference, and the fixes are sized afterwards rather than done inline. The TODO lines for these checks are ticked or rewritten with what was seen.

## Phase 5: silence gate

The arithmetic says the reported state cannot happen, so start by reproducing it:

1. Add a harness fixture: a flat-spectrum noise bed that raises `noiseFloor` (the tonal synthetic never does, because the floor only rises above `NOISE_FLAT_MIN`), followed by a signal at 1.7x the floor. Assert that the gate closes within `GATE_HANGOVER_MS` plus one block, and that `level` stays near zero. This also closes the gap that no harness check reaches the floor-relative guard on `level`.
2. If the fixture passes, the bug is on the observation side. Compare the values the web panel shows as "volume" and "noise floor" with the ones the gate compares (smoothed versus raw, feature index mapping in `gg_feature`, units). Fix whichever is mislabelled.
3. If it fails, trace which input keeps refreshing `lastSignalTime`.

Level reading 91% near the floor is a separate symptom of the `level` meaning question, which is owner-gated. This phase only shows whether the `LEVEL_REF_MIN_OVER_NOISE` floor does its job and records the result against the backlog item "Decide what Level should mean".

## Phase 6: defect-class sweep

The classes, from `TODO.md`: (a) an absolute constant that is really a claim about one input's gain, (b) a reference that is a statistic of the value it judges, (c) a per-block coefficient where a per-second rate is meant, (d) a value pinned at 0 or 1 by construction, and (e) state that a `resetTracking`/`clearStructure` path misses.

1. **Find, read-only.** Three parallel investigators split by scope: `src/audio/` (the most likely source of a, b, c and d), the render path (`src/animations/`, `src/scenes/`, `src/core/LEDStripController.h`), and the stateful reset paths (every member of `AudioProcessor`, `StructuralEpisodes`, `SceneDirector` and `MoodHistory` checked against every reset function). Each finding needs file:line, class, and a concrete input that produces the wrong output. A finding without a failing input is a note, not a defect.
2. **Verify.** Each finding gets a harness fixture that demonstrates it. Findings that cannot be demonstrated are dropped with the reason.
3. **Triage** into `plans/2026-09-27-defect-sweep-findings.md`: fix now, needs real capture (class (a) mostly lands here), or owner call.
4. **Fix** the "fix now" set one defect per review unit, each with its fixture. Class (c) fixes convert to `1 - exp(-dt / tau)` using the sample clock's `dt`, with `tau` named in seconds in `Config.h`. Class (b) fixes need a reference that is independent of the value being judged. That design choice goes in the findings file per defect.

This phase runs after Phase 5 (which is one instance of class (a)) and before Phase 8, which adds new state to `AudioProcessor` and should start on clean reset paths.

## Phase 7: coordinate inputs for the rest of the catalog

The handoff's Step 4.2. Profile tuning (4.1) is out because it needs real audio.

1. Build an inventory table of each registered animation in `AnimationCatalog` (55 per the handoff). For each, list which `AudioFeatures`/`MusicState` fields it reads and whether its phase comes from `BeatClock`, a coordinate or a free-running timer. Keep the table in `src/animations/CONTEXT.md` so it stays true afterwards.
2. For each animation that ignores the coordinates or runs a free timer, map one or two coordinates to parameters that fit what the animation is (for example `weight` to hue depth and `activity` to speed). Use `BeatClock` for beat-shaped motion with its free-running fallback. No animation gains more than two new inputs, because the aim is musical coupling, not coverage.
3. Frame-dump before and after for a sample of converted animations. The EDM replay should show visible coupling, and a steady tone should show no chatter.

This can run in parallel with Phase 6. It touches `src/animations/` only.

## Phase 8: tease as timbral departure

Read `MODEL.md` first. It is a hard rule of this repo.

### Definition

`TEASE` is open while **the established tune continues** and **something departs from it**.

- **Established tune (precondition).** `beatConfidence >= TEASE_GROOVE_CONF` has held for at least `TEASE_GROOVE_MIN_MS` (about 8 bars at the current tempo, floored at 6 s). Tempo spread is low, and no buildup, drop payoff or breakdown episode is open. A buildup riser is a departure too, but it already has its own name. Suppressing it here keeps the two apart.
- **Departure A, timbral.** Compare the short-term (about 0.75 s) 8-band log sub-band *share* profile with a long-term baseline (about 12 s EMA) that freezes while a departure is open, so the odd element is not absorbed into the baseline. Distance is L1 over band shares, which is gain-invariant by construction. It opens above `TEASE_DEPART_ENTER` and closes below `TEASE_DEPART_EXIT`, both fixed constants calibrated on the replays. Deliberately not a threshold relative to the distance's own history, because that is Phase 6's class (b).
- **Departure B, cut-outs.** At least 2 level drop-outs (below 40% of the pre-cut level, lasting 1/8 to 1 beat) within 2 bars, while beat phase keeps advancing. This overlaps the existing pre-drop chop detector (`chopSinceMs`). Resolution: the chop signal is shared. If a drop follows inside the chop arming window, the episode is the drop's setup. If the window lapses with no drop, it is recorded as a tease. For that path the tease is recognised with the arming delay, which matches how the research treats teases (recognised in retrospect).
- **End.** Departure distance below the exit threshold for at least 1 s (`END_RESOLVED`), or a buildup or drop opening (`END_REPLACED`), or `TEASE_MAX_MS` (`END_WINDOW`).
- **Removed.** `postDrop`, `hush` and `fakeOut` go. Before deleting them, list every consumer of `teaseDetected` and the tease episode (`SceneDirector`, the TEASE animation set, WASM exports, web badges, the harness), and check that each still makes sense under the new meaning.

### Evidence

The replays have no tease labels, so there is nothing to score against yet.

1. Implement the detector behind the existing `teaseDetected` output and run it over `demo.f32` and `jazz.f32` through `tools/replay.js`.
2. Emit a candidate list: timestamp, trigger (A or B), and distance or cut count, plus the near-misses just under the thresholds.
3. **Owner gate:** the owner listens at those timestamps and marks each one tease or not tease, and adds any teases the detector missed. The labels go into a `teases` key in both ground-truth files.
4. Extend `tools/evaluate_trace.py` to score teases (onset within ±1 beat) and adjust the two thresholds on that result. Stop at a threshold that holds on both tracks. Two tracks cannot justify more than that.

### Harness fixtures

- Steady groove with a new band-limited element in one upper band opens a tease and closes after the element is removed.
- The same groove with a global gain change only (±12 dB) opens no tease. This is the gain-invariance check.
- A buildup riser over the groove opens a buildup and no tease.
- Cut-outs with the beat continuing and no drop become a tease after the arming window. Cut-outs followed by a drop become a drop with no tease.
- No established groove (fewer than 8 bars of beat confidence) opens no tease whatever the departure.

### Docs

`MODEL.md` gets a new concept section for the detected tease, with its reliability marked provisional until the labels exist, and a line in §11 pointing to it. `ARCHITECTURE.md` "Musical events and musical states" is updated with the precondition-plus-departure rule. The `TODO.md` tease line is replaced with a pointer here. `BACKLOG.md` "Structural mood thresholds are unvalidated" keeps its BLOCKED status for WEIRD and DESCENT, and its TEASE sentence is rewritten.

## Build order

| Order | Phase | Depends on | Size |
|---|---|---|---|
| 1 | 0 Baseline | clean tree | minutes |
| 2 | 1 Firmware cleanup | 0 | small |
| 3 | 2 Memory health | 1 (heap numbers change after the history shrink) | medium |
| 4 | 3 Web polish | 0 | small |
| 5 | 4 Browser pass | 3 | small, produces backlog items |
| 6 | 5 Silence gate | 0 | small |
| 7 | 6 Defect sweep | 5 | large, open-ended; the triage caps it |
| 7 | 7 Catalog coordinates | 1 | medium, parallel with 6 |
| 8 | 8 Tease | 6, plus the owner's labelling gate mid-phase | large |

Phases 1, 3 and 5 are independent of each other and can go in any order or in parallel worktrees. The owner commits between phases.

## Acceptance criteria

- Every phase ends with native harness, `npm test`, `pio run -e esp32s3` and `npm run wasm` green, with the counts recorded.
- No heap literal outside `Config.h`, and `MemoryGuard` covered by harness checks for R8.
- `npm test` prints no `MODULE_TYPELESS_PACKAGE_JSON`.
- Every defect-sweep fix has a fixture that fails without it.
- Tease: all five fixtures pass, candidate lists for both tracks exist, and thresholds are set only after the owner's labels.
- `BACKLOG.md` and `TODO.md` reflect every item this plan closed, and no doc names a deleted class or file.
- Nothing in any doc claims the firmware works on hardware.

## Implementation deviations

- **Phase 0 baseline (27-09-2026).** Native harness 553/553, `npm test` 32/32, `pio run -e esp32s3` flash 26.0% (870045 B) and RAM 9.4% (30880 B), `npm run wasm` clean. The `hwStore` failure recorded in `TODO.md` no longer reproduces, so Phase 3 step 1 is void.
- **Precondition waived.** The owner said to work on top of the uncommitted drop-detector changes rather than wait for a commit. Phase diffs touching `Config.h` share the file with that work and need staging by hunk.
- **Phase 1 done (27-09-2026).** Harness 553/553, device flash 26.0% (868465 B, 1.6 KB smaller), RAM 9.4%. `AUDIO_HISTORY_CAPACITY` is 32 in `Config.h`. No frame dump was taken before the float conversion, so the before/after frame comparison did not happen. The changes are float precision only (`sinf`/`expf`/`fmodf`, `f` literals, time terms hoisted and wrapped in double once per frame). `BassShockwaveLayer` also skips pixels more than 4 widths from the ring. `CentroidRadianceLayer::ripplePhase` still grows without bound, which is left for Phase 6.
- **Phase 3 step 2 done.** `test/firmware` is CommonJS and `test/viz` is ESM, so `"type": "module"` went into `web/package.json` and `test/viz/package.json` rather than one `test/package.json`. `tools/build-dist.js` leaves `web/package.json` out of `dist/`. `npm test` 32/32 with no warning.
- **Phase 3 done (27-09-2026).** `npm test` 39/39, no warning. Step 1 was void (see Phase 0).
  - Step 3: `Spectrum` sizes its backing store from the canvas's `clientWidth`/`clientHeight` (the content box inside the CSS border, not `getBoundingClientRect`, which includes it), refreshed by a `ResizeObserver` and on a `devicePixelRatio` change. `.spectrum` in `style.css` is the only place the size is set.
  - Step 4: a plain `<` would have made a handle exactly at `radius` miss, so the radius check stays inclusive and only the best-distance comparison is strict.
  - Step 5: the "0.25 px floor" in the backlog item does not exist in `placePixels`. The sub-pixel test uses a 0.3 px pitch.
  - Step 6: `sim_main.cpp` `writeManifest` already stamped `leds0`/`leds1` from `LED_0_NUM`/`LED_1_NUM`, so only the page side was new: `web/viz/counts.js` `countMismatch`, a `countNotice` computed in `web/main.js`, and a `.count-notice` line above the bench.
  - Review-point finding: `npm run build:dist` copies `web/audio/demo.mp3` and `jazz.mp3` (4.7 MB, gitignored in `web/` as local-only) into `dist/audio/`, which is not ignored. The copy made by this build was deleted. Not fixed here; the owner decides whether the committed demo ships those tracks.
