import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  SHAPES, samplePath, sampleWrappedPath, pointAt, pointAtSmooth, angleAt, placePixels, fitScale, hitHandle,
  DEFAULT_POSES, defaultPose,
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

test('an empty or one-point path remains renderable during a drag', () => {
  for (const pts of [[], [[0.25, 0.5]]]) {
    const path = samplePath(pts, 1000, 400);
    assert.equal(path.poly.length, 601);
    for (const [x, y] of path.poly) {
      assert.ok(Number.isFinite(x) && Number.isFinite(y));
    }
  }
});

test('a bent path is longer than the straight one', () => {
  const straight = samplePath(STRAIGHT, 1000, 400).total;
  const bent = samplePath(SHAPES.zigzag, 1000, 400).total;
  assert.ok(bent > straight, `${bent} should exceed ${straight}`);
});

test('wrapped paths carry a longer software strip without non-finite points', () => {
  const path = sampleWrappedPath(STRAIGHT, 1000, 400, 3200);
  assert.ok(path.total >= 3199, `got ${path.total}`);
  assert.ok(path.poly.length > 600, 'long path should contain repeated lanes');
  for (const [x, y] of path.poly) {
    assert.ok(Number.isFinite(x) && Number.isFinite(y));
    assert.ok(x >= 0 && x <= 1000 && y >= 0 && y <= 400, 'wrapped path stays on stage');
  }
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

test('the default poses keep the two strips clear of each other', () => {
  const [first, second] = DEFAULT_POSES;
  const lowestOfFirst = Math.max(...first.map((p) => p[1]));
  const highestOfSecond = Math.min(...second.map((p) => p[1]));
  assert.ok(lowestOfFirst < highestOfSecond, 'strip 0 must sit entirely above strip 1');

  const spanOf = (pts) => Math.max(...pts.map((p) => p[0])) - Math.min(...pts.map((p) => p[0]));
  assert.ok(spanOf(first) > 0.5, 'strip 0 must span across the stage');
  assert.ok(spanOf(second) > 0.5, 'strip 1 must span across the stage');
});

test('defaultPose hands back a copy, not the shared array', () => {
  const pose = defaultPose(0);
  pose[0][1] = 0.99;
  assert.notEqual(DEFAULT_POSES[0][0][1], 0.99);
});

test('pointAtSmooth interpolates between vertices and angleAt follows the tangent', () => {
  const path = { poly: [[0, 0], [10, 0], [10, 10]], cum: [0, 10, 20], total: 20 };
  const mid = pointAtSmooth(path, 5);
  assert.deepEqual(mid, [5, 0]);
  assert.ok(Math.abs(angleAt(path, 5, 2)) < 1e-9);
  assert.ok(Math.abs(angleAt(path, 15, 2) - Math.PI / 2) < 1e-9);
});

test('placePixels reports each pixel\'s distance along the path and its heading', () => {
  const path = { poly: [[0, 0], [100, 0]], cum: [0, 100], total: 100 };
  const leds = placePixels(path, 5, 10);
  assert.equal(leds[2].at, 25);
  assert.ok(Math.abs(leds[2].angle) < 1e-9);
});

test('placePixels keeps a constant chord spacing along a coarsely sampled curve', () => {
  const path = samplePath([[0.1, 0.8], [0.4, 0.2], [0.7, 0.8], [0.9, 0.3]], 1000, 500, 40);
  const leds = placePixels(path, 40, 14);
  for (let i = 1; i < leds.length; i++) {
    const d = Math.hypot(leds[i].x - leds[i - 1].x, leds[i].y - leds[i - 1].y);
    assert.ok(d > 12.5 && d <= 14.001, `gap ${i} was ${d}`);
  }
});
