// Canvas painting shared by the recording player and the live player.
//
// Both draw the same two strips out of a flat RGB byte array, so the strip
// geometry and the glow treatment live here. Written twice they would drift, and
// a visual difference between the two views would look like a firmware bug.

const errorBox = document.getElementById('error');

export function showError(message) {
  errorBox.textContent = message;
  errorBox.hidden = false;
}

export function hideError() {
  errorBox.hidden = true;
}

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// Positions for one strip, centred on its own row. The radius is derived from the
// spacing so the strip always fills the width without overlapping badly.
function stripRow(count, width, centreY, spanWidth, radiusScale, maxRadius) {
  const spacing = spanWidth / count;
  const r = clamp(spacing * radiusScale, 1.5, maxRadius);
  const x0 = (width - spanWidth) / 2 + spacing / 2;
  const row = new Array(count);
  for (let i = 0; i < count; ++i) row[i] = { x: x0 + i * spacing, y: centreY, r };
  return row;
}

// The two strips, drawn to a canvas that fills its parent. counts is
// [first strip length, second strip length] in the order the byte array uses.
export class LedCanvas {
  constructor(canvas, counts) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.counts = counts;
    this.layout = null;
  }

  buildLayout(width, height) {
    const pad = 20;
    const full = width - 2 * pad;
    const [n0, n1] = this.counts;
    return [
      stripRow(n0, width, height * 0.34, full, 0.40, 11),
      stripRow(n1, width, height * 0.72, full * 0.42, 0.42, 20),
    ];
  }

  resize() {
    const cssWidth = this.canvas.parentElement.clientWidth - 16;
    const cssHeight = clamp(Math.round(cssWidth * 0.26), 150, 300);
    const dpr = window.devicePixelRatio || 1;

    this.canvas.width = Math.round(cssWidth * dpr);
    this.canvas.height = Math.round(cssHeight * dpr);
    this.canvas.style.height = cssHeight + 'px';

    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.layout = this.buildLayout(cssWidth, cssHeight);

    // Assigning canvas.width resets the backing store, so the resize itself has to
    // repaint. Waiting for the next animation frame leaves a blank strip whenever
    // frames are not being produced, which is the whole time a player is paused.
    this.clear();
  }

  // One RGB byte per channel, oldest strip first, three bytes per LED.
  paint(bytes) {
    if (!this.layout) return;
    const ctx = this.ctx;
    const dpr = window.devicePixelRatio || 1;
    ctx.clearRect(0, 0, this.canvas.width / dpr, this.canvas.height / dpr);

    let offset = 0;
    for (let s = 0; s < this.layout.length; ++s) {
      const row = this.layout[s];
      for (let i = 0; i < this.counts[s]; ++i) {
        drawLed(ctx, row[i].x, row[i].y, row[i].r,
                bytes[offset], bytes[offset + 1], bytes[offset + 2]);
        offset += 3;
      }
    }
  }

  clear() {
    if (!this.layout) return;
    const dpr = window.devicePixelRatio || 1;
    this.ctx.clearRect(0, 0, this.canvas.width / dpr, this.canvas.height / dpr);
    for (const row of this.layout) {
      for (const led of row) drawLed(this.ctx, led.x, led.y, led.r, 0, 0, 0);
    }
  }
}

function drawLed(ctx, x, y, r, red, green, blue) {
  if (red + green + blue < 6) {
    ctx.fillStyle = '#26262f';
    ctx.beginPath();
    ctx.arc(x, y, r, 0, Math.PI * 2);
    ctx.fill();
    return;
  }
  // Glow first, additive, then the core on top. Cheaper than a per-LED shadow
  // and it keeps neighbouring pixels summing instead of occluding each other.
  ctx.globalCompositeOperation = 'lighter';
  ctx.globalAlpha = 0.22;
  ctx.fillStyle = `rgb(${red},${green},${blue})`;
  ctx.beginPath();
  ctx.arc(x, y, r * 2.1, 0, Math.PI * 2);
  ctx.fill();

  ctx.globalAlpha = 1;
  ctx.beginPath();
  ctx.arc(x, y, r, 0, Math.PI * 2);
  ctx.fill();

  ctx.globalCompositeOperation = 'source-over';
}

// A row of bars over the FFT's lower half. The firmware's magnitudes are raw sums
// in the hundreds to thousands and vary with the input's absolute level, so the
// scale comes from a decaying running peak rather than a fixed number: a fixed
// one reads as a flat line at one volume and a solid block at another.
export class Spectrum {
  constructor(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');

    // One reference per bar rather than one for the whole row. A single reference
    // is the row's loudest bin, and on any real music that bin is in the bass, so
    // every bar above it divided by a number several times its own and drew at a
    // few percent while the first five sat at 100%. Per-bar references make each
    // bar read its own recent activity, which is what the row is there to show.
    // This is an equaliser, but a per-bar one: a bar is quiet because it has been
    // quiet, not because the bass was loud.
    this.bars = 64;
    this.barPeak = new Float32Array(this.bars);
  }

  // Forget the per-bar references. Called when the page changes which input it is
  // analysing: these are running peaks, so they are a record of the loudest thing
  // each bar has seen, and the synthetic signal is about forty times the
  // microphone. Left in place across a switch they make every bar draw at a few
  // percent of its true height for as long as the old reference has not decayed,
  // which reads as bands that are too low rather than as stale references.
  reset() {
    this.barPeak.fill(0);
  }

  // `gate` is the firmware's silence gate, 0..1. Without it a per-bar reference
  // decays onto the room tone and silence draws a full row, which is the opposite
  // of what the gate exists for.
  paint(magnitudes, count, gate = 1) {
    const bars = this.bars;
    const step = Math.max(1, Math.floor(count / bars));
    let framePeak = 0;
    for (let i = 1; i < count; ++i) if (magnitudes[i] > framePeak) framePeak = magnitudes[i];
    // The floor both bars below track, so a band with no content at all does not
    // divide by its own noise.
    const floor = Math.max(framePeak * 0.02, 1e-4);

    const dpr = window.devicePixelRatio || 1;
    const cssWidth = this.canvas.parentElement.clientWidth - 16;
    const cssHeight = 48;
    if (this.canvas.width !== Math.round(cssWidth * dpr)) {
      this.canvas.width = Math.round(cssWidth * dpr);
      this.canvas.height = Math.round(cssHeight * dpr);
      this.canvas.style.height = cssHeight + 'px';
    }
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.ctx.clearRect(0, 0, cssWidth, cssHeight);

    const barWidth = cssWidth / bars;
    const lit = Math.max(0, Math.min(1, gate));
    for (let b = 0; b < bars; ++b) {
      let value = 0;
      for (let i = 0; i < step; ++i) {
        const bin = b * step + i + 1;
        if (bin < count && magnitudes[bin] > value) value = magnitudes[bin];
      }
      const ref = Math.max(value, this.barPeak[b] * 0.97, floor);
      this.barPeak[b] = ref;
      // Square root, because the ear's and the eye's response to magnitude is
      // closer to that than to the raw value, which reads as everything at the
      // bottom and nothing above it.
      const h = Math.min(1, Math.sqrt(value / ref)) * cssHeight * lit;
      this.ctx.fillStyle = `hsl(${200 + b * 1.6}, 80%, ${28 + (h / cssHeight) * 34}%)`;
      this.ctx.fillRect(b * barWidth, cssHeight - h, Math.max(1, barWidth - 2), h);
    }
  }
}
