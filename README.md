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

`web/` is a player for frame recordings the harness produces. It is not a
JavaScript rewrite. The harness runs the real firmware code and writes the
resulting pixels, so the recording and the firmware cannot drift apart.

![The frame player showing both strips mid-scene](docs/preview.png)

```
pio run -e native
.pio/build/native/program --dump-frames web/data    # program.exe on Windows
python -m http.server 8000 --directory web
```

Then open `http://127.0.0.1:8000`. A specific moment can be linked directly
with `?scenario=device&frame=500&paused=1`.

Recordings are generated rather than committed, which is why the steps above
come before the page works.

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
| `web/` | The frame player |
| `_architecture/` | Working notes. `TODO.md` is the live set, `BACKLOG.md` is unscheduled work, `plans/` holds the audit and the fix plan |

## Where to start reading

`_architecture/TODO.md` has the current measured state and is the shortest route
into how the code got here. `_architecture/BACKLOG.md` lists what is known broken
or unmeasured. The two files in `_architecture/plans/` are the defect audit and
the plan that fixed it.

The scene pipeline is worth reading in this order: `src/audio/AudioProcessor.cpp`
produces `AudioFeatures`, `src/scenes/MoodHistory.h` classifies a mood from it,
`src/scenes/SceneRegistry.cpp` maps mood to a scene, and
`src/scenes/LayerManager.cpp` composites the result.
