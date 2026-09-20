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
  samplePath, placePixels, defaultPose,
} from './path.js';
import { paintSurface } from './surface.js';
import {
  paintPcb, paintBodies, paintSleeve, glowSpots,
} from './realistic.js';

const MAX_DPR = 2;
const MIN_PX_PER_MM = 0.35;
const MIN_PX_PER_MM_PHONE = 0.8;

// [downscale factor, alpha]. Each is the glow buffer shrunk, softened and
// stretched back, so its light reaches far past the strip. Cheap because the
// blur runs on a tiny canvas.
const ENV_LAYERS = [[10, 0.3], [32, 0.4]];

// [radius as a multiple of the halo, peak alpha], widest first.
const GLOW_LAYERS = [[3.2, 0.06], [1.6, 0.14], [0.8, 0.32], [0.4, 0.6], [0.18, 0.9]];

function zoomPath(path, zoom, width, height) {
  if (zoom === 1) return path;
  const cx = width * 0.5;
  const cy = height * 0.5;
  const poly = path.poly.map(([x, y]) => [
    cx + (x - cx) * zoom,
    cy + (y - cy) * zoom,
  ]);
  const cum = path.cum.map((distance) => distance * zoom);
  return { poly, cum, total: path.total * zoom };
}

function panPath(path, panX, panY) {
  if (!panX && !panY) return path;
  return {
    poly: path.poly.map(([x, y]) => [x + panX, y + panY]),
    cum: path.cum,
    total: path.total,
  };
}

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
      look: 'realistic',
      scale: 'physical',
      stageWidthM: 3,
      zoom: 1,
      panX: 0,
      panY: 0,
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
    this.drawPointerId = -1;
    this.drawPending = false;
    this.drawStart = null;
    this.pointers = new Map();
    this.pinch = null;
    this.hover = { strip: -1, led: -1 };
    this.onHover = null;
    this.onGeometryChange = null;
    this.onPathCommit = null;
    this.onFrame = null;
    this.attachPointer();

    // How the bench strip and the rail reach this view from Tasks 7 and 8,
    // without a second place tracking which StripView belongs to which canvas.
    canvas.__view = this;
  }

  // Geometry is rebuilt on resize, on a drawn path, on a profile change and on
  // a zoom/pan change, never per frame. Rebuilding a 600-sample spline sixty
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
      const pitchMm = Number.isFinite(strip.pitchMm) ? strip.pitchMm : profile.pitch;
      const count = this.counts[i] || 1;
      const zoom = Number.isFinite(config.zoom) ? Math.max(0.1, config.zoom) : 1;
      const stageWidthM = Number.isFinite(config.stageWidthM)
        ? Math.max(0.1, config.stageWidthM) : 3;
      // Floor: on a phone a 3 m stage puts LEDs 2 px apart and every light is
      // sub-pixel. Below the floor the stage shows less of the strip instead.
      const basePxPerMm = Math.max(width / (stageWidthM * 1000), width < 700 ? MIN_PX_PER_MM_PHONE : MIN_PX_PER_MM);
      const pxPerMm = basePxPerMm * zoom;
      const pitchPx = pitchMm * pxPerMm;
      // Build the physical path at zoom 1, then scale the finished geometry as
      // an interface transform. This keeps zoom from changing lane wrapping,
      // software length, or LED count.
      const path = panPath(zoomPath(
        samplePath(strip.pts, width, height), zoom, width, height,
      ), Number(config.panX) || 0, Number(config.panY) || 0);
      const sizeK = strip.pixelSize ?? config.pixelSize;
      const body = profile.body;
      return {
        profile,
        body,
        sizeK,
        // A pixel that is a stretch of continuous phosphor rather than a point.
        stretchPx: body.fill === 'phosphor'
          ? pitchPx * Math.min(1, body.len / profile.pitch) : 0,
        path,
        pxPerMm,
        pitchPx,
        sigmaPx: Math.max(profile.sigma * pxPerMm * (strip.glowSize ?? config.glowSize), 1.1),
        diePx: Math.max(profile.die * pxPerMm * 0.5 * (strip.pixelSize ?? config.pixelSize), 0.8),
        // The path is the strip's physical length. The profile pitch is the
        // cadence, so input speed and pointer sample density cannot change LED
        // spacing. A count change is committed when drawing ends.
        leds: placePixels(path, count, pitchPx),
      };
    });
  }

  // Handlers are stored on `this` (not inline arrows) so `detach()` can pass
  // the exact same function references to removeEventListener — an inline
  // arrow rebinds a fresh function on every read and removeEventListener
  // would silently no-op.
  attachPointer() {
    const canvas = this.canvas;

    this._onPointerDown = (event) => {
      const screen = this.localPoint(event);
      this.pointers.set(event.pointerId, screen);
      if (this.pointers.size === 1) {
        this.drawPointerId = event.pointerId;
        this.drawPending = true;
        this.drawStart = screen;
        canvas.setPointerCapture(event.pointerId);
      } else if (this.pointers.size === 2) {
        this.drawPointerId = -1;
        this.drawPending = false;
        this.pinch = this.pinchState();
        canvas.setPointerCapture(event.pointerId);
      }
    };

    this._onPointerMove = (event) => {
      const screen = this.localPoint(event);
      const previous = this.pointers.get(event.pointerId);
      this.pointers.set(event.pointerId, screen);
      if (this.drawPointerId === event.pointerId && this.pointers.size === 1 && previous) {
        const distance = Math.hypot(screen[0] - this.drawStart[0], screen[1] - this.drawStart[1]);
        if (this.drawPending && distance < 3) return;
        const [x, y] = this.toScenePoint(screen[0], screen[1]);
        const strip = this.config.strips[this.activeStrip];
        if (this.drawPending) {
          const [startX, startY] = this.toScenePoint(this.drawStart[0], this.drawStart[1]);
          strip.pts = [[
            Math.max(0.02, Math.min(0.98, startX / this.width)),
            Math.max(0.03, Math.min(0.97, startY / this.height)),
          ]];
          strip.shape = null;
          this.drawPending = false;
        }
        const next = [
          Math.max(0.02, Math.min(0.98, x / this.width)),
          Math.max(0.03, Math.min(0.97, y / this.height)),
        ];
        const last = strip.pts[strip.pts.length - 1];
        if (!last || Math.hypot((next[0] - last[0]) * this.width,
                                (next[1] - last[1]) * this.height) >= 3) {
          strip.pts.push(next);
        }
        this.invalidate();
        if (this.onGeometryChange) this.onGeometryChange();
        return;
      }
      if (this.pointers.size >= 2 && this.pinch) {
        const next = this.pinchState();
        const dx = next.center[0] - this.pinch.center[0];
        const dy = next.center[1] - this.pinch.center[1];
        this.config.panX = (Number(this.config.panX) || 0) + dx;
        this.config.panY = (Number(this.config.panY) || 0) + dy;
        this.zoomAt(next.center[0], next.center[1], next.distance / this.pinch.distance);
        this.pinch = next;
        this.invalidate();
        if (this.onGeometryChange) this.onGeometryChange();
        return;
      }
      this.updateHover(...this.toScenePoint(screen[0], screen[1]));
    };

    this._onPointerRelease = (event) => {
      const wasDrawing = this.drawPointerId >= 0 && !this.drawPending;
      if (event) this.pointers.delete(event.pointerId);
      if (!event || this.drawPointerId === event.pointerId) {
        this.drawPointerId = -1;
        this.drawPending = false;
        this.drawStart = null;
      }
      if (!event) this.pointers.clear();
      if (this.pointers.size < 2) this.pinch = null;
      if (wasDrawing && this.onPathCommit) this.onPathCommit();
    };

    this._onPointerLeave = () => {
      this._onPointerRelease();
      this.hover = { strip: -1, led: -1 };
      if (this.onHover) this.onHover(-1, -1, null);
    };

    this._onWheel = (event) => {
      event.preventDefault();
      const factor = Math.exp(-event.deltaY * 0.0015);
      this.zoomAt(...this.localPoint(event), factor);
      this.invalidate();
      if (this.onGeometryChange) this.onGeometryChange();
    };

    canvas.addEventListener('pointerdown', this._onPointerDown);
    canvas.addEventListener('pointermove', this._onPointerMove);
    canvas.addEventListener('pointerup', this._onPointerRelease);
    canvas.addEventListener('pointercancel', this._onPointerRelease);
    // Capture can be lost without either of the release events firing.
    canvas.addEventListener('lostpointercapture', this._onPointerRelease);
    canvas.addEventListener('pointerleave', this._onPointerLeave);
    canvas.addEventListener('wheel', this._onWheel, { passive: false });
  }

  pinchState() {
    const points = [...this.pointers.values()];
    const dx = points[1][0] - points[0][0];
    const dy = points[1][1] - points[0][1];
    return {
      center: [(points[0][0] + points[1][0]) * 0.5, (points[0][1] + points[1][1]) * 0.5],
      distance: Math.max(1, Math.hypot(dx, dy)),
    };
  }

  zoomAt(x, y, factor) {
    const oldZoom = Number.isFinite(this.config.zoom) ? this.config.zoom : 1;
    const nextZoom = Math.max(0.1, oldZoom * factor);
    const cx = this.width * 0.5;
    const cy = this.height * 0.5;
    const panX = Number(this.config.panX) || 0;
    const panY = Number(this.config.panY) || 0;
    const baseX = (x - cx - panX) / oldZoom;
    const baseY = (y - cy - panY) / oldZoom;
    this.config.zoom = nextZoom;
    this.config.panX = x - cx - baseX * nextZoom;
    this.config.panY = y - cy - baseY * nextZoom;
  }

  toScenePoint(x, y) {
    const zoom = Number.isFinite(this.config.zoom) ? this.config.zoom : 1;
    const panX = Number(this.config.panX) || 0;
    const panY = Number(this.config.panY) || 0;
    return [
      this.width * 0.5 + (x - this.width * 0.5 - panX) / zoom,
      this.height * 0.5 + (y - this.height * 0.5 - panY) / zoom,
    ];
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
    canvas.removeEventListener('wheel', this._onWheel);
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

  // Drawn above the grain so the path and LED spacing remain readable even
  // when the simulated light is dim or clipped.
  drawGuides() {
    const ctx = this.ctx;
    const geom = this.geometry[this.activeStrip];
    if (!geom || !geom.path.poly.length) return;
    ctx.save();
    ctx.strokeStyle = 'rgba(56, 189, 248, 0.42)';
    ctx.lineWidth = 1;
    ctx.setLineDash([5, 5]);
    ctx.beginPath();
    const poly = geom.path.poly;
    ctx.moveTo(poly[0][0], poly[0][1]);
    for (let i = 4; i < poly.length; i += 4) ctx.lineTo(poly[i][0], poly[i][1]);
    ctx.stroke();
    ctx.setLineDash([]);
    // LED markers only for the hovered pixel, ringed in the colour it is
    // showing. Permanent dark dots with white rings read as fake hardware.
    if (this.hover.strip === this.activeStrip && this.hover.led >= 0 && this.lastBytes) {
      const led = geom.leds.find((l) => l.index === this.hover.led);
      if (led) {
        let offset = 0;
        for (let i = 0; i < this.activeStrip; i++) offset += this.counts[i] * 3;
        this._paintStripIndex = this.activeStrip;
        const [r, g, b] = this.colourOf(this.lastBytes, offset, led.index);
        ctx.strokeStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(led.x, led.y, 6, 0, Math.PI * 2);
        ctx.stroke();
      }
    }
    ctx.restore();
  }

  fit(stripIndex) {
    if (!this.geometry) this.buildGeometry();
    const geom = this.geometry[stripIndex];
    const strip = this.config.strips[stripIndex];
    const stripMm = Number.isFinite(strip.lengthM)
      ? strip.lengthM * 1000
      : stripLengthMm(this.counts[stripIndex], geom.profile);
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
    const scale = this.geometry && this.geometry[0] ? this.geometry[0].pxPerMm : undefined;
    paintSurface(this.ctx, this.config.surface, this.width, this.height, scale);
  }

  paint(bytes) {
    if (!this.width) this.resize();
    this.lastBytes = bytes;
    const started = performance.now();

    if (!this.geometry) this.buildGeometry();
    this.clear();

    let offset = 0;
    for (let i = 0; i < this.geometry.length; i++) {
      const geom = this.geometry[i];
      this._paintStripIndex = i;
      this.paintStrip(geom, bytes, offset);
      offset += this.counts[i] * 3;
    }

    this.paintGrain();
    this.drawGuides();
    this.lastDrawMs = performance.now() - started;
    if (this.onFrame) this.onFrame(bytes);
  }

  colourOf(bytes, offset, index) {
    const at = offset + index * 3;
    const strip = this.config.strips[this._paintStripIndex || 0];
    const k = strip?.intensity ?? this.config.intensity;
    return expose(bytes[at] * k, bytes[at + 1] * k, bytes[at + 2] * k, this.config.ev);
  }

  paintStrip(geom, bytes, offset) {
    const real = this.config.look === 'realistic';
    if (real) paintPcb(this.ctx, geom);
    else this.drawSubstrate(geom);
    this.fillGlowBuffer(geom, bytes, offset, real);
    this.compositeEnvironment();
    this.compositeSpill(geom);
    this.compositeHalo();
    if (real) {
      paintBodies(this, geom, bytes, offset);
      paintSleeve(this.ctx, geom);
    } else {
      this.drawCores(geom, bytes, offset);
    }
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

  // Linear light for one pixel before the clip-to-white in expose().
  _glowAt(bytes, offset, index) {
    const at = offset + index * 3;
    const strip = this.config.strips[this._paintStripIndex || 0];
    const k = (strip?.intensity ?? this.config.intensity) * Math.pow(2, this.config.ev);
    const lin = (x) => Math.pow(x / 255, 2.2) * k;
    return [lin(bytes[at]), lin(bytes[at + 1]), lin(bytes[at + 2])];
  }

  fillGlowBuffer(geom, bytes, offset, real = false) {
    const ctx = this.glowCtx;
    ctx.save();
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.width, this.height);
    ctx.globalCompositeOperation = 'lighter';

    const halo = Math.max(geom.sigmaPx * (real ? 6 : 2.6), 3);
    for (const led of geom.leds) {
      const [r, g, b, peak] = this.colourOf(bytes, offset, led.index);
      if (peak < 0.004) continue;
      let rgb = `${r | 0},${g | 0},${b | 0}`;
      let strength = 1;
      if (real) {
        // Light keeps its hue as it spreads; only the source itself clips to
        // white. Normalising to the brightest channel is what keeps a red LED's
        // bloom red instead of a grey disc.
        const at = this._glowAt(bytes, offset, led.index);
        const m = Math.max(at[0], at[1], at[2], 1);
        rgb = `${(at[0] / m * 255) | 0},${(at[1] / m * 255) | 0},${(at[2] / m * 255) | 0}`;
        strength = Math.min(1.4, Math.pow(peak, 0.55) * 1.15);
      }
      for (const [sx, sy] of (real ? glowSpots(geom, led) : [[led.x, led.y]])) {
        if (real) {
          // Stacked layers rather than one curve: a faint veil that reaches far
          // across the room, then progressively tighter and hotter bloom
          // toward the die. One gradient cannot be both wide and hot.
          for (const [reach, weight] of GLOW_LAYERS) {
            const R = halo * reach;
            // Neighbours' light adds. Without this a dense strip's wide layers
            // stack until the whole stage is flooded; normalise by how many
            // pixels a layer overlaps.
            const overlap = Math.min(1, 2.5 / Math.max(1, (2 * R) / geom.pitchPx));
            const grad = ctx.createRadialGradient(sx, sy, 0, sx, sy, R);
            for (let i = 0; i <= 8; i++) {
              const t = i / 8;
              const a = Math.min(1, strength * weight * overlap * Math.pow(1 - t, 2.4));
              grad.addColorStop(t, `rgba(${rgb},${a.toFixed(3)})`);
            }
            ctx.fillStyle = grad;
            ctx.beginPath();
            ctx.arc(sx, sy, R, 0, Math.PI * 2);
            ctx.fill();
          }
        } else {
          const grad = ctx.createRadialGradient(sx, sy, 0, sx, sy, halo);
          grad.addColorStop(0, `rgba(${rgb},0.95)`);
          grad.addColorStop(0.28, `rgba(${rgb},0.42)`);
          grad.addColorStop(1, `rgba(${rgb},0)`);
          ctx.fillStyle = grad;
          ctx.beginPath();
          ctx.arc(sx, sy, halo, 0, Math.PI * 2);
          ctx.fill();
        }
      }

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

  // Room light: painted first, underneath the existing spill and halo.
  compositeEnvironment() {
    const spill = this.config.spill;
    if (spill <= 0.01) return;
    if (!this._env) this._env = new Map();
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'lighter';
    for (const [div, alpha] of ENV_LAYERS) {
      const w = Math.max(4, Math.ceil(this.width / div));
      const h = Math.max(4, Math.ceil(this.height / div));
      let e = this._env.get(div);
      if (!e) {
        e = document.createElement('canvas');
        this._env.set(div, e);
      }
      if (e.width !== w || e.height !== h) {
        e.width = w;
        e.height = h;
      }
      const ectx = e.getContext('2d');
      ectx.clearRect(0, 0, w, h);
      ectx.imageSmoothingQuality = 'high';
      ectx.filter = 'blur(1.2px)';
      ectx.drawImage(this.glow, 0, 0, w, h);
      ctx.imageSmoothingQuality = 'high';
      ctx.globalAlpha = Math.min(1, spill * alpha);
      ctx.drawImage(e, 0, 0, this.width, this.height);
    }
    ctx.restore();
  }

  compositeSpill(geom) {
    const spill = this.config.spill;
    if (spill <= 0.01) return;
    const ctx = this.ctx;
    ctx.save();
    ctx.globalCompositeOperation = 'screen';
    ctx.globalAlpha = Math.min(1, spill * (this.config.look === 'realistic' ? 0.7 : 0.55));
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
