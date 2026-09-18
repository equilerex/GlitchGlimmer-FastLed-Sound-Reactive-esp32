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
  assert.ok(Math.abs(stripLengthMm(120, profileById('ws60')) - 2004) < 1e-6, 'expected 2004 mm');
});

test('COB fuses, and a sleeve softens without fusing', () => {
  assert.equal(fuses(profileById('cob')), true);
  assert.equal(fuses(profileById('ws60')), false);

  // Silicone is the interesting middle case: four times the diffusion of the
  // bare strip it wraps, and still not continuous, because the sleeve is
  // narrower than the gap between pixels.
  assert.equal(fuses(profileById('sil')), false);
  assert.ok(profileById('sil').sigma > profileById('ws60').sigma * 4);
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
