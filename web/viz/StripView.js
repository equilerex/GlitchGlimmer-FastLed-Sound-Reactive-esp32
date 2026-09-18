// The stage: strips posed in space, lit photographically.
//
// Three composite passes out of one offscreen buffer, in this order, because
// the order is what makes it read as light:
//
//   spill  the buffer blurred wide and screened onto the surface. Light
//          landing on the wall behind the strip.
//   halo   the same buffer unblurred and added. The casing glowing.
//   core   a die-sized disc per pixel, pushed toward white as it clips.
//
// Painting the cores first would bury them under the halo and lose the
// blowout, which is the whole point.

import { expose, grainAlpha, GRAIN_DOT_COUNT } from './camera.js';
import { profileById, stripLengthMm, fuses } from './profiles.js';
import {
  samplePath, placePixels, fitScale, defaultPose, hitHandle,
} from './path.js';
import { paintSurface, paintRod, occludes } from './surface.js';

const MAX_DPR = 2;

function defaultStrip(index) {
  // shape is null because these poses are not any SHAPES preset. A rail showing
  // a preset as selected while the strip sits somewhere else would be lying.
  return { profile: 'ws60', shape: null, pts: defaultPose(index) };
}

export class StripView {
  constructor(canvas, counts) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.counts = counts;

    this.glow = document.createElement('canvas');
    this.glowCtx = this.glow.getContext('2d');

    this.width = 0;
    this.height = 0;
    this.dpr = 1;

    this.config = {
      surface: 'room',
      scale: 'fit',
      stageWidthM: 3,
      ev: 0,
      spill: 1,
      grain: 0.14,
      pixelSize: 1,
      glowSize: 1,
      intensity: 1,
      strips: counts.map((_, i) => defaultStrip(i)),
    };

    this.geometry = null;   // rebuilt only when something geometric changes
    this.lastBytes = null;
    this.lastDrawMs = 0;

    this.activeStrip = 0;
    this.dragHandle = -1;
    this.hover = { strip: -1, led: -1 };
    this.onHover = null;
    this.onGeometryChange = null;
    this.onFrame = null;
    this.attachPointer();

    // How the bench strip and the rail reach this view from Tasks 7 and 8,
    // without a second place tracking which StripView belongs to which canvas.
    canvas.__view = this;
  }

  // Geometry is rebuilt on resize, on a handle drag, on a profile change and on
  // a scale-mode change, never per frame. Rebuilding a 600-sample spline sixty
  // times a second for a shape that did not move is most of a frame budget
  // spent on nothing.
  invalidate() {
    this.geometry = null;
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    this.dpr = Math.min(window.devicePixelRatio || 1, MAX_DPR);
    this.width = Math.max(320, Math.round(rect.width));
    this.height = Math.max(180, Math.round(rect.height || rect.width * 0.42));

    for (const surface of [this.canvas, this.glow]) {
      surface.width = this.width * this.dpr;
      surface.height = this.height * this.dpr;
    }
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    this.glowCtx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);

    this.invalidate();
    if (this.lastBytes) this.paint(this.lastBytes);
  }

  buildGeometry() {
    const { width, height, config } = this;
    this.geometry = config.strips.map((strip, i) => {
      const profile = profileById(strip.profile);
      const path = samplePath(strip.pts, width, height);
      const pxPerMm = config.scale === 'true'
        ? width / (config.stageWidthM * 1000)
        : fitScale(path, this.counts[i], profile.pitch);
      const pitchPx = profile.pitch * pxPerMm;
      return {
        profile,
        path,
        pxPerMm,
        pitchPx,
        sigmaPx: Math.max(profile.sigma * pxPerMm * config.glowSize, 1.1),
        diePx: Math.max(profile.die * pxPerMm * 0.5 * config.pixelSize, 0.8),
        leds: placePixels(path, this.counts[i], pitchPx),
      };
    });
  }

  // A grabbed handle short-circuits hover so a drag never triggers the
  // inspector on whatever pixel happens to be under the cursor mid-move.
  //
  // Handlers are stored on `this` (not inline arrows) so `detach()` can pass
  // the exact same function references to removeEventListener — an inline
  // arrow rebinds a fresh function on every read and removeEventListener
  // would silently no-op.
  attachPointer() {
    const canvas = this.canvas;

    this._onPointerDown = (event) => {
      const [x, y] = this.localPoint(event);
      const pts = this.config.strips[this.activeStrip].pts;
      const grabbed = hitHandle(pts, this.width, this.height, x, y);
      if (grabbed >= 0) {
        this.dragHandle = grabbed;
        canvas.setPointerCapture(event.pointerId);
      }
    };

    this._onPointerMove = (event) => {
      const [x, y] = this.localPoint(event);
      if (this.dragHandle >= 0) {
        const pts = this.config.strips[this.activeStrip].pts;
        pts[this.dragHandle] = [
          Math.max(0.02, Math.min(0.98, x / this.width)),
          Math.max(0.03, Math.min(0.97, y / this.height)),
        ];
        this.config.strips[this.activeStrip].shape = null;
        this.invalidate();
        if (this.onGeometryChange) this.onGeometryChange();
        return;
      }
      this.updateHover(x, y);
    };

    this._onPointerRelease = () => { this.dragHandle = -1; };

    this._onPointerLeave = () => {
      this._onPointerRelease();
      this.hover = { strip: -1, led: -1 };
      if (this.onHover) this.onHover(-1, -1, null);
    };

    canvas.addEventListener('pointerdown', this._onPointerDown);
    canvas.addEventListener('pointermove', this._onPointerMove);
    canvas.addEventListener('pointerup', this._onPointerRelease);
    canvas.addEventListener('pointercancel', this._onPointerRelease);
    // Capture can be lost without either of the release events firing — the
    // window losing focus mid-drag, or an OS gesture taking over a pen or
    // touch. Without this the handle keeps following the cursor with no button
    // held down.
    canvas.addEventListener('lostpointercapture', this._onPointerRelease);
    canvas.addEventListener('pointerleave', this._onPointerLeave);
  }

  // Two StripViews (app.js's and live.js's) can share this same canvas across
  // a mode switch, and hwStore.bindView assigns the same settings object into
  // whichever one is current. A detached view must stop writing pose data
  // through pointer events it no longer should own — otherwise the stale
  // instance keeps dragging its own stale activeStrip/width/height into the
  // settings object the live view thinks it controls.
  detach() {
    const canvas = this.canvas;
    canvas.removeEventListener('pointerdown', this._onPointerDown);
    canvas.removeEventListener('pointermove', this._onPointerMove);
    canvas.removeEventListener('pointerup', this._onPointerRelease);
    canvas.removeEventListener('pointercancel', this._onPointerRelease);
    canvas.removeEventListener('lostpointercapture', this._onPointerRelease);
    canvas.removeEventListener('pointerleave', this._onPointerLeave);
  }

  localPoint(event) {
    const rect = this.canvas.getBoundingClientRect();
    return [event.clientX - rect.left, event.clientY - rect.top];
  }

  updateHover(x, y) {
    if (!this.geometry) return;
    let bestStrip = -1;
    let bestLed = -1;
    let bestDistance = 18;
    for (let s = 0; s < this.geometry.length; s++) {
      for (const led of this.geometry[s].leds) {
        const d = Math.hypot(led.x - x, led.y - y);
        if (d < bestDistance) {
          bestDistance = d;
          bestStrip = s;
          bestLed = led.index;
        }
      }
    }
    this.hover = { strip: bestStrip, led: bestLed };
    if (!this.onHover) return;
    if (bestStrip < 0 || !this.lastBytes) {
      this.onHover(-1, -1, null);
      return;
    }
    let offset = 0;
    for (let i = 0; i < bestStrip; i++) offset += this.counts[i] * 3;
    const at = offset + bestLed * 3;
    this.onHover(bestStrip, bestLed, [
      this.lastBytes[at], this.lastBytes[at + 1], this.lastBytes[at + 2],
    ]);
  }

  // Drawn above the grain so a handle stays a handle, not a mote the grain
  // pass buries. Only the active strip's handles show, because a person
  // reshaping one strip does not need the other strip's handles competing
  // for the same drag.
  drawHandles() {
    const ctx = this.ctx;
    const pts = this.config.strips[this.activeStrip].pts;
    ctx.save();
    for (let i = 0; i < pts.length; i++) {
      const x = pts[i][0] * this.width;
      const y = pts[i][1] * this.height;
      const active = this.dragHandle === i;
      // Kept in step with --accent in style.css by hand. A handle is UI chrome
      // rather than depicted hardware, so it should follow the theme, but
      // drawHandles runs every frame and reading a custom property through
      // getComputedStyle that often costs more than the coupling does.
      ctx.strokeStyle = active ? '#38bdf8' : 'rgba(244, 244, 245, 0.5)';
      ctx.fillStyle = 'rgba(9, 9, 11, 0.72)';
      ctx.lineWidth = active ? 2 : 1.25;
      ctx.beginPath();
      ctx.arc(x, y, active ? 9 : 7, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }
    ctx.restore();
  }

  fit(stripIndex) {
    if (!this.geometry) this.buildGeometry();
    const geom = this.geometry[stripIndex];
    const stripMm = stripLengthMm(this.counts[stripIndex], geom.profile);
    const pathMm = geom.path.total / geom.pxPerMm;
    return {
      pathM: pathMm / 1000,
      stripM: stripMm / 1000,
      overM: (stripMm - pathMm) / 1000,
      mmPerPx: 1 / geom.pxPerMm,
    };
  }

  clear() {
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    paintSurface(this.ctx, this.config.surface, this.width, this.height);
  }

  paint(bytes) {
    if (!this.width) this.resize();
    this.lastBytes = bytes;
    const started = performance.now();

    if (!this.geometry) this.buildGeometry();
    this.clear();

    let offset = 0;
    const rodAt = occludes(this.config.surface) ? 1 : -1;
    for (let i = 0; i < this.geometry.length; i++) {
      if (i === rodAt) paintRod(this.ctx, this.width, this.height);
      const geom = this.geometry[i];
      this.paintStrip(geom, bytes, offset);
      offset += this.counts[i] * 3;
    }

    this.paintGrain();
    this.drawHandles();
    this.lastDrawMs = performance.now() - started;
    if (this.onFrame) this.onFrame(bytes);
  }

  colourOf(bytes, offset, index) {
    const at = offset + index * 3;
    const k = this.config.intensity;
    return expose(bytes[at] * k, bytes[at + 1] * k, bytes[at + 2] * k, this.config.ev);
  }

  paintStrip(geom, bytes, offset) {
    this.drawSubstrate(geom);
    this.fillGlowBuffer(geom, bytes, offset);
    this.compositeSpill(geom);
    this.compositeHalo();
    this.drawCores(geom, bytes, offset);
  }

  // The unlit strip. Without it a dark run is nothing at all, and a strip that
  // vanishes when the firmware blanks it looks like a crash rather than like
  // a strip that is off.
  drawSubstrate(geom) {
    const ctx = this.ctx;
    const { casing } = geom.profile;
    const widthFor = {
      cob: Math.max(3, geom.sigmaPx * 1.1),
      sleeve: Math.max(5, geom.sigmaPx * 0.9),
      pip: 0.8,
    };
    ctx.save();
    ctx.strokeStyle = casing === 'pip'
      ? 'rgba(150, 160, 175, 0.16)'
      : 'rgba(120, 128, 140, 0.11)';
    ctx.lineWidth = widthFor[casing] || Math.max(2.5, geom.diePx * 1.6);
    ctx.lineCap = 'round';
    ctx.lineJoin = 'round';
    ctx.beginPath();
    const poly = geom.path.poly;
    ctx.moveTo(poly[0][0], poly[0][1]);
    for (let i = 4; i < poly.length; i += 4) ctx.lineTo(poly[i][0], poly[i][1]);
    ctx.stroke();
    ctx.restore();
  }

  fillGlowBuffer(geom, bytes, offset) {
    const ctx = this.glowCtx;
    ctx.save();
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.width, this.height);
    ctx.globalCompositeOperation = 'lighter';

    const halo = Math.max(geom.sigmaPx * 2.6, 3);
    for (const led of geom.leds) {
      const [r, g, b, peak] = this.colourOf(bytes, offset, led.index);
      if (peak < 0.004) continue;
      const rgb = `${r | 0},${g | 0},${b | 0}`;
      const grad = ctx.createRadialGradient(led.x, led.y, 0, led.x, led.y, halo);
      grad.addColorStop(0, `rgba(${rgb},0.95)`);
      grad.addColorStop(0.28, `rgba(${rgb},0.42)`);
      grad.addColorStop(1, `rgba(${rgb},0)`);
      ctx.fillStyle = grad;
      ctx.beginPath();
      ctx.arc(led.x, led.y, halo, 0, Math.PI * 2);
      ctx.fill();

      if (geom.profile.burst > 0 && peak > 0.12) {
        const arm = halo * (1.8 + geom.profile.burst * 2.6) * Math.min(1, peak * 1.4);
        ctx.strokeStyle = `rgba(${rgb},${0.26 * geom.profile.burst})`;
        ctx.lineWidth = Math.max(1, geom.diePx * 0.5);
        ctx.beginPath();
        ctx.moveTo(led.x - arm, led.y);
        ctx.lineTo(led.x + arm, led.y);
        ctx.moveTo(led.x, led.y - arm);
        ctx.lineTo(led.x, led.y + arm);
        ctx.stroke();
      }
    }
    ctx.restore();
  }

  compositeSpill(geom) {
    const spill = this.config.spill;
    if (spill <= 0.01) return;
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    ctx.globalAlpha = Math.min(1, spill * 0.55);
    ctx.filter = `blur(${Math.round(Math.max(geom.sigmaPx * 6, 16))}px)`;
    ctx.drawImage(this.glow, 0, 0, this.width, this.height);
    ctx.restore();
  }

  compositeHalo() {
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    ctx.drawImage(this.glow, 0, 0, this.width, this.height);
    ctx.restore();
  }

  drawCores(geom, bytes, offset) {
    const ctx = this.ctx;
    const continuous = fuses(geom.profile);
    const radius = continuous
      ? Math.max(geom.diePx, geom.sigmaPx * 0.55)
      : geom.diePx;

    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    for (const led of geom.leds) {
      const [r, g, b, peak] = this.colourOf(bytes, offset, led.index);
      if (peak < 0.02) continue;
      const hot = Math.min(1, peak * 1.25);
      const toward = (v) => (v + (255 - v) * hot * 0.8) | 0;
      ctx.fillStyle = `rgba(${toward(r)},${toward(g)},${toward(b)},${0.55 + 0.45 * hot})`;
      ctx.beginPath();
      ctx.arc(led.x, led.y, radius, 0, Math.PI * 2);
      ctx.fill();
    }
    ctx.restore();
  }

  paintGrain() {
    const alpha = grainAlpha(this.config.grain);
    if (alpha <= 0) return;
    const ctx = this.ctx;
    ctx.save();
    ctx.globalAlpha = alpha;
    ctx.globalCompositeOperation = 'overlay';
    for (let i = 0; i < GRAIN_DOT_COUNT; i++) {
      const v = Math.random() > 0.5 ? 255 : 0;
      ctx.fillStyle = `rgb(${v},${v},${v})`;
      ctx.fillRect(Math.random() * this.width, Math.random() * this.height, 3, 3);
    }
    ctx.restore();
  }
}
