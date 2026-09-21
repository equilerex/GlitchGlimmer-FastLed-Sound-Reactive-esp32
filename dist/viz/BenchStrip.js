// The debugging half: the same pixels, straightened out and indexed.
//
// The stage answers "does this look right". This answers "which pixel is
// wrong", which the stage cannot, because a posed strip has no readable index
// anywhere on it. Both draw from the same byte array, so a disagreement
// between them is a bug in one of these two files and nowhere else.

import { expose } from './camera.js';
import { profileById } from './profiles.js';

export const BENCH_HEIGHT_PER_STRIP = 62;

const PAD = 10;
const TICK_EVERY = 10;
const MIN_TICK_SPACING = 28;

// The canvas colours below are hand-copies of --bg, --ink, --ink-dim and
// --accent from style.css (in that order of appearance), not literals chosen
// independently. paint() runs every frame, and resolving custom properties
// through getComputedStyle that often is not worth the layout read it forces.
export class BenchStrip {
  constructor(canvas, counts) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.counts = counts;
    this.width = 0;
    this.dpr = 1;
    this.hover = { strip: -1, led: -1 };
    this.resize();
  }

  resize() {
    const rect = this.canvas.getBoundingClientRect();
    this.dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.width = Math.max(320, Math.round(rect.width));
    this.height = BENCH_HEIGHT_PER_STRIP * this.counts.length;
    this.canvas.width = this.width * this.dpr;
    this.canvas.height = this.height * this.dpr;
    this.ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
  }

  setCounts(counts) {
    this.counts = counts;
    this.resize();
  }

  setHover(stripIndex, ledIndex) {
    this.hover = { strip: stripIndex, led: ledIndex };
  }

  paint(bytes, config) {
    const ctx = this.ctx;
    ctx.fillStyle = '#09090b';
    ctx.fillRect(0, 0, this.width, this.height);

    let offset = 0;
    for (let s = 0; s < this.counts.length; s++) {
      this.paintLane(ctx, s, bytes, offset, config);
      offset += this.counts[s] * 3;
    }
  }

  paintLane(ctx, stripIndex, bytes, offset, config) {
    const count = this.counts[stripIndex];
    const top = stripIndex * BENCH_HEIGHT_PER_STRIP;
    const span = this.width - PAD * 2;
    const cell = span / count;
    const profile = profileById(config.strips[stripIndex].profile);
    const k = config.strips[stripIndex].intensity ?? config.intensity;

    for (let i = 0; i < count; i++) {
      const at = offset + i * 3;
      const [r, g, b] = expose(bytes[at] * k, bytes[at + 1] * k, bytes[at + 2] * k, config.ev);
      const x = PAD + i * cell;

      ctx.fillStyle = `rgb(${r | 0},${g | 0},${b | 0})`;
      ctx.fillRect(x, top + 8, Math.max(cell - (cell > 4 ? 1 : 0), 0.6), 24);

      // Luma, so a pixel that is bright but the wrong hue still reads as
      // present. Rec. 709 weights, because that is what the eye does.
      const luma = (0.2126 * r + 0.7152 * g + 0.0722 * b) / 255;
      ctx.fillStyle = 'rgba(244, 244, 245, 0.32)';
      ctx.fillRect(x, top + 54 - luma * 16, Math.max(cell - 1, 0.6), luma * 16);
    }

    ctx.strokeStyle = 'rgba(244, 244, 245, 0.14)';
    ctx.fillStyle = '#a1a1aa';
    ctx.font = '9px ui-monospace, SFMono-Regular, Menlo, monospace';
    for (let i = 0; i < count; i += TICK_EVERY) {
      const x = PAD + i * cell;
      ctx.beginPath();
      ctx.moveTo(x, top + 34);
      ctx.lineTo(x, top + 39);
      ctx.stroke();
      if (cell * TICK_EVERY > MIN_TICK_SPACING) {
        // Measured, not estimated: the last tick can sit within a label's width
        // of the right edge, and a number clipped in half is worse than one
        // nudged left.
        const label = String(i);
        const w = ctx.measureText(label).width;
        ctx.fillText(label, Math.min(x + 2, this.width - PAD - w), top + 47);
      }
    }

    ctx.fillStyle = '#a1a1aa';
    ctx.fillText(`${profile.name} · ${count} px`, PAD, top + 6);

    if (this.hover.strip === stripIndex && this.hover.led >= 0) {
      const x = PAD + this.hover.led * cell;
      ctx.strokeStyle = '#38bdf8';
      ctx.lineWidth = 1;
      ctx.strokeRect(x - 1, top + 6, Math.max(cell, 2) + 2, 28);
    }
  }
}
