// Live player: the browser's microphone into the firmware's own audio analysis,
// scene director and layer compositor, compiled to WebAssembly by
// tools/build-wasm.sh.
//
// Nothing about the sound is reimplemented here. The page hands raw time-domain
// samples to AudioProcessor::analyzeAudio(), which does the FFT and the feature
// extraction on the C++ side, and reads back the LED bytes the same code the
// device runs would have written. Reproducing the feature scale in JavaScript
// would be guesswork: features.energy is a raw sum of FFT magnitudes, not 0..1.

import { LedCanvas, Spectrum, showError, hideError } from './render.js';
import { Trace, debugEnabled } from './telemetry.js';

const canvas = document.getElementById('view');
const spectrumCanvas = document.getElementById('spectrum');
const micButton = document.getElementById('mic');
const demoButton = document.getElementById('demo');
const noteLabel = document.getElementById('live-note');
const sceneLabel = document.getElementById('scene-live');
const moodLabel = document.getElementById('mood-live');
const predictedLabel = document.getElementById('predicted-live');
const bpmLabel = document.getElementById('bpm-live');
const levelBar = document.getElementById('level-live');
const beatLamp = document.getElementById('beat-live');

// The firmware's FFT is sized for this rate and maps bins to bass, mid and treble
// with it, so the graph is asked for the same number and the bands land where the
// firmware expects them. Browsers are free to refuse, in which case the band edges
// shift by the ratio of the two rates and nothing else changes.
const SAMPLE_RATE = 44100;

let wasm = null;
let engine = null;          // pointers and counts inside the wasm heap
let leds = null;
let spectrum = null;
let running = false;
let rafId = 0;
let source = 'mic';         // 'demo' or 'mic', and mic unless someone asks
let audioContext = null;
let analyser = null;
let stream = null;
let micOpened = false;      // the stream is open, so the microphone is what is wanted
let noteText = null;        // set when the automatic microphone attempt could not start

const demo = { time: 0, bassPhase: 0, midPhase: 0, noise: 1 };

// Off unless ?debug=1. Kept on window so a console session can call
// ggTrace.dump('mood') and ggTrace.dwellStats() while the page is still running,
// which is the whole point: the interesting run is the one with real sound in it.
const trace = new Trace(debugEnabled());
window.ggTrace = trace;

function micFailure(err) {
  if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
    return 'This browser exposes no microphone API here. It needs a secure ' +
           'context: https, or localhost. A page opened over file:// cannot ask.';
  }
  if (err.name === 'NotAllowedError') {
    return 'Microphone permission was refused. The browser only asks after a ' +
           'click, and it needs a secure context: https, or localhost.';
  }
  if (err.name === 'NotFoundError') return 'No microphone was found on this device.';
  return 'Could not open the microphone: ' + err.name + ' ' + err.message;
}

// Reload when a new build lands.
//
// The module is fetched once, at load, so a rebuild is invisible to a page that is
// already running, and reloading by hand after every edit is the slowest part of
// changing an animation. tools/build-wasm.sh writes live/build.json on a successful
// build, and this polls it and reloads when the value moves. The response is asked
// for with no-store so the poll is never answered from the cache, and the query
// parameter keeps any intermediary from treating the requests as one resource.
//
// Self-limiting on purpose. A page without that file, which is every deployed copy
// because the stamp is not committed, stops after a single request and costs
// nothing from then on. That is also why there is no flag to switch it off.
//
// A failed fetch is ignored rather than fatal: the file is briefly unreadable while
// it is being replaced, and the next poll a second and a half later will see it.
function watchForRebuild() {
  let known = null;

  async function poll() {
    let build;
    try {
      const res = await fetch('live/build.json?t=' + Date.now(), { cache: 'no-store' });
      if (!res.ok) return false;
      ({ build } = await res.json());
    } catch (err) {
      return true;
    }
    if (known === null) {
      known = build;
    } else if (build !== known) {
      location.reload();
    }
    return true;
  }

  poll().then((keepPolling) => {
    if (keepPolling) setInterval(poll, 1500);
  });
}

async function loadWasm() {
  // Guarded on engine rather than wasm, and wasm is assigned last, because a throw
  // partway through would otherwise leave wasm set and engine null: the guard would
  // then treat the module as loaded for the rest of the page's life, and the
  // microphone button would fail with a null engine instead of retrying the load.
  if (engine) return;
  const create = window.createGlitchGlimmer;
  if (!create) {
    throw new Error('live/glitchglimmer.js is missing or did not load. ' +
                    'Build it with tools/build-wasm.sh.');
  }
  const module = await create();
  module._gg_init();

  const counts = [module._gg_leds0_count(), module._gg_leds1_count()];
  engine = {
    counts,
    sampleCount: module._gg_sample_count(),
    samplesPtr: module._gg_sample_buffer(),
    leds0Ptr: module._gg_leds0(),
    leds1Ptr: module._gg_leds1(),
    spectrumPtr: module._gg_spectrum(),
    spectrumCount: module._gg_spectrum_count(),
    scratch: new Uint8Array((counts[0] + counts[1]) * 3),
  };
  wasm = module;
  applyTuning();
}

async function openMic() {
  stream = await navigator.mediaDevices.getUserMedia({
    audio: {
      // All three off. Auto gain control in particular flattens exactly the
      // dynamics the mood classifier reads and pumps the bass band, which is the
      // opposite of what a sound-reactive installation wants.
      echoCancellation: false,
      noiseSuppression: false,
      autoGainControl: false,
    },
  });

  try {
    audioContext = new AudioContext({ sampleRate: SAMPLE_RATE });
  } catch (err) {
    audioContext = new AudioContext();
  }
  // Not awaited. A context created before any user gesture starts suspended, and
  // resume() does not settle until a gesture arrives, so awaiting it would stall
  // start() and the first paint behind a promise that may never resolve. The
  // state is checked by the caller instead, and a statechange listener finishes
  // the switch when the browser gets its gesture.
  audioContext.resume().catch(() => {});

  const node = audioContext.createMediaStreamSource(stream);
  analyser = audioContext.createAnalyser();
  // One analysis block is one NUM_SAMPLES, so the page writes the microphone's
  // samples straight into the buffer the firmware's FFT reads.
  analyser.fftSize = engine.sampleCount;
  analyser.smoothingTimeConstant = 0;      // the firmware does its own smoothing
  node.connect(analyser);
  micOpened = true;
}

function closeMic() {
  if (stream) for (const track of stream.getTracks()) track.stop();
  if (audioContext) audioContext.close();
  stream = null;
  audioContext = null;
  analyser = null;
  micOpened = false;
}

// A synthetic stand-in for music, so the view does something without a microphone
// and without a permission prompt: a 55 Hz pulse under a drifting mid tone with a
// hiss on top, shaped so all three of the firmware's bands see a signal. The noise
// is a seeded generator rather than Math.random, so a run is reproducible.
//
// Scaled to the amplitude the microphone actually reports. Written at full scale it
// produced volume 0.3 and peak 0.85 against the microphone's 0.006 and 0.017, so
// every absolute reading on the page was fifty times what the same page showed on
// the microphone and the microphone looked broken by comparison. It is not: the
// level, the dynamics and the band drives are all measured against a recent
// reference and are the same on either source, and only the absolute fields move.
// A demo that disagrees with the microphone about those teaches the wrong thing
// about which readings to trust, and this view exists to be trusted while tuning.
const DEMO_GAIN = 0.02;

function fillDemo(samples) {
  const step = 1 / SAMPLE_RATE;
  for (let i = 0; i < samples.length; ++i) {
    demo.time += step;
    const t = demo.time;
    const pulse = Math.pow(Math.max(0, Math.sin(2 * Math.PI * 2 * t)), 6);

    demo.bassPhase += 2 * Math.PI * 55 * step;
    demo.midPhase += 2 * Math.PI * (220 + 220 * Math.sin(2 * Math.PI * 0.13 * t)) * step;
    demo.noise = (demo.noise * 16807) % 2147483647;

    const bass = Math.sin(demo.bassPhase) * (0.30 + 0.55 * pulse);
    const mid = Math.sin(demo.midPhase) * 0.18 * (0.4 + 0.6 * pulse);
    const treble = (demo.noise / 2147483647 * 2 - 1) * 0.05 * (0.2 + 0.8 * pulse);
    samples[i] = (bass + mid + treble) * DEMO_GAIN;
  }
}

function readFeatures() {
  // Read once per frame and share, rather than calling across the boundary again
  // for the trace. Each gg_feature call is a wasm invocation, and at 60 fps the
  // boundary crossing is not free.
  return {
    volume:   wasm._gg_feature(0),
    loudness: wasm._gg_feature(1),
    peak:     wasm._gg_feature(2),
    bass:     wasm._gg_feature(3),
    mid:      wasm._gg_feature(4),
    treble:   wasm._gg_feature(5),
    energy:   wasm._gg_feature(6),
    dynamics: wasm._gg_feature(7),
    bpm:      wasm._gg_feature(8),
    beat:     wasm._gg_feature(9),
    level:    wasm._gg_feature(10),
    noiseFloor: wasm._gg_feature(11),
    presence: wasm._gg_feature(12),
    bassLevel:   wasm._gg_feature(13),
    midLevel:    wasm._gg_feature(14),
    trebleLevel: wasm._gg_feature(15),
    average:  wasm._gg_average(),
    centroid: wasm._gg_spectrum_centroid(),
    band:     wasm._gg_dominant_band(),
    history:  wasm._gg_history_size(),
    layers:   wasm._gg_layer_count(0),
    layers1:  wasm._gg_layer_count(1),
    sceneChanges: wasm._gg_scene_changes(),
    moodChanges: wasm._gg_mood_changes(),
    elapsed:  wasm._gg_scene_elapsed_ms(),
    minMs:    wasm._gg_scene_min_ms(),
    idealMs:  wasm._gg_scene_ideal_ms(),
    dynLow:   wasm._gg_mood_dynamics_low(),
    dynHigh:  wasm._gg_mood_dynamics_high(),
    dynActive: wasm._gg_mood_dynamics_active(),
    lit:      wasm._gg_lit_count(0),
    litSum:   Math.round(wasm._gg_lit_sum(0)),
    scene: wasm.UTF8ToString(wasm._gg_scene_name()),
    mood:  wasm.UTF8ToString(wasm._gg_mood_name()),
    predicted: wasm.UTF8ToString(wasm._gg_mood_predicted_name()),
  };
}

function updateHud(f) {
  sceneLabel.textContent = f.scene;
  moodLabel.textContent = f.mood;
  predictedLabel.textContent = f.predicted;

  bpmLabel.textContent = f.bpm > 0 ? f.bpm.toFixed(0) : '—';

  // loudness is volume * 100 on the firmware side, so 50 is a loud signal rather
  // than a quiet one. Volume itself is the RMS of the samples, which rarely
  // climbs much past 0.4 even for something mixed loud.
  levelBar.style.width = Math.min(100, f.loudness * 2).toFixed(0) + '%';
  beatLamp.classList.toggle('on', f.beat > 0);

  paintState(f);
}

// -----------------------------------------------------------------------------
//  State panel
//
//  Built from the firmware's own readings rather than from anything derived on
//  this side, and built once at startup because the row set never changes.
//
//  Every bar has a fixed maximum on purpose. A bar scaled to the maximum value
//  seen so far always looks full, which is exactly how a band pinned at 1.0 by a
//  wrong divisor stays invisible. The threshold markers are the load-bearing
//  part: they show where the classifier's comparison actually falls, so a
//  threshold that sits at 0.03% of the range reads as the bug it is.
// -----------------------------------------------------------------------------
const STATE_GROUPS = [
  {
    title: 'What the classifier reads',
    rows: [
      { name: 'level', key: 'level', max: 1, digits: 2,
        marks: [0.3, 0.4, 0.6, 0.8],
        note: 'The loudness the classifier actually tests, as a fraction of the ' +
              'loudest block in the last few seconds. It is 0..1 at any ' +
              'microphone gain, so the four markers land where the thresholds ' +
              'are.' },
      { name: 'energy', key: 'energy', max: 3000, digits: 0,
        note: 'The sum of 255 FFT magnitudes, so hundreds is a quiet room and ' +
              'thousands a loud one on a high-gain input. Nothing compares ' +
              'against it any more: the thresholds all read level.' },
      { name: 'dynamics', key: 'dynamics', max: 1, digits: 2, marks: [], movingMarks: true,
        note: 'Marked at the classifier’s own cut points, which are 30% and 70% of ' +
              'the range it has measured recently. They move, so a fixed pair here ' +
              'would show a comparison the firmware is not making. Hidden while the ' +
              'measured range is too narrow to split.' },
      { name: 'bpm', key: 'bpm', max: 600, digits: 0, marks: [80, 100] },
      // No bars on these three. They are absolute amplitudes of the samples that
      // arrived, and this microphone delivers about 0.006 RMS and 0.017 peak, so a
      // bar against a maximum of 0.5 or 1 draws at one percent and reads as a
      // broken meter when the measurement is correct. There is no maximum they
      // could be scaled against here, because 1.0 means a full-scale input and
      // nothing in a room reaches it. The number is the whole reading.
      //
      // The gain-invariant readings are the ones to compare between sources:
      // level above, and the band drives. Those are fractions of a recent
      // reference and read the same on any input. These three do not, and are not
      // supposed to: a quieter room has a smaller volume, and that is the point.
      { name: 'volume', key: 'volume', digits: 4 },
      { name: 'peak', key: 'peak', digits: 4 },
      { name: 'average', key: 'average', digits: 4,
        note: 'Absolute sample amplitude, so these scale with microphone gain. ' +
              'Nothing in the render path reads them any more: the animations ' +
              'drive from level and the band drives, which are fractions of a ' +
              'recent reference and read the same on any input.' },
    ],
  },
  {
    title: 'Silence gate',
    rows: [
      { name: 'noise floor', key: 'noiseFloor', max: 0.05, digits: 4,
        note: 'A slow follower of the quietest recent block. The gate opens above ' +
              'twice it and closes below one and a half times it, so both ' +
              'thresholds move with the room instead of sitting at a fixed level.' },
      { name: 'signal present', key: 'presence', max: 1, digits: 0 },
    ],
  },
  {
    title: 'Bands',
    rows: [
      { name: 'bass', key: 'bass', max: 1, digits: 3 },
      { name: 'mid', key: 'mid', max: 1, digits: 3 },
      { name: 'treble', key: 'treble', max: 1, digits: 3 },
      // The three the animations read. Each is that band against its own recent
      // peak, so 1.0 means "as much of this band as the room has had lately" and
      // says nothing about loudness. They sit far above the shares beside them and
      // are the number to look at when a strip is dim.
      { name: 'bass drive', key: 'bassLevel', max: 1, digits: 3 },
      { name: 'mid drive', key: 'midLevel', max: 1, digits: 3 },
      { name: 'treble drive', key: 'trebleLevel', max: 1, digits: 3 },
      { name: 'spectrum centroid', key: 'centroid', digits: 1 },
      { name: 'dominant band', key: 'band', digits: 0 },
    ],
  },
  {
    title: 'Output',
    rows: [
      { name: 'strip 0 lit', key: 'lit', max: 100, digits: 0,
        note: 'How many pixels are not black right now. A strip reading 0 here ' +
              'while energy is high is the blackout, stated directly.' },
      { name: 'strip 0 brightness', key: 'litSum', digits: 0,
        note: 'Summed channels, so 0..76500 for 100 pixels. This is the dimming ' +
              'that falls short of going fully black.' },
      { name: 'layers strip 0', key: 'layers', max: 4, digits: 0 },
      { name: 'layers strip 1', key: 'layers1', max: 4, digits: 0 },
      { name: 'scene changes', key: 'sceneChanges', digits: 0 },
      { name: 'mood changes', key: 'moodChanges', digits: 0,
        note: 'Counted in the firmware, so it reads zero changes rather than no ' +
              'measurement when the mood has held still for the whole session.' },
      { name: 'mood history', key: 'history', max: 150, digits: 0 },
    ],
  },
];

// The scene clock is the one row whose scale moves per scene, so it is built
// apart from the fixed table above.
const clockRow = { name: 'elapsed', key: 'elapsed', max: 16000, digits: 0, marks: [] };

const cells = new Map();      // row name -> { fill, num, meter }
let stateBuilt = false;
let clockCell = null;

function appendRow(parent, row) {
  const el = document.createElement('div');
  el.className = 'state-row';

  const name = document.createElement('span');
  name.className = 'state-name';
  name.textContent = row.name;

  const num = document.createElement('span');
  num.className = 'state-num';
  num.textContent = '—';

  // A row with no maximum has no bar. The three columns stay so the numbers line
  // up down the panel, which is what makes them readable at a glance.
  if (row.max === undefined) {
    el.append(name, document.createElement('span'), num);
    parent.append(el);
    cells.set(row.name, { num });
  } else {
    const meter = document.createElement('span');
    meter.className = 'meter';

    const fill = document.createElement('span');
    fill.className = 'meter-fill';
    meter.append(fill);

    for (const m of row.marks || []) {
      const mark = document.createElement('span');
      mark.className = 'meter-mark';
      mark.style.left = Math.min(100, (m / row.max) * 100).toFixed(3) + '%';
      meter.append(mark);
    }

    // Marks whose position is not known until the firmware reports it, so they
    // are kept on the cell and placed in paintState.
    let moving = null;
    if (row.movingMarks) {
      moving = [];
      for (let i = 0; i < 2; ++i) {
        const mark = document.createElement('span');
        mark.className = 'meter-mark';
        meter.append(mark);
        moving.push(mark);
      }
    }

    el.append(name, meter, num);
    parent.append(el);
    cells.set(row.name, { fill, num, meter, moving });
  }

  if (row.note) {
    const note = document.createElement('div');
    note.className = 'state-note';
    note.textContent = row.note;
    parent.append(note);
  }
}

// Everything on screen as one JSON object: the current frame, the range every
// value moved through since the page loaded, the scene and mood history, and the
// spectrum reduced to the 64 bars the panel draws. Ranges are what turn a report
// like "the bands look wrong" into something checkable, since a value whose range
// is a single number never moved.
async function copySnapshot(button) {
  if (!wasm || !engine) return;
  const bins = new Float32Array(wasm.HEAPF32.buffer, engine.spectrumPtr, engine.spectrumCount);
  const snap = trace.snapshot(readFeatures(), bins);
  const text = JSON.stringify(snap, null, 2);
  console.log('[gg] snapshot', snap);
  try {
    await navigator.clipboard.writeText(text);
    flashButton(button, 'copied');
  } catch (err) {
    // The clipboard needs a secure context and can be refused. The console copy
    // is already written, so this only has to say where it went rather than
    // leave a button that quietly did nothing.
    console.warn('[gg] clipboard refused, the snapshot is in the console', err);
    flashButton(button, 'see console');
  }
}

// The label is the only confirmation the button gives, so it has to change.
function flashButton(button, text) {
  if (button.dataset.busy) return;
  button.dataset.busy = '1';
  const original = button.textContent;
  button.textContent = text;
  setTimeout(() => {
    button.textContent = original;
    delete button.dataset.busy;
  }, 1500);
}

// -----------------------------------------------------------------------------
//  Capture recording
//
//  Writes what the page actually fed the analyser to a file the host harness can
//  replay (`.pio/build/native/program --replay <file>`), which is how a
//  threshold gets tuned against the real microphone rather than against a guess
//  about it. The browser is the only place the microphone is reachable, so this
//  is the one channel from here to the tuning loop.
//
//  The binary is named directly rather than run through `pio run -t exec`,
//  because that target takes no program arguments and rejects them as stray
//  options. `.pio/build/native/program.exe` on Windows, and a full
//  `pio run -e native` has to have run once first to produce it.
//
//  The blocks are a copy per frame, not a running buffer: the page writes each
//  new block into the same region of the wasm heap, so a reference would record
//  the last frame N times.
//
//  Caveat worth knowing when reading a replay: getFloatTimeDomainData returns the
//  analyser's most recent 512 samples, which at 60 frames a second and 512 samples
//  at 44.1 kHz means consecutive records overlap rather than tile. The capture is
//  representative of the input, not a gapless recording of it.
// -----------------------------------------------------------------------------
const CAPTURE_MAGIC = 'GGCAP001';
const CAPTURE_MAX_FRAMES = 12000;  // about three minutes at 60 fps, 24 MB

const recorder = { active: false, blocks: [], button: null };

function appendRecording(samples) {
  if (!recorder.active) return;
  if (recorder.blocks.length >= CAPTURE_MAX_FRAMES) {
    finishRecording();
    return;
  }
  recorder.blocks.push(samples.slice());
}

function toggleRecording(button) {
  if (recorder.active) {
    finishRecording();
    return;
  }
  recorder.blocks = [];
  recorder.button = button;
  recorder.active = true;
  button.textContent = 'Stop recording';
  button.setAttribute('aria-pressed', 'true');
}

function finishRecording() {
  const button = recorder.button;
  recorder.active = false;
  if (button) {
    button.textContent = 'Record';
    button.setAttribute('aria-pressed', 'false');
  }

  const blocks = recorder.blocks;
  recorder.blocks = [];
  if (!blocks.length) {
    if (button) flashButton(button, 'nothing captured');
    return;
  }

  const buffer = buildCaptureBuffer(blocks);
  const url = URL.createObjectURL(new Blob([buffer], { type: 'application/octet-stream' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = 'glitchglimmer-' + new Date().toISOString().replace(/[:.]/g, '-') + '.f32';
  link.click();
  // Revoked on a timer rather than immediately: cancelling the URL in the same
  // task as the click can cancel the download with it.
  setTimeout(() => URL.revokeObjectURL(url), 10000);

  console.log(`[gg] recorded ${blocks.length} frames (${(buffer.byteLength / 1048576).toFixed(1)} MB)`);
  if (button) flashButton(button, `${blocks.length} frames`);
}

// The bytes src/sim_main.cpp's --replay reads. Little-endian throughout, which is
// every platform this runs on, but written explicitly rather than relying on it.
function buildCaptureBuffer(blocks) {
  const buffer = new ArrayBuffer(12 + blocks.length * blocks[0].length * 4);
  const asBytes = new Uint8Array(buffer);
  for (let i = 0; i < 8; ++i) asBytes[i] = CAPTURE_MAGIC.charCodeAt(i);
  new DataView(buffer).setUint32(8, blocks.length, true);
  const out = new Float32Array(buffer, 12);
  let at = 0;
  for (const block of blocks) {
    out.set(block, at);
    at += block.length;
  }
  return buffer;
}

// Checks the capture byte format without a browser automation harness, opened
// with ?selftest=1. It records 60 frames of whatever source is selected and puts
// the resulting file in the DOM base64-encoded, so a headless run can hand it
// straight to the harness:
//
//     msedge --headless=new --virtual-time-budget=20000 --dump-dom \
//       "http://localhost:8137/?source=demo&selftest=1" > dom.html
//     sed -n 's/.*<pre id="selftest"[^>]*>\([A-Za-z0-9+/=]*\)<\/pre>.*/\1/p' \
//       dom.html | base64 -d > demo.f32
//     pio run -e native
//     .pio/build/native/program --replay demo.f32
//
// It exists because the header layout is the only thing joining this file to
// src/sim_main.cpp, and a mismatch there does not fail anything: every tuning
// number just comes out wrong. The harness's own round-trip check writes its own
// header, so it cannot catch a change on this side.
const SELFTEST = new URLSearchParams(location.search).get('selftest') === '1';
const SELFTEST_FRAMES = 60;
let selftestDone = false;
let selftestDriven = false;

function selftestStep() {
  if (!SELFTEST || selftestDone || !wasm || !engine) return;
  if (!selftestDriven) {
    selftestDriven = true;
    recorder.blocks = [];
    recorder.active = true;
    // Headless runs a virtual clock that barely advances requestAnimationFrame, so
    // the frames this needs are produced here rather than waited for.
    for (let i = 0; i < SELFTEST_FRAMES + 1; ++i) step();
    return;
  }
  if (recorder.blocks.length < SELFTEST_FRAMES) return;
  selftestDone = true;
  recorder.active = false;
  const buffer = buildCaptureBuffer(recorder.blocks);
  recorder.blocks = [];

  const bytes = new Uint8Array(buffer);
  let binary = '';
  for (let i = 0; i < bytes.length; i += 0x8000) {
    binary += String.fromCharCode.apply(null, bytes.subarray(i, i + 0x8000));
  }
  const el = document.createElement('pre');
  el.id = 'selftest';
  el.style.display = 'none';
  el.textContent = btoa(binary);
  document.body.append(el);
  console.log('[gg] selftest capture written', bytes.length, 'bytes');
}

// -----------------------------------------------------------------------------
//  Tuning dials
//
//  Every one of these is a judgement about how the installation should feel, not
//  a number that can be derived, and the answer changes with the room and with
//  the music. They were compile-time constants, so answering "what if the scene
//  held twice as long" cost a rebuild and a page reload.
//
//  The firmware side is one indexed getter and one indexed setter, so this table
//  and the switch in src/wasm_main.cpp have to agree on ordering. Values persist
//  in localStorage because a setting that took a minute of listening to find
//  should survive a reload.
// -----------------------------------------------------------------------------
const TUNING_STORAGE_KEY = 'gg-tuning-v1';

const asSeconds = (v) => (v / 1000).toFixed(2) + 's';
const asRate = (v) => v.toFixed(2) + '/s';

const TUNING = [
  { index: 0, name: 'scene minimum', min: 500, max: 20000, step: 250, value: 4000,
    format: asSeconds },
  { index: 1, name: 'scene ideal base', min: 1000, max: 40000, step: 500, value: 9000,
    format: asSeconds },
  { index: 2, name: 'scene ideal span', min: 0, max: 30000, step: 500, value: 5000,
    format: asSeconds },
  { index: 3, name: 'mood hold', min: 0, max: 10000, step: 100, value: 2000,
    format: asSeconds },
  { index: 4, name: 'mood confirm', min: 0, max: 3000, step: 50, value: 500,
    format: asSeconds },
  { index: 5, name: 'mood smoothing', min: 0.01, max: 0.3, step: 0.005, value: 0.05,
    format: (v) => v.toFixed(3) },
  { index: 6, name: 'dynamics window opens', min: 0.1, max: 6, step: 0.1, value: 1.5,
    format: asRate },
  { index: 7, name: 'dynamics window closes', min: 0.05, max: 6, step: 0.05, value: 0.3,
    format: asRate },
];

// Captured before loadTuning() may overwrite `value`, so Reset has something to
// restore. Kept out of the table above so a default is written once.
for (const dial of TUNING) dial.default = dial.value;

function saveTuning() {
  try {
    localStorage.setItem(TUNING_STORAGE_KEY,
      JSON.stringify(TUNING.map((d) => d.value)));
  } catch (err) {
    // Private windows and blocked site data throw here. Losing persistence is
    // not worth losing the dials over.
    console.log('[gg] tuning not saved:', err);
  }
}

// Pushes every dial into the firmware. Called once the module is up and again
// after a reset, so the sliders and the running values cannot drift apart.
function applyTuning() {
  if (!wasm) return;
  for (const dial of TUNING) {
    wasm._gg_set_tuning(dial.index, dial.value);
    if (dial.input) {
      dial.input.value = dial.value;
      dial.readout.textContent = dial.format(dial.value);
    }
  }
}

function loadTuning() {
  try {
    const raw = localStorage.getItem(TUNING_STORAGE_KEY);
    if (!raw) return;
    const saved = JSON.parse(raw);
    TUNING.forEach((dial, i) => {
      const v = saved[i];
      if (typeof v === 'number' && v >= dial.min && v <= dial.max) dial.value = v;
    });
  } catch (err) {
    console.log('[gg] tuning not restored:', err);
  }
}

function resetTuning() {
  for (const dial of TUNING) dial.value = dial.default;
  saveTuning();
  applyTuning();
}

function buildState() {
  const host = document.getElementById('live-state');
  const actions = document.createElement('div');
  actions.className = 'button-row';
  const copyButton = document.createElement('button');
  copyButton.type = 'button';
  copyButton.textContent = 'Copy snapshot';
  copyButton.addEventListener('click', () => copySnapshot(copyButton));
  const recordButton = document.createElement('button');
  recordButton.type = 'button';
  recordButton.textContent = 'Record';
  recordButton.addEventListener('click', () => toggleRecording(recordButton));
  actions.append(copyButton, recordButton);
  const actionNote = document.createElement('p');
  actionNote.className = 'state-note';
  actionNote.textContent =
    'Copy snapshot writes the current frame, the min and max of every value since ' +
    'the page loaded, the scene and mood history, and the spectrum bars to the ' +
    'clipboard.';
  const recordNote = document.createElement('p');
  recordNote.className = 'state-note';
  recordNote.textContent =
    'Record captures the audio this page is analysing, frame by frame, into a ' +
    '.f32 file. Stop it, then replay it offline with ' +
    '.pio/build/native/program --replay <file>, which reports the min, max, ' +
    'mean and frame-to-frame churn of every value plus the mood dwell times. That ' +
    'is the loop for tuning a threshold against your microphone without a browser ' +
    'round trip per attempt.';
  host.append(actions, actionNote, recordNote);

  for (const group of STATE_GROUPS) {
    const box = document.createElement('div');
    box.className = 'state-group';
    const h = document.createElement('h3');
    h.textContent = group.title;
    box.append(h);
    host.append(box);
    for (const row of group.rows) appendRow(box, row);
  }

  const clockBox = document.createElement('div');
  clockBox.className = 'state-group';
  const clockHead = document.createElement('h3');
  clockHead.textContent = 'Scene clock';
  clockBox.append(clockHead);
  appendRow(clockBox, clockRow);

  // The two markers carry the minimum and the ideal duration, so they are added
  // here and positioned per frame in paintState.
  const clockMeter = cells.get(clockRow.name).meter;
  for (let i = 0; i < 2; ++i) {
    const mark = document.createElement('span');
    mark.className = 'meter-mark';
    clockMeter.append(mark);
  }

  const clockNote = document.createElement('div');
  clockNote.className = 'state-note';
  clockNote.textContent =
    'A transition needs both the minimum elapsed and a mood shift. The markers ' +
    'are this scene’s own minimum and ideal durations, recomputed whenever ' +
    'a scene begins.';
  clockBox.append(clockNote);
  host.append(clockBox);

  // The tuning dials. Built from the same table the wasm indices come from, so a
  // dial cannot exist on screen without a setter behind it.
  const tuningBox = document.createElement('div');
  tuningBox.className = 'state-group';
  const tuningHead = document.createElement('h3');
  tuningHead.textContent = 'Tuning';
  tuningBox.append(tuningHead);

  for (const dial of TUNING) {
    const row = document.createElement('label');
    row.className = 'tune-row';

    const nameEl = document.createElement('span');
    nameEl.className = 'tune-name';
    nameEl.textContent = dial.name;

    const input = document.createElement('input');
    input.type = 'range';
    input.min = dial.min;
    input.max = dial.max;
    input.step = dial.step;
    input.value = dial.value;

    const readout = document.createElement('span');
    readout.className = 'tune-value';
    readout.textContent = dial.format(dial.value);

    input.addEventListener('input', () => {
      const v = parseFloat(input.value);
      dial.value = v;
      readout.textContent = dial.format(v);
      if (wasm) wasm._gg_set_tuning(dial.index, v);
      saveTuning();
    });

    dial.input = input;
    dial.readout = readout;
    row.append(nameEl, input, readout);
    tuningBox.append(row);
  }

  const resetButton = document.createElement('button');
  resetButton.type = 'button';
  resetButton.textContent = 'Reset tuning';
  resetButton.addEventListener('click', resetTuning);
  tuningBox.append(resetButton);

  const tuningNote = document.createElement('p');
  tuningNote.className = 'state-note';
  tuningNote.textContent =
    'These are live: the firmware reads them on the next frame, so a scene in ' +
    'front of you responds to a change to its own clock rather than waiting for ' +
    'the next one. Mood hold is the minimum time a mood stays before the ' +
    'classifier may leave it, and mood confirm is how long a competing mood has ' +
    'to last to replace it, which is what stops a signal sitting on a threshold ' +
    'from alternating. Smoothing is how much of each frame the classifier takes ' +
    'into its running average, so a larger number is twitchier. The two dynamics ' +
    'window dials set how fast the classifier tracks the range it measures ' +
    'dynamics against, slow for a room that should not shift, fast for a set that ' +
    'changes a lot.';
  tuningBox.append(tuningNote);
  host.append(tuningBox);

  clockCell = cells.get(clockRow.name);
  stateBuilt = true;
}

function paintState(f) {
  if (!stateBuilt) return;

  for (const group of STATE_GROUPS) {
    for (const row of group.rows) {
      const cell = cells.get(row.name);
      if (!cell) continue;
      const v = f[row.key];
      if (typeof v !== 'number') { cell.num.textContent = '—'; continue; }
      cell.num.textContent = v.toFixed(row.digits ?? 2);
      if (cell.fill) {
        cell.fill.style.width = Math.max(0, Math.min(100, (v / row.max) * 100)).toFixed(2) + '%';
      }
    }
  }

  // The dynamics cut points move with the range the classifier has measured, so
  // they are placed from the firmware's own values rather than from a constant.
  // When the range is too narrow to split, the firmware drops the dynamics clause
  // entirely, and a pair of lines through noise would claim a comparison that is
  // not happening, so they are hidden instead.
  const dynCell = cells.get('dynamics');
  if (dynCell && dynCell.moving) {
    const [lo, hi] = dynCell.moving;
    const shown = f.dynActive > 0;
    lo.hidden = !shown;
    hi.hidden = !shown;
    if (shown) {
      lo.style.left = Math.min(100, f.dynLow * 100).toFixed(3) + '%';
      hi.style.left = Math.min(100, f.dynHigh * 100).toFixed(3) + '%';
    }
  }

  // The clock rescales per scene: its ideal duration is recomputed by
  // calculateIdealDuration from the mood at the moment the scene began, so a
  // fixed maximum would either clip a long scene or compress a short one.
  if (clockCell) {
    const span = Math.max(16000, f.idealMs * 1.3, f.elapsed * 1.05);
    clockCell.num.textContent = (f.elapsed / 1000).toFixed(1) + 's / ' +
                                (f.idealMs / 1000).toFixed(1) + 's';
    clockCell.fill.style.width = Math.min(100, (f.elapsed / span) * 100).toFixed(2) + '%';
    const marks = clockCell.meter.querySelectorAll('.meter-mark');
    if (marks.length === 2) {
      marks[0].style.left = Math.min(100, (f.minMs / span) * 100).toFixed(3) + '%';
      marks[1].style.left = Math.min(100, (f.idealMs / span) * 100).toFixed(3) + '%';
    }
  }
}
// One analysis window and one rendered frame. Kept separate from the animation
// frame so the first one can run immediately, which means a screenshot taken
// before any animation frame has fired still shows the strips.
function step() {
  // Memory can grow, which replaces the heap views, so they are taken off the
  // module each frame instead of being cached. The pointers stay valid: they are
  // offsets into the heap, not addresses in the host page.
  const samples = new Float32Array(wasm.HEAPF32.buffer, engine.samplesPtr, engine.sampleCount);
  // Analyser only once the context is really running. Until the browser has had
  // its gesture it hands back silence, and silence renders an all-black strip,
  // which reads as a rendering fault rather than as a permission prompt still
  // open. What fills that gap is silence rather than the synthetic signal: the
  // synthetic signal is a stand-in loud enough to see, and measuring the
  // microphone against its references is what made every reading wrong after a
  // switch. A black strip and a note is the honest version of "no audio yet".
  const micLive = source === 'mic' && audioRunning();
  if (micLive) analyser.getFloatTimeDomainData(samples);
  else if (source === 'demo') fillDemo(samples);
  else samples.fill(0);
  appendRecording(samples);
  selftestStep();

  wasm._gg_step();

  const heap = wasm.HEAPU8;
  let offset = 0;
  for (const [ptr, count] of [[engine.leds0Ptr, engine.counts[0]],
                              [engine.leds1Ptr, engine.counts[1]]]) {
    engine.scratch.set(heap.subarray(ptr, ptr + count * 3), offset);
    offset += count * 3;
  }
  leds.paint(engine.scratch);

  const f = readFeatures();

  const bins = new Float32Array(wasm.HEAPF32.buffer, engine.spectrumPtr, engine.spectrumCount);
  spectrum.paint(bins, engine.spectrumCount, f.presence * f.level);

  updateHud(f);

  // performance.now() rather than Date.now(), because the question the trace asks
  // about dwell times is sub-frame, and Date.now() quantises to the millisecond.
  // Fed on every frame regardless of ?debug=1: the min and max the snapshot
  // reports have to cover the whole session, not only the window in which the
  // console was verbose. What the flag turns on is the frame ring and the log.
  f.t = performance.now();
  // What actually fed this frame, not what the page is pointed at. They differ
  // while the context is suspended, and a trace that named the microphone for a
  // frame the synthetic signal drove would send the next debugging session after
  // the wrong input.
  f.source = micLive ? 'mic' : 'demo';
  trace.frame(f);
}

function frame() {
  if (!running) return;
  step();
  rafId = requestAnimationFrame(frame);
}

function paintButtons() {
  micButton.setAttribute('aria-pressed', String(source === 'mic'));
  demoButton.setAttribute('aria-pressed', String(source === 'demo'));
  noteLabel.textContent = noteText ?? (source === 'mic'
    ? 'Analysing the microphone. Bass, mid and treble are the firmware\'s own bands.'
    : 'Showing a synthetic signal. Start the microphone to drive it with sound.');
}

// True only once the browser is actually delivering audio. A suspended context
// hands back silent buffers, which renders an all-black strip and reads as a
// rendering bug rather than as the permission prompt still being open.
function audioRunning() {
  return audioContext !== null && audioContext.state === 'running';
}

function watchForAudioStart() {
  if (audioContext) audioContext.addEventListener('statechange', onAudioState);
  const kick = () => {
    if (audioContext && audioContext.state === 'suspended') {
      audioContext.resume().catch(() => {});
    }
  };
  window.addEventListener('pointerdown', kick);
  window.addEventListener('keydown', kick);
}

function onAudioState() {
  // Guarded on micOpened rather than on the source, because the source is already
  // the microphone before this can fire: the page opens the stream at load and
  // stays pointed at it, feeding the analysis silence until the browser hands over
  // audio. What has to happen at the handover is the dropping of the references
  // the silence built and the clearing of the note that says why the strip was
  // dark, not a change of source.
  if (running && micOpened && audioRunning()) {
    selectSource('mic');
    noteText = null;
    paintButtons();
    hideError();
  }
}

// Opens the microphone without ever showing the error panel: this runs
// unprompted at startup, and covering the canvas because a desktop has no
// microphone would hide the thing being looked at. The reason goes in the note
// instead. An explicit click still uses setSource(), which does surface the error.
//
// It reports nothing, because the source is already the microphone before this is
// called: a failure here changes the note and not the source. The page stays on
// the microphone, and a failure means it stays there with nothing to analyse.
async function openMicQuietly() {
  try {
    await openMic();
  } catch (err) {
    noteText = micFailure(err);
    return;
  }
  if (!audioRunning()) {
    noteText = 'The microphone is open. Click anywhere, or press a key, to let ' +
               'the browser start audio. Until then there is nothing to analyse ' +
               'and the strips stay dark.';
    watchForAudioStart();
    return;
  }
  noteText = null;
}

// Point the analysis at a different input.
//
// Every feature is normalised against a reference learned from the input: the
// loudest recent block for level, the same for the bands, a slow follower of the
// quietest recent block for the gate. Those references make the readings
// gain-independent, which is what they are for, and they are also why a change of
// source is invisible to the analysis. It has no way to know the samples stopped
// coming from the same place, so it goes on measuring the new input against the
// old input's peak.
//
// The two sources here are about forty times apart. The synthetic signal is loud
// and steady so the view is not blank while permission is pending, and this
// microphone runs far below it. Switching back to the microphone after the demo
// had been playing measured it against the demo's amplitude, and levelRef forgets
// at 0.995 per frame, so every value read a fraction of the truth for about
// thirteen seconds before climbing back. Nothing was wrong with the microphone.
//
// Every path that changes the source comes through here, which is the only thing
// holding the invariant together: one place assigns `source`, and it forgets the
// references in the same breath.
function selectSource(kind) {
  source = kind;
  // Guarded, because this is now reachable before the module has loaded: stop() is
  // called on the way out of the live view whether or not start() got that far.
  if (wasm) wasm._gg_reset_analysis();
  // And the two records the page keeps of the same signal. The spectrum's running
  // peaks and the trace's min and max are session-wide by design, and they are
  // still statistics of the input: carried across a switch they describe the
  // louder source, so every bar and every range reads low against a scale that
  // belongs to audio that has stopped. `spectrum` is null until the first frame
  // has built it, and `trace` exists from module load.
  trace.resetRange();
  if (spectrum) spectrum.reset();
}

async function setSource(kind) {
  if (kind === source) return;

  if (kind === 'mic') {
    // openMic reads engine, so the module is loaded here rather than assumed: the
    // button is reachable before start() has run, and before this it threw a
    // TypeError about a null engine instead of saying the build was missing.
    try {
      await loadWasm();
    } catch (err) {
      showError(err.message);
      return;
    }
    try {
      await openMic();
      hideError();
    } catch (err) {
      showError(micFailure(err));
      return;
    }
    // Pointed at the microphone whether or not the browser has started audio yet:
    // the stream is open, so the microphone is what this page wants, and step()
    // fills the synthetic signal only while the context is still suspended. The
    // switch used to be deferred until the context resumed, which left the
    // analysis on the synthetic signal's references and made the first real
    // reading a fraction of the truth.
    selectSource('mic');
    if (!audioRunning()) {
      noteText = 'The microphone is open but the browser has not started audio ' +
                 'yet. Click anywhere, or press a key.';
      watchForAudioStart();
    } else {
      noteText = null;
    }
  } else {
    closeMic();
    selectSource('demo');
    noteText = null;
  }

  paintButtons();
}

export async function start() {
  if (running) return;
  // Before loadWasm, so the saved dials are the values the module is first given
  // and the panel is never showing a default the firmware is not running.
  loadTuning();
  try {
    await loadWasm();
  } catch (err) {
    showError(err.message);
    return;
  }

  if (!leds) {
    leds = new LedCanvas(canvas, engine.counts);
    spectrum = new Spectrum(spectrumCanvas);
    buildState();
  }

  running = true;
  leds.resize();

  // The microphone, always, and never the synthetic signal unless someone asks
  // for it there and then. The demo button is the only way to select it; the
  // ?source=demo link is the other, for a headless screenshot, and it is consumed
  // below rather than left in the address.
  //
  // The source is set here rather than when the microphone opens, so the page is
  // pointed at the microphone from the first frame. It used to fall back to the
  // synthetic signal whenever the microphone could not be had, and a browser
  // starts every audio context suspended until it has had a gesture, so that
  // fallback was the normal path: the page ran on the synthetic signal by default
  // and the microphone was the deviation. Nothing on the analysis side can undo
  // that, because the references it keeps are statistics of whatever was playing.
  //
  // Until audio is flowing the analysis is fed silence, not the synthetic signal.
  // Silence reads as no signal, which is true, and it costs a black strip until
  // the first click. A synthetic signal would cost the readings instead.
  //
  // Not awaited, so the permission prompt cannot hold up the first paint. The
  // promise only has to report a failure, since the source is already set.
  const params = new URLSearchParams(window.location.search);
  if (params.get('source') === 'demo') {
    selectSource('demo');
    // Removed from the address as it is read. The page reloads itself when the
    // wasm module is rebuilt and a reload keeps the query string, so a link
    // followed once would put every later run of this page on the synthetic
    // signal without anyone asking for it again.
    params.delete('source');
    const rest = params.toString();
    history.replaceState(null, '', window.location.pathname +
                                (rest ? '?' + rest : '') + window.location.hash);
  } else {
    selectSource('mic');
    openMicQuietly();
  }
  paintButtons();

  step();                          // paint before the first animation frame
  rafId = requestAnimationFrame(frame);
}

export function stop() {
  running = false;
  cancelAnimationFrame(rafId);
  closeMic();
  // Back to the microphone, which is the state the page rests in. This used to
  // return to the synthetic signal, so anything that stopped and restarted the
  // player silently moved the page onto audio nobody asked for.
  //
  // Through selectSource, because this is one of the two paths that changes the
  // source and it is the one that made the leak reachable. Leaving the live view and
  // coming back runs start() again on the module that is already loaded, so the
  // references the previous source built are still in it, and the source variable was
  // the only thing that said otherwise. Demo, then Recording, then Live again, and
  // the microphone was being measured against the synthetic signal's peaks and
  // classified against its dynamics window.
  selectSource('mic');
  noteText = null;
}

export function init() {
  micButton.addEventListener('click', () => setSource('mic'));
  demoButton.addEventListener('click', () => setSource('demo'));
  window.addEventListener('resize', () => {
    if (!running) return;
    leds.resize();
    step();
  });

  // Started here rather than in start(), so that it is watching even when the
  // module failed to load. That is the case where a rebuild is most likely to be
  // the fix, and it would otherwise be the one case where the page cannot notice.
  watchForRebuild();

  if (trace.enabled) {
    console.log('[gg] trace on. window.ggTrace.dump("mood"), .dwellStats(), .summary()');
  }
}
