# web-frame-player

Session: 2026-09-16. Status: built and verified, not yet published.

## Context

The fix pass left the firmware building and the harness green, and left the same
question open it started with. Nothing has run on hardware, the animations'
visual output is unconfirmed, and reading nine animation files to reason about
whether the output looks right is slow and unreliable.

The prior plan deferred this work in one line: a WASM build of the same
`animations/*.cpp` and `scenes/*.cpp` the native environment already compiles,
rendered to a canvas and served from GitHub Pages. The intent behind that line
was the load-bearing part. Whatever gets built has to replay the real firmware
rather than a reimplementation, or it proves nothing about the firmware.

The intended outcome is a page that shows what the strips actually do, built
from code the repository already has, with no new toolchain to install.

## Approach

The WASM route was tried first and abandoned. It needed `emsdk`, whose install
is a network fetch, and that was refused in this environment. Rather than fight
it, the frame dump gets the same guarantee a different way.

`src/sim_main.cpp` already compiles the real `LEDStripController`,
`SceneDirector`, `LayerManager`, `SceneRegistry` and every animation against the
stub Arduino core, and drives them over a clock the harness owns. A new
`--dump-frames <dir>` flag runs that same code and writes each frame's pixels to
a binary file, plus a `manifest.json` naming the scenarios and their scene
changes. The page replays those bytes into a canvas.

This is stronger than a WASM build on one axis and weaker on another. Stronger
because the recorded pixels are the firmware's own output with no second
compilation target to keep in sync, so the recording cannot drift from `src/`.
Weaker because the audio timeline is fixed rather than live, so it is a
recording and not an interactive simulation. Nothing here runs the FFT, the I2S
driver or the display.

The frame format is defined on the C++ side and depends on `CRGB` being three
bytes, so `sim_main.cpp` carries a `static_assert(sizeof(CRGB) == 3)` to keep the
JavaScript reader honest if that ever stops being true.

## Scenarios

Two, because one audio scale cannot exercise both ends of the code.

`device` runs the five phases the harness already uses for device-scale work:
silence, quiet, bass-heavy, mid-forward and bright, at the magnitudes
`AudioProcessor` produces on hardware, with `spectrum[]` and `waveform`
populated. This is what the strips see on device.

`moods` runs four moods in rotation on the 0..1 energy scale the classifier's
thresholds were written against. The device scenario cannot reach `CALM` or
`FLOATY` for the reason recorded in `BACKLOG.md`, and this scenario gives the
director more than one scene to choose between.

Both are 1200 frames at 30 fps, which is 40 seconds.

## Build order

1. `--dump-frames` in `sim_main.cpp`, writing per-frame pixels and a manifest.
2. `web/`, as three files with no dependencies: `index.html`, `style.css`,
   `app.js`.
3. Verify, then wire CI and Pages.

## Verification

The page was checked against a headless browser rather than by eye alone, after
headless screenshots came back with a blank canvas three times.

That turned out to be the screenshot mechanism, not the page. `--screenshot` in
headless Edge does not capture canvas content. A `getImageData` readback inside
the page returned 38,724 lit pixels with a peak channel sum of 765, which is
pure white, so the canvas was drawing correctly the whole time. The check that
settled it was `--dump-dom` plus `canvas.toDataURL()`, decoded to a PNG, which
showed both strips rendering as expected.

The DOM dump also confirms the parts that are not pixels: manifest load,
scenario selection, the `?scenario=device&frame=520&paused=1` deep link, the
binary search that picks the active scene event, scrub position at 43 percent,
time formatting at 17.3s, and the play/pause toggle.

The harness itself is unchanged in behaviour. 101 of 101 checks still pass.

## Implementation deviations

The original intent was a WASM build, per the deferral in
`2026-09-16-verification-harness-and-what-it-found.md`. That is recorded here
rather than in the live docs because the approach changed: the frame dump
replaces it, and nothing in the repository should still be planning toward
`emsdk`.

The `probe()` function added to `web/app.js` to settle the blank-canvas question
was removed once it had answered. It was scaffolding for the investigation, not
part of the player.
