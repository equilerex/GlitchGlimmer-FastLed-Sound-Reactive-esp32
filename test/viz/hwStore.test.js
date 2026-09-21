import test from 'node:test';
import assert from 'node:assert/strict';
import { defaultHw } from '../../web/viz/hwStore.js';

test('hardware settings use physical scale by default', () => {
  const hw = defaultHw([100, 10]);

  assert.equal(hw.scale, 'physical');
  assert.equal(hw.stageWidthM, 3);
  assert.equal(hw.roomGlow, 2.5);
  assert.equal(hw.glowSize, 2.0);
  assert.equal(hw.intensity, 2.0);
  assert.equal(hw.strips.length, 2);
  assert.equal(hw.strips[0].lengthM, 0.21666666666666667);
  assert.deepEqual(hw.strips[0].pts, [[0.06, 0.45], [0.32, 0.45], [0.68, 0.45], [0.94, 0.45]]);
  assert.equal(hw.strips[1].lengthM, 0.16666666666666669);
  assert.deepEqual(hw.strips[1].pts, [[0.06, 0.62], [0.32, 0.62], [0.68, 0.62], [0.94, 0.62]]);
});
