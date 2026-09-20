// One place that owns the settings object, so the rail, the renderer and
// localStorage cannot disagree about its shape.
//
// state.hw is what Vue renders and mutates. view.config is what the renderer
// reads every frame. They are the same object: assigning the reactive one into
// the view means a slider move shows on the next frame with no watcher, and
// there is no second copy to fall out of date.

import { migratePreset, profileById, DEFAULT_PROFILE_ID } from './profiles.js';
import { defaultPose } from './path.js';

// v2 resets the old handle-based poses so the new editor opens on a clear
// horizontal calibration line instead of restoring an obsolete arch layout.
const STORAGE_KEY = 'gg.hw.v2';
// A second or two after the pose stops moving. Deliberately unhurried: the
// intermediate positions of a drag have no reader, and a pose lost because the
// tab closed mid-gesture is not worth a flush handler to rescue.
const SAVE_DEBOUNCE_MS = 1200;
let saveTimer = null;

export function defaultHw(counts) {
  const DEFAULT = profileById(DEFAULT_PROFILE_ID);
  return {
    activeStrip: 0,
    surface: 'room',
    look: 'realistic',
    scale: 'physical',
    stageWidthM: 3,
    zoom: 1,
    panX: 0,
    panY: 0,
    ev: 0,
    spill: 1,
    grain: 0.14,
    pixelSize: 1.0,
    glowSize: 1.0,
    intensity: 1.0,
    // defaultPose, not SHAPES. A SHAPES entry spans the whole stage; these two
    // poses have to leave room for each other. Task 4 fixed exactly this bug
    // once already, which is why the poses live in path.js.
    strips: counts.map((count, i) => ({
      profile: DEFAULT.id,
      lengthM: Math.max(0.1, count * DEFAULT.pitch / 1000),
      pitchMm: DEFAULT.pitch,
      pitchManual: false,
      pixelSize: DEFAULT.visual.pixelSize,
      glowSize: DEFAULT.visual.glowSize,
      intensity: DEFAULT.visual.intensity,
      shape: null,
      pts: defaultPose(i),
    })),
    fit: null,
    inspect: null,
    drawMs: 0,
  };
}

export function loadHw(counts) {
  const base = defaultHw(counts);
  let stored = null;
  try {
    stored = JSON.parse(window.localStorage.getItem(STORAGE_KEY) || 'null');
  } catch (err) {
    stored = null;
  }
  if (!stored) return base;

  // Anything past this point is untrusted: it survives across versions of this
  // page, and a hand-edited or truncated value must cost the pose, never the
  // page. This function runs before anything else can render, so it must not
  // be the thing that stops the page loading.
  try {
    if (stored.surface === 'rod') stored.surface = 'room';
    for (const key of ['surface', 'stageWidthM', 'zoom', 'panX', 'panY', 'ev', 'spill', 'grain',
                       'pixelSize', 'glowSize', 'intensity']) {
      if (typeof stored[key] === typeof base[key]) base[key] = stored[key];
    }
    if (stored.look === 'stylized' || stored.look === 'realistic') base.look = stored.look;

    // A strip list saved against a different pixel count is still usable: what was
    // stored is the pose, and the count comes from the firmware either way.
    if (Array.isArray(stored.strips)) {
      for (let i = 0; i < base.strips.length; i++) {
        const s = stored.strips[i];
        if (!s) continue;
        if (typeof s.profile === 'string') base.strips[i].profile = s.profile;
        if (Number.isFinite(s.lengthM)) base.strips[i].lengthM = Math.max(0.1, Math.min(20, s.lengthM));
        if (Number.isFinite(s.pitchMm)) base.strips[i].pitchMm = Math.max(0.5, Math.min(400, s.pitchMm));
        if (typeof s.pitchManual === 'boolean') base.strips[i].pitchManual = s.pitchManual;
        for (const key of ['pixelSize', 'glowSize', 'intensity']) {
          if (Number.isFinite(s[key])) base.strips[i][key] = Math.max(0.1, Math.min(3, s[key]));
        }
        // Coercing with Number() is not enough — it turns junk into NaN, which
        // reaches the spline and corrupts the render without an error anywhere.
        // Validate instead, and reject the whole pose (shape included) rather
        // than accept a partially-numeric one.
        // Presets have four points, but freehand paths contain however many
        // samples the gesture needed. Keep the bound finite so corrupted or
        // accidentally enormous localStorage values cannot stall the renderer.
        if (Array.isArray(s.pts) && s.pts.length >= 2 && s.pts.length <= 2000) {
          const pts = s.pts.map((p) => (
            Array.isArray(p) && Number.isFinite(p[0]) && Number.isFinite(p[1])
              ? [p[0], p[1]]
              : null
          ));
          if (pts.every(Boolean)) {
            base.strips[i].pts = pts;
            base.strips[i].shape = typeof s.shape === 'string' ? s.shape : null;
          }
        }
      }
    } else if (typeof stored.preset === 'string') {
      const migrated = migratePreset(stored.preset);
      for (const strip of base.strips) strip.profile = migrated;
    }
  } catch (err) {
    return defaultHw(counts);
  }

  base.zoom = Number.isFinite(base.zoom) ? Math.max(0.1, base.zoom) : 1;
  base.panX = Number.isFinite(base.panX) ? base.panX : 0;
  base.panY = Number.isFinite(base.panY) ? base.panY : 0;

  return base;
}

export function resetHw(counts) {
  try {
    window.localStorage.removeItem(STORAGE_KEY);
  } catch (err) {}
  return defaultHw(counts);
}

function writeHw(hw) {
  try {
    window.localStorage.setItem(STORAGE_KEY, JSON.stringify({
      surface: hw.surface, look: hw.look, scale: hw.scale, stageWidthM: hw.stageWidthM, zoom: hw.zoom,
      panX: hw.panX, panY: hw.panY,
      ev: hw.ev, spill: hw.spill, grain: hw.grain,
      pixelSize: hw.pixelSize, glowSize: hw.glowSize, intensity: hw.intensity,
      strips: hw.strips.map((s) => ({
        profile: s.profile, lengthM: s.lengthM, pitchMm: s.pitchMm,
        pitchManual: s.pitchManual, pixelSize: s.pixelSize, glowSize: s.glowSize,
        intensity: s.intensity, shape: s.shape, pts: s.pts,
      })),
    }));
  } catch (err) {
    // Private windows and blocked site data both throw here. A posed path that
    // does not survive a reload is a small loss; a page that will not load is
    // not.
  }
}

// A drawn path calls this through onGeometryChange on every pointermove —
// close to a hundred times a second — but the pose is only worth persisting
// once the drag settles. JSON.stringify plus a synchronous setItem on every
// move is time spent on the audio-rate render path for no reader who cares
// about the intermediate positions, so only the write is delayed; the fit
// recalculation stays immediate since it feeds a readout the user is
// watching live while dragging.
export function saveHw(hw) {
  if (saveTimer) clearTimeout(saveTimer);
  saveTimer = setTimeout(() => {
    saveTimer = null;
    writeHw(hw);
  }, SAVE_DEBOUNCE_MS);
}

export function bindView(view, bench, hw, state) {
  view.config = hw;
  view.activeStrip = hw.activeStrip;

  view.onGeometryChange = () => {
    hw.fit = view.fit(view.activeStrip);
    saveHw(hw);
  };

  view.onPathCommit = () => {
    const strip = hw.strips[view.activeStrip];
    const fit = view.fit(view.activeStrip);
    if (strip && Number.isFinite(fit.pathM)) {
      strip.lengthM = Math.max(0.1, Math.min(20, fit.pathM));
    }
    hw.fit = fit;
    saveHw(hw);
  };

  view.onHover = (stripIndex, ledIndex, rgb) => {
    bench.setHover(stripIndex, ledIndex);
    if (stripIndex < 0 || !rgb) {
      state.hw.inspect = null;
      return;
    }
    const profile = hw.strips[stripIndex].profile;
    const pitch = profileById(profile).pitch;
    state.hw.inspect = {
      strip: stripIndex,
      index: ledIndex,
      hex: '#' + rgb.map((v) => v.toString(16).padStart(2, '0')).join('').toUpperCase(),
      mm: Math.round((ledIndex + 0.5) * pitch),
    };
  };

  view.onFrame = (bytes) => {
    bench.paint(bytes, hw);
    hw.drawMs = Math.round(view.lastDrawMs * 10) / 10;
  };

  return function sync() {
    view.config = hw;
    view.activeStrip = hw.activeStrip;
    view.invalidate();
    hw.fit = view.fit(hw.activeStrip);
    saveHw(hw);
  };
}
