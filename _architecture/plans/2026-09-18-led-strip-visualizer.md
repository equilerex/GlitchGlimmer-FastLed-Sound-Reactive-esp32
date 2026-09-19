# led strip visualizer

Session: 2026-09-18. Status: built. Ten tasks, each reviewed; see `2026-09-18-led-strip-visualizer-build.md` for the task breakdown and `## Implementation deviations` below for what diverged. Committed as `db687a6`, with a follow-up pass on shape presets and the save debounce still in the working tree.

## Context

The web view exists to answer two different questions, and it currently answers neither well.

The debugging question — *is the firmware putting the right bytes in the right pixels* — is served by `LedCanvas` in `web/render.js`, which draws two rows of circles with a radial-gradient halo. It works, but it offers no pixel index, no per-pixel value readout and no scale, so "pixel 47 is wrong" is not a thing the page can tell you.

The preview question — *what will this actually look like on the wall* — is not served at all. A row of coloured circles on a dark page is not a strip. The strip type control (`state.hw.preset`) exists and changes dot spacing and radius, but a 144/m strip and a COB strip differ only in how big the dots are, which is not the difference between them.

Every other FastLED web preview fails the second question the same way: it paints hex colours onto black and adds a bloom pass. Colours painted on black read as paint. What makes a render read as *light* is photographic, not additive — gamma to linear, an exposure gain, and desaturation toward white for everything that clips past the top of the range. That is the single largest visual difference and it costs almost nothing.

The design below was validated by building an interactive mockup first. The mockup confirmed three things worth keeping: the photographic camera model, the idea that strip continuity is an emergent property rather than a per-type renderer, and that a draggable spline is the right way to pose a strip.

## The physical model

A rendered strip is the product of four independent inputs. Nothing in the renderer special-cases a strip *type*.

```
path (spline)  x  LED profile  x  surface  x  camera
```

**Profile** is a struct of real hardware measurements, in millimetres:

| field | meaning |
|---|---|
| `pitch` | centre-to-centre spacing |
| `die` | emitting area diameter |
| `sigma` | diffusion radius of the casing |
| `casing` | `smd` / `sleeve` / `cob` / `bulb` / `pip` — affects the unlit substrate drawing only |
| `burst` | starburst strength for clear epoxy |

Continuity emerges from the numbers: when `sigma` exceeds `pitch` the pixels fuse, which is what a COB phosphor does. There is no tube geometry and no separate COB shader. The initial table:

| id | name | pitch | die | sigma | casing |
|---|---|---|---|---|---|
| `ws60` | WS2812B 60/m | 16.7 | 5.0 | 2.4 | smd |
| `ws144` | WS2812B 144/m | 6.9 | 3.5 | 1.7 | smd |
| `ws30` | WS2812B 30/m | 33.3 | 5.0 | 2.4 | smd |
| `sil` | IP65 silicone | 16.7 | 5.0 | 11.0 | sleeve |
| `cob` | COB 480/m | 2.1 | 1.8 | 3.4 | cob |
| `bul` | WS2811 bullet | 50.0 | 9.0 | 5.5 | bulb |
| `fairy` | Fairy pip | 50.0 | 2.0 | 3.0 | pip |

**Camera** converts pixel bytes to screen pixels:

```
linear = (v/255)^2.2 * 2^EV
over   = clamp((max(linear) - 0.85) / 1.4, 0, 1)
out    = mix(encode(linear), 1.0, over * 0.92)
```

The `over` term is the whole trick. A pixel at full output blows out to white and loses its hue, exactly as a camera does. Without it the render is paint.

**Emission** composites in three passes from one offscreen buffer:

1. **spill** — the buffer blurred wide, `screen`-blended onto the surface. This is light landing on the wall.
2. **halo** — the same buffer unblurred, `lighter`. This is the casing glowing.
3. **core** — a `die`-sized disc per pixel, pushed toward white by `over`.

Two `drawImage` calls plus one pass of small discs. No postprocessing library, no WebGL.

## Count is fixed, length is derived

The mockup had the relationship backwards, and the real page must not.

Pixel count comes from the firmware (`_gg_leds0_count()`, `_gg_leds1_count()`, or `manifest.leds0`/`leds1` for a recording). It is not a user control. So:

```
strip length = count x pitch
```

120 pixels of 60/m strip is 2.00 m and cannot be anything else. The page therefore reports the fit rather than silently rescaling:

```
path 1.42 m  ·  strip 2.00 m  ·  0.58 m over
```

Two scale modes:

- **fit** (default) — the mm-to-pixel scale is chosen so the strip exactly fills the drawn path. Shape is honest, absolute size is not.
- **true** — the scale comes from a stage-width-in-metres control. The strip runs its real length and stops where it stops, which is what makes the over/under number mean something.

## Module split

`web/render.js` is 353 lines and this roughly triples it. Split by responsibility, one job each:

| file | holds |
|---|---|
| `web/viz/profiles.js` | the profile table and lookup |
| `web/viz/path.js` | catmull-rom spline, arc-length parameterisation, shape presets, handle hit-testing |
| `web/viz/camera.js` | gamma, exposure, clip-to-white, sensor grain |
| `web/viz/surface.js` | dark room / slat wall / bar rod backplates |
| `web/viz/StripView.js` | the stage renderer, multi-strip |
| `web/viz/BenchStrip.js` | the ruler: index ticks, luma bars, hover inspector |
| `web/render.js` | keeps `showError`, `hideError`, `Spectrum`; re-exports `LedCanvas` as `StripView` |

The public contract is unchanged: `new LedCanvas(canvas, counts)`, `.resize()`, `.paint(bytes)`, `.clear()`. `web/app.js`, `web/live.js` and `web/main.js` are not edited.

## Two strips, two paths

`counts` is `[n0, n1]` and each strip gets its own profile and its own path. The rail carries a Strip 0 / Strip 1 selector; profile and shape controls apply to whichever is selected. Defaults reproduce the current layout — strip 0 straight across at 34% height, strip 1 a shorter arc at 72%.

Shape presets move the four control handles; dragging any handle on the stage clears the preset and leaves the path freehand.

## Stage and bench

The stage is the preview half. The bench is the debug half. Both are visible by default and either can be collapsed.

The bench replaces nothing that exists and adds what is missing: a straight ruler of the same pixels, index ticks every 10, a per-pixel luma bar, and a hover readout giving index, hex and position in millimetres. Hovering a pixel on the stage highlights the same pixel on the bench.

### Nothing already on the page is removed

This is a renderer swap plus additions. Every existing graph, meter and readout survives, in both modes. Enumerated so it can be checked off rather than remembered:

**Live mode** — scene, mood now, mood of window, BPM, the level meter bar, the beat lamp, the spectrum canvas (`Spectrum` in `render.js`), the microphone/demo source buttons, the source note, *Copy snapshot*, the record toggle, and the telemetry rows `live.js` builds into `#live-state` including their tuning sliders.

**Recording mode** — scene, mood, time, play/pause, the scrub range, the scenario buttons and the scenario note.

**Both** — the hardware sidebar (extended, not replaced) and the `?debug=1` console trace in `web/telemetry.js`, whose `Trace` gains a draw-time field and loses nothing.

If a rail control gains a better home during the rebuild it moves; it does not disappear. The bench and stage are added around this furniture, not in place of it.

## State

`state.hw` grows. The three existing multipliers keep their meaning and become overrides on top of the profile; `preset` migrates into `strips[i].profile` on first load.

```js
hw: {
  view: 'both',          // 'stage' | 'bench' | 'both'
  surface: 'room',
  scale: 'fit',          // 'fit' | 'true'
  stageWidthM: 3,
  ev: 0, spill: 1.0, grain: 0.14,
  pixelSize: 1.0, glowSize: 1.0, intensity: 1.0,
  strips: [
    { profile: 'ws60', shape: 'line', pts: [[0.08,0.34], ... ] },
    { profile: 'ws60', shape: 'arc',  pts: [[0.29,0.72], ... ] }
  ]
}
```

The whole `hw` object persists to `localStorage` behind try/catch, because a posed path that is lost on reload is worse than no posing.

Defaults ship photographic: grain and spill on, `ev` at 0. The debug look is one control away, not the starting point.

## Performance

`web/live.js` repaints on every WASM frame, so the renderer is in the audio-rate path and a slow one is visible as stutter in the thing being debugged.

Budget: 8 ms of draw time for both strips. The first implementation uses per-pixel radial gradients, which is roughly 300 gradient allocations per frame at the default counts. If measurement puts it over budget, the fallback is a single white glow sprite rendered once and `drawImage`-ed per pixel under `lighter`, with the tint applied as a second pass.

The mockup rebuilt the spline and the pixel layout every frame. The real renderer caches both and invalidates only on resize, handle drag, profile change or scale-mode change.

Draw time is reported through the existing `Trace` in `web/telemetry.js` rather than a separate counter, so the budget is measured rather than assumed.

## Build order

1. `viz/profiles.js` and `viz/camera.js` — pure functions, no canvas. Both are testable on their own.
2. `viz/path.js` — spline, arc length, presets, hit-testing.
3. `viz/StripView.js` behind the existing `LedCanvas` name, single strip, straight path only. At this point the page must look no worse than today and the contract must be unchanged.
4. Multi-strip, then handles and dragging.
5. `viz/surface.js` — the three backplates.
6. `viz/BenchStrip.js` and the hover inspector, wired to both views.
7. Rail controls and the `state.hw` migration, including `localStorage`.
8. Measure draw time against the 8 ms budget; apply the sprite fallback only if it misses.
9. Walk the inventory above against the running page in both modes, confirming every listed control and readout is present and still driven.

## Implementation deviations

Written after the build. Each entry is something the shipped code does differently from the
design above, with the reason, so a future read reconciles against this section rather than
only against the sections before it.

**The page was rebuilt in the mockup's design language, not merely reframed.** The plan assumed
the existing centred document layout would keep its component styling and gain a new frame. Built
that way, it still read as the old page, because a design lives in its type scale, density and
component rules rather than its outer grid. `web/style.css` was therefore rewritten from the
approved mockup, which now lives in the repo at `web/design/mockup-reference.html` as the design
source. The accent stays `#38bdf8` rather than the mockup's orange, because two canvas files
hand-shadow that hex and changing it needs all three at once.

**Silicone does not fuse.** The profile table's note oversold it and a test asserted it. With
`pitch 16.7` and `sigma 11.0`, `sigma > pitch` is false, which is correct: a sleeved 60/m strip is
far softer than a bare one and still shows its pitch. The measurement stands; the claim was
corrected. Raising `sil.sigma` above 16.7 is the one-line change if a continuous look is wanted.

**The rod occludes whole strips, not halves of one.** The spec's wrap-around reading implies a
depth per pixel and two glow passes per strip. What ships paints the rod between strip 0 and
strip 1, which buys most of the look for one extra draw call. Strip 0 ending up behind the rod is
a consequence of that ordering, not a requirement — `surface.js` says so where someone might
otherwise "fix" it.

**Per-strip opening poses live in `path.js`, not in the renderer.** `DEFAULT_POSES` and
`defaultPose(index)` exist because a `SHAPES` entry spans the whole stage, so using one for both
strips overlaps them. Keeping the poses beside the shapes stops each consumer reinventing them;
two places would have reintroduced the overlap.

**The fit readout reports the scale in `fit` mode.** Fit mode chooses the scale so the strip
exactly fills the path, which made the spare figure permanently `0.00 m` — not merely
uninformative but readable as a bug. Fit mode now reports `1 px ≈ X mm`, the number it actually
determines; the path-against-strip comparison appears only in `true` mode.

**Renderer binding is repeatable, not one-shot.** `app.js` and `live.js` each construct their own
`StripView` on the same canvas, so a mode switch replaces the instance. Binding happens from
`selectMode` and is idempotent, the previous view is detached so its pointer handlers cannot write
pose data it no longer owns, and settings load once so a rebind cannot overwrite a pose mid-drag.

**Continuity is read through `fuses()`, everywhere in the lit path.** The first cut of `drawCores`
branched on `casing`, which is exactly the per-type branch this design exists to avoid, and it
contradicted the test asserting silicone does not fuse. `casing` now scopes only to how the unlit
substrate is drawn.

**Node's floor rose to 22.** `node --test` takes a glob and no longer accepts a directory
argument, so no single form of the test script spans Node 18 and Node 24. CI does not run
`npm test`, so nothing there is affected.

**Audio path confirmed by the owner.** The spectrum panel, the level meter and the microphone
connection all work. They were unverifiable from the test browser, which blocks the microphone and
produced silence from the demo signal, so the confirmation is the owner's rather than measured
here.

**The pose is saved only once it settles, and that is the intended behaviour.** Persisting on
every `pointermove` meant a synchronous `localStorage` write about a hundred times a second on the
render path. The write is debounced by 1.2 s and nothing flushes it on unload, so a pose abandoned
mid-gesture is lost. The owner's call, and the right one: the intermediate positions of a drag have
no reader, and a flush handler would be machinery guarding something nobody minds losing.

**Ten shape presets, not five.** `SHAPES` gained `bowl`, `wave`, `corner`, `column` and `diag`
alongside the original five. Each is still four handles, because four is what a catmull-rom spline
needs to express one bend or one reversal — a preset wanting five handles is a shape worth
dragging by hand.
