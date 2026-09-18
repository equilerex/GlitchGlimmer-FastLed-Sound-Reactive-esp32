// The posed shape of a strip, as a catmull-rom spline through four handles.
//
// Catmull-rom rather than bezier because the handles are the curve: a person
// dragging a strip around expects the thing under the cursor to be on the
// strip, and bezier control points are not.
//
// Everything here is arc-length based. A spline parameter is not distance, and
// pixels on a real strip are evenly spaced in distance, so spacing pixels by
// the parameter bunches them up in the corners.

export const SHAPES = {
  line:   [[0.08, 0.50], [0.36, 0.50], [0.64, 0.50], [0.92, 0.50]],
  arc:    [[0.08, 0.70], [0.34, 0.30], [0.66, 0.30], [0.92, 0.70]],
  zigzag: [[0.08, 0.26], [0.36, 0.72], [0.64, 0.26], [0.92, 0.72]],
  wrap:   [[0.20, 0.14], [0.50, 0.38], [0.50, 0.62], [0.80, 0.86]],
  drape:  [[0.08, 0.30], [0.34, 0.78], [0.66, 0.72], [0.92, 0.24]],
};

// The opening pose of each strip, reproducing the layout the page had before
// this renderer: strip 0 straight across the upper third, strip 1 a shorter and
// shallower arc below it, clear of strip 0.
//
// Deliberately not SHAPES entries. A SHAPES entry is a pose the user picks for
// whichever strip is selected, so it spans the full stage; these are per-strip
// and have to leave room for each other.
export const DEFAULT_POSES = [
  [[0.04, 0.34], [0.36, 0.34], [0.68, 0.34], [0.96, 0.34]],
  [[0.29, 0.78], [0.43, 0.66], [0.57, 0.66], [0.71, 0.78]],
];

export function defaultPose(stripIndex) {
  const pose = DEFAULT_POSES[stripIndex] || DEFAULT_POSES[DEFAULT_POSES.length - 1];
  return pose.map((p) => p.slice());
}

const DEFAULT_SAMPLES = 600;

function interpolate(pts, t) {
  const n = pts.length;
  const seg = Math.min(Math.floor(t * (n - 1)), n - 2);
  const u = t * (n - 1) - seg;
  const p0 = pts[Math.max(seg - 1, 0)];
  const p1 = pts[seg];
  const p2 = pts[seg + 1];
  const p3 = pts[Math.min(seg + 2, n - 1)];
  const u2 = u * u;
  const u3 = u2 * u;
  const axis = (a, b, c, d) => 0.5 * (
    (2 * b)
    + (-a + c) * u
    + (2 * a - 5 * b + 4 * c - d) * u2
    + (-a + 3 * b - 3 * c + d) * u3
  );
  return [axis(p0[0], p1[0], p2[0], p3[0]), axis(p0[1], p1[1], p2[1], p3[1])];
}

export function samplePath(pts, width, height, samples = DEFAULT_SAMPLES) {
  const abs = pts.map(([x, y]) => [x * width, y * height]);
  const poly = [];
  const cum = [];
  let total = 0;
  for (let i = 0; i <= samples; i++) {
    const p = interpolate(abs, i / samples);
    if (i > 0) {
      total += Math.hypot(p[0] - poly[i - 1][0], p[1] - poly[i - 1][1]);
    }
    poly.push(p);
    cum.push(total);
  }
  return { poly, cum, total };
}

export function pointAt(path, lengthPx) {
  const { poly, cum } = path;
  if (lengthPx <= 0) return poly[0];
  if (lengthPx >= cum[cum.length - 1]) return poly[poly.length - 1];
  let lo = 0;
  let hi = cum.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (cum[mid] < lengthPx) lo = mid + 1;
    else hi = mid;
  }
  return poly[lo];
}

// Pixel count comes from the firmware, so the path does not get to decide it.
// A path shorter than the strip simply runs out, which is the honest answer and
// the thing the fit readout reports.
export function placePixels(path, count, pitchPx) {
  const out = [];
  const step = Math.max(pitchPx, 0.25);
  for (let i = 0; i < count; i++) {
    const at = (i + 0.5) * step;
    if (at > path.total) break;
    const [x, y] = pointAt(path, at);
    out.push({ x, y, index: i });
  }
  return out;
}

export function fitScale(path, count, pitchMm) {
  const lengthMm = count * pitchMm;
  return lengthMm > 0 ? path.total / lengthMm : 1;
}

export function hitHandle(pts, width, height, x, y, radius = 16) {
  let best = -1;
  let bestDistance = radius;
  for (let i = 0; i < pts.length; i++) {
    const d = Math.hypot(pts[i][0] * width - x, pts[i][1] * height - y);
    if (d <= bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}
