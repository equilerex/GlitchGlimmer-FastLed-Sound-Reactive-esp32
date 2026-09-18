// Photographic response, not additive blending.
//
// The reason this is its own module: what separates a render that reads as
// light from one that reads as paint is not the glow radius, it is that a
// real sensor loses hue as it clips. A saturated red at four stops over is
// white on a photograph and red in every other LED preview. That behaviour
// is three lines of arithmetic, so it belongs somewhere it can be tested
// without a canvas.

const GAMMA = 2.2;

// Where clipping starts, and how far past it the desaturation takes to
// complete. Both are taste, tuned against the mockup: a knee below 0.85
// washes mid-brightness colour out, and a range under 1.0 snaps to white
// hard enough to look like a bug.
const KNEE = 0.85;
const KNEE_RANGE = 1.4;
const MAX_WHITEN = 0.92;

export const GRAIN_DOT_COUNT = 900;

const toLinear = (v) => Math.pow(v / 255, GAMMA);
const toDisplay = (v) => Math.pow(Math.min(1, v), 1 / GAMMA);

export function expose(r, g, b, ev) {
  const gain = Math.pow(2, ev);
  const lr = toLinear(r) * gain;
  const lg = toLinear(g) * gain;
  const lb = toLinear(b) * gain;

  const peak = Math.max(lr, lg, lb);
  if (peak === 0) return [0, 0, 0, 0];

  const over = Math.max(0, Math.min(1, (peak - KNEE) / KNEE_RANGE));
  const whiten = over * MAX_WHITEN;
  const mix = (v) => (v + (1 - v) * whiten) * 255;

  return [mix(toDisplay(lr)), mix(toDisplay(lg)), mix(toDisplay(lb)), peak];
}

export function grainAlpha(grain) {
  return Math.max(0, Math.min(1, grain)) * 0.5;
}
