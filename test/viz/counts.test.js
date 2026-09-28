import { test } from 'node:test';
import assert from 'node:assert/strict';
import { countMismatch } from '../../web/viz/counts.js';

test('countMismatch stays quiet until both sides are known', () => {
  assert.equal(countMismatch(null, [100, 10]), null);
  assert.equal(countMismatch([100, 10], null), null);
});

test('countMismatch stays quiet when the counts agree', () => {
  assert.equal(countMismatch([100, 10], [100, 10]), null);
});

test('countMismatch names both sides when a count or the strip count differs', () => {
  const differentCount = countMismatch([100, 10], [120, 10]);
  assert.match(differentCount, /live engine 100 \+ 10/);
  assert.match(differentCount, /recording 120 \+ 10/);
  assert.ok(countMismatch([100, 10], [100]) !== null);
});
