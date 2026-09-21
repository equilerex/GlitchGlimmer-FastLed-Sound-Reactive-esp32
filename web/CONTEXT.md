# CONTEXT — web
updated: 2026-09-21

## What this is

The browser page around the renderer: layout, controls, live and recording players. The renderer
itself is `viz/`, which has its own `CONTEXT.md`. Read that before touching anything strip-related.

## Files

- `index.html`: the whole Vue template. No bundler; the app mounts on `#app`.
- `main.js`: the Vue app. Actions (`selectProfile`, `setDensity`, `applyShape`, `syncHw`), and the
  binding between the current `StripView` and the settings object.
  Everything the template calls or reads has to be defined here: `coordLabel`, `coordTooltip`, `clearEvents`, and the computed `visibleEvents` and `structureLabel`. An undefined name in a template expression throws on render and blanks the panel.
  `structureLabel` shows the structural mood or `Steady`; the loudness-ladder names no longer mean anything to the selector.
- `state.js`: the reactive state. `state.hw` starts as defaults and is overwritten from
  localStorage by `loadHw`. Defaults live here and in `viz/hwStore.js` `defaultHw`; keep them equal.
- `live.js`, `app.js`: the live WASM player and the frame-dump player. Each builds its own
  `StripView` on `#view`; `main.js` rebinds after a mode switch.
- `render.js`: error banner, spectrum, and the `LedCanvas` alias for `StripView`.
- `telemetry.js`: `?debug=1` console tracing only.
- `style.css`: layout and controls. Tokens follow `design/mockup-reference.html`.

## Layout

Two columns (stage, telemetry). The LED settings panel docks as a third column at 1101 px and up
and is open by default there; below that it is a slide-over drawer, closed by default.

## Gotchas

- `#app` is the mount element, so a `:class` on it in `index.html` is silently ignored. The
  `hw-open` class is toggled in `toggleHwDrawer` and `mounted`.
- Docking changes the stage width without a window resize. Canvases only re-measure on the
  `resize` event, so the toggle dispatches one.
- Software length and pixel count go through `live.setSoftwareStripLength`; the settings object
  holds `lengthM` and `pitchMm`, and `syncHw` derives the count. Density in the UI is
  `1000 / pitchMm`.
- A tab the automation opens is `hidden`, so the browser throttles its frame loop to about 0.6 fps and live behaviour cannot be judged from it. Headless Edge hangs on live mode waiting for the microphone prompt, even with the fake-media flags. Layout and console errors can be checked in an automated tab with `?source=demo`; dynamics need a real foreground tab.
- Browser pane and headless runs block the microphone. Use the Demo Signal button and a synthetic
  frame for visual checks, and resize with the viewport tools to test phone widths.
