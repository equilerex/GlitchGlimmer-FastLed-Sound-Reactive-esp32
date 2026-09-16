// Player for the frame dumps src/sim_main.cpp writes with --dump-frames.
//
// The byte layout is defined on the C++ side: per frame, oldest byte first,
// leds0 pixels then leds1 pixels, three bytes each in CRGB order, and
// manifest.json carries the counts. The static_assert in sim_main.cpp is what
// keeps this reader honest if CRGB ever stops being three bytes.

const canvas = document.getElementById('view');
const ctx = canvas.getContext('2d');
const errorBox = document.getElementById('error');
const playButton = document.getElementById('play');
const scrub = document.getElementById('scrub');
const buttons = document.getElementById('buttons');
const sceneLabel = document.getElementById('scene');
const moodLabel = document.getElementById('mood');
const timeLabel = document.getElementById('time');
const noteLabel = document.getElementById('note');

const MAX_FRAME_MS = 250;   // a backgrounded tab must not fast-forward the loop

let manifest = null;
let scenario = null;
let pixels = null;          // Uint8Array over the current recording
let layout = null;          // per-strip positions, rebuilt on resize
let frame = 0;
let playing = true;
let accumulator = 0;
let lastTimestamp = 0;
let lastDrawnFrame = -1;

function showError(message) {
  errorBox.textContent = message;
  errorBox.hidden = false;
}

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

// Positions for one strip, centred on its own row. The radius is derived from
// the spacing so the strip always fills the width without overlapping badly.
function stripRow(count, width, centreY, spanWidth, radiusScale, maxRadius) {
  const spacing = spanWidth / count;
  const r = clamp(spacing * radiusScale, 1.5, maxRadius);
  const x0 = (width - spanWidth) / 2 + spacing / 2;
  const row = new Array(count);
  for (let i = 0; i < count; ++i) row[i] = { x: x0 + i * spacing, y: centreY, r };
  return row;
}

function buildLayout(width, height) {
  if (!manifest) return null;
  const pad = 20;
  const full = width - 2 * pad;
  return [
    stripRow(manifest.leds0, width, height * 0.34, full, 0.40, 11),
    stripRow(manifest.leds1, width, height * 0.72, full * 0.42, 0.42, 20),
  ];
}

function resize() {
  const cssWidth = canvas.parentElement.clientWidth - 16;
  const cssHeight = clamp(Math.round(cssWidth * 0.26), 150, 300);
  const dpr = window.devicePixelRatio || 1;

  canvas.width = Math.round(cssWidth * dpr);
  canvas.height = Math.round(cssHeight * dpr);
  canvas.style.height = cssHeight + 'px';

  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  layout = buildLayout(cssWidth, cssHeight);
  lastDrawnFrame = -1;

  // Assigning canvas.width resets the backing store, so the resize itself has to
  // repaint. Waiting for the next animation frame leaves a blank strip whenever
  // frames are not being produced, which is the whole time the player is paused.
  draw();
}

function drawLed(x, y, r, red, green, blue) {
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

function draw() {
  if (!layout || !pixels) return;
  if (frame === lastDrawnFrame) return;
  lastDrawnFrame = frame;

  const w = canvas.width / (window.devicePixelRatio || 1);
  const h = canvas.height / (window.devicePixelRatio || 1);
  ctx.clearRect(0, 0, w, h);

  const base = frame * manifest.bytesPerFrame;
  const counts = [manifest.leds0, manifest.leds1];
  let offset = base;

  for (let s = 0; s < layout.length; ++s) {
    const row = layout[s];
    for (let i = 0; i < counts[s]; ++i) {
      drawLed(row[i].x, row[i].y, row[i].r,
              pixels[offset], pixels[offset + 1], pixels[offset + 2]);
      offset += 3;
    }
  }

  updateHud();
}

// Events are sorted by frame, so the active one is the last at or before the
// current frame. Binary search rather than a cached index because scrubbing
// jumps around.
function activeEvent(events) {
  let lo = 0, hi = events.length - 1, best = null;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (events[mid].frame <= frame) { best = events[mid]; lo = mid + 1; }
    else hi = mid - 1;
  }
  return best;
}

function updateHud() {
  const event = activeEvent(scenario.events);
  sceneLabel.textContent = event ? event.scene : '—';
  moodLabel.textContent = event ? event.mood : '—';
  timeLabel.textContent = (frame / manifest.fps).toFixed(1) + 's';
  scrub.value = String(frame);
}

function tick(timestamp) {
  if (lastTimestamp === 0) lastTimestamp = timestamp;
  const delta = Math.min(timestamp - lastTimestamp, MAX_FRAME_MS);
  lastTimestamp = timestamp;

  if (playing && scenario) {
    accumulator += delta;
    const step = 1000 / manifest.fps;
    while (accumulator >= step) {
      accumulator -= step;
      frame = (frame + 1) % scenario.frames;
    }
  }

  draw();
  requestAnimationFrame(tick);
}

async function selectScenario(id, startFrame = 0) {
  const next = manifest.scenarios.find(s => s.id === id);
  if (!next) return;

  scenario = next;
  frame = clamp(Math.round(startFrame), 0, next.frames - 1);
  accumulator = 0;
  lastDrawnFrame = -1;

  for (const button of buttons.children) {
    button.setAttribute('aria-pressed', String(button.dataset.id === id));
  }
  noteLabel.textContent = next.note;
  scrub.max = String(next.frames - 1);

  const response = await fetch('data/' + next.file);
  if (!response.ok) {
    showError(`Could not load data/${next.file} (HTTP ${response.status}).`);
    return;
  }
  pixels = new Uint8Array(await response.arrayBuffer());
  draw();
}

function setPlaying(next) {
  playing = next;
  playButton.textContent = playing ? 'Pause' : 'Play';
}

async function start() {
  let loaded;
  try {
    const response = await fetch('data/manifest.json');
    if (!response.ok) throw new Error('HTTP ' + response.status);
    loaded = await response.json();
  } catch (err) {
    showError(
      'Could not load data/manifest.json (' + err.message + ').\n\n' +
      'Recordings are generated, not committed. Build them with:\n\n' +
      '    pio run -e native\n' +
      '    .pio/build/native/program.exe --dump-frames web/data\n\n' +
      'Then serve this directory over HTTP, since fetch() is blocked on file://:\n\n' +
      '    python -m http.server 8000 --directory web');
    return;
  }

  manifest = loaded;

  for (const item of manifest.scenarios) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = item.label;
    button.dataset.id = item.id;
    button.addEventListener('click', () => selectScenario(item.id));
    buttons.appendChild(button);
  }

  resize();
  window.addEventListener('resize', resize);

  playButton.addEventListener('click', () => setPlaying(!playing));
  scrub.addEventListener('input', () => {
    frame = Number(scrub.value);
    accumulator = 0;
  });
  window.addEventListener('keydown', (event) => {
    if (event.code === 'Space') {
      event.preventDefault();
      setPlaying(!playing);
    } else if (event.code === 'ArrowRight') {
      frame = Math.min(frame + 1, scenario.frames - 1);
    } else if (event.code === 'ArrowLeft') {
      frame = Math.max(frame - 1, 0);
    }
  });

  // Deep link to a moment, so a specific frame can be shared or pulled up for a
  // screenshot: ?scenario=device&frame=500
  const params = new URLSearchParams(window.location.search);
  const wanted = manifest.scenarios.find(s => s.id === params.get('scenario'));
  const startFrame = Number(params.get('frame'));
  const paused = params.get('paused') === '1';

  setPlaying(!paused);
  await selectScenario(wanted ? wanted.id : manifest.scenarios[0].id,
                       Number.isFinite(startFrame) ? startFrame : 0);
  requestAnimationFrame(tick);
}

start();
