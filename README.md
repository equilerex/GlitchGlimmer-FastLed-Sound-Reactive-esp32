# GlitchGlimmer

Sound-reactive LED firmware for an ESP32. An I2S microphone feeds an FFT, the
spectrum drives a mood classifier, and the mood picks a scene. Each scene is a
base animation plus reactive layers that fade in over it. Two LED strips run
independently, and a small TFT shows the current scene, mood and levels.

## Status

Both build environments are green and the host harness passes 101 checks. The
firmware **has not been run on hardware**. Everything below the build is
verified by simulation and by reading the code, not by watching a strip.

Two known gaps are worth stating up front rather than discovering later:

- The mood classifier's thresholds are written against a 0..1 energy scale, but
  on device `AudioFeatures::energy` is a raw sum of 255 FFT magnitudes, in the
  hundreds to thousands. `CALM` is therefore unreachable on hardware. The host
  harness scripted its energy in the 0..1 range, so it does not show this.
  `_architecture/BACKLOG.md` carries it.
- Audio tuning macros in `src/config/Config.h` (`BEAT_THRESHOLD`,
  `MIN_BEAT_INTERVAL` and others) have no call sites. The values that actually
  run are hardcoded elsewhere, and `MIN_BEAT_INTERVAL` disagrees with its
  hardcoded counterpart.

## Hardware

| Part | Wiring |
|---|---|
| ESP32 board | TTGO T1 (TTGO T-Display) |
| TFT | ST7789V 240x135, SPI on MOSI 19, SCLK 18, CS 5, DC 16, RST 23, backlight 4 |
| I2S microphone | INMP441, WS 26, SCK 27, SD 32 |
| LED strip 0 | WS2812B on GPIO 25, 100 pixels |
| LED strip 1 | WS2812B on GPIO 33, 10 pixels |
| Encoder | A 39, B 38, button 17 |
| Buttons | 0 and 35 |

Pins and counts live in `src/config/Config.h`. The display is configured in
`include/tft_setup.h`.

## Build

PlatformIO is the build system. Neither command below needs a board attached,
since no environment sets `upload_port` and only `-t upload` opens a port.

```
pio run                      # the firmware, via default_envs
pio run -e ttgo-t1           # the same thing, spelled out
pio run -e ttgo-t1 -t upload # needs the board connected
```

The device environment compiles at `-std=gnu++11`, which is what catches any
C++17 or newer construct reaching device-reachable code. Current size is RAM
8.2 percent and flash 32.6 percent.

## Tests

`src/sim_main.cpp` is a host harness. It compiles the real `LEDStripController`,
`SceneDirector`, `LayerManager`, `SceneRegistry` and every layer against a stub
Arduino core in `sim/stubs/`, then drives them over a clock the harness owns and
asserts on the result. It needs a host C++ compiler on `PATH` and nothing else.

On Linux that is already true. On Windows it is the one thing that is not
installed for you, since PlatformIO ships a cross-compiler for the device
environment but not a host one. Any 64-bit mingw-w64 `g++` works, from MSYS2 or
from `winget install BrechtSanders.WinLibs.POSIX.UCRT`. Put its `bin` on `PATH`
and the command below works unchanged. Leave it there for the Visualisation
steps as well: the binary needs the same DLLs to run that it needed to link,
and without them it exits with no output rather than an error.

```
pio run -e native -t exec
```

101 checks cover the scene and layer lifecycle over 3000 frames, a sweep of all
eight catalog animations at two strip lengths, every reachable layer factory,
device-scale audio with a populated spectrum and waveform, and a soak of the
float phase accumulators for 20000 frames each.

One thing the harness cannot see: `sim/stubs/Arduino.h` aliases `String` to
`std::string`, whose small-string optimisation hides the per-frame churn the
device pays. Allocation counts from the harness exclude it.

## Visualisation

`web/` has two views over one canvas, and neither is a JavaScript rewrite. Both
run the firmware's own code, so what you see cannot drift from what the device
would do.

| View | Driven by | Needs |
|---|---|---|
| Recording | Frames the harness writes with `--dump-frames` | `web/data/`, generated |
| Live microphone | `src/` compiled to WebAssembly and stepped once per frame | `web/live/glitchglimmer.wasm`, generated, plus a microphone |

The recording view replays a fixed audio timeline and runs no FFT. The live view
hands the microphone's samples to `AudioProcessor` unchanged, so the FFT, the
feature extraction, the beat detector and the mood classifier are all the
firmware's.

![The frame player showing both strips mid-scene](docs/preview.png)

### Run it

Node is the only requirement, and there is nothing to install.

```
npm start
```

Then open `http://127.0.0.1:8000`. A specific moment can be linked directly with
`?scenario=device&frame=500&paused=1`. Pass a port with `npm start -- 8080`.

### Recording view

The frames are generated rather than committed, so the harness has to write them
first.

```
npm run frames -- --build    # pio run -e native, then --dump-frames web/data
```

### Live view

The Live microphone button needs the WebAssembly module. Sourcing `emsdk_env.sh`
is the one part a script cannot do, so run this from a shell where Emscripten is
on `PATH`.

```
source /path/to/emsdk/emsdk_env.sh
npm run wasm -- --watch      # rebuild on save
```

Leave that running while editing an animation. The page polls
`web/live/build.json` and reloads itself when a build lands, so the loop is edit,
save, look at the page. Until the script has run once the module is missing and
the live view 404s.

### The server

`npm start` runs `tools/serve-web.js`, a static server for `web/` with no
dependencies, which is why it works on a bare clone with nothing installed but
Node. It is not interchangeable with a plain file server, for two reasons. The
module has to be served as `application/wasm` to be stream-compiled, and nothing
under `web/` may be cached, because `build-wasm.sh` replaces the module and the
recordings under paths that are already in the browser's cache. A cached copy of
the module is indistinguishable from a rebuild that did not take.

`python -m http.server 8000 --directory web` also works, and will serve you a
stale module after a rebuild, because it answers conditional requests.

`.github/workflows/pages.yml` is written to build them on every push to `main`
and publish `web/`, but it cannot do that yet: Pages is not switched on in this
repository's settings, so `actions/configure-pages` fails and the workflow shows
red on every push. Turning it on (Settings, Pages, Source, GitHub Actions) is
the only thing standing between that workflow and a published copy of the page.
Until then the local steps above are the way to look at it.

## Layout

| Path | Contents |
|---|---|
| `src/` | Firmware. `main.ino` holds `setup()` and `loop()`, `sim_main.cpp` is the host harness |
| `src/audio/` | I2S capture, FFT, feature extraction, history rings |
| `src/scenes/` | Scene registry and director, mood history, layer manager and pool |
| `src/animations/` | The eight catalog animations and the compositing layers |
| `src/display/` | TFT layout, widgets and themes |
| `include/` | `tft_setup.h`, the TFT_eSPI display configuration |
| `sim/stubs/` | Arduino core stubs used only by the host build |
| `tools/` | `build-wasm.sh` for the browser module, `serve-web.js` and `dump-frames.js` behind `npm run` |
| `web/` | The two views, `web/data/` and `web/live/` both generated |
| `AGENTS.md` | Stack rules and conventions for this repo, for contributors and agents alike |
| `_architecture/` | Working notes. `TODO.md` is the live set, `BACKLOG.md` is unscheduled work, `ARCHITECTURE.md` is why the repo is shaped this way, `plans/` holds the audit and the fix plan |

## Where to start reading

`_architecture/ARCHITECTURE.md` is the shortest route into why the code is shaped
the way it is, and it carries the reasoning behind the parts that look like
tuning constants. `_architecture/TODO.md` is where the work stands now.
`_architecture/BACKLOG.md` lists what is known broken or unmeasured. The two
files in `_architecture/plans/` are the defect audit and the plan that fixed it.

The scene pipeline is worth reading in this order: `src/audio/AudioProcessor.cpp`
produces `AudioFeatures`, `src/scenes/MoodHistory.h` classifies a mood from it,
`src/scenes/SceneRegistry.cpp` maps mood to a scene, and
`src/scenes/LayerManager.cpp` composites the result.
