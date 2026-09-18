// Canvas painting shared by the recording player and the live player.
//
// The strip rendering itself moved to viz/, which is where the physical model
// lives. This file keeps the two things that are not strips: the error banner
// and the spectrum panel. LedCanvas stays exported under its old name because
// app.js and live.js construct it by that name, and the point of the split was
// that neither of them had to care.

export { StripView as LedCanvas } from './viz/StripView.js';

export function showError(message) {
  const errorBox = document.getElementById('error');
  if(errorBox) {
    errorBox.textContent = message;
    errorBox.hidden = false;
  }
}

export function hideError() {
  const errorBox = document.getElementById('error');
  if(errorBox) errorBox.hidden = true;
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
