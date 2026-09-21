# AGENTS.md — GlitchGlimmer

Stack rules and project conventions for this repo. Behaviour rules live in the global `AGENTS.md`, and the shape of the code with the reasoning behind it lives in `_architecture/ARCHITECTURE.md`. Read that before changing the audio pipeline, the compositor or the brightness curves, and do not restate it here.

## Stack

ESP32 firmware built with PlatformIO, board `ttgo-t1`. FastLED drives two LED strips on pins 25/33, TFT_eSPI drives the panel on 18/19/5/16/23, and an INMP441 I2S microphone sits on 26/27/32.

`src/` is also the audio analysis and the animation library, and three entry points drive the same code.

| Entry point | Command | Runs on |
|---|---|---|
| `src/main.ino` | `pio run` | the board |
| `src/sim_main.cpp` | `npm run frames`, `pio run -e native -t exec` | the host, as frame dumper and harness |
| `src/wasm_main.cpp` | `npm run wasm` | the browser |

## Rules the build depends on

- **The device environment compiles at `-std=gnu++11` and nothing else does.** It is the only build that catches a newer construct reaching device-reachable code, so code that compiles under `[env:native]` has not been checked. Run `pio run` before calling device-reachable work done.
- **Nothing in the repo may point outside it.** All four libraries `src/` includes are named in `lib_deps`. Do not reintroduce `lib_extra_dirs`, or any absolute path into a global Arduino libraries directory, which is how three of them used to arrive and why a fresh clone used to find only FastLED.
- **TFT_eSPI's panel configuration lives in `include/tft_setup.h`, not in the library.** `TFT_eSPI.h` checks `__has_include(<tft_setup.h>)` before it reads its own `User_Setup_Select.h`, so the repo's copy wins. Editing the library's version instead makes the build depend on one machine again.

## Running it

The page needs a served origin, not `file://`. Set up the project-owned host
toolchains once; the setup command installs native WinLibs through WinGet on
Windows and the pinned Emscripten SDK into ignored `.tools/`:

```
npm run setup
```

```
npm start                        # serve web/ on 127.0.0.1:8000
npm start -- 8080                # or on another port
```

The recording view needs the harness to have written `web/data/`, which is generated rather than committed.

```
npm run frames -- --build        # build the harness, then record into web/data/
npm run native                   # build and execute the native harness
```

The live view needs the WebAssembly module. The repository pins Emscripten and the
Node launcher activates it in the child process, so no global `PATH` setup is
required. The first run downloads the SDK into ignored `.tools/emsdk/`:

```
npm run setup:wasm
npm run wasm -- --watch          # rebuild on save, for the edit-look loop
```

Set `GG_WASM_JOBS=2` if the compiler host cannot handle the default parallel
compile count.

`EMSDK` may point at an existing installation instead. A machine-local path such
as `D:\emsdk` is only a fallback; it is not part of the repository contract.

The native harness is a MinGW executable. `npm run setup` provisions the
toolchain and `npm run native` supplies its `bin` directory to PlatformIO, so
Windows can locate the matching runtime DLLs, including `libstdc++-6.dll`.
Direct `pio run` is an advanced command; use the npm command for a fresh clone.

If using Git Bash, `npm run native` also prevents Git's `/mingw64/bin` runtime
from winning over the selected WinLibs runtime. Do not repair Git's global PATH.

## Do not

- **Do not describe the firmware as working.** Nothing has run on hardware. The harness being green and both environments building are statements about the host and the compiler, not about the device.
- **Do not add a `LICENSE` file or a `license` field.** The repo has neither, which leaves it all-rights-reserved by default. That is the owner's call rather than an oversight, and it is logged in `BACKLOG.md`.
- **Do not tune against the synthetic signal.** The page's demo generator is a fake input whose gain is set to match the microphone's range, so it agrees about scale and about nothing else. Validate against a `.f32` recorded from the real microphone, which the live page's `Record` button writes.

## Written memory

This repo keeps its own trail, and the `jookoi-paper-trail` skill owns its formats. `_architecture/TODO.md` is the live working set, `BACKLOG.md` holds unscoped items, `ARCHITECTURE.md` holds why the repo is shaped this way, `VISUALIZER_UI.md` holds the browser visualizer and UI architecture, `plans/` holds the audit and its fix plan, and `archive/` holds flushed history. A change that makes any of them wrong is not finished until that file is fixed.
