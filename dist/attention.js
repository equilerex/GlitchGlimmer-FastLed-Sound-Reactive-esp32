// Session weight for the live page. The event stream only keeps the last hundred
// lines, so a name that wins all evening is not visible as weight. This counts
// time held and how often each mood, scene and layer starts.

const STALL_MS = 250;

export function emptyAttention() {
  return {
    scenes: {},
    moods: {},
    tones: {},
    layers: {},
    ms: 0,
    prevScene: '',
    prevOn: [],
    prevLayers: [],
  };
}

export function resetAttention(att) {
  att.scenes = {};
  att.moods = {};
  att.tones = {};
  att.layers = {};
  att.ms = 0;
  att.prevScene = '';
  att.prevOn = [];
  att.prevLayers = [];
}

function bump(bucket, name, dt, started) {
  let row = bucket[name];
  if (!row) {
    row = { ms: 0, starts: 0 };
    bucket[name] = row;
  }
  row.ms += dt;
  if (started) row.starts += 1;
}

// `layers` is the names present on this frame. A start is a name that was not
// present on the previous frame. Time is the frame's dt, ignored when it is
// missing or a stall, so one hitch does not become the heaviest row.
//
// Moods overlap, so a mood's time is the frame's dt weighted by its strength, and
// the shares of all moods can add up to more than the session. A mood starts when
// its strength crosses ON going up.
const ON = 0.3;
const PRESENT = 0.05;
export function noteAttention(att, sample) {
  const dt = sample.dtMs;
  if (!(dt > 0) || dt > STALL_MS) return;
  const scene = sample.scene || '';
  const moods = sample.moods || [];
  const layers = sample.layers || [];
  att.ms += dt;
  if (scene) bump(att.scenes, scene, dt, scene !== att.prevScene);
  const on = [];
  for (let i = 0; i < moods.length; i++) {
    const m = moods[i];
    if (!m.inSelector || m.strength < PRESENT) continue;
    const isOn = m.strength >= ON;
    // Tone moods are always partly true, so they would take most of the time if
    // they shared a list with the character moods. They get their own.
    if (m.tone) {
      bump(att.tones, m.name, dt * m.strength, isOn && att.prevOn.indexOf(m.name) < 0);
    } else {
      bump(att.moods, m.name, dt * m.strength, isOn && att.prevOn.indexOf(m.name) < 0);
    }
    if (isOn) on.push(m.name);
  }
  const seen = new Set(att.prevLayers);
  for (let i = 0; i < layers.length; i++) {
    const name = layers[i];
    if (!name) continue;
    bump(att.layers, name, dt, !seen.has(name));
  }
  att.prevScene = scene;
  att.prevOn = on;
  att.prevLayers = layers.slice();
}

export function attentionRows(bucket, sessionMs, limit) {
  const cap = limit || 8;
  const total = sessionMs > 0 ? sessionMs : 0;
  const names = Object.keys(bucket);
  const rows = [];
  for (let i = 0; i < names.length; i++) {
    const name = names[i];
    const row = bucket[name];
    rows.push({
      name,
      ms: row.ms,
      starts: row.starts,
      pct: total > 0 ? Math.round((100 * row.ms) / total) : 0,
    });
  }
  rows.sort((a, b) => b.ms - a.ms || b.starts - a.starts || (a.name < b.name ? -1 : 1));
  return rows.slice(0, cap);
}
