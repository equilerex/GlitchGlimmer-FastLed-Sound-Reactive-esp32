# Structural Event Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make buildup, descent, drop, tease, and anomaly complete structural events with a start, an end, a duration and an end reason, decided by the firmware. The animations and layers follow those episodes, and the page only displays them.

**Architecture:** The firmware owns every decision. `AudioProcessor` gains a small episode state machine per signal and a ring of started, ended and triggered events. `SceneDirector` and the animations consume the episode state, so a layer or an animation is attached when an episode opens and released when it ends, instead of running on a timer. The WASM layer exports state, timing and the event ring. The browser reads those values and draws them. It does no edge detection, no merge window, no duration arithmetic and no end decisions. The windows are firmware constants exposed through the existing `gg_tuning` and `gg_set_tuning` path, so the page can send a new value and the firmware decides what it means.

**Tech Stack:** ESP32 C++11 audio analyser and scene director, PlatformIO native harness, WASM exports in `src/wasm_main.cpp`, Vue bindings in `web/index.html`.

**Spec:** Existing structural detector contract in `src/audio/AudioFeatures.h` and `src/audio/AudioProcessor.cpp`; UI behavior defined below.

**Session:** 2026-09-21. Status: Tasks 1 to 5 built the same day and Task 6 (real audio) open, see Implementation deviations. An earlier draft put a lifecycle reducer in browser JavaScript. That broke the repo's firmware-authority rule (`_architecture/VISUALIZER_UI.md`, Core Principles 1) and was removed. A later request added the layer and animation coupling (Task 4). A second review corrected the musical model, then a third corrected the second: a drop has an onset event and a drop window that follows it, the window ends because the music stops behaving like a payoff and never because a visual layer needs time; preparation raises confidence but is not a prerequisite; every opening and ending rule is a provisional detector hypothesis, not the model; a drop resolves a tease; and layer eviction is by priority. The 3 s drop hold that started this came from an earlier draft of this plan, not from the research or the owner. The concepts come from `docs/music-research/MODEL.md` (concepts 8 to 11), which this plan had not read. Implementation starts with Tasks 1 and 2 only. The web layout waits until the firmware contract is stable.

## Global Constraints

- `src/` remains shared by device, native, and WASM entry points.
- Device-reachable code must compile as `gnu++11`; run `pio run` before completion claims.
- The browser holds no structural logic. Anything that decides whether an episode started, continues, ended or was replaced lives in `src/`.
- The 8D coordinate values remain independent from structural-event values.
- `dropDetected` remains a one-frame edge in `AudioFeatures` and is the drop onset. The drop window is separate state that follows it, and its length comes from the music. No fixed duration defines it, and no constant exists to give a visual layer something to live for.
- Tease and anomaly keep their current detector conditions; this plan adds lifecycle state on top of them.
- Do not tune thresholds against the synthetic signal; use a real `.f32` capture for behavioral tuning.
- No hardware-working claim is allowed; only builds, harness checks, and browser behavior may be reported.
- Ring storage and per-frame cost stay small and fixed. No heap allocation on the device path.

## Context

`BUILDUP` and `DESCENT` already clear internally when their displacement stays below threshold for the 350 ms hangover. `features.buildup` and `features.descent` are `level - slowLevel` (a 10 s follower, `BUILDUP_TAU_SEC`) while active and zero otherwise, so they are not 0..1 scores. Activation needs a displacement of at least `BUILDUP_LEVEL` (0.15) held for `BUILDUP_HOLD_MS` (1500 ms), so a 0..1 bar would sit mostly empty and show nothing about how close an episode is to starting. A drop currently leaves buildup and descent running until the displacement decays.

Layers are not tied to any of this. `SceneDirector::maybeInjectReactiveLayer` adds a `HIGHLIGHT` for 2500 ms and an `ENERGY` for 3500 ms on a drop, and an `OVERLAY` for 3000 ms on a buildup, each behind a 4 to 5 s cooldown. A buildup that lasts 20 s gets one 3 s layer, and a drop's layers can outlive the drop or the following section. Descent adds nothing. The browser derives `BUILDUP ACTIVE` from a rising edge in `web/live.js` and has no end or duration.

## Musical model this plan encodes

Read `docs/music-research/MODEL.md` (concepts 8 to 11) first. In short:

```text
buildup   = a state that unfolds, confirmable only while in progress
drop      = two related things: the onset (a point event, the arrival that releases a buildup)
            and the drop window (the payoff section that follows, for as long as the music stays in it)
impact    = an arrival that does not turn out to be followed by a payoff: an onset with no lasting window
tease     = a buildup whose expected arrival is withheld, recognised in retrospect
breakdown = a sustained stripping-back (concept 10); descent here is the closest existing detector
```

A drop is defined by its relation to what came before. The detector already requires a quiet, armed state and a broad arrival. The onset record carries a `preparation` score (0..1, from whether a section was open and how strong it was), and preparation raises `dropConfidence` without being required. The buildup may have been missed, the listener may have joined halfway, or the drop may be abrupt. So every onset opens a provisional window. Strong preparation confirms it at once. Weak or absent preparation leaves it provisional until the music after the arrival is judged: if a payoff state is sustained it is confirmed as a drop, and if not it is closed as an `impact`. Three lifetimes stay separate: the onset (one instant), the drop window (as long as the music stays in the payoff), and a layer's own attack, hold and decay envelope. None is derived from another.

## Event semantics

The firmware tracks one episode per signal. Buildup, descent and the drop window are the section group, and at most one of them is open. Tease and anomaly are overlays.

| Signal | Detector condition | Episode |
|---|---|---|
| buildup | active flag from `updateStructure` | opens on activation, ends per the rules below |
| descent | active flag from `updateStructure` | same |
| drop onset | `dropDetected` | a point event. The record carries `preparation` (0..1). It closes the open section and resolves a tease |
| drop window | opened by every onset, provisional until confirmed | a section episode that runs while the music stays in the payoff, ended by the rules below. Confirmed as a drop or closed as an `impact` |
| tease | `teaseDetected` | overlay episode. It can coexist with a buildup or a descent. An arrival resolves it, so a drop ends it |
| anomaly | held `anomaly` score | overlay; own episode, no extra window |

### Episode end rules (all in firmware)

- Buildup and descent are mutually exclusive, because both come from one signed displacement. A buildup replaces an open descent and the reverse, with end reason `replaced`.
- A drop ends an open buildup or descent immediately, with end reason `drop`, and clears the internal active flag and hold timers, so the animations reading `f.buildup` stop as well.
- Every drop onset opens a provisional drop window. High `preparation` confirms it immediately. Otherwise it is confirmed when the post-arrival state is sustained, and closed as an `impact` when it is not. The window ends, in order of precedence, when: a descent starts (`replaced`), a new buildup is confirmed (`replaced`), the gate closes (`gate`), the payoff stops holding (`faded`), or the safety bound `STRUCT_DROP_MAX_MS` is reached (`timeout`). The bound is a backstop and never the definition, and it is long (starting at 60000).
- **The end and the confirmation tests are a first detector hypothesis, not the model of a drop.** A drop section is not defined by loudness and can have internal dips while remaining the same payoff. The intended test compares the current musical character with the character at the drop: intensity, bass weight, activity and density, pulse strength, spectral character, major novelty, and a descent or new buildup. The first implementation uses the cheapest stand-in, `level` staying below `STRUCT_DROP_HOLD_FRACTION` (starting at 0.6) of the level averaged over the first seconds after onset, for the section window, so that short dips do not end the window. The plateau is measured in the window because the slow follower catches up with a sustained payoff. Each test sits behind its own named function so it can be replaced without touching the state machine, and the constants are tuned on real captures. Nothing outside the analyser may depend on which test is in use.
- A buildup or descent otherwise ends when its condition has been false for the section window (`STRUCT_SECTION_WINDOW_MS`, starting at 4000). One second is too short to judge that a riser is over, since a section can thin out for a bar. The value is a starting guess, tuned on a real capture.
- Tease ends when a drop arrives, with reason `resolved`, since the withheld arrival happened. Otherwise it ends after `STRUCT_TEASE_WINDOW_MS` (starting at 4000) without its flag, since that flag has no hold or cooldown and flickers. Tease can run alongside a buildup or a descent and is not ended by them. Anomaly is an overlay independent of all of this and ends when the firmware's own hold releases. Whether a drop should end a tease that started before the buildup is a question for the real captures.
- A buildup is only confirmable while it runs, so its episode start is stamped at the moment the hold began (`buildupHoldSince`), not at confirmation. The start record is emitted at confirmation with that earlier stamp. Descent works the same way.
- A gate below `GATE_SETTLED` ends every open episode with reason `gate`, so silence is not read as a section change.
- An end is stamped at the last active frame. Duration excludes the waiting window. While waiting, the state is `fading`, which the animations also see.

### Exposed state

Per signal the firmware provides `state` (idle, arming, active, fading; the drop window has no arming), `elapsedMs` of the open episode, `lastDurationMs`, `sinceEndMs`, `lastEndReason`, and a monotonically increasing `episodeId` that changes on every new episode. The drop window also provides `dropConfidence` and its `confirmed` flag. `dropConfidence` is 0..1, raised by how far the arrival cleared its conditions, by `preparation`, and by post-arrival evidence. Its exact definition is provisional and settled in Task 2. For the section group it also provides signed `displacement` and `arming` (0..1 progress through the hold). Events go into a fixed ring of about 32 records `{ seq, signal, kind (started|ended|triggered), atMs, durationMs, reason }`. The WASM layer exports a count, the newest `seq`, and an accessor by index. Time is the sample time the analyser already uses.

## Animation and layer coupling

The lifecycle is only useful to the show if the lights follow it.

- **Section and overlay layers are bound to an episode, not a timer.** `LayerManager` gains a way to add a layer with an owner (signal and `episodeId`) and to release everything owned by a signal. `maybeInjectReactiveLayer` attaches the layer when an episode starts and releases it when the episode ends. The release is a short fade, not a cut. A drop has three visual roles. The onset triggers one-shot impact layers that run their own attack, hold and decay envelope, chosen by the visual design and not stored in the analyser. The drop window carries sustained stronger base animation and layers once it is confirmed, bound to the episode, and its end fades those layers according to the state that follows. A provisional window drives only the one-shots.
  - buildup: a swell layer for as long as the buildup lasts, growing with `arming` and `elapsedMs` rather than a fixed 3 s.
  - drop onset: the impact layers (`HIGHLIGHT`, then `ENERGY`), triggered once, scaled by `dropConfidence`.
  - drop window: a sustained layer set for the payoff once confirmed, released when the window ends. A window closed as an `impact` never gets one.
  - descent: a fade-down or cool layer that follows the descent, which does not exist today.
  - tease and anomaly: overlay layers that come and go on their own episodes. A drop releases the tease layer.
- **The cooldown on structural layers goes away.** It exists because the drop flag fires for several frames. A drop onset and an episode start are each a single record in the ring, so `lastDrop` and `lastBuild` are no longer needed for these.
- **Eviction is by declared priority, not age.** Each layer type carries a priority class (section layer, impact, overlay, accent). When the cap is reached the lowest class goes first, and among equals the oldest. A musically important active episode is never evicted because it is oldest. The classes are agreed in Task 4 before any code.
- **Animations get episode inputs.** The animations that already read `f.buildup` (Rising Tension, Strobe Pulse, Pop Fade, `HoldSelect`) switch to `state`, `arming`, `elapsedMs` and the section's progress, so a long buildup can develop over its whole length instead of following a raw displacement. Where an animation currently uses a timer for a build or a release, it uses the episode instead.
- **Scene selection.** A section episode counts as a reason to hold or change the scene, through the structural mood the director already reads, without adding a second decision path. That mapping is checked in Task 4 and not redesigned here.

## Files and responsibilities

- `src/audio/AudioFeatures.h`: `displacement`, `arming`, and the per-signal state fields with safe defaults, so hand-built harness fixtures stay valid.
- `src/audio/AudioProcessor.h`, `src/audio/AudioProcessor.cpp`: the state machines, the end rules, the event ring, `clearStructure()` extended to reset them and to end open episodes with reason `gate`.
- `src/config/Config.h`: `STRUCT_SECTION_WINDOW_MS`, `STRUCT_TEASE_WINDOW_MS`, `STRUCT_DROP_MAX_MS`, `STRUCT_DROP_HOLD_FRACTION`.
- `src/scenes/LayerManager.h`, `src/scenes/SceneDirector.h`: owner-bound layers with fade-out release; `maybeInjectReactiveLayer` driven by episode start and end.
- Animations under `src/`: only those listed in Task 4.
- `src/wasm_main.cpp`: feature indices for the new fields, event ring accessors, four entries in the `gg_tuning` table.
- `src/sim_main.cpp`: harness checks for every rule above and for the layer coupling, and the CLI dump columns.
- `web/live.js`: read the new features and drain the event ring by `seq`. Format each record into an event-stream row. Delete the `lastSeen` edge checks and the structural `eventHold` logic. Drop and tease badge holds stay as presentation only.
- `web/state.js`, `web/index.html`, `web/style.css`: hold the values as received and draw the structure card; its position is set by `2026-09-21-visualizer-density-redesign.md`. The layer HUD already shows active layers, which lets you see them come and go.
- `_architecture/VISUALIZER_UI.md`, `src/scenes/CONTEXT.md`: document the new feature indices, the ring and the owner-bound layers.
- `_architecture/TODO.md`: record status after verification.

## Build order

Tasks 1 and 2 first, on their own. Tasks 3 to 6 start only after the firmware state and event contract is reviewed, because the web layout, the layers and the animations all depend on its shape.

### Task 1: Lock down detector end behavior in the native harness

**Files:**
- Modify: `src/sim_main.cpp` near the existing structural detector checks.

- [ ] Add a fixture that holds a buildup displacement long enough to activate it, then feeds below-threshold frames for longer than the 350 ms hangover.
- [ ] Assert that `buildup` becomes positive during the episode and returns to zero after the hangover, and the same for descent.
- [ ] Assert that `dropDetected` is true only on the triggering frame.
- [ ] Run `pio run -e native` and the native harness.

### Task 2: Episode state machine and end rules in `AudioProcessor`

**Interfaces:**
- Consumes: the active flags, `dropDetected`, `teaseDetected`, `anomaly`, `gateGain`.
- Produces: the per-signal state, timing, end reason, `episodeId`, and the event ring.

- [ ] Write harness checks first: a 3 s gap inside a buildup stays one episode; a gap longer than the window ends it with reason `window` and an end stamp at the last active frame; a drop during a buildup or descent ends it with reason `drop` and clears the active flag; a buildup and a descent replace each other with reason `replaced`; a tease that starts mid-buildup runs concurrently until the drop resolves it; tease flicker with gaps under the window is one episode; a gate close ends everything with reason `gate`; repeated or backward timestamps never give a negative duration; each new episode gets a new `episodeId`; a buildup followed by a drop gives a start record, then a drop onset with a high `preparation` score, the buildup ended with reason `drop` and a drop window started, each exactly once; an onset with no preparation still opens a provisional window, which is confirmed when the payoff is sustained and closed as an `impact` when it is not; a mid-drop dip shorter than the window does not end it; a drop window stays open through a long steady payoff well beyond any layer's length, ends with reason `faded` when the level falls and stays down, ends with `replaced` when a descent starts, and only reaches `timeout` at the safety bound; a tease open at the drop ends with reason `resolved`; a descent runs the same start, end and replaced cycle as a buildup; the event ring wraps without losing the newest records and its `seq` never goes backwards; a start stamp equals the hold start, not the confirmation time.
- [ ] Implement the state machines and the ring with fixed storage. Keep the old `buildup` and `descent` outputs unchanged for the animations until Task 4 moves them.
- [ ] Make a drop clear `buildupActive`, `descentActive` and their hold timers. This is the one change to existing detector behavior in this task.
- [ ] Add the four constants to `Config.h`.
- [ ] Run `pio run -e native`, the harness, then `pio run` (gnu++11).

### Task 3: WASM exports and the live bridge

**Files:**
- Modify: `src/wasm_main.cpp`, `web/live.js`, `web/state.js`.

- [ ] Export the new feature indices, the ring count and accessors, and add the four values to the tuning table.
- [ ] Extend `readFeatures()` and drain the ring by newest `seq`, so a page reload or a slow frame never repeats or loses a record. Reset the drained position with `gg_reset_analysis()` and source switches.
- [ ] Turn each record into an event-stream label (`BUILDUP STARTED`, `BUILDUP ENDED (drop)`, `DROP TRIGGERED`, and so on) with no comparison logic beyond the record's own fields.
- [ ] Run `npm run wasm` and confirm the module loads.

### Task 4: Bind layers and animations to episodes

**Files:**
- Modify: `src/scenes/LayerManager.h`, `src/scenes/SceneDirector.h`, the animations named below, `src/sim_main.cpp`.

- [ ] Add owner-bound layers to `LayerManager` (owner is a signal and an `episodeId`) with a release that fades over a short fixed time. A layer whose owner episode is gone is released even if the layer's own duration has not run out.
- [ ] Rework the structural branch of `maybeInjectReactiveLayer` to attach on episode start and release on episode end for buildup, drop, descent, tease and anomaly, and remove `lastDrop` and `lastBuild` for those. Keep the four layer cap and replace age-based eviction with the priority classes above.
- [ ] Add a descent layer, since none exists.
- [ ] Move the animations that read `f.buildup` (Rising Tension, Strobe Pulse, Pop Fade, `HoldSelect`) to the episode's `state`, `arming` and `elapsedMs`. List any others found by searching for `buildup` and `descent` use, and note each in the deviations section.
- [ ] Harness checks: a layer appears on episode start and is gone within its fade time after the episode ends; a drop releases the buildup layer; a drop releases the tease layer; no owner-bound layer outlives its episode; the cap is never exceeded.
- [ ] Run `pio run -e native`, the harness, then `pio run` (gnu++11), and check flash and RAM stay within the current margins (35.4% and 9.3% at last record).

### Task 5: Structure card

**Files:**
- Modify: `web/index.html`, `web/style.css`.

- [ ] One diverging bar on signed `displacement`, scaled to about ±0.4, buildup to the right and descent to the left, with threshold ticks at ±`BUILDUP_LEVEL` and ±`DESCENT_LEVEL`. `arming` fills the bar's edge while the hold accumulates. The bar changes colour when the state is `active` and dims when `fading`.
- [ ] Show elapsed time while active, last duration and end reason after an end, and a muted `idle` otherwise.
- [ ] Tease and anomaly are separate badges with their own timers, because they can overlap a section. Drop is a pulse badge.
- [ ] Coordinate bars stay bound to coordinate values only.
- [ ] Add accessible labels and check the narrow breakpoints.

### Task 6: Verify the full shared build and real-audio behavior

- [ ] Run `npm test`, `pio run -e native` plus the harness, `pio run`, and `npm run wasm`.
- [ ] Serve the page and confirm the card, the event stream and the layer HUD render each state from the ring. The synthetic tone cannot produce a buildup and must not be used to judge the detector.
- [ ] Replay a real `.f32` capture long enough to contain sections, and check that starts, ends, reasons, durations, and the layers appearing and leaving read sensibly. Tune the windows there through the tuning drawer. Leave real-audio verification open in `_architecture/TODO.md` if no such capture exists.

## Acceptance criteria

- A buildup or descent episode produces a start record, a live state and elapsed time, and an end record with a duration and a reason, all created by the firmware.
- A drop onset ends an open buildup or descent in the firmware, so the animations stop following it too, and is recorded once with its `preparation` score. Every onset opens a provisional drop window that lasts as long as the music stays in the payoff, and preparation raises confidence without being required.
- A drop resolves an open tease. Tease and anomaly overlap buildup and descent without being ended by them.
- No analyser state or constant exists only to give a visual layer a lifetime. The safety bound on the drop window is a backstop and is never the reason a window ends in the tests, and the level-based end test is replaceable and is not part of the contract.
- Layers for buildup, descent, tease and anomaly appear when their episode starts and are removed, with a fade, when it ends. Drop onset layers are one-shots with their own envelope, and the drop window's layers follow its episode. Eviction follows priority classes.
- `web/` contains no code that decides when an episode starts, continues or ends.
- The event stream never repeats or drops a record across a reload, a slow frame or a source switch.
- Source switching resets the analyser state and the drained position.
- Native harness, strict device build and WASM build pass.

## Implementation deviations

Tasks 1 and 2 built 2026-09-21. Differences from the plan:

- **The state machine is a header-only class, `src/audio/StructuralEpisodes.h`**, with types in `EpisodeTypes.h`, owned by `AudioProcessor` as `episodes`. It takes an `Inputs` struct per block, so the harness scripts detector flags directly instead of engineering audio. The real analyser is checked end to end by amplitude-schedule fixtures.
- **Buildup is latched after a drop** (`buildupLatched`, cleared when displacement falls under `BUILDUP_LEVEL`). Without it the payoff sits above the slow mean, the hold restarts and a buildup opens inside the drop and replaces the window.
- **A tease cannot open while a drop window is open or on the onset frame.** The post-drop tease trigger stays true for `TEASE_POST_DROP_MS`, so a resolved tease would reopen on the next block.
- **New event kind `EVT_CONFIRMED`** and new end reasons `END_IMPACT` and `END_RELEASED` (anomaly). The drop onset is `EVT_TRIGGERED` on `SIG_DROP`, and the window's start and end share that signal.
- **Extra constants:** `STRUCT_DROP_PLATEAU_MS` (2000), `STRUCT_DROP_CONFIRM_LEVEL` (0.5), `STRUCT_DROP_PREP_CONFIRM` (0.7), beside the four named ones.
- **Provisional definitions, unreviewed:** `preparation` is 0.6 to 1.0 for an open buildup by its length up to 6 s, 0.4 for an open descent, else 0. `dropConfidence` is 0.25 x arrival + 0.45 x preparation + 0.30 x plateau. An impact is stamped at the last frame at or above the confirm level.
- **The ring keeps its records across `clearStructure`/`resetTracking`**, but `atMs` restarts with the sample clock, so `seq` is the ordering key.
- **Tasks 3 to 5 built the same day.** Feature indices are 52 to 85, not 21 and 22, which were already gateGain and spectralFlatness. The four windows are tuning entries 11 to 14, held as runtime members on `StructuralEpisodes`. The ring drain does not reset on `gg_reset_analysis()`, because seq never goes backwards, so the end records the reset produced are still read. `web/live.js` kept the drop and tease badge holds and the buildup/descent time-since stamps, and lost only the edge checks that pushed events.
- **Layers.** New types `BUILDUP_SWELL` and `DESCENT_COOL`. Classes are accent, scene, overlay, impact, section, and a scene layer can be evicted by a section layer. One-shots are fired once per episode per strip from the strip's own manager, which also fixes the old cooldown giving the drop's layers to the first strip only. Tease uses `MOOD_ARC`, anomaly `DYNAMICS_FLICKER_STORM`, the drop window `ENERGY_SPIRAL`.
- **Animations.** `HoldSelect` does not read buildup and was left alone. The consumers were `TensionRamp` (Rising Tension, Strobe Pulse, Pop Fade) and Cosmic Chaos. `TensionRamp` keeps the old `f.buildup` path for hand-built blocks with no episode.
- **The web card** is inserted before the event stream, and the buildup and descent readouts and their tooltips in `web/live.js` were corrected to say they are displacements. Its position is left to the density plan.
- **The build has no `ttgo-t1` environment.** `pio run` builds `esp32s3`: flash 25.9%, RAM 9.4%. The 35.4% and 9.3% baseline does not match that environment.
