# structural-events-and-visualizer — handoff

Session: 2026-09-21. Status: designed and reviewed, no code written. Start with Tasks 1 and 2 of the lifecycle plan and nothing else.

For an implementer picking this up cold. The design and its reasoning are in the two plans named below and this file does not repeat them. It covers what to read, the decisions that are already settled, what to build first, how to check it, and what not to do.

## What this work is

The browser telemetry page is hard to read, and its Structure section (buildup, descent, drop, tease, anomaly) shows only zeros. Tracing that found that the firmware has no notion of a structural episode with a start, an end and a reason. Two plans came out of it:

- `2026-09-21-structural-event-lifecycle.md`: the firmware work. An episode state machine per structural signal in `AudioProcessor`, an event ring exported through WASM, layers and animations bound to the episodes, then a structure card in the page.
- `2026-09-21-visualizer-density-redesign.md`: the page layout. One screen without scrolling, health cells, a tuning drawer. It depends on the first plan's data and is not started.

## Read before touching anything

In this order. Stop when you can say why a drop onset and a drop window are different things.

1. `docs/music-research/MODEL.md`, concepts 8 to 11 (buildup, drop, breakdown, tease) and the "Standing rule" paragraph in `plans/2026-09-21-music-model-for-lighting-research.md` (a detector is never a definition).
2. `_architecture/ARCHITECTURE.md`, the section "Musical events and musical states". Then "The audio feature model" if you will touch thresholds.
3. `plans/2026-09-21-structural-event-lifecycle.md`. Work from its Tasks 1 and 2. Its "Event semantics" and "Episode end rules" sections are the specification.
4. `src/audio/AudioProcessor.cpp`, `updateStructure` (about line 822) and `clearStructure`. Then the structural constants in `src/config/Config.h` (`BUILDUP_*`, `DESCENT_*`, `DROP_*`, `GATE_SETTLED`) and the comments in `src/audio/AudioFeatures.h`.
5. `src/scenes/SceneDirector.h`, `maybeInjectReactiveLayer`, only for context. Do not change it until Task 4.

Do not read `_architecture/archive/`.

## Settled decisions

These were argued over several rounds. Do not reopen them without the owner.

- **The firmware decides everything.** The browser displays values and sends tuning numbers. It never detects edges, applies a merge window, computes a duration or decides an episode ended. A JavaScript reducer was designed first and rejected for this reason. `_architecture/VISUALIZER_UI.md`, Core Principles 1.
- **Buildup and descent are mutually exclusive**, since both come from one signed displacement. A drop ends an open buildup or descent, and today the firmware does not: it leaves them running until the displacement decays. Fixing that is part of Task 2.
- **A drop has three separate lifetimes:** the onset (one instant, `dropDetected`), the drop window (the payoff section that follows, as long as the music stays in it), and a layer's own attack, hold and decay envelope. Do not derive one from another. Do not add analyser state or a constant only to give a visual layer something to live for.
- **Preparation raises drop confidence and is not required.** The buildup may have been missed, the listener may have joined halfway, or the drop may be abrupt. Every onset opens a provisional window, confirmed by strong preparation or by a sustained payoff, otherwise closed as an `impact`.
- **The tests that open, confirm and end the drop window are provisional detector hypotheses.** The first implementation may use `level` against a plateau, behind a replaceable named function, and short dips must not end a window. The intended judgement is by intensity, bass weight, activity, pulse, spectral character and structural change.
- **A drop resolves an open tease.** Tease and anomaly are overlays that can overlap a buildup or descent. Anomaly is not ended by a drop.
- **The 4 s section and tease windows are starting guesses.** One second was rejected as too short. The end of an episode is stamped at its last active frame, so the wait adds latency but not duration. A buildup's start is stamped at the hold start.
- **Layer eviction is by priority class**, not by age. The classes (section layer, impact, overlay, accent) are agreed in Task 4 before any code.
- **Expose `displacement` and hold `arming` as features** so the page can show a signed bar that arms, fires and ends. Next free feature indices are 21 and 22 (16 to 20 hold buildup, descent, drop, tease, anomaly).

## Build order

Tasks 1 and 2 only. Tasks 3 to 6 and the whole density plan wait until the owner has reviewed the firmware state and event contract.

1. Task 1: harness checks in `src/sim_main.cpp` for the current buildup, descent and drop behaviour.
2. Task 2: the episode state machines, end rules, `episodeId` and the event ring in `AudioProcessor`, with the harness checks from the plan written first.

Stop after Task 2 and report. Say what the state and event shapes are, so the owner can review the contract.

## Building and checking

- `npm run native` builds and runs the harness. Do not call `pio run -e native` directly on Windows: the MinGW runtime DLLs need the WinLibs `bin` directory on `PATH`, which the npm command supplies, and the binary exits 1 with no output otherwise.
- `pio run` is the strict device build at `-std=gnu++11`. It is the only build that catches a construct newer than C++11 in device-reachable code, so run it before calling anything done. No heap allocation on the device path, so the ring is a fixed array.
- Flash was 35.4% and RAM 9.3% on `ttgo-t1` at the last record. Report the new figures.
- `npm run wasm` builds the browser module. Not needed until Task 3.
- Never run `git commit` or `git push`. The owner commits.

## Traps

- **`gateGain` defaults to `1.0f`, not `0.0f`.** The harness builds `AudioFeatures` by hand, and a zero default would make every fixture read as silent. New fields need defaults that keep hand-built fixtures valid.
- **No structural detector runs while `gateGain < GATE_SETTLED`.** The gate's own opening looks like a full-range rise. `clearStructure()` wipes the state, so the episode machinery has to end open episodes there with reason `gate`, and it has to reset with `resetTracking` on a source change.
- **`features.buildup` and `features.descent` are not 0..1 scores.** They are `level - slowLevel` (10 s follower) while active and zero otherwise, and activation needs at least 0.15 held for 1500 ms.
- **The existing tooltips in `web/live.js` around line 777 describe `buildup` and `descent` as 0..1 window scores.** That is wrong, and Task 5 corrects it.
- **Time is sample time.** Use the analyser's own timestamps, never a wall clock, and make sure repeated or backward timestamps cannot give a negative duration.
- **Do not tune against the synthetic signal.** A 55 Hz tone cannot produce a buildup. The only capture in `docs/` is 2.3 s, shorter than every window used here, so the window values cannot be tuned yet. Real tuning needs a longer `.f32` recorded with the page's `Record` button, and that stays open in `TODO.md` if none exists.
- **Do not describe the firmware as working.** Nothing has run on hardware. A green harness and two building environments are statements about the host and the compiler.
- **The plan files are not to be regenerated.** Edit the section that changed. Update `Implementation deviations` in the lifecycle plan when the build differs from it.

## Files that must be updated when the shape changes

`_architecture/VISUALIZER_UI.md` (feature indices and the ring), `src/scenes/CONTEXT.md` (when layers change), `_architecture/ARCHITECTURE.md` (only if a settled decision moves), `_architecture/TODO.md` (status after verification). Use the `jookoi-paper-trail` skill for these.

## Open questions for the owner

- The definition of `dropConfidence` and of the post-arrival confirmation test. Provisional, settle in Task 2 against the harness, then against a capture.
- Whether a drop should end a tease that began before the buildup.
- The layer priority classes, needed before Task 4.
- The density plan's health thresholds (green from 0.8, amber from 0.5) and the right-hand tab that replaces the LED panel with the tuning sliders. Both are recommendations awaiting confirmation.

## Implementation deviations

Tasks 1 to 5 were built on 2026-09-21. The deviations are in the lifecycle plan. The handoff's "next free feature indices are 21 and 22" was wrong: those are gateGain and spectralFlatness, and the episode features start at 52.
