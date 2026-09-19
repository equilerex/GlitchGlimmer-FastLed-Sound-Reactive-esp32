// The posed shape of a strip, as a catmull-rom spline through four handles.
//
// Catmull-rom rather than bezier because the handles are the curve: a person
// dragging a strip around expects the thing under the cursor to be on the
// strip, and bezier control points are not.
//
// Everything here is arc-length based. A spline parameter is not distance, and
// pixels on a real strip are evenly spaced in distance, so spacing pixels by
// the parameter bunches them up in the corners.

// Each preset is four handles, because four is what a catmull-rom spline needs
// to express a single bend or a single reversal and no more. A preset that
// wanted five would be a shape the user should drag for themselves.
export const SHAPES = {
  line:   [[0.08, 0.50], [0.36, 0.50], [0.64, 0.50], [0.92, 0.50]],
  arc:    [[0.08, 0.70], [0.34, 0.30], [0.66, 0.30], [0.92, 0.70]],
  bowl:   [[0.08, 0.24], [0.36, 0.76], [0.64, 0.76], [0.92, 0.24]],
  wave:   [[0.06, 0.50], [0.34, 0.24], [0.66, 0.76], [0.94, 0.50]],
  zigzag: [[0.08, 0.26], [0.36, 0.72], [0.64, 0.26], [0.92, 0.72]],
  drape:  [[0.08, 0.30], [0.34, 0.78], [0.66, 0.72], [0.92, 0.24]],
  corner: [[0.12, 0.14], [0.12, 0.58], [0.40, 0.84], [0.90, 0.84]],
  column: [[0.50, 0.08], [0.50, 0.36], [0.50, 0.64], [0.50, 0.92]],
  diag:   [[0.10, 0.12], [0.36, 0.38], [0.64, 0.62], [0.90, 0.88]],
  wrap:   [[0.20, 0.14], [0.50, 0.38], [0.50, 0.62], [0.80, 0.86]],
};

// The opening pose of each strip, reproducing the layout the page had before
// this renderer: strip 0 straight across the upper third, strip 1 a shorter and
// shallower arc below it, clear of strip 0.
//
// Deliberately not SHAPES entries. A SHAPES entry is a pose the user picks for
// whichever strip is selected, so it spans the full stage; these are per-strip
// and have to leave room for each other.
export const DEFAULT_POSES = [
  // Strip 0: upper arch rising toward center, curving left to right
  [[0.06, 0.52], [0.32, 0.22], [0.68, 0.22], [0.94, 0.52]],
  // Strip 1: mirrored lower arch dipping toward center, curving left to right
  [[0.06, 0.62], [0.32, 0.86], [0.68, 0.86], [0.94, 0.62]],
  // Strip 2 (if present): straight horizontal close-up in the middle
  [[0.14, 0.54], [0.38, 0.54], [0.62, 0.54], [0.86, 0.54]],
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

function appendLimited(poly, cum, total, point, limit) {
  if (!poly.length) {
    poly.push(point);
    cum.push(0);
    return 0;
  }
  const previous = poly[poly.length - 1];
  const segment = Math.hypot(point[0] - previous[0], point[1] - previous[1]);
  if (total + segment <= limit) {
    poly.push(point);
    total += segment;
    cum.push(total);
    return total;
  }
  if (segment > 0) {
    const t = (limit - total) / segment;
    poly.push([
      previous[0] + (point[0] - previous[0]) * t,
      previous[1] + (point[1] - previous[1]) * t,
    ]);
    cum.push(limit);
  }
  return limit;
}

// Extend a posed path with bounded loops. The first section is the user's actual
// pose; longer software strips continue through circular lanes inside the
// canvas. Keeping the extension bounded means a long software strip remains
// inspectable instead of disappearing beyond an edge.
export function sampleWrappedPath(pts, width, height, lengthPx, samples = DEFAULT_SAMPLES) {
  const limit = Math.max(0, lengthPx);
  const poly = [];
  const cum = [];
  let total = 0;
  const margin = Math.min(12, width * 0.04, height * 0.04);
  const bounded = (point) => [
    Math.max(margin, Math.min(width - margin, point[0])),
    Math.max(margin, Math.min(height - margin, point[1])),
  ];
  const base = samplePath(pts, width, height, samples);
  const basePoly = base.poly.map(bounded);

  for (const point of basePoly) {
    total = appendLimited(poly, cum, total, point, limit);
    if (total >= limit) return { poly, cum, total };
  }

  if (!poly.length || total >= limit) return { poly, cum, total };

  const cx = width * 0.5;
  const cy = height * 0.5;
  const minRadius = Math.max(18, Math.min(width, height) * 0.12);
  const maxRadius = Math.max(minRadius, Math.min(width, height) * 0.42);
  const pointsPerLoop = 96;
  let loop = 0;
  let angle = Math.atan2(poly[poly.length - 1][1] - cy, poly[poly.length - 1][0] - cx);

  while (total < limit) {
    const radius = minRadius + (maxRadius - minRadius) * ((loop % 4) / 3);
    const direction = loop % 2 ? -1 : 1;
    for (let i = 1; i <= pointsPerLoop; i++) {
      angle += direction * (Math.PI * 2) / pointsPerLoop;
      const point = bounded([
        cx + Math.cos(angle) * radius,
        cy + Math.sin(angle) * radius,
      ]);
      total = appendLimited(poly, cum, total, point, limit);
      if (total >= limit) break;
    }
    loop += 1;
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

// Place pixels across the path.
// When count > 0, we distribute all count pixels along the entire arc length of
// the path so the visual curve is always complete and filled from end to end.
export function placePixels(path, count, pitchPx) {
  const out = [];
  if (count <= 0 || path.total <= 0) return out;
  if (count === 1) {
    const [x, y] = pointAt(path, path.total * 0.5);
    out.push({ x, y, index: 0 });
    return out;
  }
  // If a fixed pitch is forced (true scale), place at steps; otherwise distribute across full path
  const step = pitchPx > 0 ? pitchPx : (path.total / count);
  for (let i = 0; i < count; i++) {
    const at = (i + 0.5) * step;
    if (at > path.total) break;
    const [x, y] = pointAt(path, at);
    out.push({ x, y, index: i });
  }
  return out;
}

export function fitScale(path, count, pitchMm) {
  // Return pixels-per-mm so that the total strip length (count * pitchMm) exactly matches path.total
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
