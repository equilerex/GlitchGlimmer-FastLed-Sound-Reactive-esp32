// The "realistic" look: what the strip is made of, drawn to scale.
//
// The stylized look is glow first, with a disc where the die is. This one draws
// the physical package and lets the same glow passes light it, so a WS2812B
// reads as a white 5050 on a black PCB, a COB as a phosphor bar, an FCOB as a
// long seamless stretch per IC, and a pebble string as milky beads on a black
// cable.
//
// It is a second renderer selected by `config.look`, not a per-type branch in
// the stylized lit path. Placement is untouched: every LED still sits where
// placePixels put it, at the profile's pitch. All dimensions come from the
// profile's `body`, in millimetres, and are scaled by geom.pxPerMm.

import { pointAtSmooth, angleAt } from './path.js';

// How lit a dark room leaves an unlit part. Physical materials, so these are
// literals and do not follow the page theme.
const AMBIENT_PCB = 0.22;
const AMBIENT_PKG = 0.5;

const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const rgb = (r, g, b, a = 1) => `rgba(${r | 0},${g | 0},${b | 0},${a})`;
const toward = (v, k) => v + (255 - v) * k;

function hexToRgb(hex) {
  const n = parseInt(hex.slice(1), 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

function dimmed(hex, k) {
  const [r, g, b] = hexToRgb(hex);
  return rgb(r * k, g * k, b * k);
}

// The polyline of a stretch of path, sampled finely enough for a curve.
function span(path, from, to) {
  const a = clamp(from, 0, path.total);
  const b = clamp(to, 0, path.total);
  const steps = Math.max(1, Math.ceil((b - a) / 5));
  const out = [];
  for (let i = 0; i <= steps; i++) out.push(pointAtSmooth(path, a + (b - a) * (i / steps)));
  return out;
}

function stroke(ctx, pts, width, style, cap = 'butt') {
  if (pts.length < 2) return;
  ctx.strokeStyle = style;
  ctx.lineWidth = width;
  ctx.lineCap = cap;
  ctx.lineJoin = 'round';
  ctx.beginPath();
  ctx.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
  ctx.stroke();
}

function rrect(ctx, x, y, w, h, r) {
  ctx.beginPath();
  if (ctx.roundRect) ctx.roundRect(x, y, w, h, r);
  else ctx.rect(x, y, w, h);
}

// The spine is the strip's physical extent: first pixel's start to last
// pixel's end. Drawing the substrate along the whole path would leave a dead
// tail whenever the path is longer than the strip.
function spine(geom) {
  if (geom.spine) return geom.spine;
  const { leds, pitchPx, path } = geom;
  if (!leds.length) return (geom.spine = []);
  const last = leds[leds.length - 1];
  geom.spine = span(path, leds[0].at - pitchPx / 2, last.at + pitchPx / 2);
  return geom.spine;
}

export function paintPcb(ctx, geom) {
  const { body, pxPerMm, leds, pitchPx, path } = geom;
  const pts = spine(geom);
  if (pts.length < 2) return;
  const width = Math.max(1.5, body.pcb.w * pxPerMm);
  const isBead = body.fill === 'bead';

  ctx.save();
  if (!isBead) stroke(ctx, pts, width + 1.6, 'rgba(0,0,0,0.45)');
  stroke(ctx, pts, width, dimmed(body.pcb.color, isBead ? 1 : AMBIENT_PCB), isBead ? 'round' : 'butt');
  // Cable sheen, so a black wire reads as a round object rather than a line.
  if (isBead) stroke(ctx, pts, Math.max(0.6, width * 0.3), 'rgba(255,255,255,0.10)', 'round');

  if (body.pcb.pads && pitchPx >= 7 && leds.length < 800) {
    const across = width * 0.27;
    const padLen = Math.max(1.2, 2.2 * pxPerMm);
    const padWid = Math.max(1, width * 0.2);
    ctx.fillStyle = 'rgba(184, 116, 63, 0.75)';
    for (let i = 0; i <= leds.length; i++) {
      const at = (i) * pitchPx + (leds[0].at - pitchPx / 2);
      if (at < 0 || at > path.total) continue;
      const [x, y] = pointAtSmooth(path, at);
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(angleAt(path, at, Math.max(3, pitchPx * 0.4)));
      for (const off of [-across, 0, across]) {
        rrect(ctx, -padLen / 2, off - padWid / 2, padLen, padWid, padWid * 0.4);
        ctx.fill();
      }
      ctx.restore();
    }
  }
  ctx.restore();
}

export function paintSleeve(ctx, geom) {
  const { body, pxPerMm } = geom;
  if (!body.sleeve) return;
  const pts = spine(geom);
  ctx.save();
  stroke(ctx, pts, body.sleeve * pxPerMm, 'rgba(205, 215, 230, 0.075)', 'round');
  stroke(ctx, pts, body.sleeve * pxPerMm * 0.55, 'rgba(255, 255, 255, 0.045)', 'round');
  ctx.restore();
}

// Where the glow buffer should put light for one pixel. A pixel that is a long
// stretch has to light the whole stretch, not just its middle.
export function glowSpots(geom, led) {
  const gap = geom.sigmaPx * 1.2;
  if (!geom.stretchPx || geom.stretchPx <= gap) return [[led.x, led.y]];
  const n = Math.ceil(geom.stretchPx / gap);
  const out = [];
  for (let i = 0; i < n; i++) {
    out.push(pointAtSmooth(geom.path, led.at - geom.stretchPx / 2 + ((i + 0.5) / n) * geom.stretchPx));
  }
  return out;
}

function packageLed(view, geom, led, c) {
  const ctx = view.ctx;
  const { body, pxPerMm, sizeK } = geom;
  const L = body.len * pxPerMm * sizeK;
  const W = body.wid * pxPerMm * sizeK;
  const [r, g, b, peak] = c;
  const lit = Math.min(1, peak * 1.5);
  const hot = Math.min(1, peak * 1.25);

  ctx.save();
  ctx.translate(led.x, led.y);
  ctx.rotate(led.angle);

  if (Math.min(L, W) < 3) {
    // Below a few pixels the package is not resolvable; a disc is what a
    // camera would record.
    ctx.fillStyle = rgb(toward(r, hot * 0.8), toward(g, hot * 0.8), toward(b, hot * 0.8), 0.6 + 0.4 * lit);
    ctx.beginPath();
    ctx.arc(0, 0, Math.max(L, W) * 0.5, 0, Math.PI * 2);
    ctx.fill();
    ctx.restore();
    return;
  }

  // White plastic, tinted by the light inside it.
  const u = 255 * AMBIENT_PKG;
  const tint = (v) => u + (toward(v, hot * 0.35) * 0.9 - u) * lit;
  ctx.fillStyle = rgb(tint(r), tint(g), tint(b));
  rrect(ctx, -L / 2, -W / 2, L, W, Math.min(L, W) * 0.08);
  ctx.fill();
  ctx.strokeStyle = 'rgba(0,0,0,0.35)';
  ctx.lineWidth = 0.8;
  ctx.stroke();

  // Reflector cup, and the die inside it.
  const R = Math.min(L, W) * 0.4;
  const grad = ctx.createRadialGradient(0, 0, 0, 0, 0, R);
  if (lit > 0.03) {
    grad.addColorStop(0, rgb(toward(r, hot * 0.9), toward(g, hot * 0.9), toward(b, hot * 0.9), 1));
    grad.addColorStop(0.55, rgb(r, g, b, 0.95));
    grad.addColorStop(1, rgb(r * 0.7, g * 0.7, b * 0.7, 0.9));
  } else {
    grad.addColorStop(0, 'rgb(64,70,80)');
    grad.addColorStop(1, 'rgb(34,38,46)');
  }
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.arc(0, 0, R, 0, Math.PI * 2);
  ctx.fill();
  if (lit < 0.6) {
    ctx.fillStyle = `rgba(10,10,12,${0.8 * (1 - lit)})`;
    ctx.fillRect(-R * 0.14, -R * 0.14, R * 0.28, R * 0.28);
  }
  ctx.restore();
}

function phosphorLed(view, geom, led, c, prev, next) {
  const ctx = view.ctx;
  const { body, pxPerMm, sizeK, stretchPx, path, pitchPx } = geom;
  const W = Math.max(1.5, body.wid * pxPerMm * sizeK);
  const a0 = led.at - stretchPx / 2;
  const a1 = led.at + stretchPx / 2;
  const [r, g, b, peak] = c;
  const lit = Math.min(1, peak * 1.5);

  ctx.save();
  // Unlit phosphor: dull warm off-white.
  stroke(ctx, span(path, a0, a1), W, dimmed('#d8d2c2', 0.3));
  if (peak < 0.008) {
    ctx.restore();
    return;
  }

  const edge = body.blend;
  const parts = edge > 0 ? clamp(Math.round(stretchPx / 2), 4, 12) : 1;
  for (let j = 0; j < parts; j++) {
    const u = (j + 0.5) / parts;
    let cr = r; let cg = g; let cb = b; let cp = peak;
    // Neighbouring pixels of a flip-chip strip bleed into each other across
    // the join; the width of that transition is `blend`.
    if (edge > 0 && u < edge) {
      const t = 0.5 + 0.5 * (u / edge);
      cr = prev[0] + (r - prev[0]) * t; cg = prev[1] + (g - prev[1]) * t;
      cb = prev[2] + (b - prev[2]) * t; cp = prev[3] + (peak - prev[3]) * t;
    } else if (edge > 0 && u > 1 - edge) {
      const t = 0.5 + 0.5 * ((1 - u) / edge);
      cr = next[0] + (r - next[0]) * t; cg = next[1] + (g - next[1]) * t;
      cb = next[2] + (b - next[2]) * t; cp = next[3] + (peak - next[3]) * t;
    }
    const seg = span(path, a0 + (j / parts) * stretchPx - 0.4, a0 + ((j + 1) / parts) * stretchPx + 0.4);
    const k = Math.min(1, cp * 1.5);
    stroke(ctx, seg, W, rgb(cr, cg, cb, 0.35 + 0.6 * k));
    ctx.globalCompositeOperation = 'lighter';
    const h = Math.min(1, cp * 1.25) * 0.8;
    stroke(ctx, seg, W * 0.45, rgb(toward(cr, h), toward(cg, h), toward(cb, h), 0.55 * k));
    ctx.globalCompositeOperation = 'source-over';
  }

  // Individual LED chips along a bar dense enough to show them but not so
  // dense they are one smear.
  if (body.dot > 0 && pitchPx >= 3 && lit > 0.05) {
    ctx.globalCompositeOperation = 'lighter';
    ctx.fillStyle = rgb(255, 255, 255, 0.28 * lit);
    ctx.beginPath();
    ctx.arc(led.x, led.y, Math.max(0.5, W * 0.2), 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

function beadLed(view, geom, led, c) {
  const ctx = view.ctx;
  const { body, pxPerMm, sizeK, pitchPx } = geom;
  const L = Math.min(Math.max(1.6, body.len * pxPerMm * sizeK), pitchPx * 0.95);
  const W = Math.max(1.6, body.wid * pxPerMm * sizeK);
  const [r, g, b, peak] = c;
  const lit = Math.min(1, peak * 1.5);
  const hot = Math.min(1, peak * 1.25);

  ctx.save();
  ctx.translate(led.x, led.y);
  ctx.rotate(led.angle);
  const grad = ctx.createRadialGradient(0, 0, 0, 0, 0, Math.max(L, W) * 0.55);
  if (lit > 0.03) {
    grad.addColorStop(0, rgb(toward(r, hot * 0.85), toward(g, hot * 0.85), toward(b, hot * 0.85), 1));
    grad.addColorStop(0.6, rgb(r, g, b, 0.95));
    grad.addColorStop(1, rgb(r * 0.65, g * 0.65, b * 0.65, 0.9));
  } else {
    grad.addColorStop(0, 'rgb(96,100,108)');
    grad.addColorStop(1, 'rgb(46,49,56)');
  }
  ctx.fillStyle = grad;
  ctx.beginPath();
  ctx.ellipse(0, 0, L / 2, W / 2, 0, 0, Math.PI * 2);
  ctx.fill();

  // A soft catchlight is what makes translucent epoxy read as a solid.
  if (L > 5) {
    ctx.fillStyle = `rgba(255,255,255,${0.22 - 0.1 * lit})`;
    ctx.beginPath();
    ctx.ellipse(-L * 0.14, -W * 0.2, L * 0.22, W * 0.14, 0, 0, Math.PI * 2);
    ctx.fill();
  }
  ctx.restore();
}

const PAINTERS = { package: packageLed, phosphor: phosphorLed, bead: beadLed };

export function paintBodies(view, geom, bytes, offset) {
  const paint = PAINTERS[geom.body.fill];
  if (!paint) return;
  const count = view.counts[view._paintStripIndex || 0] || 1;
  for (const led of geom.leds) {
    const c = view.colourOf(bytes, offset, led.index);
    const blends = geom.body.blend > 0;
    const prev = blends ? view.colourOf(bytes, offset, Math.max(0, led.index - 1)) : c;
    const next = blends ? view.colourOf(bytes, offset, Math.min(count - 1, led.index + 1)) : c;
    paint(view, geom, led, c, prev, next);
  }
}
