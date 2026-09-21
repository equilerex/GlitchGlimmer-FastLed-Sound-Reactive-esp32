# GlitchGlimmer

Make music visible.

GlitchGlimmer is a FastLED-based sound-reactive LED project for the ESP32. It
listens to music through an I2S microphone, extracts musical features such as
energy, rhythm, tempo, and frequency balance, and turns them into animated LED
scenes.

The project also includes a browser demo. You can explore the animations and
their responses before connecting any hardware.

LIVE DEMO: [https://equilerex.github.io/GlitchGlimmer-FastLed-Sound-Reactive-esp32/](https://equilerex.github.io/GlitchGlimmer-FastLed-Sound-Reactive-esp32/)

![GlitchGlimmer browser visualizer](docs/preview.png)

## Try the visualizer

The repository contains a complete demo bundle in `dist/`, including recorded
frames and the WebAssembly version of the animation engine.

Requirements: Node.js 24 LTS or newer.

```bash
npm start
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). or [live demo](https://equilerex.github.io/GlitchGlimmer-FastLed-Sound-Reactive-esp32/)

The demo has two modes:

- **Recording** replays a prepared musical timeline through the LED scenes.
- **Live microphone** uses your microphone and runs the firmware's audio and
  animation code in WebAssembly.

The browser needs a served origin for microphone access, so open the page through
`npm start` rather than double-clicking `index.html`.

## What the project is for

The goal is to make LED animation respond to the character of music, not just
its raw volume. A beat can trigger a pulse, sustained energy can build tension,
and changes in texture or tempo can move the visualizer into a different scene.

The browser and native harness use the same C++ animation and audio-analysis
code as the firmware. That makes the visualizer useful for experimenting with
FastLED effects before putting them on a strip.

## Hardware

The current target is an ESP32-S3 DevKitC-1 with:

| Component | Connection |
|---|---|
| LED strip 0 | GPIO 4, 100 pixels |
| LED strip 1 | GPIO 33, 10 pixels |
| INMP441 microphone | WS 26, SCK 27, SD 32 |
| ST7789V TFT | MOSI 19, SCLK 18, CS 5, DC 16, RST 23 |
| Encoder | A 39, B 38, button 17 |

Check the pin definitions in [`src/config/Config.h`](src/config/Config.h) and
the display setup in [`include/tft_setup.h`](include/tft_setup.h) before wiring
different hardware.

## Build the firmware

Install PlatformIO and the project libraries, then build the device target:

```bash
pio run -e esp32s3
```

Upload to a connected board with:

```bash
pio run -e esp32s3 -t upload
```

The firmware has not been verified on physical hardware yet. A successful build
confirms compilation, not that a particular board, microphone, power supply, or
LED strip installation works.

## Develop and test

The project has three consumers of the shared code:

| Target | Purpose |
|---|---|
| `esp32s3` | Device firmware |
| `native` | Host harness and frame recorder |
| WebAssembly | Browser live visualizer |

Useful commands:

```bash
npm test                 # JavaScript and visualizer tests
npm run native           # build and run the native harness
npm run wasm             # build the browser module
npm run wasm -- --watch  # rebuild WASM while editing
```

## Refresh the committed demo

`dist/` is the artifact deployed to GitHub Pages and is intentionally committed
so a fresh clone can run the demo without rebuilding PlatformIO or Emscripten.

To regenerate it:

```bash
npm run build:dist
```

This command:

1. Builds the native harness.
2. Runs the harness to record frames.
3. Builds the pinned WebAssembly module.
4. Copies the complete browser bundle into `dist/`.

The repository hook at `.githooks/pre-commit` runs this automatically and stages
the refreshed `dist/` with your commit. Install the hooks once per clone:

```bash
npm run setup:hooks
```

The pre-push hook only checks that the committed bundle exists and is clean; it
does not rebuild or mutate a commit during push.

## GitHub Pages

`.github/workflows/pages.yml` deploys the committed `dist/` bundle. It does not
rebuild the demo on GitHub's runner.

For the first deployment, set **Settings → Pages → Build and deployment →
Source → GitHub Actions**. After that, pushes to `main` publish the current
`dist/` bundle.

## Project layout

```text
src/                 ESP32 firmware, audio analysis, scenes, and animations
sim/stubs/            Arduino and FastLED host stubs
tools/                Native, WASM, frame, server, and distribution scripts
web/                  Browser source and local development output
dist/                 Committed browser demo bundle
docs/preview.png      README screenshot
_architecture/        Design decisions and project working notes
```

For the reasoning behind the audio model, compositor, and brightness curves,
start with [`_architecture/ARCHITECTURE.md`](_architecture/ARCHITECTURE.md).
The active work list is in [`_architecture/TODO.md`](_architecture/TODO.md).
