import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  SHAPES, samplePath, pointAt, placePixels, fitScale, hitHandle,
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

test('the default poses keep the two strips clear of each other', () => {
  const [first, second] = DEFAULT_POSES;
  const lowestOfFirst = Math.max(...first.map((p) => p[1]));
  const highestOfSecond = Math.min(...second.map((p) => p[1]));
  assert.ok(lowestOfFirst < highestOfSecond, 'strip 0 must sit entirely above strip 1');

  const spanOf = (pts) => Math.max(...pts.map((p) => p[0])) - Math.min(...pts.map((p) => p[0]));
  assert.ok(spanOf(second) < spanOf(first) * 0.6, 'strip 1 must be the shorter run');
});

test('defaultPose hands back a copy, not the shared array', () => {
  const pose = defaultPose(0);
  pose[0][1] = 0.99;
  assert.notEqual(DEFAULT_POSES[0][0][1], 0.99);
});
