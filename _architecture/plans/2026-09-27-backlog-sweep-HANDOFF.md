# backlog-sweep — handoff

Session: 2026-09-28. Status: Phases 0, 1 and 3 done. Phases 2, 4, 5, 6, 7 and 8 not started.

For a session picking this up cold. The design, requirements and fixtures are in `2026-09-27-backlog-sweep.md`, and this file does not repeat them. It covers where the tree is, what order to work in, and what the finished phases taught.

## Read before starting

1. `2026-09-27-backlog-sweep.md`: "Global constraints", then the phase you are starting, then "Implementation deviations". The deviations section holds the baseline counts and every place the plan's facts turned out wrong.
2. `_architecture/ARCHITECTURE.md` before Phase 2, 5 or 6 (audio pipeline, compositor, brightness curves).
3. `docs/music-research/MODEL.md` before Phase 8. Repo rule.
4. `web/viz/CONTEXT.md` before Phase 4.

## State of the tree

Nothing from this plan is committed. The working tree mixes three sets of changes:

- the owner's drop-detector work (`src/audio/*`, `src/config/Config.h`, `web/live.js`, the `tools/*.py` scripts), which predates the plan. The owner waived the clean-tree precondition, so later phases build on top of it.
- Phase 1 firmware cleanup (`MainController`, deleted `LayerPool.h` and `AlienSquirtTrailLayer.h`, widget removals, per-pixel float math, `AUDIO_HISTORY_CAPACITY = 32`).
- Phase 3 web polish (`web/render.js`, `web/viz/path.js`, `web/viz/counts.js`, `web/main.js`, `web/index.html`, `web/style.css`, `web/package.json`, `test/viz/*`, `tools/build-dist.js`), plus the rebuilt `dist/`.

`Config.h` holds hunks from both the drop-detector work and Phase 1, so staging it needs `git add -p`. `New Text Document.txt` is staged at the repo root and is not part of this plan.

Ask the owner whether they have committed before starting a phase. Each phase should begin from a tree whose earlier phases are committed, so its diff reviews on its own.

## Current numbers

| Check | Command | Last result |
|---|---|---|
| Native harness | `npm run native` | 553/553 (after Phase 1) |
| Device build | `pio run -e esp32s3` | flash 26.0% (868465 B), RAM 9.4% (after Phase 1) |
| Web tests | `npm test` | 39/39, no `MODULE_TYPELESS_PACKAGE_JSON` (after Phase 3) |
| WASM | `npm run wasm` | clean (Phase 0) |

The device environment is `esp32s3` (board `esp32-s3-devkitc-1`). `AGENTS.md` still names board `ttgo-t1`, which is stale. `npm run native` is the harness entry point on Windows, because direct `pio run -e native` can pick up Git's MinGW runtime.

## Order

From the plan's build order, with what is left:

1. **Phase 2, memory health.** Depends on Phase 1, which is done. A new `src/core/MemoryGuard.h` state machine plus harness checks for R8. Device build required.
2. **Phase 5, silence gate.** Independent, small. Start with the harness fixture. The arithmetic says the reported bug cannot happen, so the likely cause is on the display side.
3. **Phase 4, browser pass.** Only observes and turns what it finds into backlog entries. It also verifies Phase 3's spectrum sizing and count notice in a real browser, which has not been done.
4. **Phase 6, defect sweep** and **Phase 7, catalog coordinates**, which can run in parallel. Phase 6 is open-ended, and its triage file caps it.
5. **Phase 8, tease.** Last. It stops at the owner's labelling gate halfway through: emit the candidate list, then wait.

## Owner gates

Stop and ask at these points, and do not tune around them:

- Phase 8 step 3: the owner labels tease candidates by ear.
- Any constant that encodes microphone gain waits for the real 60 to 120 s capture. Log it as blocked rather than tuning it on `demo.f32`/`jazz.f32`.
- The meaning of `level` (Phase 5 records evidence only).
- `dist/audio/`: `npm run build:dist` copies the gitignored `web/audio/demo.mp3` and `jazz.mp3` (4.7 MB) into an unignored `dist/audio/`. The copy from the Phase 3 build was deleted. Ask whether the published demo should ship those tracks. If not, exclude them in `tools/build-dist.js`. Delete `dist/audio/` again after any `build:dist` until that is decided.

## Traps already found

- **Plan facts can be stale.** Phase 0 found the `hwStore` failure gone. Phase 3 found no "0.25 px floor" in `placePixels`, and that the manifest was already stamped with LED counts. Check each "Verified state" line against the code before building on it.
- **Phase 1 has no frame comparison.** No frame dump was taken before the float-math change. If Phase 4 or 7 sees a visual difference in a layer that Phase 1 touched, suspect that change first.
- **`CentroidRadianceLayer::ripplePhase` grows without bound.** Left for Phase 6 as a class (d)/(e) candidate.
- **`TODO.md` is partly stale.** Its "Emscripten not on PATH" note is obsolete because the SDK is pinned in `.tools/emsdk`, and its `hwStore` failure no longer reproduces. Phase 4 rewrites those lines.
- **No commits.** The owner commits between phases. End each phase at its review point with the counts written into the plan's deviations section.

## Record-keeping

Invoke the `jookoi-paper-trail` skill before editing `TODO.md`, `BACKLOG.md`, `ARCHITECTURE.md`, a `CONTEXT.md` or the plan. The repo still uses the legacy `TODO.md`/`BACKLOG.md` layout, and closed backlog items get `Status: DROPPED. Done <date>, Phase N of plans/2026-09-27-backlog-sweep.md.` Update the plan's `Status:` line and add a deviations entry at the end of each phase.
