# Matrices and 2D

A matrix is a 1D strip whose LEDs happen to be arranged in rows. All the 1D primitives still apply — the only new problem is turning an `(x, y)` coordinate into a strip index, and getting that mapping wrong is the single most common matrix bug.

## Start here: there is no `XY()`

Older code, and a great many tutorials, define their own:

```cpp
uint16_t XY(uint8_t x, uint8_t y) { return (y * WIDTH) + x; }   // don't
```

This is not part of the library. `examples/XYMatrix/XYMatrix.ino` rolls its own, which is how the idiom spread. Every sketch that does this is a copy of the same duplicated work, and each copy gets the serpentine case wrong differently.

The library has `fl::XYMap`. Use it.

```cpp
#include "fl/math/xymap.h"

fl::XYMap xyMap(WIDTH, HEIGHT, /*is_serpentine=*/true);

leds[xyMap(x, y)] = color;      // operator() returns the strip index
```

`is_serpentine` is the parameter to think about, not to copy. A serpentine (boustrophedon) panel wires row 0 left-to-right, row 1 right-to-left, and so on — physically shorter wiring, and very common in purchased panels. A progressive panel wires every row the same direction. Getting this backwards produces an image that looks correct on every other row, which is a distinctive and instantly recognisable symptom. Note that `is_serpentine` **defaults to `true`** (`src/fl/math/xymap.h:74`), so serpentine is the case that needs nothing said and a *progressive* panel is the one that requires an explicit `false` — the opposite of what the parameter's prominence suggests.

```cpp
fl::XYMap::constructSerpentine(w, h);
fl::XYMap::constructRectangularGrid(w, h);
fl::XYMap::constructWithUserFunction(w, h, fn);
fl::XYMap::constructWithLookUpTable(w, h, table);
fl::XYMap::identity(w, h);
fl::XYMap::fromXMap(xmap);
```

`constructWithLookUpTable` looks like the escape hatch for panels that follow no regular pattern — a hand-soldered installation, a non-rectangular shape, LEDs placed on a curve — but the source flags it as not currently working (`src/fl/math/xymap.h:52`: "This isn't working right, but the user function works just fine"). Use `constructWithUserFunction` instead and close over your table or compute the index directly. Either way, build it once in `setup()` and the effect code stays readable.

`xyMap.has(x, y)` tests bounds. `xyMap(x, y)` / `xyMap.mapToIndex(x, y)` do the lookup, and **wrap** out-of-range values (`x % width`, `y % height`) rather than clamping — so an off-by-one in a loop produces a mirrored artifact, not a dead pixel. `has()` first if you want clamping behaviour.

## ScreenMap is a different thing

`fl::ScreenMap` (`fl/math/screenmap.h`) maps strip index to floating-point XY for the web/WASM preview — it tells the simulator where pixels are on screen. It is not for indexing during animation. Naming is close enough that this gets confused; if you find yourself calling `ScreenMap` inside a draw loop, you want `XYMap`.

```cpp
fl::ScreenMap screenMap = fl::ScreenMap::DefaultStrip(NUM_LEDS, 1.5, 0.4);
FastLED.addLeds<...>(leds, NUM_LEDS).setScreenMap(screenMap);
```

## Owning the buffer: `Leds` and `LedsXY`

```cpp
fl::LedsXY<16, 16> leds;
leds(x, y) = fl::CRGB::Red;      // bounds-safe; out-of-range writes go to a shared empty pixel
```

`LedsXY<W, H>` owns the buffer and an `XYMap` together, so there is no separate index array to keep in sync. Its accessor is bounds-safe, which matters because an out-of-range write into a raw `CRGB[]` corrupts an adjacent variable rather than failing loudly.

One trap: `Leds::width()` and `height()` are transposed — `width()` returns the map's height. Read `leds.xymap().getWidth()` when the actual dimension matters.

For most effects the plain `CRGB leds[N]` plus an explicit `XYMap` is simpler and does the same job. Reach for `LedsXY` when you want the ownership.

## 2D effects

Subclass `fl::Fx2d`, which holds the map for you:

```cpp
class Plasma : public fl::Fx2d {
  public:
    Plasma(const fl::XYMap &map) : Fx2d(map) {}

    void draw(fl::Fx::DrawContext ctx) override {
        for (fl::u16 y = 0; y < getHeight(); y++) {
            for (fl::u16 x = 0; x < getWidth(); x++) {
                ctx.leds[xyMap(x, y)] = ...;
            }
        }
    }

    fl::string fxName() const override { return "Plasma"; }
};
```

The loop order matters for cache behaviour: iterate `y` outer, `x` inner, and index through `xyMap` — the map's internal layout is what determines memory order, so don't assume `x` is contiguous.

## 2D operations

```cpp
fl::blur2d(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
           fract8 amount, const XYMap &xymap);
fl::blurRows(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
             fract8 amount, const XYMap &xymap);
fl::blurColumns(fl::span<CRGB> leds, fl::u8 width, fl::u8 height,
                fract8 amount, const XYMap &xymap);
fill_2dnoise8(CRGB *leds, int width, int height, bool serpentine, ...);
```

The old four-argument `blur2d(leds, w, h, amount)` is deprecated and binds to a legacy `XY()` symbol — which means it silently depends on a function you were supposed to have defined. If you see that form, it is a compile error waiting to happen on a different sketch. Pass the `XYMap`.

## Drawing primitives

`fl::gfx::Canvas<CRGB>` (`fl/gfx/canvas.h`) is the newest layer, for effects that want to draw shapes rather than paint pixels:

```cpp
fl::gfx::Canvas<fl::CRGB> canvas;
canvas.drawLine(...);
canvas.drawDisc(...);
canvas.drawRing(...);
canvas.blurGaussian<h, v>(...);
```

This is the right tool for text, sprites, and geometric overlays. For a field effect — noise, plasma, waves — direct pixel writes through `XYMap` are simpler and faster.

## Practical ordering

1. Determine the panel's physical wiring: serpentine or progressive.
2. Build the `XYMap` once, at file scope, and never construct one inside a draw loop.
3. Write the effect in `(x, y)` terms against that map.
4. If the image is mirrored, flipped, or correct only on alternate rows, the map construction is wrong — fix the map, not the effect.
