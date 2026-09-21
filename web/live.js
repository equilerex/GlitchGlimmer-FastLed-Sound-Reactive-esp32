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

let canvas = null;
let spectrumCanvas = null;
const noteLabel = null;
const sceneLabel = null;
const moodLabel = null;
const predictedLabel = null;
const bpmLabel = null;
const levelBar = null;
const beatLamp = null;

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
let source = 'mic';         // 'mic', 'demo', or 'synth'
let audioContext = null;
let analyser = null;
let stream = null;
let micNode = null;
let micOpened = false;      // the stream is open, so the microphone is what is wanted
let noteText = null;        // set when the automatic microphone attempt could not start

let currentAudioBuffer = null;
let customAudioBuffer = null;
let bufferSourceNode = null;
let audioGainNode = null;
let isAudioMuted = false;
let isAudioPlaying = true;
let currentTrackId = 'edm';

const DEMO_URLS = {
  edm: 'audio/demo.mp3',
  jazz: 'audio/jazz.mp3',
};

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
  const wasmCapacity = 12000;
  engine = {
    counts,
    capacities: [
      module._gg_strip_capacity ? module._gg_strip_capacity(0) : wasmCapacity,
      module._gg_strip_capacity ? module._gg_strip_capacity(1) : wasmCapacity,
    ],
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

export function setSoftwareStripLength(strip, length) {
  if (!wasm || !engine) return 0;
  const setter = wasm._gg_set_software_length || wasm._gg_set_strip_length;
  if (!setter) return 0;
  const actual = setter(strip, Math.round(length));
  if (actual > 0) {
    engine.counts[strip] = actual;
    engine.scratch = new Uint8Array((engine.counts[0] + engine.counts[1]) * 3);
  }
  return actual;
}

export function softwareStripCapacity(strip) {
  return engine ? engine.capacities[strip] : 0;
}

function ensureAudioContext() {
  if (!audioContext || audioContext.state === 'closed') {
    try {
      audioContext = new AudioContext({ sampleRate: SAMPLE_RATE });
    } catch (err) {
      audioContext = new AudioContext();
    }
  }
  if (!analyser) {
    analyser = audioContext.createAnalyser();
    analyser.fftSize = engine ? engine.sampleCount : 512;
    analyser.smoothingTimeConstant = 0;
  }
  if (!audioGainNode) {
    audioGainNode = audioContext.createGain();
    audioGainNode.gain.value = isAudioMuted ? 0 : 0.7;
    audioGainNode.connect(audioContext.destination);
  }
  return audioContext;
}

async function openMic() {
  closeMic();
  stopAudioBuffer();

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

  const ctx = ensureAudioContext();
  ctx.resume().catch(() => {});

  micNode = ctx.createMediaStreamSource(stream);
  micNode.connect(analyser);
  micOpened = true;
}

function closeMic() {
  if (stream) {
    for (const track of stream.getTracks()) track.stop();
    stream = null;
  }
  if (micNode) {
    try { micNode.disconnect(); } catch (e) {}
    micNode = null;
  }
  micOpened = false;
}

export function stopAudioBuffer() {
  if (bufferSourceNode) {
    try {
      bufferSourceNode.stop();
      bufferSourceNode.disconnect();
    } catch (e) {}
    bufferSourceNode = null;
  }
}

export function playAudioBuffer() {
  if (!currentAudioBuffer) return;
  const ctx = ensureAudioContext();
  if (ctx.state === 'suspended') {
    ctx.resume().catch(() => {});
  }
  stopAudioBuffer();

  bufferSourceNode = ctx.createBufferSource();
  bufferSourceNode.buffer = currentAudioBuffer;
  bufferSourceNode.loop = true;

  // Analyser node receives audio for FFT and LED reactivity
  bufferSourceNode.connect(analyser);

  // Gain node routes audio to destination (speakers) so user can hear the beat
  bufferSourceNode.connect(audioGainNode);

  bufferSourceNode.start(0);
  isAudioPlaying = true;
  state.live.audioPlaying = true;
}

export function pauseAudioBuffer() {
  stopAudioBuffer();
  isAudioPlaying = false;
  state.live.audioPlaying = false;
}

export function toggleAudioPlay() {
  if (isAudioPlaying) {
    pauseAudioBuffer();
  } else {
    isAudioPlaying = true;
    if (source !== 'demo') {
      setSource('demo');
    } else {
      playAudioBuffer();
    }
  }
}

export function toggleAudioMute() {
  isAudioMuted = !isAudioMuted;
  state.live.audioMuted = isAudioMuted;
  if (audioGainNode) {
    audioGainNode.gain.value = isAudioMuted ? 0 : 0.7;
  }
}

export async function loadDemoTrack(trackId) {
  currentTrackId = trackId;
  state.live.demoTrack = trackId;
  const url = DEMO_URLS[trackId];
  if (!url) return;

  const ctx = ensureAudioContext();
  try {
    const res = await fetch(url);
    if (!res.ok) throw new Error(`HTTP ${res.status}`);
    const arrayBuf = await res.arrayBuffer();
    currentAudioBuffer = await ctx.decodeAudioData(arrayBuf);
    if (source === 'demo' && isAudioPlaying) {
      playAudioBuffer();
    }
  } catch (err) {
    console.warn('[gg] failed to load demo track:', err);
  }
}

export async function loadAudioFile(file) {
  const ctx = ensureAudioContext();
  try {
    const arrayBuf = await file.arrayBuffer();
    customAudioBuffer = await ctx.decodeAudioData(arrayBuf);
    currentAudioBuffer = customAudioBuffer;
    currentTrackId = 'custom';
    state.live.demoTrack = 'custom';
    state.live.hasCustomAudio = true;
    state.live.audioFileName = file.name;
    await setSource('demo');
  } catch (err) {
    console.warn('[gg] failed to decode audio file:', err);
    alert('Could not decode audio file: ' + err.message);
  }
}

export function selectDemoTrack(trackId) {
  if (trackId === 'custom') {
    if (customAudioBuffer) {
      currentAudioBuffer = customAudioBuffer;
      currentTrackId = 'custom';
      state.live.demoTrack = 'custom';
      if (source === 'demo' && isAudioPlaying) {
        playAudioBuffer();
      }
    }
    return;
  }
  loadDemoTrack(trackId);
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
  const layerCnt = wasm._gg_layer_count ? wasm._gg_layer_count(0) : 0;
  const layers = [];
  for (let i = 0; i < layerCnt; i++) {
    const namePtr = wasm._gg_layer_name ? wasm._gg_layer_name(0, i) : 0;
    const name = namePtr ? wasm.UTF8ToString(namePtr) : 'Layer ' + i;
    const elapsed = wasm._gg_layer_elapsed_ms ? wasm._gg_layer_elapsed_ms(0, i) : 0;
    layers.push({ name, elapsedMs: Math.round(elapsed) });
  }

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
    buildup: wasm._gg_feature(16),
    descent: wasm._gg_feature(17),
    dropDetected: wasm._gg_feature(18),
    teaseDetected: wasm._gg_feature(19),
    anomaly: wasm._gg_feature(20),
    gateGain: wasm._gg_feature(21),
    spectralFlatness: wasm._gg_feature(22),
    // MusicState coordinates (value, confidence, trend)
    coordIntensity: wasm._gg_feature(23),
    coordIntensityConf: wasm._gg_feature(24),
    coordIntensityTrend: wasm._gg_feature(25),
    coordActivity: wasm._gg_feature(26),
    coordActivityConf: wasm._gg_feature(27),
    coordActivityTrend: wasm._gg_feature(28),
    coordBrightness: wasm._gg_feature(29),
    coordBrightnessConf: wasm._gg_feature(30),
    coordBrightnessTrend: wasm._gg_feature(31),
    coordWeight: wasm._gg_feature(32),
    coordWeightConf: wasm._gg_feature(33),
    coordWeightTrend: wasm._gg_feature(34),
    coordPulse: wasm._gg_feature(35),
    coordPulseConf: wasm._gg_feature(36),
    coordPulseTrend: wasm._gg_feature(37),
    coordTempo: wasm._gg_feature(38),
    coordTempoConf: wasm._gg_feature(39),
    coordTempoTrend: wasm._gg_feature(40),
    coordTexture: wasm._gg_feature(41),
    coordTextureConf: wasm._gg_feature(42),
    coordTextureTrend: wasm._gg_feature(43),
    coordPresence: wasm._gg_feature(44),
    coordPresenceConf: wasm._gg_feature(45),
    coordPresenceTrend: wasm._gg_feature(46),
    dtSeconds: wasm._gg_dt_seconds ? wasm._gg_dt_seconds() : wasm._gg_feature(47),
    dtMs: (wasm._gg_dt_seconds ? wasm._gg_dt_seconds() : wasm._gg_feature(47)) * 1000,
    sampleTimeMs: wasm._gg_sample_time_ms ? wasm._gg_sample_time_ms() : wasm._gg_feature(48),
    sampleFrame: wasm._gg_sample_frame ? wasm._gg_sample_frame() : wasm._gg_feature(49),
    beatPhase: wasm._gg_beat_phase ? wasm._gg_beat_phase() : wasm._gg_feature(50),
    beatConfidence: wasm._gg_beat_confidence ? wasm._gg_beat_confidence() : wasm._gg_feature(51),
    average:  wasm._gg_average(),
    centroid: wasm._gg_spectrum_centroid(),
    band:     wasm._gg_dominant_band(),
    history:  wasm._gg_history_size(),
    layers:   layerCnt,
    layerDetails: layers,
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

import { state } from './state.js';

let lastSeen = {
  mood: '',
  scene: '',
  drop: false,
  tease: false,
  buildup: false,
  descent: false,
  presenceConfirmed: undefined,
  presenceCandidate: undefined,
  presenceCandidateSince: 0,
  lastGateEventTime: 0,
};

function pushLiveEvent(type, label, detail) {
  if (!state.live.events) state.live.events = [];
  const timeStr = (state.live.sampleTimeMs > 0)
    ? (state.live.sampleTimeMs / 1000).toFixed(1) + 's'
    : (performance.now() / 1000).toFixed(1) + 's';
  state.live.events.unshift({ time: timeStr, type, label, detail });
  if (state.live.events.length > 100) state.live.events.pop();
}

function updateHud(f) {
  state.live.scene = f.scene;
  state.live.mood = f.mood;
  state.live.predicted = f.predicted;
  state.live.bpm = f.bpm;
  state.live.beat = f.beat > 0;
  state.live.beatPhase = f.beatPhase !== undefined ? f.beatPhase : 0;
  state.live.beatConfidence = f.beatConfidence !== undefined ? f.beatConfidence : 0;
  state.live.level = f.level;
  state.live.loudness = f.loudness;
  state.live.volume = f.volume;
  state.live.peak = f.peak;
  state.live.energy = f.energy;
  state.live.dynamics = f.dynamics;
  state.live.noiseFloor = f.noiseFloor;
  state.live.presence = f.presence > 0;
  state.live.bass = f.bass;
  state.live.mid = f.mid;
  state.live.treble = f.treble;
  state.live.bassLevel = f.bassLevel;
  state.live.midLevel = f.midLevel;
  state.live.trebleLevel = f.trebleLevel;
  state.live.centroid = f.centroid;
  state.live.dominantBand = f.band;
  state.live.sceneElapsed = (f.elapsed / 1000).toFixed(1) + 's';
  state.live.sceneMin = (f.minMs / 1000).toFixed(1) + 's';
  state.live.sceneIdeal = (f.idealMs / 1000).toFixed(1) + 's';
  state.live.layers = f.layerDetails;
  state.live.lit = f.lit;
  state.live.litSum = f.litSum;
  state.live.sceneChanges = f.sceneChanges;
  state.live.moodChanges = f.moodChanges;

  if (state.live.coords) {
    state.live.coords.intensity = { value: f.coordIntensity, conf: f.coordIntensityConf, trend: f.coordIntensityTrend };
    state.live.coords.activity = { value: f.coordActivity, conf: f.coordActivityConf, trend: f.coordActivityTrend };
    state.live.coords.brightness = { value: f.coordBrightness, conf: f.coordBrightnessConf, trend: f.coordBrightnessTrend };
    state.live.coords.weight = { value: f.coordWeight, conf: f.coordWeightConf, trend: f.coordWeightTrend };
    state.live.coords.pulse = { value: f.coordPulse, conf: f.coordPulseConf, trend: f.coordPulseTrend };
    state.live.coords.tempo = { value: f.coordTempo, conf: f.coordTempoConf, trend: f.coordTempoTrend };
    state.live.coords.texture = { value: f.coordTexture, conf: f.coordTextureConf, trend: f.coordTextureTrend };
    state.live.coords.presence = { value: f.coordPresence, conf: f.coordPresenceConf, trend: f.coordPresenceTrend };
  }
  state.live.sampleFrame = f.sampleFrame;
  state.live.sampleTimeMs = f.sampleTimeMs;
  state.live.dtSeconds = f.dtSeconds;
  state.live.buildup = f.buildup;
  state.live.descent = f.descent;
  state.live.dropDetected = f.dropDetected > 0;
  state.live.teaseDetected = f.teaseDetected > 0;
  state.live.anomaly = f.anomaly;
  state.live.gateGain = f.gateGain;
  state.live.spectralFlatness = f.spectralFlatness;

  const now = Date.now();
  if (state.live.eventHold) {
    if (f.dropDetected > 0) {
      state.live.eventHold.drop = now + 2500;
      if (!lastSeen.drop) {
        pushLiveEvent('drop', 'DROP DETECTED', `bass: ${(f.bassLevel * 100).toFixed(0)}%, level: ${(f.level * 100).toFixed(0)}%`);
      }
    }
    lastSeen.drop = f.dropDetected > 0;

    if (f.teaseDetected > 0) {
      state.live.eventHold.tease = now + 2500;
      if (!lastSeen.tease) {
        pushLiveEvent('tease', 'TEASE DETECTED', `breakdown / tension`);
      }
    }
    lastSeen.tease = f.teaseDetected > 0;

    const isBuildup = f.buildup > 0;
    if (isBuildup && !lastSeen.buildup) {
      pushLiveEvent('buildup', 'BUILDUP ACTIVE', `displacement: +${f.buildup.toFixed(3)}`);
    }
    lastSeen.buildup = isBuildup;

    const isDescent = f.descent > 0;
    if (isDescent && !lastSeen.descent) {
      pushLiveEvent('descent', 'DESCENT ACTIVE', `displacement: -${f.descent.toFixed(3)}`);
    }
    lastSeen.descent = isDescent;

    if (lastSeen.mood && f.mood !== lastSeen.mood) {
      pushLiveEvent('mood', `MOOD → ${f.mood}`, `pred: ${f.predicted}`);
    }
    lastSeen.mood = f.mood;

    if (lastSeen.scene && f.scene !== lastSeen.scene) {
      pushLiveEvent('scene', `SCENE → ${f.scene}`, '');
    }
    lastSeen.scene = f.scene;

    const hasPresence = f.presence > 0;
    if (lastSeen.presenceConfirmed === undefined) {
      lastSeen.presenceConfirmed = hasPresence;
      lastSeen.presenceCandidate = hasPresence;
      lastSeen.presenceCandidateSince = now;
    } else if (hasPresence !== lastSeen.presenceConfirmed) {
      if (hasPresence !== lastSeen.presenceCandidate) {
        lastSeen.presenceCandidate = hasPresence;
        lastSeen.presenceCandidateSince = now;
      } else {
        const requiredDwell = hasPresence ? 300 : 1500;
        if ((now - lastSeen.presenceCandidateSince >= requiredDwell) && (now - lastSeen.lastGateEventTime >= 2000)) {
          lastSeen.presenceConfirmed = hasPresence;
          lastSeen.lastGateEventTime = now;
          pushLiveEvent('gate', hasPresence ? 'GATE OPENED' : 'GATE CLOSED', `noise: ${f.noiseFloor.toFixed(4)}, gain: ${f.gateGain.toFixed(2)}`);
        }
      }
    } else {
      lastSeen.presenceCandidate = hasPresence;
      lastSeen.presenceCandidateSince = now;
    }
  }

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
        note: 'Loudness tested by the mood classifier (0..1).\n\n' +
              '• Base data: 512-sample time-domain block RMS amplitude envelope.\n' +
              '• Calculation: Dual EMA follower against a 20s rolling peak envelope and adaptive room noise floor: clamp((RMS - floor) / (peak - floor), 0, 1).\n' +
              '• Meaning: Gain-invariant volume. The four markers (0.3, 0.4, 0.6, 0.8) indicate mood classification threshold boundaries.' },
      { name: 'energy', key: 'energy', max: 3000, digits: 0,
        note: 'Raw spectral energy sum.\n\n' +
              '• Base data: 256-bin FFT spectrum calculated from 512 samples.\n' +
              '• Calculation: Direct unscaled sum of 255 FFT magnitude bins: sum(|X[k]|).\n' +
              '• Meaning: Absolute energy that scales with analog mic gain (~100s in quiet rooms, ~1000s in loud music).' },
      { name: 'dynamics', key: 'dynamics', max: 1, digits: 2, marks: [], movingMarks: true,
        note: 'Dynamic range score (0..1).\n\n' +
              '• Base data: Sliding historical window of recent level readings.\n' +
              '• Calculation: (max(level) - min(level)) / max(level).\n' +
              '• Meaning: Distinguishes compressed, wall-of-sound audio from expressive, high-contrast passages. Moving markers indicate classifier cut points at 30% and 70% of measured range.' },
      { name: 'bpm', key: 'bpm', max: 600, digits: 0, marks: [80, 100],
        note: 'Estimated musical tempo.\n\n' +
              '• Base data: Detected beat onset timestamps from level and bass flux threshold crossings.\n' +
              '• Calculation: Derived from median inter-beat interval: 60000 / median(interval_ms).\n' +
              '• Meaning: Track BPM. Markers at 80 and 100 BPM split low, mid, and fast tempo moods.' },
      { name: 'volume', key: 'volume', digits: 4,
        note: 'Raw physical RMS sample amplitude.\n\n' +
              '• Base data: 512 raw PCM input samples.\n' +
              '• Calculation: Root Mean Square: sqrt(sum(sample^2) / 512).\n' +
              '• Meaning: Unscaled physical signal amplitude (~0.006 RMS in quiet room). Dependent on mic hardware gain.' },
      { name: 'peak', key: 'peak', digits: 4,
        note: 'Raw physical peak sample amplitude.\n\n' +
              '• Base data: 512 raw PCM input samples.\n' +
              '• Calculation: Maximum absolute sample value: max(|sample|).\n' +
              '• Meaning: Instantaneous peak amplitude (~0.017 peak in quiet room). Indicates headroom and clipping.' },
      { name: 'average', key: 'average', digits: 4,
        note: 'Mean absolute deviation of audio samples.\n\n' +
              '• Base data: 512 raw PCM input samples.\n' +
              '• Calculation: sum(|sample|) / 512.\n' +
              '• Meaning: Absolute sample amplitude. Visualizer drives from gain-invariant level and band drives instead.' },
    ],
  },
  {
    title: 'Silence gate',
    rows: [
      { name: 'noise floor', key: 'noiseFloor', max: 0.05, digits: 4,
        note: 'Adaptive ambient noise floor tracker.\n\n' +
              '• Base data: 512-sample block RMS values.\n' +
              '• Calculation: Asymmetric slow leaky follower tracking the quietest blocks over a 10s window.\n' +
              '• Meaning: Baseline room noise level. Thresholds adapt to ambient room acoustics.' },
      { name: 'signal present', key: 'presence', max: 1, digits: 0,
        note: 'Binary silence gate state.\n\n' +
              '• Base data: Block RMS volume vs adaptive noise floor.\n' +
              '• Calculation: Schmitt trigger with hysteresis: 1 when RMS > 2.0 * noiseFloor, dropping to 0 when RMS < 1.5 * noiseFloor.\n' +
              '• Meaning: 1 = deliberate sound or music present; 0 = quiet room silence.' },
      { name: 'gate gain', key: 'gateGain', max: 1, digits: 3,
        note: 'Silence gate smoothing gain multiplier.\n\n' +
              '• Base data: Signal present binary trigger state.\n' +
              '• Calculation: Linear slew rate ramp (tau ~50ms attack, ~200ms decay) between 0.0 and 1.0.\n' +
              '• Meaning: Smoothly attenuates animation levels to prevent erratic flicker during room silence.' },
    ],
  },
  {
    title: 'Bands',
    rows: [
      { name: 'bass', key: 'bass', max: 1, digits: 3,
        note: 'Bass band energy share (0..1).\n\n' +
              '• Base data: 256-bin FFT spectrum from 512 samples.\n' +
              '• Calculation: Sum of bins below 200 Hz divided by total spectrum energy.\n' +
              '• Meaning: Fraction of current audio energy located in sub and bass frequencies.' },
      { name: 'mid', key: 'mid', max: 1, digits: 3,
        note: 'Midrange band energy share (0..1).\n\n' +
              '• Base data: 256-bin FFT spectrum from 512 samples.\n' +
              '• Calculation: Sum of bins between 200 Hz and 2000 Hz divided by total spectrum energy.\n' +
              '• Meaning: Fraction of current audio energy located in vocal and instrument midrange.' },
      { name: 'treble', key: 'treble', max: 1, digits: 3,
        note: 'Treble band energy share (0..1).\n\n' +
              '• Base data: 256-bin FFT spectrum from 512 samples.\n' +
              '• Calculation: Sum of bins above 2000 Hz divided by total spectrum energy.\n' +
              '• Meaning: Fraction of current audio energy located in high treble, cymbals, and harmonics.' },
      { name: 'bass drive', key: 'bassLevel', max: 1, digits: 3,
        note: 'Gain-invariant bass animation driver (0..1).\n\n' +
              '• Base data: Bass band energy sum (< 200 Hz).\n' +
              '• Calculation: Normalized against its own 20s rolling bass peak envelope: bass / bassPeak.\n' +
              '• Meaning: "As much bass as the music has had lately." Primary driver for kick drum pulses and low-end LED response.' },
      { name: 'mid drive', key: 'midLevel', max: 1, digits: 3,
        note: 'Gain-invariant mid animation driver (0..1).\n\n' +
              '• Base data: Mid band energy sum (200 - 2000 Hz).\n' +
              '• Calculation: Normalized against its own 20s rolling mid peak envelope: mid / midPeak.\n' +
              '• Meaning: Drives melody, synth body, and vocal lighting elements.' },
      { name: 'treble drive', key: 'trebleLevel', max: 1, digits: 3,
        note: 'Gain-invariant treble animation driver (0..1).\n\n' +
              '• Base data: Treble band energy sum (> 2000 Hz).\n' +
              '• Calculation: Normalized against its own 20s rolling treble peak envelope: treble / treblePeak.\n' +
              '• Meaning: Primary driver for high-frequency sparkles, glitter, and crisp percussion.' },
      { name: 'spectrum centroid', key: 'centroid', digits: 1,
        note: 'Spectral center of mass (brightness).\n\n' +
              '• Base data: 256-bin FFT magnitudes.\n' +
              '• Calculation: Energy-weighted bin average: sum(k * mag[k]) / sum(mag[k]).\n' +
              '• Meaning: Average perceived frequency. Low bins indicate dark, bass-heavy audio; high bins indicate bright, airy sound.' },
      { name: 'dominant band', key: 'band', digits: 0,
        note: 'Dominant spectral peak bin.\n\n' +
              '• Base data: 256-bin FFT magnitudes.\n' +
              '• Calculation: Bin index containing the maximum magnitude peak across the entire spectrum.\n' +
              '• Meaning: Fundamental frequency or strongest tonal resonance.' },
      { name: 'spectral flatness', key: 'spectralFlatness', max: 1, digits: 3,
        note: 'Wiener entropy / spectral flatness (0..1).\n\n' +
              '• Base data: 256-bin FFT magnitudes.\n' +
              '• Calculation: Geometric mean divided by arithmetic mean: exp(mean(ln(mag + eps))) / (mean(mag) + eps).\n' +
              '• Meaning: 0.0 = pure harmonic tone or sine wave; 1.0 = white noise, distortion, or snare splash.' },
    ],
  },
  {
    title: 'Structure',
    rows: [
      { name: 'buildup', key: 'buildup', max: 1, digits: 3,
        note: 'Buildup progression score (0..1).\n\n' +
              '• Base data: Multi-second trends in energy, activity, and high-frequency centroid.\n' +
              '• Calculation: Integrated positive trend displacement slope over a 3-5s rolling window.\n' +
              '• Meaning: Rising musical tension, pre-drop risers, and drum roll acceleration.' },
      { name: 'descent', key: 'descent', max: 1, digits: 3,
        note: 'Descent / outro fade score (0..1).\n\n' +
              '• Base data: Multi-second trends in energy and level.\n' +
              '• Calculation: Integrated negative trend displacement slope over a 3-5s rolling window.\n' +
              '• Meaning: Song outro, breakdown energy drain, or quiet transitional passage.' },
      { name: 'drop detected', key: 'dropDetected', max: 1, digits: 0,
        note: 'Musical drop event flag (0 or 1).\n\n' +
              '• Base data: Bass drive surge and silence gate recovery.\n' +
              '• Calculation: Single-frame edge triggered when bass drive spikes >0.8 immediately following a buildup or silence drop-out (held for 2.5s in UI).\n' +
              '• Meaning: Sudden release of tension / heavy kick arrival.' },
      { name: 'tease detected', key: 'teaseDetected', max: 1, digits: 0,
        note: 'Musical tease / fake drop flag (0 or 1).\n\n' +
              '• Base data: High energy/activity paired with absent bass.\n' +
              '• Calculation: Triggered when activity/energy remains high (>0.6) while bass drops below 0.25.\n' +
              '• Meaning: Breakdown or tension section where the beat does not drop.' },
      { name: 'anomaly', key: 'anomaly', max: 3, digits: 0,
        note: 'Coordinate anomaly counter (0..3).\n\n' +
              '• Base data: 8D coordinate values and confidence metrics.\n' +
              '• Calculation: Count of coordinates exhibiting extreme rate-of-change or zero confidence.\n' +
              '• Meaning: Detects sudden audio discontinuities or tracking failure.' },
    ],
  },
  {
    title: 'Music Coordinates (8D State)',
    rows: [
      { name: 'intensity', key: 'coordIntensity', max: 1, digits: 3,
        note: 'Loudness Coordinate (0..1).\n\n' +
              '• Base data: 512-sample time-domain block RMS envelope.\n' +
              '• Calculation: Dual EMA follower (fast tau 0.3s, slow tau 3.0s) tracking level = clamp((RMS - floor)/(peak - floor), 0, 1) * gateGain.\n' +
              '• Meaning: Gain-invariant perceived volume.' },
      { name: 'intensity conf', key: 'coordIntensityConf', max: 1, digits: 2,
        note: 'Intensity Tracker Confidence (0..1).\n\n' +
              '• Base data: Silence gate state and SNR.\n' +
              '• Calculation: 1.0 when gate is settled open; decays toward 0 during silence or ambiguous low levels.\n' +
              '• Meaning: Reliability of loudness measurement.' },
      { name: 'intensity trend', key: 'coordIntensityTrend', digits: 3,
        note: 'Intensity Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow intensity EMA follower.\n' +
              '• Calculation: (currIntensity - prevIntensity) / dtSeconds.\n' +
              '• Meaning: Positive = crescendo / rising volume; negative = decrescendo / fading volume.' },
      { name: 'activity', key: 'coordActivity', max: 1, digits: 3,
        note: 'Activity Coordinate (0..1).\n\n' +
              '• Base data: 256-bin FFT magnitude spectra from consecutive blocks.\n' +
              '• Calculation: Half-wave rectified spectral flux sum(max(0, mag[k] - prevMag[k])) normalized against 20s rolling peak flux. Fast tau 0.5s, slow tau 5.0s.\n' +
              '• Meaning: Rhythm density and onset speed (spikes on drum hits and note attacks).' },
      { name: 'activity conf', key: 'coordActivityConf', max: 1, digits: 2,
        note: 'Activity Tracker Confidence (0..1).\n\n' +
              '• Base data: Flux reference stability and gate state.\n' +
              '• Calculation: Ratio of rolling flux headroom and gateGain.\n' +
              '• Meaning: Reliability of transient tracking.' },
      { name: 'activity trend', key: 'coordActivityTrend', digits: 3,
        note: 'Activity Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow activity EMA follower.\n' +
              '• Calculation: (currActivity - prevActivity) / dtSeconds.\n' +
              '• Meaning: Positive = accelerating rhythm / percussion density; negative = thinning rhythm.' },
      { name: 'brightness coord', key: 'coordBrightness', max: 1, digits: 3,
        note: 'Brightness Coordinate (0..1).\n\n' +
              '• Base data: 256-bin FFT magnitude spectrum.\n' +
              '• Calculation: Normalized spectral centroid: sum(k * mag[k]) / (sum(mag[k]) * 128). Fast tau 0.3s, slow tau 3.0s.\n' +
              '• Meaning: Timbre color (0 = deep sub/bass, 1 = crisp treble/cymbals).' },
      { name: 'brightness conf', key: 'coordBrightnessConf', max: 1, digits: 2,
        note: 'Brightness Tracker Confidence (0..1).\n\n' +
              '• Base data: Spectral energy sum.\n' +
              '• Calculation: 1.0 when total energy > silence threshold; fades if signal is too quiet to measure centroid.\n' +
              '• Meaning: Reliability of timbre estimation.' },
      { name: 'brightness trend', key: 'coordBrightnessTrend', digits: 3,
        note: 'Brightness Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow brightness EMA follower.\n' +
              '• Calculation: (currBrightness - prevBrightness) / dtSeconds.\n' +
              '• Meaning: Positive = filter opening / brighter timbre; negative = filter sweep down / darkening.' },
      { name: 'weight', key: 'coordWeight', max: 1, digits: 3,
        note: 'Weight Coordinate (0..1).\n\n' +
              '• Base data: Low-frequency FFT bins (< 200 Hz).\n' +
              '• Calculation: Bass energy sum normalized against its own 20s rolling peak envelope. Fast tau 0.3s, slow tau 3.0s.\n' +
              '• Meaning: Low-end acoustic weight independent of overall track volume.' },
      { name: 'weight conf', key: 'coordWeightConf', max: 1, digits: 2,
        note: 'Weight Tracker Confidence (0..1).\n\n' +
              '• Base data: Bass peak reference stability and gate gain.\n' +
              '• Calculation: Normalized ratio of bass peak to total energy floor.\n' +
              '• Meaning: Reliability of low-end measurement.' },
      { name: 'weight trend', key: 'coordWeightTrend', digits: 3,
        note: 'Weight Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow weight EMA follower.\n' +
              '• Calculation: (currWeight - prevWeight) / dtSeconds.\n' +
              '• Meaning: Positive = bass entry / kick buildup; negative = bass cut / breakdown.' },
      { name: 'pulse', key: 'coordPulse', max: 1, digits: 3,
        note: 'Pulse Regularity Coordinate (0..1).\n\n' +
              '• Base data: Ring buffer of last 12 inter-beat intervals in ms.\n' +
              '• Calculation: Periodicity consistency: 1.0 - (Median Absolute Deviation / Median Interval). Fast tau 2.0s, slow tau 10.0s.\n' +
              '• Meaning: Metric regularity (near 1.0 for steady electronic beats; near 0 for ambient, speech, or syncopated breaks).' },
      { name: 'pulse conf', key: 'coordPulseConf', max: 1, digits: 2,
        note: 'Pulse Tracker Confidence (0..1).\n\n' +
              '• Base data: Beat history buffer occupancy.\n' +
              '• Calculation: Fraction of the 12-beat window filled: min(1.0, beatCount / 12.0).\n' +
              '• Meaning: Confidence in beat periodicity.' },
      { name: 'pulse trend', key: 'coordPulseTrend', digits: 3,
        note: 'Pulse Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow pulse EMA follower.\n' +
              '• Calculation: (currPulse - prevPulse) / dtSeconds.\n' +
              '• Meaning: Positive = rhythm locking into steady meter; negative = rhythm dissolving into rubato/ambient.' },
      { name: 'tempo coord', key: 'coordTempo', max: 1, digits: 3,
        note: 'Tempo Coordinate (0..1).\n\n' +
              '• Base data: Median inter-beat interval from onset detection.\n' +
              '• Calculation: BPM = 60000 / medianInterval, normalized as BPM / 240.0. Decays exponentially on beat silence.\n' +
              '• Meaning: Speed coordinate (0.5 = 120 BPM, 1.0 = 240 BPM).' },
      { name: 'tempo conf', key: 'coordTempoConf', max: 1, digits: 2,
        note: 'Tempo Tracker Confidence (0..1).\n\n' +
              '• Base data: Beat interval variance and window fill.\n' +
              '• Calculation: pulseConsistency * min(1.0, beatCount / 12.0).\n' +
              '• Meaning: Confidence that estimated BPM matches actual musical meter.' },
      { name: 'tempo trend', key: 'coordTempoTrend', digits: 3,
        note: 'Tempo Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow tempo EMA follower.\n' +
              '• Calculation: (currTempo - prevTempo) / dtSeconds.\n' +
              '• Meaning: Positive = speeding up (accelerando); negative = slowing down (ritardando).' },
      { name: 'texture', key: 'coordTexture', max: 1, digits: 3,
        note: 'Texture / Noisiness Coordinate (0..1).\n\n' +
              '• Base data: 256 FFT magnitude bins across full spectrum.\n' +
              '• Calculation: Wiener entropy (spectral flatness): exp(mean(ln(mag + eps))) / (mean(mag) + eps). Fast tau 1.0s, slow tau 8.0s.\n' +
              '• Meaning: Tone vs noise (0 = pure sine / harmonic tones; 1 = white noise, heavy distortion, snare splash).' },
      { name: 'texture conf', key: 'coordTextureConf', max: 1, digits: 2,
        note: 'Texture Tracker Confidence (0..1).\n\n' +
              '• Base data: Spectrum total energy and gate gain.\n' +
              '• Calculation: 1.0 when energy is sufficient for reliable entropy computation; decays during silence.\n' +
              '• Meaning: Reliability of noise/tonality measurement.' },
      { name: 'texture trend', key: 'coordTextureTrend', digits: 3,
        note: 'Texture Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of slow texture EMA follower.\n' +
              '• Calculation: (currTexture - prevTexture) / dtSeconds.\n' +
              '• Meaning: Positive = audio becoming noisier / more distorted; negative = audio becoming cleaner / more tonal.' },
      { name: 'presence coord', key: 'coordPresence', max: 1, digits: 3,
        note: 'Presence Coordinate (0..1).\n\n' +
              '• Base data: Time-domain RMS volume vs adaptive noise floor follower.\n' +
              '• Calculation: Schmitt trigger with hysteresis slewed through attack/release ramp = gateGain (0..1).\n' +
              '• Meaning: Smooth gate (0 = room silence; 1 = active music playback).' },
      { name: 'presence conf', key: 'coordPresenceConf', max: 1, digits: 2,
        note: 'Presence Tracker Confidence (0..1).\n\n' +
              '• Base data: Distance between volume and noise floor.\n' +
              '• Calculation: min(1.0, |volume - noiseFloor| / noiseFloor).\n' +
              '• Meaning: Confidence that gate state is not ambiguous.' },
      { name: 'presence trend', key: 'coordPresenceTrend', digits: 3,
        note: 'Presence Rate of Change (units/sec).\n\n' +
              '• Base data: Derivative of presence coordinate.\n' +
              '• Calculation: (currPresence - prevPresence) / dtSeconds.\n' +
              '• Meaning: Positive = audio appearing / gate opening; negative = audio ceasing / gate closing.' },
    ],
  },
  {
    title: 'Sample Clock & Stream Timing',
    rows: [
      { name: 'sample frame', key: 'sampleFrame', digits: 0,
        note: 'Continuous audio block counter.\n\n' +
              '• Base data: Inbound I2S audio hardware blocks.\n' +
              '• Calculation: Increments by 1 for each 512-sample buffer processed (~86.13 blocks/sec at 44.1 kHz).\n' +
              '• Meaning: Deterministic frame counter in the sample-clock domain, decoupled from video render rate.' },
      { name: 'sample time (ms)', key: 'sampleTimeMs', digits: 0,
        note: 'Elapsed audio time in milliseconds.\n\n' +
              '• Base data: Cumulative processed sample count.\n' +
              '• Calculation: (sampleFrame * 512 * 1000) / 44100.\n' +
              '• Meaning: True audio stream timeline in milliseconds, free of browser setTimeout or requestAnimationFrame jitter.' },
      { name: 'block delta (ms)', key: 'dtMs', max: 50, digits: 2,
        note: 'Audio hop size in milliseconds.\n\n' +
              '• Base data: Audio buffer size (512 samples) and sample rate (44100 Hz).\n' +
              '• Calculation: (512 / 44100) * 1000 = ~11.61 ms.\n' +
              '• Meaning: Physical time step of every feature extraction cycle.' },
    ],
  },
  {
    title: 'Output',
    rows: [
      { name: 'strip 0 lit', key: 'lit', max: 100, digits: 0,
        note: 'Active non-black pixels on strip 0.\n\n' +
              '• Base data: Strip 0 framebuffer RGB channels.\n' +
              '• Calculation: Count of pixels where (R + G + B) > 0.\n' +
              '• Meaning: Visual pixel activity. Reading 0 while audio energy is high identifies a blackout defect.' },
      { name: 'strip 0 brightness', key: 'litSum', digits: 0,
        note: 'Aggregated luminous output on strip 0.\n\n' +
              '• Base data: Strip 0 framebuffer RGB channels.\n' +
              '• Calculation: Summed R + G + B values across all pixels (0..76500 for 100 pixels).\n' +
              '• Meaning: Total photonic drive. Measures aggregate dimming without going fully black.' },
      { name: 'layers strip 0', key: 'layers', max: 4, digits: 0,
        note: 'Active compositor layers on strip 0.\n\n' +
              '• Base data: Compositor active layer stack for strip 0.\n' +
              '• Calculation: Number of overlay layers currently blended onto the base animation.\n' +
              '• Meaning: Visual layering complexity.' },
      { name: 'layers strip 1', key: 'layers1', max: 4, digits: 0,
        note: 'Active compositor layers on strip 1.\n\n' +
              '• Base data: Compositor active layer stack for strip 1.\n' +
              '• Calculation: Number of overlay layers currently blended onto the base animation on strip 1.\n' +
              '• Meaning: Visual layering complexity on secondary strip.' },
      { name: 'scene changes', key: 'sceneChanges', digits: 0,
        note: 'Scene transition counter.\n\n' +
              '• Base data: Scene manager transition events.\n' +
              '• Calculation: Cumulative count of scene switches triggered by timers, mood changes, or beat cadence.\n' +
              '• Meaning: Scene rotation activity.' },
      { name: 'mood changes', key: 'moodChanges', digits: 0,
        note: 'Mood transition counter.\n\n' +
              '• Base data: Mood classifier state machine.\n' +
              '• Calculation: Total count of mood shifts committed by the classifier.\n' +
              '• Meaning: Reads 0 if mood has held constant for the entire session.' },
      { name: 'mood history', key: 'history', max: 150, digits: 0,
        note: 'Mood history buffer occupancy.\n\n' +
              '• Base data: Circular buffer of past classified moods.\n' +
              '• Calculation: Number of entries currently stored in the 150-slot transition history ring.\n' +
              '• Meaning: Memory depth of recent musical mood trajectory.' },
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
    el.title = row.note;
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
  { index: 8, name: 'dynamics signal decay', min: 0, max: 0.02, step: 0.00001, value: 0.0005,
    format: (v) => v.toFixed(5) + '/block' },
  { index: 9, name: 'input gain smoothing', min: 0, max: 0.99, step: 0.01, value: 0.85,
    format: (v) => v.toFixed(2) },
  { index: 10, name: 'dynamics signal growth', min: 0, max: 1, step: 0.01, value: 0.5,
    format: (v) => v.toFixed(2) + '/block' },
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
    'changes a lot. Signal decay controls how quickly the raw dynamics span closes ' +
    'when the loudness stays level; lower values let it float longer. Input gain ' +
    'smoothing controls how quickly the loudness estimate follows the microphone; ' +
    'lower values respond faster. The two signal controls set how quickly the raw ' +
    'dynamics span grows and closes.';
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
  const demoLive = source === 'demo' && audioRunning() && isAudioPlaying && bufferSourceNode;

  if (micLive || demoLive) {
    analyser.getFloatTimeDomainData(samples);
  } else if (source === 'synth' || (source === 'demo' && !currentAudioBuffer)) {
    fillDemo(samples);
  } else {
    samples.fill(0);
  }
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
  spectrum.paint(bins, engine.spectrumCount, f.gateGain);

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
  f.source = micLive ? 'mic' : (demoLive ? 'demo' : (source === 'synth' ? 'synth' : 'silent'));
  f.drawMs = leds.lastDrawMs;
  trace.frame(f);
}

function frame() {
  if (!running) return;
  step();
  rafId = requestAnimationFrame(frame);
}

function paintButtons() {
  state.live.source = source;
  state.live.note = noteText ?? (source === 'mic'
    ? 'Analysing the microphone. Bass, mid and treble are the firmware\'s own bands.'
    : source === 'demo'
    ? 'Playing demo track. Select a song or load an audio file to test rhythm & animations.'
    : 'Showing a synthetic signal (55 Hz tone). Start the microphone or demo track to drive it with sound.');
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
  if (running && micOpened && audioRunning() && source === 'mic') {
    selectSource('mic');
    noteText = null;
    paintButtons();
    hideError();
  } else if (running && audioRunning() && source === 'demo' && !bufferSourceNode && currentAudioBuffer && isAudioPlaying) {
    playAudioBuffer();
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
function selectSource(kind) {
  source = kind;
  if (wasm) wasm._gg_reset_analysis();
  trace.resetRange();
  if (spectrum) spectrum.reset();
}

export async function setSource(kind) {
  if (kind === source && (kind !== 'demo' || bufferSourceNode)) return;

  if (kind === 'mic') {
    stopAudioBuffer();
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
    selectSource('mic');
    if (!audioRunning()) {
      noteText = 'The microphone is open but the browser has not started audio ' +
                 'yet. Click anywhere, or press a key.';
      watchForAudioStart();
    } else {
      noteText = null;
    }
  } else if (kind === 'demo') {
    closeMic();
    selectSource('demo');
    isAudioPlaying = true;
    state.live.audioPlaying = true;
    if (!currentAudioBuffer) {
      await loadDemoTrack(currentTrackId || 'edm');
    } else {
      playAudioBuffer();
    }
    if (!audioRunning()) {
      noteText = 'Audio demo selected. Click anywhere, or press a key, to let the browser start audio.';
      watchForAudioStart();
    } else {
      noteText = null;
    }
  } else {
    // 'synth'
    closeMic();
    stopAudioBuffer();
    selectSource('synth');
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
    canvas = document.getElementById('view');
    spectrumCanvas = document.getElementById('spectrum');
    leds = new LedCanvas(canvas, engine.counts);
    spectrum = new Spectrum(spectrumCanvas);
    buildState();
  }

  running = true;
  leds.resize();

  const params = new URLSearchParams(window.location.search);
  const requestedSource = params.get('source');
  if (requestedSource === 'demo') {
    setSource('demo');
    params.delete('source');
    const rest = params.toString();
    history.replaceState(null, '', window.location.pathname +
                                (rest ? '?' + rest : '') + window.location.hash);
  } else if (requestedSource === 'synth') {
    setSource('synth');
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
  stopAudioBuffer();
  selectSource('mic');
  noteText = null;
}

export function init() {
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
