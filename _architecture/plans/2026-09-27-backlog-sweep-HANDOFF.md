# backlog-sweep — handoff

Session: 2026-09-28. Status: Phases 0, 1, 2, 3 and 5 done. Phases 4, 6, 7 and 8 not started.

For a session picking this up cold. The design, requirements and fixtures are in `2026-09-27-backlog-sweep.md`, and this file does not repeat them. It covers where the tree is, what order to work in, and what the finished phases taught.

## Read before starting

1. `2026-09-27-backlog-sweep.md`: "Global constraints", then the phase you are starting, then "Implementation deviations". The deviations section holds the baseline counts and every place the plan's facts turned out wrong.
2. `_architecture/ARCHITECTURE.md` before Phase 2, 5 or 6 (audio pipeline, compositor, brightness curves).
3. `docs/music-research/MODEL.md` before Phase 8. Repo rule.
4. `web/viz/CONTEXT.md` before Phase 4.

## State of the tree

`081e1fd` ("autopilot cleanup") already contains the drop-detector work, Phase 1, Phase 3, the first copy of this handoff, and `dist/audio/demo.mp3` plus `jazz.mp3`. Phases 1 and 3 are not a separate uncommitted diff anymore. They cannot be committed on their own without rewriting that commit.

Uncommitted, and already separate from `081e1fd`:

- Phase 2 memory guard (`src/core/MemoryGuard.h`, which is untracked, plus `main.ino`, `MainController`, `LEDStripController`, `DisplayManager`, and the heap thresholds in `Config.h`). A commit that takes `main.ino` and misses `MemoryGuard.h` does not build.
- Phase 5 silence-gate fixture and the corrected panel strings (`src/sim_main.cpp`, `web/live.js`, `web/state.js`, `web/index.html`).
- The owner decision that the published demo does not ship the two tracks: `dist/audio/demo.mp3` and `jazz.mp3` deleted, and `tools/build-dist.js` excludes them.
- The doc updates from this session.

`Config.h` in the working tree is only the Phase 2 thresholds on top of `081e1fd`. It does not need `git add -p`.

`New Text Document.txt` is the owner's saved strip shape, a localStorage copy kept as the base shape for the web. Do not delete, rename, or rewrite it.

The owner commits. Phase 4 should start after Phase 2 and Phase 5 are committed, so its diff is not mixed with theirs.

## Current numbers

| Check | Command | Last result |
|---|---|---|
| Native harness | `npm run native` | 571/571 (after Phase 5) |
| Device build | `pio run -e esp32s3` | flash 26.0% (869273 B), RAM 9.4% (30888 B) (after Phase 2) |
| Web tests | `npm test` | 39/39, no `MODULE_TYPELESS_PACKAGE_JSON` (after Phase 3) |
| WASM | `npm run wasm` | clean (Phase 0) |

The device environment is `esp32s3` (board `esp32-s3-devkitc-1`). `AGENTS.md` still says `ttgo-t1`. That was noted and left unchanged on purpose. `npm run native` is the harness entry point on Windows, because direct `pio run -e native` can pick up Git's MinGW runtime.

## Order

From the plan's build order, with what is left:

1. **Phase 4, browser pass.** Only observes and turns what it finds into backlog entries. It also verifies Phase 3's spectrum sizing and count notice in a real browser, which has not been done. The silence-gate threshold strings were corrected in Phase 5. Confirm the chip and the event log against a real signal while you are there.
2. **Phase 6, defect sweep** and **Phase 7, catalog coordinates**, which can run in parallel. Phase 6 is open-ended, and its triage file caps it.
3. **Phase 8, tease.** Last. It stops at the owner's labelling gate halfway through: emit the candidate list, then wait.

## Owner gates

Stop and ask at these points, and do not tune around them:

- Phase 8 step 3: the owner labels tease candidates by ear.
- Any constant that encodes microphone gain waits for the real 60 to 120 s capture. Log it as blocked rather than tuning it on `demo.f32`/`jazz.f32`.
- The meaning of `level` (Phase 5 records evidence only).
- `dist/audio/`: decided 28-09-2026. The published demo does not ship `demo.mp3` or `jazz.mp3`. `tools/build-dist.js` excludes them. Do not put them back into `dist/`.

## Traps already found

- **Plan facts can be stale.** Phase 0 found the `hwStore` failure gone. Phase 3 found no "0.25 px floor" in `placePixels`, and that the manifest was already stamped with LED counts. Phase 5 found the backlog calling the close line the open line, and found that level is not near zero inside the hangover. Check each "Verified state" line against the code before building on it.
- **Phase 1 has no frame comparison.** No frame dump was taken before the float-math change, so there is no before image. The edit was `sinf`, `expf`, `fmodf`, `f` literals, and time terms hoisted out of the per-LED loops. `BassShockwaveLayer` also skips pixels more than 4 widths from the ring. If Phase 4 or 7 sees a visual difference in a layer Phase 1 touched, suspect that change first, and suspect the skip in that one layer.
- **`CentroidRadianceLayer::ripplePhase` grows without bound.** `update` adds `0.1f` every call and nothing wraps it. The constructor is the only place it is set back to 0. `render` passes it to `sinf`, so once the value is large the ripple stops being a smooth phase. Left for Phase 6 as a class (d)/(e) candidate. Not fixed here.
- **`TODO.md` is gone.** It had three false lines, corrected and then removed when the store moved to `items.yaml` on 28-09-2026. `AGENTS.md` still says `ttgo-t1`, and that file was not edited for the board. The device environment is `esp32s3`.
- **No commits.** The owner commits between phases. End each phase at its review point with the counts written into the plan's deviations section.
- **The gate's open line is not the close line.** The backlog item said the gate opens at `noiseFloor * 1.5 + 0.0005`. That is the close line. Open is `2.5 * floor + 0.001`. The panel used to say 2.0x and 1.5x with no additives. Those strings are corrected. Read `AudioProcessor.cpp` before trusting either doc.
- **Level at the hangover is still the previous sound.** `LEVEL_ENV_RELEASE` is slower than `GATE_HANGOVER_MS`. On the Phase 5 fixture the gate flag was already 0 while level still read 0.31. That is the opener's envelope, not the 1.7x tone, and not the reported 91 percent.
- **`dist/audio/` can be tracked even when the handoff says the copy was deleted.** It was, again, until 28-09-2026. The owner decided the published demo does not ship `demo.mp3` or `jazz.mp3`, and `tools/build-dist.js` excludes them. Check `git ls-files dist/audio` before assuming they are gone.

## Record-keeping

Invoke the `jookoi-paper-trail` skill before editing `ARCHITECTURE.md`, a `CONTEXT.md` or the plan. Work items go through the script into `items.yaml`. Close one with `done` or `drop`, named as `id title`. Update the plan's `Status:` line and add a deviations entry at the end of each phase. The page over the store is `http://127.0.0.1:4173`.
