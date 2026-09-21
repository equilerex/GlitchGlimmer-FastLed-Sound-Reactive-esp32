# web/viz
updated: 2026-09-20

The renderer behind the browser visualiser. Everything here answers one of two questions: *what
will this look like on the wall*, and *which pixel is wrong*.

## File map

| File | Owns | Pure (tested) |
|---|---|---|
| `profiles.js` | Strip catalogue in mm: pitch, die, sigma, `body`, UI defaults | yes |
| `path.js` | Spline, arc-length pixel placement, `pointAtSmooth`, `angleAt` | yes |
| `camera.js` | Gamma, exposure, clip-to-white | yes |
| `surface.js` | Backdrops (`room`, `light`, `slat`), drawn to scale | no |
| `StripView.js` | The stage: geometry, glow buffer, composite order, pointer editing | no |
| `realistic.js` | Physical parts for the realistic look: PCB, pads, bodies, sleeve, `glowSpots` | no |
| `BenchStrip.js` | Straightened indexed strip under the stage | no |
| `hwStore.js` | The settings object, localStorage, view binding | partly |

`web/render.js` re-exports `StripView` as `LedCanvas`, the name `app.js` and `live.js` use.

## Paint order, per frame

`paint()` builds geometry if invalidated, paints the surface, then per strip: PCB or substrate,
fill the glow buffer, environment layer, spill, halo, then bodies (realistic) or cores
(stylized), then sleeve. Grain and the hover guide go last. Order matters; the glow passes sit
under the bodies on purpose.

`geometry[i]` carries `profile`, `body`, `sizeK`, `pxPerMm`, `pitchPx`, `sigmaPx`, `diePx`,
`stretchPx` (0 unless the pixel is a bar), `path` and `leds` (each `{x, y, index, at, angle}`).
Anything drawn per strip reads from there. It is rebuilt only through `invalidate()`, so a setting
that changes geometry must call it (the store's `sync()` does).

## Recipes

- **New strip type:** a row in `profiles.js` with a `body`. Density is LEDs per metre and
  `pitch = 1000 / density`. Nothing else changes; the button list is generated from `PROFILES`.
- **New surface:** an entry in `SURFACES` and a painter in `surface.js`. Painters get `pxPerMm`
  so real-world features stay to scale.
- **New setting the renderer reads:** add it to `defaultHw` and the persisted keys in
  `hwStore.js`, mirror the default in `web/state.js`, then read it from `view.config`. The user
  must be able to reach it in `web/index.html`; see `web/CONTEXT.md`.
- **New realistic package:** a `body.fill` painter in `realistic.js`'s `PAINTERS`, and nowhere else.

## The model

A rendered strip is the product of four independent inputs. Nothing here special-cases a strip
*type*.

```
path (spline)  x  LED profile  x  surface  x  camera
```

Adding a strip type is a row in `profiles.js` and nothing else. If you find yourself writing
`if (casing === ...)` in the lit path, the model has leaked — that is the defect this folder is
shaped to prevent, and it has already been caught once in review.

`casing` has exactly one legitimate use: how the *unlit* substrate is drawn in `drawSubstrate`.
A dark strip still has to read as a physical object.

## Profiles are millimetres

`pitch`, `die` and `sigma` are real hardware measurements. Never store pixels in a profile.

Continuity is emergent, not configured: when `sigma` exceeds `pitch` the pixels fuse, which is
what a COB phosphor does. `fuses(profile)` is that rule and is the only thing the lit path may
ask about a profile's identity.

A consequence worth knowing before you change a number: silicone sleeve at `sigma 11.0` against
`pitch 1000/60` does **not** fuse. That is correct — a sleeved 60/m strip is far softer than a bare
one and still shows its pitch.

## Two looks

`config.look` is `realistic` (default) or `stylized`. Both use the same placement, camera and glow
passes, so LED positions and pitch never differ between them.

`stylized` is the glow-and-disc look in `StripView`. `realistic.js` draws the physical parts
instead: PCB or cable, copper pads, then a body per pixel, all from the profile's `body` block in
mm. `body.fill` is `package` (5050 with reflector), `phosphor` (COB bar; `blend` > 0 bleeds
neighbouring pixels across the join, which is what an FCOB does) or `bead` (pebble, bullet, fairy).
A pixel that is a stretch (`geom.stretchPx`) lights its whole length through `glowSpots`.

Dispatching on `body.fill` inside `realistic.js` is the one place per-package drawing is allowed,
because drawing the package is the point. It must not leak into the stylized path. A new strip
type is still a profile row; give it a `body` or the realistic look has nothing to draw.

Also in the realistic pipeline: an environment layer (`compositeEnvironment`) shrinks, softens and
re-stretches the glow buffer twice so light bleeds far beyond the strip, painted underneath the
spill and halo. Below 700 px canvas width the physical scale is floored (`MIN_PX_PER_MM_PHONE`),
because a 3 m stage on a phone puts LEDs 2 px apart and every light is sub-pixel; the stage then
shows less of the strip rather than shrinking it.

Surfaces are `room`, `light` and `slat`. The slat wall is drawn to scale (24 mm boards, 10 mm black
gaps) from the current `pxPerMm`, so a strip fits a gap.

Density, not pitch, is the user-facing unit: profiles are LEDs per metre and `pitch = 1000 / density`.
Every WS2812B profile shares one body, so widths never vary with density.

## Device capacity and software geometry are separate

Device capacity comes from the firmware — `_gg_leds0_count()` for the physical/default live
configuration and `manifest.leds0` for a recording. The browser's live WASM path now has a
software-controlled active length and count inside the shared animation library. The board's
physical capacity remains fixed; the WASM simulation has a larger capacity and reports its active
software count after the browser sets it.

```
strip length = count x pitch
```

The browser derives active count by rounding `lengthM * 1000 / pitchMm` to a whole pixel. Built-in
profiles use exact density-derived pitches (`1000/60`, `1000/144`, and so on), so 2 m at 60/m is
120 pixels and 2 m at 144/m is 288 pixels. A profile's pitch is the default, but the user may
override it. The hardware/readout model keeps that physical relationship; the editor draws the
active path directly. The path length becomes the software strip length when a drag
commits, while the selected profile's pitch places LEDs at fixed physical intervals.
Zoom changes the view scale, not the software length.

The visual editor starts with straight horizontal calibration paths and uses a
single drag gesture to replace the active strip path. It samples that gesture
as a smoothed spline and places LEDs at the selected profile's fixed physical
pitch. On release, the measured path length becomes the software length and
the count is recalculated from that pitch; zoom remains a display-only control.

## Why the camera matters more than the glow

`camera.js` is the single largest visual difference from every other FastLED preview, and it is
three lines of arithmetic: gamma to linear, gain from exposure, then desaturation toward white for
everything past the knee. A real sensor loses hue as it clips. Without that, a saturated red at
four stops over stays red, and the render reads as paint rather than light.

The three composite passes in `StripView` run spill, then halo, then cores, out of one offscreen
buffer. Painting cores first buries them and loses the blowout, which is the point.

## Canvas colours

Colours depicting physical material — substrate, glow, cores, and every surface — are literals
with no CSS token, deliberately. They describe how hardware behaves optically and must not follow
the page theme.

Colours that are UI chrome track the theme. Where a painter runs every frame, resolving a custom
property through `getComputedStyle` costs more than the coupling does, so those literals are
hand-synced to their token and carry a comment saying so. `drawGuides` and `BenchStrip` are the
two.

## Two views, one canvas

`app.js` and `live.js` each construct their own `StripView` on `#view`, so a mode switch replaces
the instance. Binding is therefore repeatable rather than one-shot, and the previous view is
detached — otherwise its pointer handlers keep writing pose data through a stale `activeStrip`
into a settings object it no longer owns.

`canvas.__view` is how the Vue layer reaches the current renderer. It is load-bearing.

## The bench is not decoration

`BenchStrip` and `StripView` read the same byte array and must never disagree about a pixel — a
difference between them reads as a firmware bug, which is the worst possible signal from a
debugging tool. Their offset walks and their `expose` calls are deliberately identical. Change one
and change the other.

## Things that cost time

- `sigma`, `die` and `body` are independent. `sigma` drives glow reach and fusion, `die` the
  stylized disc, `body` the realistic geometry. Changing one does not move the others.
- The realistic glow deliberately does not use `expose()`'s output, which is whitened as it
  clips. It uses `_glowAt`, the linear light, normalised to its brightest channel, so bloom keeps
  its hue. Using `colourOf` there brings back the grey discs.
- Glow layers are normalised by how many pixels they overlap. Add a layer without that and dense
  strips flood the stage.
- LED markers are hover-only. Permanent guide dots were removed on request; do not restore them.
- Draw time is about 8 ms on a dense test frame, at the budget. The wide glow layers are the first
  thing to cut.

## Testing

`camera.js`, `profiles.js` and `path.js` are pure and tested under `test/viz/`. The canvas modules
are not, and are verified by hand in a browser. Keep new logic on the pure side of that line where
you can.
