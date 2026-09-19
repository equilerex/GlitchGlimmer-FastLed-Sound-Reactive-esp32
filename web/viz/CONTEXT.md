# web/viz

The renderer behind the browser visualiser. Everything here answers one of two questions: *what
will this look like on the wall*, and *which pixel is wrong*.

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
override it. The path extends as a serpentine and the camera clips what is outside the viewport;
zoom changes the view scale, not the software length.

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
hand-synced to their token and carry a comment saying so. `drawHandles` and `BenchStrip` are the
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

## Testing

`camera.js`, `profiles.js` and `path.js` are pure and tested under `test/viz/`. The canvas modules
are not, and are verified by hand in a browser. Keep new logic on the pure side of that line where
you can.
