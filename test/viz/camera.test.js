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
