// What the light lands on.
//
// Procedural rather than photographic, because a photograph has its own
// exposure baked in and compositing emitted light onto it fights that. A drawn
// surface is dim and neutral by construction, so the spill pass is the only
// thing that lights it.

export const SURFACES = [
  { id: 'room', name: 'Dark room' },
  { id: 'slat', name: 'Slat wall' },
  { id: 'rod', name: 'Bar rod' },
];

const SLAT_PITCH = 34;
const ROD_WIDTH = 52;

// Whole-strip ordering, not per-pixel depth. The rod is painted between the two
// strips, so strip 1 reads as being in front of it and strip 0 behind. A strip
// genuinely spiralling around the rod would need a depth per pixel and two glow
// passes per strip; this buys most of that look for one extra draw call.
//
// Strip 0 ending up behind the rod is a consequence of that ordering rather
// than something anyone asked for. Do not "fix" it by reordering without
// replacing the model.
export function occludes(id) {
  return id === 'rod';
}

export function paintSurface(ctx, id, width, height) {
  if (id === 'slat') return paintSlat(ctx, width, height);
  if (id === 'rod') return paintRodBackdrop(ctx, width, height);
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

function paintSlat(ctx, width, height) {
  ctx.fillStyle = '#101216';
  ctx.fillRect(0, 0, width, height);
  for (let y = 0; y < height; y += SLAT_PITCH) {
    const grad = ctx.createLinearGradient(0, y, 0, y + SLAT_PITCH);
    grad.addColorStop(0, '#191d22');
    grad.addColorStop(0.62, '#14171c');
    grad.addColorStop(1, '#0a0c0f');
    ctx.fillStyle = grad;
    ctx.fillRect(0, y, width, SLAT_PITCH - 5);
    ctx.fillStyle = '#050607';
    ctx.fillRect(0, y + SLAT_PITCH - 5, width, 5);
  }
}

function paintRodBackdrop(ctx, width, height) {
  const grad = ctx.createLinearGradient(0, 0, 0, height);
  grad.addColorStop(0, '#0c0e12');
  grad.addColorStop(1, '#07080b');
  ctx.fillStyle = grad;
  ctx.fillRect(0, 0, width, height);
}

export function paintRod(ctx, width, height) {
  const cx = width * 0.5;
  const grad = ctx.createLinearGradient(cx - ROD_WIDTH / 2, 0, cx + ROD_WIDTH / 2, 0);
  grad.addColorStop(0, '#0a0b0e');
  grad.addColorStop(0.32, '#2a2f38');
  grad.addColorStop(0.5, '#3c434f');
  grad.addColorStop(0.75, '#1b1f26');
  grad.addColorStop(1, '#070809');
  ctx.fillStyle = grad;
  ctx.fillRect(cx - ROD_WIDTH / 2, -10, ROD_WIDTH, height + 20);
}
