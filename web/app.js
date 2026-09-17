// Player for the frame dumps src/sim_main.cpp writes with --dump-frames.
//
// The byte layout is defined on the C++ side: per frame, oldest byte first,
// leds0 pixels then leds1 pixels, three bytes each in CRGB order, and
// manifest.json carries the counts. The static_assert in sim_main.cpp is what
// keeps this reader honest if CRGB ever stops being three bytes.
//
// Painting is shared with the live microphone player in web/live.js; see
// web/render.js.

import { LedCanvas, showError } from './render.js';

const canvas = document.getElementById('view');
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
let leds = null;            // shared strip painter, built once the counts are known
let prepared = null;        // memoised first-run setup
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
  if (!running) return;
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
  rafId = requestAnimationFrame(tick);
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

// Fetches the manifest, builds the scenario buttons and wires the controls. Done
// on first start rather than at load, so a page opened straight into the live view
// never downloads the recordings.
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
    leds = new LedCanvas(canvas, [manifest.leds0, manifest.leds1]);

    for (const item of manifest.scenarios) {
      const button = document.createElement('button');
      button.type = 'button';
      button.textContent = item.label;
      button.dataset.id = item.id;
      button.addEventListener('click', () => selectScenario(item.id, item.startFrame));
      buttons.appendChild(button);
    }

    playButton.addEventListener('click', () => setPlaying(!playing));
    scrub.addEventListener('input', () => {
      frame = Number(scrub.value);
      accumulator = 0;
    });
    window.addEventListener('keydown', (event) => {
      if (!running) return;
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
    const wantedId = wanted ? wanted.id : manifest.scenarios[0].id;
    const chosen = manifest.scenarios.find(s => s.id === wantedId);

    // No frame in the URL means the recording decides, because a scenario can open
    // on silence and starting there would show a black strip for several seconds.
    // Tested with has() rather than on the parsed number, since Number(null) is 0
    // and would pass the finite check while meaning "no frame was asked for".
    const startFrame = params.has('frame') ? Number(params.get('frame'))
                                           : chosen.startFrame;

    setPlaying(params.get('paused') !== '1');
    await selectScenario(wantedId, Number.isFinite(startFrame) ? startFrame : 0);
  })();

  return prepared;
}

export async function start() {
  if (running) return;
  setPlaying(playing);      // the button keeps whatever state the last run left
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
