import test from 'node:test';
import assert from 'node:assert/strict';
import { defaultHw } from '../../web/viz/hwStore.js';

test('hardware settings use physical scale by default', () => {
  const hw = defaultHw([100, 10]);

  assert.equal(hw.scale, 'true');
  assert.equal(hw.stageWidthM, 3);
});
