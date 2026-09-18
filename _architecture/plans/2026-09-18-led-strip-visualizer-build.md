# LED Strip Visualizer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the flat two-row LED renderer in `web/` with a physically-grounded one that poses each strip along a draggable path, renders it photographically, and keeps a pixel-exact debug ruler beside it.

**Architecture:** Four independent inputs multiply into one render — path, LED profile, surface, camera. Pure logic (profiles, camera maths, spline geometry) lives in testable modules under `web/viz/` with no canvas dependency; the canvas work sits on top of them. The public contract `new LedCanvas(canvas, counts)` / `.resize()` / `.paint(bytes)` / `.clear()` never changes, so `web/app.js`, `web/live.js` and `web/main.js` are never edited.

**Tech Stack:** Vanilla ES modules, Vue 3 (vendored at `web/vendor/vue.esm-browser.js`), Canvas 2D. Tests run on Node's built-in `node:test` — no test dependency is added.

**Spec:** `_architecture/plans/2026-09-18-led-strip-visualizer.md`

## Global Constraints

- **No git branch, no worktree.** All work happens on `main` in the existing checkout.
- **Never run `git commit` or `git push`.** Commit steps below list the exact command for the repo owner to run; the implementer stages nothing and commits nothing.
- **No new runtime dependencies.** No npm install. Tests use `node --test`, which ships with Node 18.
- **`render.js` keeps exporting `LedCanvas`, `Spectrum`, `showError`, `hideError`** with unchanged signatures.
- **Every existing readout survives.** Live mode: scene, mood now, mood of window, BPM, level meter,
  beat lamp, spectrum canvas, mic/demo buttons, source note, Copy snapshot, record toggle,
  `#live-state` telemetry rows and their tuning sliders. Recording mode: scene, mood, time,
  play/pause, scrub, scenario buttons, scenario note. Both: the hardware controls and the
  `?debug=1` trace. From Task 8 onward these are **rehomed into the new shell, never removed** —
  a control that moves is fine, a control that disappears is a failed task.
- **`web/app.js` and `web/live.js` are never modified.** `web/main.js` is modified only by Task 9
  (wiring) and Task 10 (one telemetry field). Any other task needing a change there stops and reports.
- **Draw budget: 8 ms** for both strips combined, measured through `Trace` in `web/telemetry.js`.
- **Profile measurements are millimetres** throughout. Never store pixels in a profile.
- **Colour tokens come from `web/style.css`** (`--bg`, `--panel`, `--panel-edge`, `--ink`, `--ink-dim`, `--accent`, `--accent-glow`, `--error`, `--mono`). Do not introduce a second palette.

---

## File Structure

| File | Responsibility | Canvas? |
|---|---|---|
| `web/viz/profiles.js` | LED profile table, lookup, length maths, legacy preset migration | no |
| `web/viz/camera.js` | gamma → exposure → clip-to-white, grain amount | no |
| `web/viz/path.js` | catmull-rom spline, arc length, pixel placement, shape presets, handle hit-test | no |
| `web/viz/surface.js` | dark room / slat wall / bar rod backplates, rod occluder | yes |
| `web/viz/StripView.js` | the stage renderer; owns the offscreen glow buffer and the three composite passes | yes |
| `web/viz/BenchStrip.js` | the ruler: colour cells, index ticks, luma bars, hover highlight | yes |
| `web/render.js` | keeps `showError`, `hideError`, `Spectrum`; re-exports `StripView` as `LedCanvas` | yes |
| `test/viz/camera.test.js` | camera maths | no |
| `test/viz/profiles.test.js` | profile table and migration | no |
| `test/viz/path.test.js` | spline geometry and placement | no |

---

### Task 1: Test harness and the camera model

The camera is the single largest visual difference from every other FastLED preview, and it is pure arithmetic, so it goes first and gets real tests.

**Files:**
- Create: `web/viz/camera.js`
- Create: `test/viz/camera.test.js`
- Modify: `package.json` (add the `test` script)

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `expose(r, g, b, ev) -> [r, g, b, peak]` — input bytes 0–255, output bytes 0–255 plus `peak`, the unclamped linear maximum.
  - `GRAIN_DOT_COUNT` — integer, the number of grain dots a full-strength grain pass draws.
  - `grainAlpha(grain) -> number` — 0–1 alpha for the grain overlay.

- [ ] **Step 1: Add the test script**

In `package.json`, add one line to `scripts`:

```json
"test": "node --test test/"
```

- [ ] **Step 2: Write the failing test**

Create `test/viz/camera.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { expose, grainAlpha, GRAIN_DOT_COUNT } from '../../web/viz/camera.js';

test('black stays black at any exposure', () => {
  for (const ev of [-2, 0, 3]) {
    const [r, g, b, peak] = expose(0, 0, 0, ev);
    assert.equal(r, 0);
    assert.equal(g, 0);
    assert.equal(b, 0);
    assert.equal(peak, 0);
  }
});

test('full white survives a round trip at EV 0', () => {
  const [r, g, b, peak] = expose(255, 255, 255, 0);
  assert.ok(r > 250, `expected near 255, got ${r}`);
  assert.ok(peak >= 1);
});

test('mid grey round trips to roughly itself at EV 0', () => {
  const [r] = expose(128, 128, 128, 0);
  assert.ok(Math.abs(r - 128) < 3, `expected ~128, got ${r}`);
});

test('EV doubles linear light, so +1 EV brightens', () => {
  const dim = expose(64, 0, 0, 0)[0];
  const bright = expose(64, 0, 0, 1)[0];
  assert.ok(bright > dim, `expected ${bright} > ${dim}`);
});

test('a clipping red desaturates toward white', () => {
  const [r, g, b] = expose(255, 0, 0, 3);
  assert.ok(g > 60, `green should rise as red clips, got ${g}`);
  assert.ok(b > 60, `blue should rise as red clips, got ${b}`);
  assert.ok(r >= g && r >= b, 'red must stay the strongest channel');
});

test('no channel ever exceeds 255', () => {
  const [r, g, b] = expose(255, 200, 40, 3);
  for (const v of [r, g, b]) assert.ok(v <= 255, `got ${v}`);
});

test('grain alpha is zero at zero and bounded at one', () => {
  assert.equal(grainAlpha(0), 0);
  assert.ok(grainAlpha(1) > 0 && grainAlpha(1) <= 1);
  assert.ok(GRAIN_DOT_COUNT > 0);
});
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `npm test`
Expected: FAIL — `Cannot find module '../../web/viz/camera.js'`.

- [ ] **Step 4: Write the implementation**

Create `web/viz/camera.js`:

```js
// Photographic response, not additive blending.
//
// The reason this is its own module: what separates a render that reads as
// light from one that reads as paint is not the glow radius, it is that a
// real sensor loses hue as it clips. A saturated red at four stops over is
// white on a photograph and red in every other LED preview. That behaviour
// is three lines of arithmetic, so it belongs somewhere it can be tested
// without a canvas.

const GAMMA = 2.2;

// Where clipping starts, and how far past it the desaturation takes to
// complete. Both are taste, tuned against the mockup: a knee below 0.85
// washes mid-brightness colour out, and a range under 1.0 snaps to white
// hard enough to look like a bug.
const KNEE = 0.85;
const KNEE_RANGE = 1.4;
const MAX_WHITEN = 0.92;

export const GRAIN_DOT_COUNT = 900;

const toLinear = (v) => Math.pow(v / 255, GAMMA);
const toDisplay = (v) => Math.pow(Math.min(1, v), 1 / GAMMA);

export function expose(r, g, b, ev) {
  const gain = Math.pow(2, ev);
  const lr = toLinear(r) * gain;
  const lg = toLinear(g) * gain;
  const lb = toLinear(b) * gain;

  const peak = Math.max(lr, lg, lb);
  if (peak === 0) return [0, 0, 0, 0];

  const over = Math.max(0, Math.min(1, (peak - KNEE) / KNEE_RANGE));
  const whiten = over * MAX_WHITEN;
  const mix = (v) => (v + (1 - v) * whiten) * 255;

  return [mix(toDisplay(lr)), mix(toDisplay(lg)), mix(toDisplay(lb)), peak];
}

export function grainAlpha(grain) {
  return Math.max(0, Math.min(1, grain)) * 0.5;
}
```

- [ ] **Step 5: Run the test to verify it passes**

Run: `npm test`
Expected: PASS, 7 tests.

- [ ] **Step 6: Commit** (for the repo owner to run)

```bash
git add package.json web/viz/camera.js test/viz/camera.test.js
git commit -m "Give the visualiser a camera instead of a paint bucket"
```

---

### Task 2: LED profiles

**Files:**
- Create: `web/viz/profiles.js`
- Create: `test/viz/profiles.test.js`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `PROFILES` — array of `{ id, name, pitch, die, sigma, casing, burst, note }`, millimetres.
  - `profileById(id) -> profile` — falls back to the `ws60` profile for an unknown id.
  - `stripLengthMm(count, profile) -> number`
  - `fuses(profile) -> boolean` — true when `sigma > pitch`, meaning the pixels merge.
  - `migratePreset(preset) -> id` — maps a legacy `state.hw.preset` string onto a profile id.

- [ ] **Step 1: Write the failing test**

Create `test/viz/profiles.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  PROFILES, profileById, stripLengthMm, fuses, migratePreset,
} from '../../web/viz/profiles.js';

test('every profile carries the full measurement set', () => {
  for (const p of PROFILES) {
    for (const key of ['id', 'name', 'pitch', 'die', 'sigma', 'casing', 'burst', 'note']) {
      assert.ok(p[key] !== undefined, `${p.id} is missing ${key}`);
    }
    assert.ok(p.pitch > 0, `${p.id} has a non-positive pitch`);
  }
});

test('profile ids are unique', () => {
  const ids = PROFILES.map((p) => p.id);
  assert.equal(new Set(ids).size, ids.length);
});

test('an unknown id falls back to the standard strip', () => {
  assert.equal(profileById('nope').id, 'ws60');
});

test('120 pixels of 60 per metre is two metres', () => {
  assert.equal(stripLengthMm(120, profileById('ws60')), 2004);
});

test('COB fuses and bare SMD does not', () => {
  assert.equal(fuses(profileById('cob')), true);
  assert.equal(fuses(profileById('ws60')), false);
  assert.equal(fuses(profileById('sil')), true);
});

test('legacy presets migrate onto profile ids', () => {
  assert.equal(migratePreset('144'), 'ws144');
  assert.equal(migratePreset('60'), 'ws60');
  assert.equal(migratePreset('30'), 'ws30');
  assert.equal(migratePreset('fairy'), 'fairy');
  assert.equal(migratePreset('bullet'), 'bul');
  assert.equal(migratePreset('cob'), 'cob');
  assert.equal(migratePreset('none'), 'ws60');
  assert.equal(migratePreset(undefined), 'ws60');
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `npm test`
Expected: FAIL — `Cannot find module '../../web/viz/profiles.js'`.

- [ ] **Step 3: Write the implementation**

Create `web/viz/profiles.js`:

```js
// What a strip physically is, in millimetres.
//
// There is no per-type renderer anywhere in viz/. A COB bar looks continuous
// because its diffusion radius is larger than its pitch, not because a branch
// somewhere draws a tube for it. Keeping the difference in the numbers is why
// adding a strip type is a row in this table and nothing else.

export const PROFILES = [
  {
    id: 'ws60', name: 'WS2812B 60/m',
    pitch: 16.7, die: 5.0, sigma: 2.4, casing: 'smd', burst: 0,
    note: 'Bare 5050 SMD. Hard dots, visible gaps past a couple of metres.',
  },
  {
    id: 'ws144', name: 'WS2812B 144/m',
    pitch: 6.9, die: 3.5, sigma: 1.7, casing: 'smd', burst: 0,
    note: 'Dense 3535. Reads as a line at arm’s length, dotty up close.',
  },
  {
    id: 'ws30', name: 'WS2812B 30/m',
    pitch: 33.3, die: 5.0, sigma: 2.4, casing: 'smd', burst: 0,
    note: 'Same 5050 die, half the density. Dots read individually.',
  },
  {
    id: 'sil', name: 'IP65 silicone',
    pitch: 16.7, die: 5.0, sigma: 11.0, casing: 'sleeve', burst: 0,
    note: 'A 60/m strip inside a milky sleeve. The sleeve does the blending.',
  },
  {
    id: 'cob', name: 'COB 480/m',
    pitch: 2.1, die: 1.8, sigma: 3.4, casing: 'cob', burst: 0,
    note: 'Diffusion wider than the pitch, so the pixels fuse into one bar.',
  },
  {
    id: 'bul', name: 'WS2811 bullet',
    pitch: 50.0, die: 9.0, sigma: 5.5, casing: 'bulb', burst: 0.35,
    note: '12 mm epoxy bullets. Bright point, dark cable between.',
  },
  {
    id: 'fairy', name: 'Fairy pip',
    pitch: 50.0, die: 2.0, sigma: 3.0, casing: 'pip', burst: 1.0,
    note: 'A tiny die in clear epoxy, so it scatters into a starburst.',
  },
];

export const DEFAULT_PROFILE_ID = 'ws60';

export function profileById(id) {
  return PROFILES.find((p) => p.id === id)
      || PROFILES.find((p) => p.id === DEFAULT_PROFILE_ID);
}

export function stripLengthMm(count, profile) {
  return count * profile.pitch;
}

export function fuses(profile) {
  return profile.sigma > profile.pitch;
}

// The old sidebar stored a density string. Kept so a browser that has one in
// localStorage, or a state object written before this change, lands on the
// right profile instead of silently resetting.
const LEGACY = {
  144: 'ws144', 60: 'ws60', 30: 'ws30',
  fairy: 'fairy', bullet: 'bul', cob: 'cob', none: 'ws60',
};

export function migratePreset(preset) {
  return LEGACY[preset] || DEFAULT_PROFILE_ID;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `npm test`
Expected: PASS, 13 tests total across both files.

- [ ] **Step 5: Commit** (for the repo owner to run)

```bash
git add web/viz/profiles.js test/viz/profiles.test.js
git commit -m "Describe strips by their millimetres, not by a type branch"
```

---

### Task 3: Path geometry

**Files:**
- Create: `web/viz/path.js`
- Create: `test/viz/path.test.js`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `SHAPES` — object mapping a shape name to four normalised `[x, y]` control points.
  - `samplePath(pts, width, height, samples = 600) -> { poly, cum, total }` — `poly` is an array of `[x, y]` in pixels, `cum` the cumulative length at each sample, `total` the full length in pixels.
  - `pointAt(path, lengthPx) -> [x, y]`
  - `placePixels(path, count, pitchPx) -> [{ x, y, index }]` — pixels laid along the path from its start, spaced `pitchPx` apart, stopping at `count` or at the end of the path, whichever comes first.
  - `fitScale(path, count, pitchMm) -> number` — pixels per millimetre that makes the strip exactly fill the path.
  - `hitHandle(pts, width, height, x, y, radius = 16) -> number` — index of the grabbed handle, or `-1`.

- [ ] **Step 1: Write the failing test**

Create `test/viz/path.test.js`:

```js
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  SHAPES, samplePath, pointAt, placePixels, fitScale, hitHandle,
} from '../../web/viz/path.js';

const STRAIGHT = [[0, 0.5], [0.25, 0.5], [0.75, 0.5], [1, 0.5]];

test('every shape preset has four control points in range', () => {
  for (const [name, pts] of Object.entries(SHAPES)) {
    assert.equal(pts.length, 4, `${name} must have four handles`);
    for (const [x, y] of pts) {
      assert.ok(x >= 0 && x <= 1, `${name} x out of range`);
      assert.ok(y >= 0 && y <= 1, `${name} y out of range`);
    }
  }
});

test('a straight path across 1000 px measures 1000 px', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  assert.ok(Math.abs(path.total - 1000) < 1, `got ${path.total}`);
});

test('a bent path is longer than the straight one', () => {
  const straight = samplePath(STRAIGHT, 1000, 400).total;
  const bent = samplePath(SHAPES.zigzag, 1000, 400).total;
  assert.ok(bent > straight, `${bent} should exceed ${straight}`);
});

test('pointAt walks from the start to the end', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  const [x0] = pointAt(path, 0);
  const [x1] = pointAt(path, path.total);
  assert.ok(x0 < 5, `start should be at the left, got ${x0}`);
  assert.ok(x1 > 995, `end should be at the right, got ${x1}`);
});

test('placePixels lays exactly count pixels when the path is long enough', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  const leds = placePixels(path, 50, 10);
  assert.equal(leds.length, 50);
  assert.equal(leds[0].index, 0);
  assert.equal(leds[49].index, 49);
});

test('placePixels stops at the end of a path too short to hold them', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  const leds = placePixels(path, 500, 10);
  assert.ok(leds.length < 500, `got ${leds.length}`);
  assert.ok(leds.length > 90, `got ${leds.length}`);
});

test('pixels advance monotonically along the path', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  const leds = placePixels(path, 40, 12);
  for (let i = 1; i < leds.length; i++) {
    assert.ok(leds[i].x > leds[i - 1].x, `pixel ${i} went backwards`);
  }
});

test('fitScale makes the strip exactly fill the path', () => {
  const path = samplePath(STRAIGHT, 1000, 400);
  const pxPerMm = fitScale(path, 120, 16.7);
  assert.ok(Math.abs(120 * 16.7 * pxPerMm - path.total) < 0.001);
});

test('hitHandle grabs the nearest handle and misses empty space', () => {
  assert.equal(hitHandle(STRAIGHT, 1000, 400, 0, 200), 0);
  assert.equal(hitHandle(STRAIGHT, 1000, 400, 1000, 200), 3);
  assert.equal(hitHandle(STRAIGHT, 1000, 400, 500, 20), -1);
});
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `npm test`
Expected: FAIL — `Cannot find module '../../web/viz/path.js'`.

- [ ] **Step 3: Write the implementation**

Create `web/viz/path.js`:

```js
// The posed shape of a strip, as a catmull-rom spline through four handles.
//
// Catmull-rom rather than bezier because the handles are the curve: a person
// dragging a strip around expects the thing under the cursor to be on the
// strip, and bezier control points are not.
//
// Everything here is arc-length based. A spline parameter is not distance, and
// pixels on a real strip are evenly spaced in distance, so spacing pixels by
// the parameter bunches them up in the corners.

export const SHAPES = {
  line:   [[0.08, 0.50], [0.36, 0.50], [0.64, 0.50], [0.92, 0.50]],
  arc:    [[0.08, 0.70], [0.34, 0.30], [0.66, 0.30], [0.92, 0.70]],
  zigzag: [[0.08, 0.26], [0.36, 0.72], [0.64, 0.26], [0.92, 0.72]],
  wrap:   [[0.20, 0.14], [0.50, 0.38], [0.50, 0.62], [0.80, 0.86]],
  drape:  [[0.08, 0.30], [0.34, 0.78], [0.66, 0.72], [0.92, 0.24]],
};

const DEFAULT_SAMPLES = 600;

function interpolate(pts, t) {
  const n = pts.length;
  const seg = Math.min(Math.floor(t * (n - 1)), n - 2);
  const u = t * (n - 1) - seg;
  const p0 = pts[Math.max(seg - 1, 0)];
  const p1 = pts[seg];
  const p2 = pts[seg + 1];
  const p3 = pts[Math.min(seg + 2, n - 1)];
  const u2 = u * u;
  const u3 = u2 * u;
  const axis = (a, b, c, d) => 0.5 * (
    (2 * b)
    + (-a + c) * u
    + (2 * a - 5 * b + 4 * c - d) * u2
    + (-a + 3 * b - 3 * c + d) * u3
  );
  return [axis(p0[0], p1[0], p2[0], p3[0]), axis(p0[1], p1[1], p2[1], p3[1])];
}

export function samplePath(pts, width, height, samples = DEFAULT_SAMPLES) {
  const abs = pts.map(([x, y]) => [x * width, y * height]);
  const poly = [];
  const cum = [];
  let total = 0;
  for (let i = 0; i <= samples; i++) {
    const p = interpolate(abs, i / samples);
    if (i > 0) {
      total += Math.hypot(p[0] - poly[i - 1][0], p[1] - poly[i - 1][1]);
    }
    poly.push(p);
    cum.push(total);
  }
  return { poly, cum, total };
}

export function pointAt(path, lengthPx) {
  const { poly, cum } = path;
  if (lengthPx <= 0) return poly[0];
  if (lengthPx >= cum[cum.length - 1]) return poly[poly.length - 1];
  let lo = 0;
  let hi = cum.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (cum[mid] < lengthPx) lo = mid + 1;
    else hi = mid;
  }
  return poly[lo];
}

// Pixel count comes from the firmware, so the path does not get to decide it.
// A path shorter than the strip simply runs out, which is the honest answer and
// the thing the fit readout reports.
export function placePixels(path, count, pitchPx) {
  const out = [];
  const step = Math.max(pitchPx, 0.25);
  for (let i = 0; i < count; i++) {
    const at = (i + 0.5) * step;
    if (at > path.total) break;
    const [x, y] = pointAt(path, at);
    out.push({ x, y, index: i });
  }
  return out;
}

export function fitScale(path, count, pitchMm) {
  const lengthMm = count * pitchMm;
  return lengthMm > 0 ? path.total / lengthMm : 1;
}

export function hitHandle(pts, width, height, x, y, radius = 16) {
  let best = -1;
  let bestDistance = radius;
  for (let i = 0; i < pts.length; i++) {
    const d = Math.hypot(pts[i][0] * width - x, pts[i][1] * height - y);
    if (d <= bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `npm test`
Expected: PASS, 22 tests total.

- [ ] **Step 5: Commit** (for the repo owner to run)

```bash
git add web/viz/path.js test/viz/path.test.js
git commit -m "Pose a strip along a spline instead of a table row"
```

---

### Task 4: The stage renderer, single strip

The riskiest task, because it replaces the thing already on screen. It ends with the page rendering one strip along a straight path and looking no worse than it does today. Multi-strip, handles and surfaces come after.

**Files:**
- Create: `web/viz/StripView.js`
- Modify: `web/render.js` (remove `stripRow`, `LedCanvas` and its helpers; keep `showError`, `hideError`, `Spectrum`; re-export `StripView` as `LedCanvas`)

**Interfaces:**
- Consumes: `expose`, `grainAlpha`, `GRAIN_DOT_COUNT` from `camera.js`; `profileById`, `stripLengthMm` from `profiles.js`; `SHAPES`, `samplePath`, `placePixels`, `fitScale` from `path.js`.
- Produces:
  - `class StripView { constructor(canvas, counts); resize(); paint(bytes); clear(); }` — the exact contract `web/app.js` and `web/live.js` already call.
  - `view.config` — the mutable render settings object read every frame: `{ surface, scale, stageWidthM, ev, spill, grain, pixelSize, glowSize, intensity, strips: [{ profile, shape, pts }] }`.
  - `view.lastDrawMs` — number, the duration of the most recent `paint`.
  - `view.fit(stripIndex) -> { pathM, stripM, overM }` — the fit readout, metres.

- [ ] **Step 1: Read what the contract actually is**

Read these call sites before writing anything. They are the only consumers and none of them may change:

- `web/app.js:170` — `leds = new LedCanvas(canvas, [manifest.leds0, manifest.leds1])`
- `web/app.js:41` — `leds.resize()`
- `web/app.js:51` — `leds.paint(pixels.subarray(base, base + manifest.bytesPerFrame))`
- `web/live.js:1091` — `leds = new LedCanvas(canvas, engine.counts)`
- `web/live.js:904` — `leds.paint(engine.scratch)`

`bytes` is one flat RGB array covering both strips back to back: `counts[0] * 3` bytes, then `counts[1] * 3`.

- [ ] **Step 2: Write `StripView.js` with a single strip along a straight path**

Create `web/viz/StripView.js`:

```js
// The stage: strips posed in space, lit photographically.
//
// Three composite passes out of one offscreen buffer, in this order, because
// the order is what makes it read as light:
//
//   spill  the buffer blurred wide and screened onto the surface. Light
//          landing on the wall behind the strip.
//   halo   the same buffer unblurred and added. The casing glowing.
//   core   a die-sized disc per pixel, pushed toward white as it clips.
//
// Painting the cores first would bury them under the halo and lose the
// blowout, which is the whole point.

import { expose, grainAlpha, GRAIN_DOT_COUNT } from './camera.js';
import { profileById, stripLengthMm } from './profiles.js';
import { SHAPES, samplePath, placePixels, fitScale } from './path.js';

const MAX_DPR = 2;

function defaultStrip(shape) {
  return { profile: 'ws60', shape, pts: SHAPES[shape].map((p) => p.slice()) };
}

export class StripView {
  constructor(canvas, counts) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.counts = counts;

    this.glow = document.createElement('canvas');
    this.glowCtx = this.glow.getContext('2d');

    this.width = 0;
    this.height = 0;
    this.dpr = 1;

    this.config = {
      surface: 'room',
      scale: 'fit',
      stageWidthM: 3,
      ev: 0,
      spill: 1,
      grain: 0.14,
      pixelSize: 1,
      glowSize: 1,
      intensity: 1,
      strips: counts.map((_, i) => defaultStrip(i === 0 ? 'line' : 'arc')),
    };

    this.geometry = null;   // rebuilt only when something geometric changes
    this.lastBytes = null;
    this.lastDrawMs = 0;
  }

  // Geometry is rebuilt on resize, on a handle drag, on a profile change and on
  // a scale-mode change, never per frame. Rebuilding a 600-sample spline sixty
  // times a second for a shape that did not move is most of a frame budget
  // spent on nothing.
  invalidate() {
    this.geometry = null;
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    this.dpr = Math.min(window.devicePixelRatio || 1, MAX_DPR);
    this.width = Math.max(320, Math.round(rect.width));
    this.height = Math.max(180, Math.round(rect.height || rect.width * 0.42));

    for (const surface of [this.canvas, this.glow]) {
      surface.width = this.width * this.dpr;
      surface.height = this.height * this.dpr;
    }
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.glowCtx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);

    this.invalidate();
    if (this.lastBytes) this.paint(this.lastBytes);
  }

  buildGeometry() {
    const { width, height, config } = this;
    this.geometry = config.strips.map((strip, i) => {
      const profile = profileById(strip.profile);
      const path = samplePath(strip.pts, width, height);
      const pxPerMm = config.scale === 'true'
        ? width / (config.stageWidthM * 1000)
        : fitScale(path, this.counts[i], profile.pitch);
      const pitchPx = profile.pitch * pxPerMm;
      return {
        profile,
        path,
        pxPerMm,
        pitchPx,
        sigmaPx: Math.max(profile.sigma * pxPerMm * config.glowSize, 1.1),
        diePx: Math.max(profile.die * pxPerMm * 0.5 * config.pixelSize, 0.8),
        leds: placePixels(path, this.counts[i], pitchPx),
      };
    });
  }

  fit(stripIndex) {
    if (!this.geometry) this.buildGeometry();
    const geom = this.geometry[stripIndex];
    const stripMm = stripLengthMm(this.counts[stripIndex], geom.profile);
    const pathMm = geom.path.total / geom.pxPerMm;
    return {
      pathM: pathMm / 1000,
      stripM: stripMm / 1000,
      overM: (stripMm - pathMm) / 1000,
    };
  }

  clear() {
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.ctx.fillStyle = '#09090b';
    this.ctx.fillRect(0, 0, this.width, this.height);
  }

  paint(bytes) {
    if (!this.width) this.resize();
    this.lastBytes = bytes;
    const started = performance.now();

    if (!this.geometry) this.buildGeometry();
    this.clear();

    let offset = 0;
    for (let i = 0; i < this.geometry.length; i++) {
      const geom = this.geometry[i];
      this.paintStrip(geom, bytes, offset);
      offset += this.counts[i] * 3;
    }

    this.paintGrain();
    this.lastDrawMs = performance.now() - started;
  }

  colourOf(bytes, offset, index) {
    const at = offset + index * 3;
    const k = this.config.intensity;
    return expose(bytes[at] * k, bytes[at + 1] * k, bytes[at + 2] * k, this.config.ev);
  }

  paintStrip(geom, bytes, offset) {
    this.drawSubstrate(geom);
    this.fillGlowBuffer(geom, bytes, offset);
    this.compositeSpill(geom);
    this.compositeHalo();
    this.drawCores(geom, bytes, offset);
  }

  // The unlit strip. Without it a dark run is nothing at all, and a strip that
  // vanishes when the firmware blanks it looks like a crash rather than like
  // a strip that is off.
  drawSubstrate(geom) {
    const ctx = this.ctx;
    const { casing } = geom.profile;
    const widthFor = {
      cob: Math.max(3, geom.sigmaPx * 1.1),
      sleeve: Math.max(5, geom.sigmaPx * 0.9),
      pip: 0.8,
    };
    ctx.save();
    ctx.strokeStyle = casing === 'pip'
      ? 'rgba(150, 160, 175, 0.16)'
      : 'rgba(120, 128, 140, 0.11)';
    ctx.lineWidth = widthFor[casing] || Math.max(2.5, geom.diePx * 1.6);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.beginPath();
    const poly = geom.path.poly;
    ctx.moveTo(poly[0][0], poly[0][1]);
    for (let i = 4; i < poly.length; i += 4) ctx.lineTo(poly[i][0], poly[i][1]);
    ctx.stroke();
    ctx.restore();
  }

  fillGlowBuffer(geom, bytes, offset) {
    const ctx = this.glowCtx;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.width, this.height);
    ctx.globalCompositeOperation = 'lighter';

    const halo = Math.max(geom.sigmaPx * 2.6, 3);
    for (const led of geom.leds) {
      const [r, g, b, peak] = this.colourOf(bytes, offset, led.index);
      if (peak < 0.004) continue;
      const rgb = `${r | 0},${g | 0},${b | 0}`;
      const grad = ctx.createRadialGradient(led.x, led.y, 0, led.x, led.y, halo);
      grad.addColorStop(0, `rgba(${rgb},0.95)`);
      grad.addColorStop(0.28, `rgba(${rgb},0.42)`);
      grad.addColorStop(1, `rgba(${rgb},0)`);
      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(led.x, led.y, halo, 0, Math.PI * 2);
      ctx.fill();

      if (geom.profile.burst > 0 && peak > 0.12) {
        const arm = halo * (1.8 + geom.profile.burst * 2.6) * Math.min(1, peak * 1.4);
        ctx.strokeStyle = `rgba(${rgb},${0.26 * geom.profile.burst})`;
        ctx.lineWidth = Math.max(1, geom.diePx * 0.5);
        ctx.beginPath();
        ctx.moveTo(led.x - arm, led.y);
        ctx.lineTo(led.x + arm, led.y);
        ctx.moveTo(led.x, led.y - arm);
        ctx.lineTo(led.x, led.y + arm);
        ctx.stroke();
      }
    }
    ctx.globalCompositeOperation = 'source-over';
  }

  compositeSpill(geom) {
    const spill = this.config.spill;
    if (spill <= 0.01) return;
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    ctx.globalAlpha = Math.min(1, spill * 0.55);
    ctx.filter = `blur(${Math.round(Math.max(geom.sigmaPx * 6, 16))}px)`;
    ctx.drawImage(this.glow, 0, 0, this.width, this.height);
    ctx.restore();
  }

  compositeHalo() {
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    ctx.drawImage(this.glow, 0, 0, this.width, this.height);
    ctx.restore();
  }

  drawCores(geom, bytes, offset) {
    const ctx = this.ctx;
    const continuous = geom.profile.casing === 'cob' || geom.profile.casing === 'sleeve';
    const radius = continuous
      ? Math.max(geom.diePx, geom.sigmaPx * 0.55)
      : geom.diePx;

    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    for (const led of geom.leds) {
      const [r, g, b, peak] = this.colourOf(bytes, offset, led.index);
      if (peak < 0.02) continue;
      const hot = Math.min(1, peak * 1.25);
      const toward = (v) => (v + (255 - v) * hot * 0.8) | 0;
      ctx.fillStyle = `rgba(${toward(r)},${toward(g)},${toward(b)},${0.55 + 0.45 * hot})`;
      ctx.beginPath();
      ctx.arc(led.x, led.y, radius, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  paintGrain() {
    const alpha = grainAlpha(this.config.grain);
    if (alpha <= 0) return;
    const ctx = this.ctx;
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.globalCompositeOperation = 'overlay';
    for (let i = 0; i < GRAIN_DOT_COUNT; i++) {
      const v = Math.random() > 0.5 ? 255 : 0;
      ctx.fillStyle = `rgb(${v},${v},${v})`;
      ctx.fillRect(Math.random() * this.width, Math.random() * this.height, 3, 3);
    }
    ctx.restore();
  }
}
```

- [ ] **Step 3: Rewire `render.js`**

In `web/render.js`, delete `clamp`, `stripRow` and the whole `LedCanvas` class (lines 22 through the end of that class, roughly `:22`–`:285`). Keep `showError`, `hideError` and `Spectrum` exactly as they are. Replace the `state` import at the top and add the re-export:

```js
// Canvas painting shared by the recording player and the live player.
//
// The strip rendering itself moved to viz/, which is where the physical model
// lives. This file keeps the two things that are not strips: the error banner
// and the spectrum panel. LedCanvas stays exported under its old name because
// app.js and live.js construct it by that name, and the point of the split was
// that neither of them had to care.

export { StripView as LedCanvas } from './viz/StripView.js';
```

Remove `import { state } from './state.js';` — nothing left in this file reads it.

- [ ] **Step 4: Verify the page still runs**

Run: `npm start`, then open `http://localhost:8000/?mode=live` and `http://localhost:8000/?mode=recording`.

Expected: both modes render two strips along their default paths. The spectrum panel, HUD, scrub, scenario buttons and live tuning rows all still work. No console errors.

The sidebar's `pixelSize` / `glowSize` / `intensity` sliders do nothing yet — they are wired in Task 8. The `preset` dropdown also does nothing yet. Note this and move on; do not patch `main.js` to compensate.

- [ ] **Step 5: Commit** (for the repo owner to run)

```bash
git add web/render.js web/viz/StripView.js
git commit -m "Move strip rendering out of render.js and onto a path"
```

---

### Task 5: Handles, dragging and the fit readout

**Files:**
- Modify: `web/viz/StripView.js` (add pointer handling and handle drawing)
- Modify: `web/index.html` (add the fit readout element inside `.strip-panel`)
- Modify: `web/style.css` (style the fit readout)

**Interfaces:**
- Consumes: `hitHandle` from `path.js`; `StripView` from Task 4.
- Produces:
  - `view.attachPointer()` — binds `pointerdown` / `pointermove` / `pointerup` / `pointerleave` on the canvas. Called from the constructor.
  - `view.activeStrip` — index of the strip whose handles are shown and draggable; defaults to `0`.
  - `view.onHover` — assignable callback, invoked as `onHover(stripIndex, ledIndex, rgb)` or `onHover(-1, -1, null)` when nothing is under the cursor.
  - `view.onGeometryChange` — assignable callback, invoked after a drag changes `config.strips[i].pts`.

- [ ] **Step 1: Add pointer state and handle drawing to `StripView`**

Add to the constructor, after `this.lastDrawMs = 0;`:

```js
    this.activeStrip = 0;
    this.dragHandle = -1;
    this.hover = { strip: -1, led: -1 };
    this.onHover = null;
    this.onGeometryChange = null;
    this.attachPointer();
```

Add these methods to the class:

```js
  attachPointer() {
    const canvas = this.canvas;

    canvas.addEventListener('pointerdown', (event) => {
      const [x, y] = this.localPoint(event);
      const pts = this.config.strips[this.activeStrip].pts;
      const grabbed = hitHandle(pts, this.width, this.height, x, y);
      if (grabbed >= 0) {
        this.dragHandle = grabbed;
        canvas.setPointerCapture(event.pointerId);
      }
    });

    canvas.addEventListener('pointermove', (event) => {
      const [x, y] = this.localPoint(event);
      if (this.dragHandle >= 0) {
        const pts = this.config.strips[this.activeStrip].pts;
        pts[this.dragHandle] = [
          Math.max(0.02, Math.min(0.98, x / this.width)),
          Math.max(0.03, Math.min(0.97, y / this.height)),
        ];
        this.config.strips[this.activeStrip].shape = null;
        this.invalidate();
        if (this.onGeometryChange) this.onGeometryChange();
        return;
      }
      this.updateHover(x, y);
    });

    const release = () => { this.dragHandle = -1; };
    canvas.addEventListener('pointerup', release);
    canvas.addEventListener('pointercancel', release);
    canvas.addEventListener('pointerleave', () => {
      release();
      this.hover = { strip: -1, led: -1 };
      if (this.onHover) this.onHover(-1, -1, null);
    });
  }

  localPoint(event) {
    const rect = this.canvas.getBoundingClientRect();
    return [event.clientX - rect.left, event.clientY - rect.top];
  }

  updateHover(x, y) {
    if (!this.geometry) return;
    let bestStrip = -1;
    let bestLed = -1;
    let bestDistance = 18;
    for (let s = 0; s < this.geometry.length; s++) {
      for (const led of this.geometry[s].leds) {
        const d = Math.hypot(led.x - x, led.y - y);
        if (d < bestDistance) {
          bestDistance = d;
          bestStrip = s;
          bestLed = led.index;
        }
      }
    }
    this.hover = { strip: bestStrip, led: bestLed };
    if (!this.onHover) return;
    if (bestStrip < 0 || !this.lastBytes) {
      this.onHover(-1, -1, null);
      return;
    }
    let offset = 0;
    for (let i = 0; i < bestStrip; i++) offset += this.counts[i] * 3;
    const at = offset + bestLed * 3;
    this.onHover(bestStrip, bestLed, [
      this.lastBytes[at], this.lastBytes[at + 1], this.lastBytes[at + 2],
    ]);
  }

  drawHandles() {
    const ctx = this.ctx;
    const pts = this.config.strips[this.activeStrip].pts;
    ctx.save();
    for (let i = 0; i < pts.length; i++) {
      const x = pts[i][0] * this.width;
      const y = pts[i][1] * this.height;
      const active = this.dragHandle === i;
      ctx.strokeStyle = active ? '#38bdf8' : 'rgba(244, 244, 245, 0.5)';
      ctx.fillStyle = 'rgba(9, 9, 11, 0.72)';
      ctx.lineWidth = active ? 2 : 1.25;
      ctx.beginPath();
      ctx.arc(x, y, active ? 9 : 7, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }
    ctx.restore();
  }
```

Add `hitHandle` to the `path.js` import at the top of the file:

```js
import { SHAPES, samplePath, placePixels, fitScale, hitHandle } from './path.js';
```

In `paint`, insert `this.drawHandles();` between `this.paintGrain();` and the `lastDrawMs` assignment, so handles sit above the grain.

- [ ] **Step 2: Add the fit readout to the page**

In `web/index.html`, inside `<div class="strip-panel">`, after the `<canvas id="view">` line:

```html
        <div class="fit-readout" v-if="s.hw.fit">
          <span>path <b>{{ s.hw.fit.pathM.toFixed(2) }} m</b></span>
          <span>strip <b>{{ s.hw.fit.stripM.toFixed(2) }} m</b></span>
          <span :class="{ over: s.hw.fit.overM > 0.02 }">
            {{ s.hw.fit.overM > 0 ? 'short by' : 'spare' }}
            <b>{{ Math.abs(s.hw.fit.overM).toFixed(2) }} m</b>
          </span>
        </div>
```

Add `fit: null` to the `hw` block in `web/state.js`. `StripView` does not write it directly; Task 8 wires the update.

- [ ] **Step 3: Style the readout**

Append to `web/style.css`:

```css
.fit-readout {
  display: flex;
  gap: 1.25rem;
  padding: 0.5rem 0.25rem 0;
  font-family: var(--mono);
  font-size: 0.72rem;
  color: var(--ink-dim);
  font-variant-numeric: tabular-nums;
}

.fit-readout b { color: var(--ink); font-weight: 500; }
.fit-readout .over b { color: var(--error); }
```

- [ ] **Step 4: Verify by hand**

Run: `npm start`, open `http://localhost:8000/?mode=live`.

Expected: four handles are visible on strip 0. Dragging one reshapes the strip and the pixels re-space along it. Hovering a pixel does nothing visible yet — the inspector arrives in Task 7. The readout stays empty until Task 8.

- [ ] **Step 5: Commit** (for the repo owner to run)

```bash
git add web/viz/StripView.js web/index.html web/style.css web/state.js
git commit -m "Let the strip be dragged into shape"
```

---

### Task 6: Surfaces

**Files:**
- Create: `web/viz/surface.js`
- Modify: `web/viz/StripView.js` (call the backplate, and split the render when a rod occludes)

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `SURFACES` — array of `{ id, name }` for the rail.
  - `paintSurface(ctx, id, width, height)` — fills the whole canvas.
  - `paintRod(ctx, width, height)` — draws the rod body only, used as an occluder between the two halves of a wrapped strip.
  - `occludes(id) -> boolean` — true for `rod`.

- [ ] **Step 1: Write `surface.js`**

Create `web/viz/surface.js`:

```js
// What the light lands on.
//
// Procedural rather than photographic, because a photograph has its own
// exposure baked in and compositing emitted light onto it fights that. A drawn
// surface is dim and neutral by construction, so the spill pass is the only
// thing that lights it.

export const SURFACES = [
  { id: 'room', name: 'Dark room' },
  { id: 'slat', name: 'Slat wall' },
  { id: 'rod', name: 'Bar rod' },
];

const SLAT_PITCH = 34;
const ROD_WIDTH = 52;

export function occludes(id) {
  return id === 'rod';
}

export function paintSurface(ctx, id, width, height) {
  if (id === 'slat') return paintSlat(ctx, width, height);
  if (id === 'rod') return paintRodBackdrop(ctx, width, height);
  return paintRoom(ctx, width, height);
}

function paintRoom(ctx, width, height) {
  const grad = ctx.createLinearGradient(0, 0, 0, height);
  grad.addColorStop(0, '#0d1014');
  grad.addColorStop(0.72, '#0a0c0f');
  grad.addColorStop(1, '#06070a');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, width, height);

  // The floor line. One stroke, but without it the room has no depth and the
  // spill has nothing to describe.
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.035)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, height * 0.78);
  ctx.lineTo(width, height * 0.78);
  ctx.stroke();
}

function paintSlat(ctx, width, height) {
  ctx.fillStyle = '#101216';
  ctx.fillRect(0, 0, width, height);
  for (let y = 0; y < height; y += SLAT_PITCH) {
    const grad = ctx.createLinearGradient(0, y, 0, y + SLAT_PITCH);
    grad.addColorStop(0, '#191d22');
    grad.addColorStop(0.62, '#14171c');
    grad.addColorStop(1, '#0a0c0f');
    ctx.fillStyle = grad;
    ctx.fillRect(0, y, width, SLAT_PITCH - 5);
    ctx.fillStyle = '#050607';
    ctx.fillRect(0, y + SLAT_PITCH - 5, width, 5);
  }
}

function paintRodBackdrop(ctx, width, height) {
  const grad = ctx.createLinearGradient(0, 0, 0, height);
  grad.addColorStop(0, '#0c0e12');
  grad.addColorStop(1, '#07080b');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, width, height);
}

export function paintRod(ctx, width, height) {
  const cx = width * 0.5;
  const grad = ctx.createLinearGradient(cx - ROD_WIDTH / 2, 0, cx + ROD_WIDTH / 2, 0);
  grad.addColorStop(0, '#0a0b0e');
  grad.addColorStop(0.32, '#2a2f38');
  grad.addColorStop(0.5, '#3c434f');
  grad.addColorStop(0.75, '#1b1f26');
  grad.addColorStop(1, '#070809');
  ctx.fillStyle = grad;
  ctx.fillRect(cx - ROD_WIDTH / 2, -10, ROD_WIDTH, height + 20);
}
```

- [ ] **Step 2: Wire the surface into `StripView`**

Add the import at the top of `web/viz/StripView.js`:

```js
import { paintSurface, paintRod, occludes } from './surface.js';
```

Replace the body of `clear()` with:

```js
  clear() {
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    paintSurface(this.ctx, this.config.surface, this.width, this.height);
  }
```

In `paint`, draw the rod between the strips when it occludes, so a strip wrapped around it has a back half:

```js
    let offset = 0;
    const rodAt = occludes(this.config.surface) ? 1 : -1;
    for (let i = 0; i < this.geometry.length; i++) {
      if (i === rodAt) paintRod(this.ctx, this.width, this.height);
      this.paintStrip(this.geometry[i], bytes, offset);
      offset += this.counts[i] * 3;
    }
```

- [ ] **Step 3: Verify by hand**

Run: `npm start`, open the page, and switch surfaces from the console for now:

```js
document.getElementById('view').__view.config.surface = 'slat';
```

For this to work, add `canvas.__view = this;` as the last line of the `StripView` constructor. Keep it — it is also how the bench strip and the rail reach the view in Tasks 7 and 8.

Expected: the slat wall shows horizontal boards and the spill lights the board faces near the strip. The rod surface draws a vertical bar with strip 1 behind it.

- [ ] **Step 4: Commit** (for the repo owner to run)

```bash
git add web/viz/surface.js web/viz/StripView.js
git commit -m "Give the light something to land on"
```

---

### Task 7: The bench strip

**Files:**
- Create: `web/viz/BenchStrip.js`
- Modify: `web/index.html` (add the bench canvas and the inspector readout inside `.strip-panel`)
- Modify: `web/style.css`

**Interfaces:**
- Consumes: `expose` from `camera.js`; `profileById` from `profiles.js`.
- Produces:
  - `class BenchStrip { constructor(canvas, counts); resize(); paint(bytes, config); setHover(stripIndex, ledIndex); }` — `config` is the same `state.hw` object the view reads.
  - `BENCH_HEIGHT_PER_STRIP` — number, pixels of canvas height each strip's lane occupies.

- [ ] **Step 1: Write `BenchStrip.js`**

Create `web/viz/BenchStrip.js`:

```js
// The debugging half: the same pixels, straightened out and indexed.
//
// The stage answers "does this look right". This answers "which pixel is
// wrong", which the stage cannot, because a posed strip has no readable index
// anywhere on it. Both draw from the same byte array, so a disagreement
// between them is a bug in one of these two files and nowhere else.

import { expose } from './camera.js';
import { profileById } from './profiles.js';

export const BENCH_HEIGHT_PER_STRIP = 62;

const PAD = 10;
const TICK_EVERY = 10;
const MIN_TICK_SPACING = 28;

export class BenchStrip {
  constructor(canvas, counts) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.counts = counts;
    this.width = 0;
    this.dpr = 1;
    this.hover = { strip: -1, led: -1 };
    this.resize();
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    this.dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.width = Math.max(320, Math.round(rect.width));
    this.height = BENCH_HEIGHT_PER_STRIP * this.counts.length;
    this.canvas.width = this.width * this.dpr;
    this.canvas.height = this.height * this.dpr;
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
  }

  setHover(stripIndex, ledIndex) {
    this.hover = { strip: stripIndex, led: ledIndex };
  }

  paint(bytes, config) {
    const ctx = this.ctx;
    ctx.fillStyle = '#09090b';
    ctx.fillRect(0, 0, this.width, this.height);

    let offset = 0;
    for (let s = 0; s < this.counts.length; s++) {
      this.paintLane(ctx, s, bytes, offset, config);
      offset += this.counts[s] * 3;
    }
  }

  paintLane(ctx, stripIndex, bytes, offset, config) {
    const count = this.counts[stripIndex];
    const top = stripIndex * BENCH_HEIGHT_PER_STRIP;
    const span = this.width - PAD * 2;
    const cell = span / count;
    const profile = profileById(config.strips[stripIndex].profile);
    const k = config.intensity;

    for (let i = 0; i < count; i++) {
      const at = offset + i * 3;
      const [r, g, b] = expose(bytes[at] * k, bytes[at + 1] * k, bytes[at + 2] * k, config.ev);
      const x = PAD + i * cell;

      ctx.fillStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
      ctx.fillRect(x, top + 8, Math.max(cell - (cell > 4 ? 1 : 0), 0.6), 24);

      // Luma, so a pixel that is bright but the wrong hue still reads as
      // present. Rec. 709 weights, because that is what the eye does.
      const luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
      ctx.fillStyle = 'rgba(244, 244, 245, 0.32)';
      ctx.fillRect(x, top + 54 - luma * 16, Math.max(cell - 1, 0.6), luma * 16);
    }

    ctx.strokeStyle = 'rgba(244, 244, 245, 0.14)';
    ctx.fillStyle = '#a1a1aa';
    ctx.font = '9px ui-monospace, SFMono-Regular, Menlo, monospace';
    for (let i = 0; i < count; i += TICK_EVERY) {
      const x = PAD + i * cell;
      ctx.beginPath();
      ctx.moveTo(x, top + 34);
      ctx.lineTo(x, top + 39);
      ctx.stroke();
      if (cell * TICK_EVERY > MIN_TICK_SPACING) ctx.fillText(String(i), x + 2, top + 47);
    }

    ctx.fillStyle = '#a1a1aa';
    ctx.fillText(`${profile.name} · ${count} px`, PAD, top + 6);

    if (this.hover.strip === stripIndex && this.hover.led >= 0) {
      const x = PAD + this.hover.led * cell;
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 1;
      ctx.strokeRect(x - 1, top + 6, Math.max(cell, 2) + 2, 28);
    }
  }
}
```

- [ ] **Step 2: Add the bench canvas and inspector to the page**

In `web/index.html`, inside `<div class="strip-panel">`, after the fit readout added in Task 5:

```html
        <canvas id="bench" class="bench" aria-label="Pixel indices and values"></canvas>
        <div class="inspector" v-if="s.hw.inspect">
          <span>index <b>{{ s.hw.inspect.index }}</b></span>
          <span>strip <b>{{ s.hw.inspect.strip }}</b></span>
          <span>rgb <b>{{ s.hw.inspect.hex }}</b>
            <i class="inspector-dot" :style="{ background: s.hw.inspect.hex }"></i></span>
          <span>at <b>{{ s.hw.inspect.mm }} mm</b></span>
        </div>
```

Add `inspect: null` to the `hw` block in `web/state.js`.

- [ ] **Step 3: Style them**

Append to `web/style.css`:

```css
.bench {
  display: block;
  width: 100%;
  margin-top: 0.75rem;
  border-radius: 6px;
  background: var(--bg);
  border: 1px solid var(--panel-edge);
}

.inspector {
  display: flex;
  flex-wrap: wrap;
  gap: 1.25rem;
  padding: 0.5rem 0.25rem 0;
  font-family: var(--mono);
  font-size: 0.72rem;
  color: var(--ink-dim);
  font-variant-numeric: tabular-nums;
}

.inspector b { color: var(--ink); font-weight: 500; }

.inspector-dot {
  display: inline-block;
  width: 9px;
  height: 9px;
  border-radius: 2px;
  vertical-align: -1px;
  margin-left: 0.35rem;
}
```

- [ ] **Step 4: Verify by hand**

The bench is not painted yet — Task 8 owns the wiring that calls it. Confirm only that the page still loads, the canvas element exists and nothing in the console errors.

- [ ] **Step 5: Commit** (for the repo owner to run)

```bash
git add web/viz/BenchStrip.js web/index.html web/style.css web/state.js
git commit -m "Add the ruler that says which pixel is wrong"
```

---

### Task 8: The app shell

**Why this task exists, and what went wrong the first time.** An earlier attempt at this task
kept the page's existing component CSS and added a frame around it. The result still read as the
old page, because a design lives in its type scale, density, tokens and component styling — not
in its outer grid. The owner's verdict was blunt and correct: the mockup was miles ahead of what
that produced.

So this task inverts the direction. **The mockup is the base. The existing page's functionality
is what gets ported into it.** Not the other way round.

**The design source is a real file in the repo:** `web/design/mockup-reference.html`. It is a
standalone, self-contained page — the interactive mockup the owner approved, with fake pixel
data. Read it in full before writing anything. Its `<style>` block is the design system this task
implements: the palette, the two typefaces and their roles, the topbar, the rail groups, the
profile cards, the chips, the range inputs, the stage, the readout overlay, the bench footer and
the legend. Its JavaScript is **not** in scope — the real renderer already exists under
`web/viz/` and is wired in Tasks 9 and 10.

**Files:**
- Modify: `web/index.html` — rebuilt from the mockup's structure
- Rewrite: `web/style.css` — in the mockup's design language
- Keep, do not edit: `web/design/mockup-reference.html`
- Do not touch: `web/main.js`, `web/app.js`, `web/live.js`, anything under `web/viz/`

**Interfaces:**
- Consumes: every binding already in `web/index.html`, and every class name `web/live.js` writes.
- Produces: `#view`, `#bench`, `#spectrum`, `#live-state`, `#error`, `#t-count`, `#t-pitch`,
  `#t-draw`, `#t-frame`, `#t-power`, `.stage`, `.rail`, `.diagnostics`.

- [ ] **Step 1: Read the mockup and take its design system**

Lift these from `web/design/mockup-reference.html` into `web/style.css` as the page's own tokens
and base rules, adapting names to the ones the rest of the page already uses where they overlap:

- The full palette, including the panel/raised/line separation the old stylesheet lacked.
- Both typefaces and their roles: a UI face for labels and controls, a monospace face for every
  number, measurement and uppercase micro-label. Load them the way the mockup does, and give each
  a real fallback stack so the page still reads if the font host is unreachable.
- The type scale, including the 9-10px uppercase letterspaced section labels.
- `font-variant-numeric: tabular-nums` everywhere digits sit in a column.
- The control styling: segmented buttons, chips, profile cards with swatches, and the range input
  styling for all three of `-webkit-slider-runnable-track`, `-moz-range-track` and both thumbs.
- Focus-visible outlines on every interactive element.

The existing `:root` tokens (`--bg`, `--panel`, `--panel-edge`, `--ink`, `--ink-dim`, `--accent`,
`--accent-glow`, `--error`, `--mono`) must keep working, because `web/viz/BenchStrip.js` and
`web/viz/StripView.js` hand-shadow four of them in canvas literals. Keep those names and give
them the mockup's values where they correspond; add the mockup's extra tokens alongside.

**One deliberate divergence from the mockup:** the mockup's accent is a solder orange. This page's
`--accent` is `#38bdf8`, and two canvas files hard-code that value with comments saying they track
it by hand. Keep `#38bdf8` as the accent. Changing it would silently orphan those literals, and
the blue reads correctly against the LED colours anyway.

- [ ] **Step 2: Rebuild the page structure**

Follow the mockup's layout, with a third column the mockup did not need, because this page has
diagnostics the mockup had no equivalent for:

```
+------------------------------------------------------------------+
| GlitchGlimmer  fastled . wasm  [Recording|Live]       telemetry   |
+------------+---------------------------------+-------------------+
|  rail      |   stage (#view) + overlay        |  diagnostics      |
|  268px     |                                  |  320px            |
|            +---------------------------------+                    |
|            |   bench footer (#bench)          |                   |
|            +---------------------------------+                    |
|            |   transport (recording mode)     |                   |
+------------+---------------------------------+-------------------+
```

The topbar, rail, stage, overlay and bench footer come from the mockup essentially as they are.
The diagnostics column is new and must be built in the same language — same section labels, same
monospace numbers, same density. It is not a place to fall back on the old styling.

Rail groups, in the mockup's order: **Strip** (a strip selector plus profile cards), **Shape**,
**Scene** (surface, scale), **Camera** (exposure, spill, grain), **Hardware** (pixel size, glow
size, brightness). Task 9 fills these with real bindings; for now carry the existing
`.hw-controls` inputs into the Hardware group so nothing stops working mid-plan, and leave the
other groups' markup in place with the mockup's static content.

Diagnostics column contents, all carried over with their bindings intact:
- Recording mode: the scene, mood and time cells.
- Live mode: scene, mood now, mood of window, BPM, the level meter, the beat lamp, the
  `#spectrum` canvas, the microphone and demo buttons, the source note, and `#live-state`.

Transport under the stage, recording mode only: play/pause, the scrub range, the scenario button
row and the scenario note.

- [ ] **Step 3: Restyle what `live.js` injects**

This is the step most likely to be skipped and it is the one that decides whether the page looks
finished. `web/live.js` builds DOM at runtime and must not be modified, so its class names are a
fixed contract. Every one of these needs a rule in the new language, sized and spaced like the
mockup's rail and telemetry rather than like the old page:

`.state-group`, `.state-row`, `.state-name`, `.state-num`, `.state-note`, `.meter`,
`.meter-fill`, `.meter-mark`, `.tune-row`, `.tune-name`, `.tune-value`, `.button-row`, `.lamp`,
`.lamp.on`, `.spectrum`, `.hud`, `.hud-cell`, `.hud-label`, `.hud-value`, `.controls`,
`.scenarios`, `.note`, `.error`.

Numbers in `.state-num`, `.tune-value`, `.hud-value` and the telemetry cells take the monospace
face and tabular figures. Labels in `.state-name`, `.hud-label` and section headings take the
small uppercase letterspaced treatment.

- [ ] **Step 4: Two bugs the previous attempt found — do not reintroduce them**

**The stage must not size itself.** `#view` is `position: absolute; inset: 0` inside a
`position: relative` stage, and the app frame has a definite `height: 100dvh`, not `min-height`.
With a percentage height against an indefinite grid row the canvas contributes its own height
back to the row it is measuring, `StripView.resize()` reads the grown rect, and the page gains a
viewport on every resize. Verify at a 1400x900 viewport that the frame measures 900 tall and
stays there across two reloads.

**Copy snapshot and Record are built by `live.js`.** `buildState()` creates that button row inside
`#live-state` with its own handlers. Do not add a second pair in the markup — the old page had
one and it rendered twice.

- [ ] **Step 5: Verify every control survived**

Open `http://localhost:8000/?mode=live` and `http://localhost:8000/?mode=recording`. Every item
below must be present and driven. A missing control fails this task however good the design is.

Live: scene, mood now, mood of window, BPM, level meter, beat lamp, spectrum canvas, microphone
and demo buttons, source note, `#live-state` rows with their tuning sliders, and the Copy
snapshot and Record buttons `live.js` builds. Recording: scene, mood, time, play/pause, scrub,
scenario buttons, scenario note. Both: the hardware inputs, the stage canvas, the bench canvas,
the error banner.

The telemetry cells and power readout stay as em dashes until Task 10. Expected.

- [ ] **Step 6: Commit** (for the repo owner to run)

```bash
git add web/index.html web/style.css web/design/mockup-reference.html
git commit -m "Rebuild the page in the bench design instead of around it"
```

---

### Task 9: Rail controls, state migration and wiring

Unchanged in substance from the original plan: build `web/viz/hwStore.js`, extend `state.hw`,
fill the rail, and connect the stage, the bench and the inspector. The difference is that the
rail markup goes inside Task 8's `.rail`, and profiles render as cards rather than a dropdown,
because the swatch and the pitch are what make one strip type recognisable from another.

**Files:**
- Modify: `web/state.js` (extend `hw`)
- Modify: `web/index.html` (fill `.rail`; nothing outside it)
- Modify: `web/style.css` (rail group, profile card and chip rules)
- Modify: `web/main.js` (imports, data, computed, methods, mounted wiring)
- Modify: `web/viz/StripView.js` (call `onFrame` after each paint)
- Create: `web/viz/hwStore.js`

**Interfaces:**
- Consumes: `PROFILES`, `profileById`, `migratePreset` from `profiles.js`; `SURFACES` from
  `surface.js`; `SHAPES` and `defaultPose` from `path.js`; `StripView`; `BenchStrip`.
- Produces:
  - `defaultHw(counts) -> object`
  - `loadHw(counts) -> object`
  - `saveHw(hw)`
  - `bindView(view, bench, hw, state) -> sync()`

- [ ] **Step 1: Extend `state.js`**

Replace the `hw` block in `web/state.js` with:

```js
  hw: {
    activeStrip: 0,
    surface: 'room',
    scale: 'fit',
    stageWidthM: 3,
    ev: 0,
    spill: 1,
    grain: 0.14,
    pixelSize: 1.0,
    glowSize: 1.0,
    intensity: 1.0,
    strips: [],
    fit: null,
    inspect: null,
    drawMs: 0,
  },
```

- [ ] **Step 2: Write `hwStore.js`**

```js
// One place that owns the settings object, so the rail, the renderer and
// localStorage cannot disagree about its shape.
//
// state.hw is what Vue renders and mutates. view.config is what the renderer
// reads every frame. They are the same object: assigning the reactive one into
// the view means a slider move shows on the next frame with no watcher, and
// there is no second copy to fall out of date.

import { migratePreset, PROFILES } from './profiles.js';
import { defaultPose } from './path.js';

const STORAGE_KEY = 'gg.hw.v1';

export function defaultHw(counts) {
  return {
    activeStrip: 0,
    surface: 'room',
    scale: 'fit',
    stageWidthM: 3,
    ev: 0,
    spill: 1,
    grain: 0.14,
    pixelSize: 1.0,
    glowSize: 1.0,
    intensity: 1.0,
    // defaultPose, not SHAPES. A SHAPES entry spans the whole stage; these two
    // poses have to leave room for each other. Task 4 fixed exactly this bug
    // once already, which is why the poses live in path.js.
    strips: counts.map((_, i) => ({ profile: PROFILES[0].id, shape: null, pts: defaultPose(i) })),
    fit: null,
    inspect: null,
    drawMs: 0,
  };
}

export function loadHw(counts) {
  const base = defaultHw(counts);
  let stored = null;
  try {
    stored = JSON.parse(window.localStorage.getItem(STORAGE_KEY) || 'null');
  } catch (err) {
    stored = null;
  }
  if (!stored) return base;

  for (const key of ['surface', 'scale', 'stageWidthM', 'ev', 'spill', 'grain',
                     'pixelSize', 'glowSize', 'intensity']) {
    if (typeof stored[key] === typeof base[key]) base[key] = stored[key];
  }

  // A strip list saved against a different pixel count is still usable: what was
  // stored is the pose, and the count comes from the firmware either way.
  if (Array.isArray(stored.strips)) {
    for (let i = 0; i < base.strips.length; i++) {
      const s = stored.strips[i];
      if (!s) continue;
      if (typeof s.profile === 'string') base.strips[i].profile = s.profile;
      if (Array.isArray(s.pts) && s.pts.length === 4) {
        base.strips[i].pts = s.pts.map((p) => [Number(p[0]), Number(p[1])]);
        base.strips[i].shape = typeof s.shape === 'string' ? s.shape : null;
      }
    }
  } else if (typeof stored.preset === 'string') {
    const migrated = migratePreset(stored.preset);
    for (const strip of base.strips) strip.profile = migrated;
  }

  return base;
}

export function saveHw(hw) {
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify({
      surface: hw.surface, scale: hw.scale, stageWidthM: hw.stageWidthM,
      ev: hw.ev, spill: hw.spill, grain: hw.grain,
      pixelSize: hw.pixelSize, glowSize: hw.glowSize, intensity: hw.intensity,
      strips: hw.strips.map((s) => ({ profile: s.profile, shape: s.shape, pts: s.pts })),
    }));
  } catch (err) {
    // Private windows and blocked site data both throw here. A posed path that
    // does not survive a reload is a small loss; a page that will not load is
    // not.
  }
}

export function bindView(view, bench, hw, state) {
  view.config = hw;
  view.activeStrip = hw.activeStrip;

  view.onGeometryChange = () => {
    hw.fit = view.fit(view.activeStrip);
    saveHw(hw);
  };

  view.onHover = (stripIndex, ledIndex, rgb) => {
    bench.setHover(stripIndex, ledIndex);
    if (stripIndex < 0 || !rgb) {
      state.hw.inspect = null;
      return;
    }
    const profile = hw.strips[stripIndex].profile;
    const pitch = (PROFILES.find((p) => p.id === profile) || PROFILES[0]).pitch;
    state.hw.inspect = {
      strip: stripIndex,
      index: ledIndex,
      hex: '#' + rgb.map((v) => v.toString(16).padStart(2, '0')).join('').toUpperCase(),
      mm: Math.round((ledIndex + 0.5) * pitch),
    };
  };

  view.onFrame = (bytes) => {
    bench.paint(bytes, hw);
    hw.drawMs = Math.round(view.lastDrawMs * 10) / 10;
  };

  return function sync() {
    view.activeStrip = hw.activeStrip;
    view.invalidate();
    hw.fit = view.fit(hw.activeStrip);
    saveHw(hw);
  };
}
```

- [ ] **Step 3: Have `StripView` announce each frame**

Add `this.onFrame = null;` to the constructor, and at the end of `paint`, after
`this.lastDrawMs = ...`:

```js
    if (this.onFrame) this.onFrame(bytes);
```

- [ ] **Step 4: Fill the rail**

Replace the placeholder `.hw-controls` div inside `.rail` with:

```html
    <section class="rail-group">
      <h2>Strip</h2>
      <div class="hw-group">
        <label for="hw-strip">Which strip</label>
        <select id="hw-strip" v-model.number="s.hw.activeStrip" @change="syncHw">
          <option v-for="(strip, i) in s.hw.strips" :key="i" :value="i">Strip {{ i }}</option>
        </select>
      </div>

      <div class="profiles" role="group" aria-label="LED profile"
           v-if="s.hw.strips[s.hw.activeStrip]">
        <button v-for="p in profiles" :key="p.id" type="button" class="profile"
                :aria-pressed="s.hw.strips[s.hw.activeStrip].profile === p.id"
                @click="selectProfile(p.id)">
          <span class="swatch" :style="{ background: swatchFor(p) }"></span>
          <span class="profile-name">{{ p.name }}</span>
          <span class="profile-pitch">{{ p.pitch.toFixed(1) }}mm</span>
        </button>
      </div>
      <p class="note">{{ profileNote }}</p>
    </section>

    <section class="rail-group">
      <h2>Shape</h2>
      <div class="chips" role="group" aria-label="Path shape">
        <button v-for="name in shapes" :key="name" type="button" class="chip"
                :aria-pressed="s.hw.strips[s.hw.activeStrip] &&
                               s.hw.strips[s.hw.activeStrip].shape === name"
                @click="applyShape(name)">{{ name }}</button>
      </div>
      <p class="note">Presets move the handles. Drag any handle on the stage to freehand it.</p>
    </section>

    <section class="rail-group">
      <h2>Scene</h2>
      <div class="hw-group">
        <label for="hw-surface">Surface</label>
        <select id="hw-surface" v-model="s.hw.surface" @change="syncHw">
          <option v-for="su in surfaces" :key="su.id" :value="su.id">{{ su.name }}</option>
        </select>
      </div>
      <div class="hw-group">
        <label for="hw-scale">Scale</label>
        <select id="hw-scale" v-model="s.hw.scale" @change="syncHw">
          <option value="fit">Fit to path</option>
          <option value="true">True scale</option>
        </select>
      </div>
      <div class="hw-group" v-if="s.hw.scale === 'true'">
        <label for="hw-stage-width">Stage width ({{ s.hw.stageWidthM.toFixed(1) }} m)</label>
        <input type="range" id="hw-stage-width" min="0.5" max="8" step="0.1"
               v-model.number="s.hw.stageWidthM" @input="syncHw">
      </div>
    </section>

    <section class="rail-group">
      <h2>Camera</h2>
      <div class="hw-group">
        <label for="hw-ev">Exposure ({{ s.hw.ev > 0 ? '+' : '' }}{{ s.hw.ev.toFixed(1) }} EV)</label>
        <input type="range" id="hw-ev" min="-2" max="3" step="0.1" v-model.number="s.hw.ev">
      </div>
      <div class="hw-group">
        <label for="hw-spill">Room spill</label>
        <input type="range" id="hw-spill" min="0" max="2.2" step="0.05" v-model.number="s.hw.spill">
      </div>
      <div class="hw-group">
        <label for="hw-grain">Sensor grain</label>
        <input type="range" id="hw-grain" min="0" max="0.6" step="0.01" v-model.number="s.hw.grain">
      </div>
      <p class="note">Push exposure until the peaks clip to white. That blowout is what reads as
        light rather than paint.</p>
    </section>

    <section class="rail-group">
      <h2>Hardware</h2>
      <div class="hw-group">
        <label for="hw-pixel-size">Pixel size</label>
        <input type="range" id="hw-pixel-size" min="0.2" max="2.0" step="0.1"
               v-model.number="s.hw.pixelSize" @input="syncHw">
      </div>
      <div class="hw-group">
        <label for="hw-glow-size">Glow size</label>
        <input type="range" id="hw-glow-size" min="0.1" max="5.0" step="0.1"
               v-model.number="s.hw.glowSize" @input="syncHw">
      </div>
      <div class="hw-group">
        <label for="hw-intensity">Brightness</label>
        <input type="range" id="hw-intensity" min="0.1" max="3.0" step="0.1"
               v-model.number="s.hw.intensity">
      </div>
    </section>
```

`ev`, `spill`, `grain` and `intensity` carry no `@change="syncHw"` because they change colour,
not geometry, and the renderer reads them from the same object every frame. The others change
geometry and must invalidate the cached spline.

- [ ] **Step 5: Style the rail additions**

```css
.rail-group {
  border-bottom: 1px solid var(--panel-edge);
  padding-bottom: 1rem;
  margin-bottom: 1rem;
}
.rail-group:last-child { border-bottom: 0; }
.rail-group > h2 {
  margin: 0 0 0.75rem;
  font-family: var(--mono);
  font-size: 0.6rem;
  letter-spacing: 0.16em;
  text-transform: uppercase;
  color: var(--ink-dim);
  font-weight: 500;
}

.profiles { display: grid; gap: 4px; }
.profile {
  display: grid;
  grid-template-columns: 22px 1fr auto;
  align-items: center;
  gap: 0.55rem;
  padding: 0.45rem 0.5rem;
  border: 1px solid transparent;
  border-radius: 6px;
  background: transparent;
  color: var(--ink-dim);
  font: inherit;
  font-size: 0.78rem;
  text-align: left;
  cursor: pointer;
}
.profile:hover { background: var(--panel-edge); }
.profile[aria-pressed="true"] {
  background: var(--panel-edge);
  border-color: var(--accent);
  color: var(--ink);
}
.profile:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }
.profile-pitch { font-family: var(--mono); font-size: 0.62rem; color: var(--ink-dim); }
.swatch { height: 18px; border-radius: 3px; }

.chips { display: flex; flex-wrap: wrap; gap: 5px; }
.chip {
  border: 1px solid var(--panel-edge);
  background: var(--bg);
  color: var(--ink-dim);
  font: inherit;
  font-size: 0.72rem;
  padding: 0.35rem 0.55rem;
  border-radius: 5px;
  cursor: pointer;
}
.chip[aria-pressed="true"] { border-color: var(--accent); color: var(--ink); }
.chip:focus-visible { outline: 2px solid var(--accent); outline-offset: 1px; }
```

- [ ] **Step 6: Wire the Vue app**

Imports for `web/main.js`:

```js
import { PROFILES, profileById } from './viz/profiles.js';
import { SURFACES } from './viz/surface.js';
import { SHAPES } from './viz/path.js';
import { BenchStrip } from './viz/BenchStrip.js';
import { loadHw, bindView } from './viz/hwStore.js';
```

Add to `data()`: `profiles: PROFILES`, `surfaces: SURFACES`, `shapes: Object.keys(SHAPES)`.

Add a `computed` block:

```js
  computed: {
    profileNote() {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      return strip ? profileById(strip.profile).note : '';
    },
  },
```

Add methods:

```js
    syncHw() {
      if (this.syncView) this.syncView();
    },
    selectProfile(id) {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      strip.profile = id;
      this.syncHw();
    },
    applyShape(name) {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      strip.shape = name;
      strip.pts = SHAPES[name].map((p) => p.slice());
      this.syncHw();
    },
    // Drawn from the profile's own casing rather than a per-type image, for the
    // same reason the renderer has no per-type branch: a new strip type should
    // be a row in the table and nothing else.
    swatchFor(p) {
      if (p.casing === 'cob') return 'linear-gradient(90deg,#f6d9a8,#ffe9c4)';
      if (p.casing === 'sleeve') return 'linear-gradient(90deg,#3b4049,#5a6170)';
      if (p.casing === 'pip') return 'repeating-linear-gradient(90deg,#2a2f38 0 6px,#8ad6ff 6px 7px)';
      if (p.casing === 'bulb') return 'repeating-linear-gradient(90deg,#20252c 0 8px,#ffd9a8 8px 11px)';
      return 'repeating-linear-gradient(90deg,#1b1f26 0 4px,#d9e4ff 4px 6px)';
    },
```

Delete the `watch` on `s.hw` that dispatches a `resize` event — the view reads the same object
directly now, and a full resize per slider tick is wasted work.

Add to `mounted`, after `live.init()`:

```js
    const stageCanvas = document.getElementById('view');
    const benchCanvas = document.getElementById('bench');
    const attach = () => {
      const view = stageCanvas.__view;
      if (!view) return false;
      Object.assign(this.s.hw, loadHw(view.counts));
      const bench = new BenchStrip(benchCanvas, view.counts);
      this.syncView = bindView(view, bench, this.s.hw, this.s);
      this.syncView();
      window.addEventListener('resize', () => bench.resize());
      return true;
    };
    if (!attach()) {
      const poll = setInterval(() => { if (attach()) clearInterval(poll); }, 100);
    }
```

This is the one place the plan accepts a poll: `live.js` builds its view inside an async
`start()` and nothing signals when. If `live.js` later gains a ready event, replace the poll
with it.

- [ ] **Step 7: Verify by hand, both modes**

Switching strip moves the handles. Changing LED type visibly changes pitch and diffusion, with
COB rendering as a continuous bar. Shape chips reposition the handles, and dragging clears the
pressed state. Surface, scale, exposure, spill and grain all take effect immediately. The fit
readout and the bench update live. Hovering a stage pixel highlights the same cell on the bench
and fills the inspector. A reload restores the posed paths and every setting.

- [ ] **Step 8: Commit** (for the repo owner to run)

```bash
git add web/state.js web/index.html web/style.css web/main.js web/viz/hwStore.js web/viz/StripView.js
git commit -m "Wire the rail to the renderer and remember the pose"
```

---

### Task 10: Telemetry, power, draw budget and the inventory check

**Files:**
- Modify: `web/main.js` (populate the four telemetry cells and the power readout)
- Modify: `web/live.js` — the one authorised exception, a single field added to the telemetry
  sample. If that object already spreads an open set of fields, no change is needed; check first.

**Interfaces:**
- Consumes: `view.lastDrawMs`, `view.counts`, `state.hw`, `profileById`.
- Produces: `drawMs` as a tracked range in `Trace.summary()` and in `?debug=1` output.

- [ ] **Step 1: Fill the telemetry cells**

These are plain DOM ids rather than Vue bindings, because they update every frame and a reactive
write per frame costs more than the readout is worth.

In `web/main.js`, inside `attach()`, after `bindView` returns:

```js
      const cells = {
        count: document.getElementById('t-count'),
        pitch: document.getElementById('t-pitch'),
        draw: document.getElementById('t-draw'),
        frame: document.getElementById('t-frame'),
        power: document.getElementById('t-power'),
      };
      let lastFrameAt = 0;
      let frameMs = 16;
      const paintBench = view.onFrame;

      view.onFrame = (bytes) => {
        paintBench(bytes);

        const now = performance.now();
        if (lastFrameAt) frameMs += ((now - lastFrameAt) - frameMs) * 0.08;
        lastFrameAt = now;

        const strip = this.s.hw.strips[this.s.hw.activeStrip];
        const profile = profileById(strip ? strip.profile : '');

        // 60 mA per pixel at full white is the number every WS2812 supply is
        // sized against, so the readout is in the unit the decision gets made
        // in rather than in normalised brightness.
        let duty = 0;
        for (let i = 0; i + 2 < bytes.length; i += 3) {
          duty += (bytes[i] + bytes[i + 1] + bytes[i + 2]) / 765;
        }
        const amps = (duty * 60) / 1000;

        cells.count.textContent = view.counts.join(' + ');
        cells.pitch.textContent = profile.pitch.toFixed(1) + ' mm';
        cells.draw.textContent = this.s.hw.drawMs.toFixed(1) + ' ms';
        cells.frame.textContent = frameMs.toFixed(1) + ' ms';
        cells.power.textContent =
          'draw ≈ ' + amps.toFixed(2) + ' A @ 5 V · ' + (amps * 5).toFixed(1) + ' W';
      };
```

- [ ] **Step 2: Add draw time to the telemetry sample**

Find the `trace.frame({...})` call in `web/live.js` and add one field:

```js
      drawMs: leds.lastDrawMs,
```

`Trace.track` folds every numeric field into a min/max automatically and `drawMs` is not in
`Trace.SKIP`, so it appears in `summary()` with no other change.

- [ ] **Step 3: Measure against the budget**

Open `http://localhost:8000/?mode=live&debug=1`, start the microphone, let it run thirty
seconds, and read the `[gg]` summary line. Expected: `drawMs` high water under 8.

If it is over, apply the sprite fallback in `fillGlowBuffer`: build one white radial-gradient
sprite at construction, sized `sigmaPx * 2.6 * 2` square and rebuilt in `invalidate()`; per pixel
`drawImage` it under `globalCompositeOperation = 'lighter'`, then tint the whole buffer by
filling it with the per-pixel colour under `'multiply'`. Re-measure before moving on.

- [ ] **Step 4: Walk the inventory**

The acceptance test for the whole plan. Every item present and updating, in each mode.

Live mode:
- [ ] scene
- [ ] mood now
- [ ] mood of window
- [ ] BPM
- [ ] level meter fills
- [ ] beat lamp flashes
- [ ] spectrum canvas draws
- [ ] microphone / demo buttons switch source
- [ ] source note text
- [ ] Copy snapshot writes to the clipboard
- [ ] record toggle changes label
- [ ] `#live-state` rows render, with their tuning sliders working

Recording mode:
- [ ] scene
- [ ] mood
- [ ] time
- [ ] play / pause
- [ ] scrub
- [ ] scenario buttons load their scenario
- [ ] scenario note text
- [ ] space, left arrow and right arrow keys still work

Both:
- [ ] `?debug=1` prints scene and mood change lines
- [ ] the rail renders every control from Task 9
- [ ] the four telemetry cells and the power readout update
- [ ] the bench, fit readout and inspector update

- [ ] **Step 5: Run the full test suite**

Run: `npm test`
Expected: PASS, 24 tests.

- [ ] **Step 6: Commit** (for the repo owner to run)

```bash
git add web/main.js web/live.js
git commit -m "Report the renderer's own numbers alongside the firmware's"
```

---

## After the build

Update `_architecture/plans/2026-09-18-led-strip-visualizer.md` under `## Implementation
deviations` with everything that diverged, then write `web/viz/CONTEXT.md` describing the
four-input model and why profiles hold millimetres. Both are `jookoi-paper-trail` operations —
invoke the skill rather than writing the files by hand.
