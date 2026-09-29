import test from 'node:test';
import assert from 'node:assert/strict';
import { emptyAttention, noteAttention, attentionRows, resetAttention } from '../web/attention.js';

test('time share and starts, layers only count a fresh appearance', () => {
  const att = emptyAttention();
  noteAttention(att, { dtMs: 100, scene: 'Lava Lamp', mood: 'Tease', layers: ['Mood arc'] });
  noteAttention(att, { dtMs: 100, scene: 'Lava Lamp', mood: 'Tease', layers: ['Mood arc'] });
  noteAttention(att, { dtMs: 100, scene: 'Heartbeat', mood: 'Intense', layers: ['Mood arc', 'Shockwave'] });

  const scenes = attentionRows(att.scenes, att.ms);
  assert.equal(scenes[0].name, 'Lava Lamp');
  assert.equal(scenes[0].pct, 67);
  assert.equal(scenes[0].starts, 1);
  assert.equal(scenes[1].name, 'Heartbeat');
  assert.equal(scenes[1].starts, 1);

  const moods = attentionRows(att.moods, att.ms);
  assert.equal(moods[0].name, 'Tease');
  assert.equal(moods[1].name, 'Intense');

  const layers = attentionRows(att.layers, att.ms);
  assert.equal(layers.find(r => r.name === 'Mood arc').starts, 1);
  assert.equal(layers.find(r => r.name === 'Shockwave').starts, 1);
  assert.equal(layers.find(r => r.name === 'Mood arc').ms, 300);
});

test('a stalled frame does not become the heaviest row', () => {
  const att = emptyAttention();
  noteAttention(att, { dtMs: 50, scene: 'A', mood: 'Calm', layers: [] });
  noteAttention(att, { dtMs: 5000, scene: 'B', mood: 'Calm', layers: [] });
  assert.equal(attentionRows(att.scenes, att.ms).length, 1);
  assert.equal(attentionRows(att.scenes, att.ms)[0].name, 'A');
});

test('reset clears the session', () => {
  const att = emptyAttention();
  noteAttention(att, { dtMs: 40, scene: 'A', mood: 'Calm', layers: ['Glow'] });
  resetAttention(att);
  assert.equal(att.ms, 0);
  assert.deepEqual(attentionRows(att.scenes, att.ms), []);
  noteAttention(att, { dtMs: 40, scene: 'A', mood: 'Calm', layers: ['Glow'] });
  assert.equal(attentionRows(att.layers, att.ms)[0].starts, 1);
});
