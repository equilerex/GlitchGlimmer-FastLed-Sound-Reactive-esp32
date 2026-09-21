// Console telemetry for the live view, off unless the page is opened with
// ?debug=1.
//
// The reason this exists: once the browser build started feeding the firmware's
// own classifier live audio, the HUD's mood flickered, scenes churned and the
// strips went dark for stretches at a time, and there was no way to see which of
// the three was the cause rather than the symptom. A frame dump cannot answer
// that. A time-ordered record of every scene change, every mood change and the
// feature values between them can.
//
// It keeps a bounded ring rather than logging every frame, because the console is
// the slowest thing on the page and a per-frame log changes the timing it is
// trying to measure.

const DEFAULT_LIMIT = 4000;
const SUMMARY_MS = 2000;

export class Trace {
  constructor(enabled) {
    this.enabled = enabled;
    this.frames = [];        // ring of { t, ...features }
    this.events = [];        // every scene and mood change, in order
    this.limit = DEFAULT_LIMIT;
    this.lastSummary = 0;
    this.lastFrameTime = 0;
    this.lastScene = null;
    this.lastMood = null;
    this.sceneSince = 0;
    this.moodSince = 0;
    this.fps = 0;
    this.moodChanges = 0;
    this.range = new Map();  // key -> { lo, hi }, over every frame this session
  }

  // One rendered frame. `sample` carries the values the HUD shows plus whatever
  // else the caller has. Anything not passed is simply absent from the record.
  frame(sample) {
    const t = sample.t;

    if (this.lastFrameTime) {
      // Smoothed, because a single stalled frame would otherwise report a rate
      // that never happened.
      const instant = 1000 / Math.max(1, t - this.lastFrameTime);
      this.fps = this.fps ? this.fps * 0.9 + instant * 0.1 : instant;
    }
    this.lastFrameTime = t;

    // Ahead of the enabled check, so the snapshot's ranges exist on a page opened
    // without ?debug=1. This is the cheap half of the trace: a Map of two floats
    // per key.
    this.track(sample);

    if (!this.enabled) return;

    this.frames.push(sample);
    if (this.frames.length > this.limit) this.frames.shift();

    if (sample.scene !== this.lastScene) {
      this.events.push({
        t, kind: 'scene', from: this.lastScene, to: sample.scene,
        dwellMs: this.sceneSince ? t - this.sceneSince : 0,
      });
      console.log(
        `[gg] scene ${this.lastScene ?? '-'} -> ${sample.scene} ` +
        `after ${this.sceneSince ? t - this.sceneSince : 0} ms ` +
        `(mood ${sample.mood}, energy ${fmt(sample.energy)}, bpm ${fmt(sample.bpm)})`);

      this.sceneSince = t;
      this.lastScene = sample.scene;
    }

    if (sample.mood !== this.lastMood) {
      this.events.push({
        t, kind: 'mood', from: this.lastMood, to: sample.mood,
        dwellMs: this.moodSince ? t - this.moodSince : 0,
      });
      // Logged without the console group so a flicker reads as a run of lines
      // rather than a stack of collapsed groups.
      console.log(
        `[gg] mood ${this.lastMood ?? '-'} -> ${sample.mood} ` +
        `after ${this.moodSince ? t - this.moodSince : 0} ms ` +
        `(energy ${fmt(sample.energy)}, dynamics ${fmt(sample.dynamics)}, bpm ${fmt(sample.bpm)})`);

      this.moodSince = t;
      this.lastMood = sample.mood;
    }

    if (t - this.lastSummary >= SUMMARY_MS) {
      this.lastSummary = t;
      console.log('[gg] ' + JSON.stringify(this.summary()));
    }
  }

  // Every numeric field of one frame, folded into a running minimum and maximum.
  // The keys to skip are the ones that are not measurements: the timestamp, the
  // names, and the three scene-clock rows, which climb by design and are already
  // shown against their own markers.
  static SKIP = new Set(['t', 'source', 'scene', 'mood', 'predicted',
                         'elapsed', 'minMs', 'idealMs', 'moodChanges']);

  // Drop the recorded ranges. Called when the page changes which input it is
  // analysing. The ranges are the whole point of the snapshot, and a range is a
  // statistic of one signal: a demo run and a microphone run share no scale, so
  // the min and max of one say nothing about the other and reporting the union
  // describes neither. A range that spans both is also exactly what makes a
  // reading look wrong, since the microphone's values all sit at the bottom of it.
  resetRange() {
    this.range.clear();
  }

  track(sample) {
    // A counter, not a measurement, so a min and max over it says nothing. Held
    // as the latest value instead. It comes from the firmware and not from the
    // event log below, because the log only records with ?debug=1 on and the
    // count read 0 on a page opened without it, which looks like a frozen
    // classifier rather than like an absent measurement.
    if (typeof sample.moodChanges === 'number') this.moodChanges = sample.moodChanges;

    for (const key in sample) {
      if (Trace.SKIP.has(key)) continue;
      const v = sample[key];
      if (typeof v !== 'number' || !isFinite(v)) continue;
      const r = this.range.get(key);
      if (r === undefined) {
        this.range.set(key, { lo: v, hi: v });
      } else {
        if (v < r.lo) r.lo = v;
        if (v > r.hi) r.hi = v;
      }
    }
  }

  // An aggregate over the whole session. Ranges rather than averages, because the
  // question being asked about level and bpm is how far they swing, and a mean
  // hides exactly that. A range whose lo equals its hi is a value that never
  // moved, which is the shortest statement of a stuck reading.
  summary() {
    const out = {
      fps: Math.round(this.fps),
      frames: this.frames.length,
      scene: this.lastScene,
      mood: this.lastMood,
      sceneChanges: this.events.filter(e => e.kind === 'scene').length,
      moodChanges: this.moodChanges ?? 0,
    };
    for (const [key, r] of this.range) out[key] = [round(r.lo), round(r.hi)];
    return out;
  }

  // Everything needed to report a session as numbers: the frame in front of the
  // user, the range every value moved through while they watched, the change
  // history, and the spectrum bins as they stand. The ranges are what make a
  // description like "the bands look wrong" checkable, and the bins are what make
  // the spectrum panel's shape reproducible without a screenshot.
  //
  // The spectrum is downsampled to the 64 bars the panel draws, taking each bar's
  // maximum, because 255 raw bins is more than anyone reads and the panel's own
  // reduction is the thing being questioned.
  snapshot(current, magnitudes) {
    let bars = null;
    if (magnitudes) {
      bars = [];
      const step = Math.max(1, Math.floor(magnitudes.length / 64));
      for (let b = 0; b < 64; ++b) {
        let v = 0;
        for (let i = 0; i < step; ++i) {
          const bin = b * step + i + 1;
          if (bin < magnitudes.length && magnitudes[bin] > v) v = magnitudes[bin];
        }
        bars.push(round(v));
      }
    }
    return {
      at: new Date().toISOString(),
      fps: Math.round(this.fps),
      frames: this.frames.length,
      debug: this.enabled,
      current,
      ranges: this.summary(),
      events: this.events.map(e => ({
        t: Math.round(e.t), kind: e.kind, from: e.from, to: e.to,
        dwellMs: Math.round(e.dwellMs),
      })),
      spectrumBars: bars,
    };
  }

  // The change history, which is the part worth keeping: it says how long each
  // mood actually lasted, so a flicker is visible as a run of short dwells.
  dump(kind) {
    const rows = kind ? this.events.filter(e => e.kind === kind) : this.events;
    console.table(rows.map(e => ({
      t: Math.round(e.t), kind: e.kind, from: e.from, to: e.to,
      dwellMs: Math.round(e.dwellMs),
    })));
    return rows;
  }

  // Dwell times per mood change, which is the shortest way to see whether the
  // classifier is settling or oscillating.
  dwellStats() {
    const moods = this.events.filter(e => e.kind === 'mood' && e.dwellMs > 0);
    if (!moods.length) return { changes: 0 };
    const d = moods.map(e => e.dwellMs).sort((a, b) => a - b);
    return {
      changes: d.length,
      minMs: Math.round(d[0]),
      medianMs: Math.round(d[Math.floor(d.length / 2)]),
      maxMs: Math.round(d[d.length - 1]),
    };
  }
}

function round(v) {
  return Math.abs(v) >= 100 ? Math.round(v) : Math.round(v * 1000) / 1000;
}

function fmt(v) {
  return typeof v === 'number' ? round(v) : String(v);
}

// Enabled by ?debug=1 or #debug, so the flag survives a reload and can be kept in
// a bookmark.
export function debugEnabled() {
  const params = new URLSearchParams(window.location.search);
  return params.get('debug') === '1' || window.location.hash === '#debug';
}
