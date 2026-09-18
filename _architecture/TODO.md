# TODO
<!-- Live working set. `jookoi-paper-trail flush` archives it and resets it. See AGENTS.md. -->

## Context

ESP32 sound-reactive LED firmware. PlatformIO, board `ttgo-t1`, FastLED on pins 25/33, TFT_eSPI on 18/19/5/16/23, INMP441 I2S mic on 26/27/32. `src/` is also the audio analysis and the animation library, and three entry points drive it: `src/main.ino` on the board, `src/sim_main.cpp` as the host harness, `src/wasm_main.cpp` in the browser.

`ARCHITECTURE.md` holds the shape and the load-bearing decisions, and the previous Context is in `archive/2026-09.md`. Read `ARCHITECTURE.md` before touching the audio feature model, the compositor or the brightness curves, because most of what looks like a tuning constant in those places is a rule with a reason behind it.

Where this stands. Both environments build, CI runs both, and the harness is green. Nothing has run on hardware, which is the headline open item, so do not describe the firmware as working. `web/` has two views over one canvas and `npm start` serves them.

Two things a cold session would otherwise re-derive. The only real microphone capture is `docs/glitchglimmer-2026-09-17T20-27-18-691Z.f32` and it is gitignored, 2.3 seconds long, which is shorter than the windows the measurements now use. And the harness's allocation counts exclude `String`, because `sim/stubs/Arduino.h` aliases it to `std::string` and small-string optimisation hides the per-frame churn the device would pay.

Four open questions are the owner's call rather than defects to fix, and are logged in `BACKLOG.md`.

## Checklist

- [x] Give the browser visualiser a run command and write the conventions around it: `package.json` carries `npm start` over `tools/serve-web.js`, a static server with no dependencies so it works on a bare clone with nothing but Node, and `npm run frames` over `tools/dump-frames.js`, which exists because the harness binary is `program.exe` on Windows and `pio run -t exec` rejects program arguments as stray options. The server is not interchangeable with a plain file server: the module needs `application/wasm` to stream-compile, and nothing under `web/` may be cached while `build-wasm.sh` replaces the module and the recordings under paths already in the browser's cache. `AGENTS.md` is new and holds this repo's stack rules and its three standing prohibitions (`package.json`, `tools/serve-web.js`, `tools/dump-frames.js`, `AGENTS.md`, `README.md`, `.gitignore`)
- [x] Write `ARCHITECTURE.md` and flush the Context it replaced, rather than leaving the durable reasoning inside a working file that gets wiped (`_architecture/ARCHITECTURE.md`, `_architecture/archive/2026-09.md`)
- [ ] Sweep the pipeline for the rest of the defect class the noise floor belonged to: an absolute constant that is really a claim about one input's gain, a reference that is a statistic of the value it is used to judge, a per-block coefficient where a rate per second is meant, a value pinned at 0 or 1 by construction, and state a `resetTracking`-style path misses. Three read-only investigators over `src/audio`, the render path and the stateful reset paths, findings to be verified before any of them is fixed
- [ ] Re-verify the feature scale against the real microphone rather than the synthetic signal, using `docs/glitchglimmer-2026-09-17T20-27-18-691Z.f32`. One 2.3 s capture is all there is, and it is shorter than the windows the measurements now use, so it can show that a value moves but not that it has settled. Replayed through it, `level` reads 0.04 to 0.83 at 2.3 percent churn and is still climbing when the file ends, `mid` reads 0.03 to 0.70 against bass 0.001 to 0.049, and `dynamics` reads 0.00 to 0.26 at 1.5 percent churn. That capture is tonal, so it exercises the noise floor's no-rise path and cannot exercise the rise, which is why the flatness gate is held by a synthetic fixture instead. The tempo's behaviour on live music is still unmeasured, and so is the bass condition on a source with little energy under 200 Hz
- [ ] Enable Pages once in the repository settings, Source set to GitHub Actions, for `.github/workflows/pages.yml` to publish
- [ ] Flash and confirm on hardware: strips animate at all, no crash under scene changes, stable heap, screen free of flicker, mic responsive
