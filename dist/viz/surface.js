// What the light lands on.
//
// Procedural rather than photographic, because a photograph has its own
// exposure baked in and compositing emitted light onto it fights that. A drawn
// surface is dim and neutral by construction, so the spill pass is the only
// thing that lights it.

export const SURFACES = [
  { id: 'room', name: 'Dark room' },
  { id: 'light', name: 'Light room' },
  { id: 'slat', name: 'Wood slat wall' },
];

// Acoustic slat panel: real dimensions, so a strip laid in a gap sits between
// the boards the way it would on the wall. Gap is the black felt backing.
const SLAT_MM = 24;
const GAP_MM = 10;

export function paintSurface(ctx, id, width, height, pxPerMm = 0.3) {
  if (id === 'slat') return paintSlat(ctx, width, height, pxPerMm);
  if (id === 'light') return paintLight(ctx, width, height);
  return paintRoom(ctx, width, height);
}

function paintRoom(ctx, width, height) {
  const grad = ctx.createLinearGradient(0, 0, 0, height);
  grad.addColorStop(0, '#0d1014');
  grad.addColorStop(0.72, '#0a0c0f');
  grad.addColorStop(1, '#06070a');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, width, height);

  // The floor line. One stroke, but without it the room has no depth and the
  // spill has nothing to describe.
  ctx.save();
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.035)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(0, height * 0.78);
  ctx.lineTo(width, height * 0.78);
  ctx.stroke();
  ctx.restore();
}

// A lit room: a pale wall with the light falling off toward the corners. The
// strips are additive light, so on this they read mostly through their bodies.
function paintLight(ctx, width, height) {
  const grad = ctx.createLinearGradient(0, 0, 0, height);
  grad.addColorStop(0, '#dcd8d0');
  grad.addColorStop(0.75, '#cfcac1');
  grad.addColorStop(1, '#b9b4ab');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, width, height);
  const vig = ctx.createRadialGradient(width / 2, height / 2, Math.min(width, height) * 0.3,
    width / 2, height / 2, Math.max(width, height) * 0.75);
  vig.addColorStop(0, 'rgba(0,0,0,0)');
  vig.addColorStop(1, 'rgba(0,0,0,0.22)');
  ctx.fillStyle = vig;
  ctx.fillRect(0, 0, width, height);
  ctx.fillStyle = 'rgba(0,0,0,0.08)';
  ctx.fillRect(0, height * 0.78, width, 1);
}

let slatCache = null;

function paintSlat(ctx, width, height, pxPerMm) {
  const key = `${width}x${height}@${pxPerMm.toFixed(4)}`;
  if (!slatCache || slatCache.key !== key) {
    const cv = document.createElement('canvas');
    cv.width = Math.max(1, Math.round(width));
    cv.height = Math.max(1, Math.round(height));
    drawSlats(cv.getContext('2d'), width, height, pxPerMm);
    slatCache = { key, cv };
  }
  ctx.drawImage(slatCache.cv, 0, 0, width, height);
}

// Deterministic, so the wall does not shimmer between frames or rebuilds.
function rand(seed) {
  const x = Math.sin(seed * 127.1 + 311.7) * 43758.5453;
  return x - Math.floor(x);
}

function drawSlats(ctx, width, height, pxPerMm) {
  const slat = Math.max(3, SLAT_MM * pxPerMm);
  const gap = Math.max(1.5, GAP_MM * pxPerMm);
  ctx.fillStyle = '#08090a';
  ctx.fillRect(0, 0, width, height);
  let n = 0;
  for (let x = gap / 2; x < width; x += slat + gap, n++) {
    const tone = 0.85 + rand(n) * 0.3;
    const grad = ctx.createLinearGradient(x, 0, x + slat, 0);
    grad.addColorStop(0, `rgb(${96 * tone | 0},${58 * tone | 0},${38 * tone | 0})`);
    grad.addColorStop(0.5, `rgb(${118 * tone | 0},${72 * tone | 0},${48 * tone | 0})`);
    grad.addColorStop(1, `rgb(${90 * tone | 0},${54 * tone | 0},${35 * tone | 0})`);
    ctx.fillStyle = grad;
    ctx.fillRect(x, 0, slat, height);
    // Grain: long faint vertical streaks.
    const streaks = Math.max(4, Math.round(slat / 2));
    for (let i = 0; i < streaks; i++) {
      const gx = x + rand(n * 31 + i) * slat;
      const dark = rand(n * 57 + i) > 0.5;
      ctx.strokeStyle = dark ? 'rgba(30,14,6,0.16)' : 'rgba(190,130,90,0.10)';
      ctx.lineWidth = 0.6 + rand(n * 13 + i) * 0.8;
      ctx.beginPath();
      ctx.moveTo(gx, 0);
      ctx.lineTo(gx + (rand(n + i) - 0.5) * 3, height);
      ctx.stroke();
    }
    // Rounded edge, dark toward the gap.
    ctx.fillStyle = 'rgba(0,0,0,0.28)';
    ctx.fillRect(x, 0, Math.min(1.5, slat * 0.08), height);
    ctx.fillRect(x + slat - Math.min(1.5, slat * 0.08), 0, Math.min(1.5, slat * 0.08), height);
  }
}
