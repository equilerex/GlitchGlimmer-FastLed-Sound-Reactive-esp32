// Player for the frame dumps src/sim_main.cpp writes with --dump-frames.
import { LedCanvas, showError } from './render.js';
import { state } from './state.js';

let canvas = null;

const MAX_FRAME_MS = 250;
let manifest = null;
let scenario = null;
let pixels = null;
let leds = null;
let prepared = null;
let running = false;
let rafId = 0;

let frame = 0;
let playing = true;
let accumulator = 0;
let lastTimestamp = 0;
let lastDrawnFrame = -1;

function clamp(v, lo, hi) { return v < lo ? lo : v > hi ? hi : v; }

function resize() {
  if (!running || !leds) return;
  leds.resize();
  lastDrawnFrame = -1;
  draw();
}

function draw() {
  if (!leds || !pixels) return;
  if (frame === lastDrawnFrame) return;
  lastDrawnFrame = frame;

  const base = frame * manifest.bytesPerFrame;
  leds.paint(pixels.subarray(base, base + manifest.bytesPerFrame));
  updateHud();
}

function activeEvent(events) {
  if (!events || events.length === 0) return null;
  let lo = 0;
  let hi = events.length - 1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (events[mid].frame > frame) hi = mid - 1;
    else lo = mid + 1;
  }
  return hi >= 0 ? events[hi] : null;
}

function updateHud() {
  const ev = activeEvent(manifest.sceneEvents);
  if (ev) {
    state.recording.scene = ev.scene;
    state.recording.mood = ev.mood;
  }
  state.recording.time = (frame / 60).toFixed(1) + 's';
  state.recording.scrub = frame;
}

function tick(timestamp) {
  if (!running) return;

  if (lastTimestamp === 0) {
    lastTimestamp = timestamp;
    rafId = requestAnimationFrame(tick);
    return;
  }

  const dt = Math.min(timestamp - lastTimestamp, MAX_FRAME_MS);
  lastTimestamp = timestamp;

  if (playing && pixels) {
    accumulator += dt;
    const msPerFrame = 1000 / 60;
    while (accumulator >= msPerFrame) {
      accumulator -= msPerFrame;
      if (frame < scenario.frames - 1) {
        frame++;
      } else {
        playing = false;
        state.recording.playing = playing;
      }
    }
    draw();
  } else {
    // Redraw anyway, in case hardware dials were turned
    draw();
  }

  rafId = requestAnimationFrame(tick);
}

async function selectScenario(id, startFrame = 0) {
  const next = manifest.scenarios.find(s => s.id === id);
  if (!next) return;

  scenario = next;
  frame = clamp(Math.round(startFrame), 0, next.frames - 1);
  accumulator = 0;
  lastDrawnFrame = -1;

  state.recording.note = next.note;
  state.recording.maxScrub = next.frames - 1;
  
  state.recording.buttons.forEach(b => {
    b.active = b.id === id;
  });

  const response = await fetch('data/' + next.file);
  if (!response.ok) {
    showError(`Could not load data/${next.file} (HTTP ${response.status}).`);
    return;
  }
  pixels = new Uint8Array(await response.arrayBuffer());
  draw();
}

export function togglePlay() {
  playing = !playing;
  state.recording.playing = playing;
}

export function scrubTo(val) {
  playing = false;
  state.recording.playing = playing;
  frame = clamp(val, 0, scenario ? scenario.frames - 1 : 0);
  accumulator = 0;
  draw();
}

export function playScenario(index) {
  if (!manifest) return;
  const item = manifest.scenarios[index];
  if (item) selectScenario(item.id, item.startFrame);
  playing = true;
  state.recording.playing = playing;
}

function setPlaying(next) {
  playing = next;
  state.recording.playing = playing;
}

function prepare() {
  if (prepared) return prepared;

  prepared = (async () => {
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
    canvas = document.getElementById('view');
    leds = new LedCanvas(canvas, [manifest.leds0, manifest.leds1]);

    state.recording.buttons = manifest.scenarios.map(s => ({
      name: s.label,
      id: s.id,
      active: false
    }));

    window.addEventListener('keydown', (event) => {
      if (!running) return;
      if (event.code === 'Space') {
        event.preventDefault();
        togglePlay();
      } else if (event.code === 'ArrowRight') {
        scrubTo(frame + 1);
      } else if (event.code === 'ArrowLeft') {
        scrubTo(frame - 1);
      }
    });

    const params = new URLSearchParams(window.location.search);
    const wanted = manifest.scenarios.find(s => s.id === params.get('scenario'));
    const wantedId = wanted ? wanted.id : manifest.scenarios[0].id;
    const chosen = manifest.scenarios.find(s => s.id === wantedId);

    const startFrame = params.has('frame') ? Number(params.get('frame')) : chosen.startFrame;

    setPlaying(params.get('paused') !== '1');
    await selectScenario(wantedId, Number.isFinite(startFrame) ? startFrame : 0);
  })();

  return prepared;
}

export async function start() {
  if (running) return;
  setPlaying(playing);
  await prepare();
  if (!manifest) return;

  running = true;
  lastTimestamp = 0;
  resize();
  rafId = requestAnimationFrame(tick);
}

export function stop() {
  running = false;
  cancelAnimationFrame(rafId);
}

window.addEventListener('resize', resize);
