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
let analyserGainNode = null;
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
  if (module._gg_scene_count && module._gg_scene_name_by_index) {
    const count = module._gg_scene_count();
    const catalog = [];
    for (let i = 0; i < count; ++i) {
      const namePtr = module._gg_scene_name_by_index(i);
      const name = namePtr ? module.UTF8ToString(namePtr) : `Scene ${i}`;
      const moodPtr = module._gg_scene_mood_by_index ? module._gg_scene_mood_by_index(i) : 0;
      const mood = moodPtr ? module.UTF8ToString(moodPtr) : '';
      const rolePtr = module._gg_scene_role_by_index ? module._gg_scene_role_by_index(i) : 0;
      const role = rolePtr ? module.UTF8ToString(rolePtr) : '';
      const intensity = module._gg_scene_intensity_by_index ? module._gg_scene_intensity_by_index(i) : 0;
      catalog.push({ index: i, name, mood, role, intensity });
    }
    state.live.sceneCatalog = catalog;
  }
  if (module._gg_layer_type_count && module._gg_layer_type_name) {
    const layers = [];
    for (let i = 0, n = module._gg_layer_type_count(); i < n; ++i) {
      layers.push({ index: i, name: module.UTF8ToString(module._gg_layer_type_name(i)) });
    }
    state.live.layerCatalog = layers;
  }
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
  if (!analyserGainNode) {
    analyserGainNode = audioContext.createGain();
    // Mastered digital audio (~0.30 RMS) is 10-20x louder than an INMP441 mic (~0.02-0.03 RMS).
    // Attenuate to realistic mic range so level and energy meters stay dynamic and do not saturate at 100%.
    analyserGainNode.gain.value = 0.12;
    analyserGainNode.connect(analyser);
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

  // Analyser receives gain-scaled audio so full-scale digital audio matches INMP441 mic calibration
  if (analyserGainNode) {
    bufferSourceNode.connect(analyserGainNode);
  } else {
    bufferSourceNode.connect(analyser);
  }

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
    if (/\.f32$/i.test(file.name)) {
      // A capture from the Record button: raw mono float32 at the analysis rate. Not
      // decodable as audio, so it becomes a buffer directly and plays through the
      // same path as any other file, which is what lets a real-microphone capture be
      // replayed in the page with its event stream and structure readouts.
      const samples = new Float32Array(arrayBuf, 0, Math.floor(arrayBuf.byteLength / 4));
      customAudioBuffer = ctx.createBuffer(1, samples.length, SAMPLE_RATE);
      customAudioBuffer.copyToChannel(samples, 0);
    } else {
      customAudioBuffer = await ctx.decodeAudioData(arrayBuf);
    }
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

const EPISODE_SIGNALS = ['buildup', 'descent', 'drop', 'tease', 'anomaly'];
const EPISODE_STATES = ['idle', 'arming', 'active', 'fading'];
const EPISODE_END_REASONS = ['none', 'window', 'drop', 'replaced', 'gate', 'faded',
  'timeout', 'resolved', 'impact', 'released'];

function readEpisodes() {
  const out = {};
  EPISODE_SIGNALS.forEach((name, i) => {
    const b = 52 + i * 6;
    out[name] = {
      state: EPISODE_STATES[wasm._gg_feature(b)] || 'idle',
      episodeId: wasm._gg_feature(b + 1),
      elapsedMs: wasm._gg_feature(b + 2),
      lastDurationMs: wasm._gg_feature(b + 3),
      sinceEndMs: wasm._gg_feature(b + 4),
      lastEndReason: EPISODE_END_REASONS[wasm._gg_feature(b + 5)] || 'none',
    };
  });
  return out;
}

// Drains the firmware's event ring by seq. The firmware numbers records from 1 and
// never repeats or reuses a number, so remembering the newest one drained means a
// slow frame or a reload cannot repeat a record or silently lose one. This only
// formats: no edge is detected and no duration is computed here.
let lastEventSeq = 0;

function drainEpisodeEvents() {
  if (!wasm._gg_event_newest_seq) return;
  const newest = wasm._gg_event_newest_seq();
  if (newest < lastEventSeq) lastEventSeq = 0;   // a new module instance
  if (newest === lastEventSeq) return;
  const oldest = wasm._gg_event_oldest_seq();
  if (oldest > lastEventSeq + 1) {
    pushLiveEvent('gate', 'EVENTS MISSED',
      `${oldest - lastEventSeq - 1} records left the ring before they were read`);
    lastEventSeq = oldest - 1;
  }
  for (let seq = lastEventSeq + 1; seq <= newest; ++seq) {
    if (!wasm._gg_event_load(seq)) continue;
    const signal = EPISODE_SIGNALS[wasm._gg_event_field(0)] || 'unknown';
    const kind = wasm._gg_event_field(1);
    const reason = EPISODE_END_REASONS[wasm._gg_event_field(2)] || 'none';
    const confirmed = wasm._gg_event_field(3) > 0;
    const atMs = wasm._gg_event_field(4);
    const durationMs = wasm._gg_event_field(5);
    const value = wasm._gg_event_field(6);
    const name = signal === 'drop' && kind !== 2 ? 'DROP WINDOW' : signal.toUpperCase();
    if (kind === 0) {
      pushLiveEvent(signal, `${name} STARTED`, confirmed ? 'confirmed' : '', atMs);
    } else if (kind === 1) {
      pushLiveEvent(signal, `${name} ENDED (${reason})`,
        `${(durationMs / 1000).toFixed(1)}s` + (signal === 'drop' && confirmed ? ', confirmed' : ''), atMs);
    } else if (kind === 2) {
      pushLiveEvent(signal, 'DROP TRIGGERED', `preparation ${(value * 100).toFixed(0)}%`, atMs);
    } else if (kind === 3) {
      pushLiveEvent(signal, 'DROP CONFIRMED', '', atMs);
    }
  }
  lastEventSeq = newest;
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
    // Episode state, decided in the firmware. Six fields a signal from index 52, see
    // gg_feature in src/wasm_main.cpp. Held as received.
    episodes: readEpisodes(),
    displacement: wasm._gg_feature(82),
    arming: wasm._gg_feature(83),
    dropConfidence: wasm._gg_feature(84),
    dropConfirmed: wasm._gg_feature(85) > 0,
    average:  wasm._gg_average(),
    // Derived here from level with BRIGHTNESS_GAMMA (src/config/Config.h) so the
    // values the animations actually drive brightness from are visible.
    pixelLevel: Math.pow(Math.max(0, wasm._gg_feature(10)), 0.5),
    hsvLevel: Math.pow(Math.max(0, wasm._gg_feature(10)), 0.25),
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
    sceneIndex: wasm._gg_current_scene_index ? wasm._gg_current_scene_index() : -1,
    lockedScene: wasm._gg_locked_scene ? wasm._gg_locked_scene() : -1,
    mood:  wasm.UTF8ToString(wasm._gg_mood_name()),
    predicted: wasm.UTF8ToString(wasm._gg_mood_predicted_name()),
  };
}

import { state } from './state.js';

let lastSeen = {
  mood: '',
  scene: '',
  presenceConfirmed: undefined,
  presenceCandidate: undefined,
  presenceCandidateSince: 0,
  lastGateEventTime: 0,
};

function pushLiveEvent(type, label, detail, atMs) {
  if (!state.live.events) state.live.events = [];
  const rawTimeMs = atMs !== undefined ? atMs : (state.live.sampleTimeMs > 0)
    ? state.live.sampleTimeMs
    : performance.now();
  const timeStr = (rawTimeMs / 1000).toFixed(1) + 's';
  state.live.events.unshift({ time: timeStr, rawTimeMs, type, label, detail });
  if (state.live.events.length > 100) state.live.events.pop();
}

function updateHud(f) {
  if (!state.live.lastSeenTimes) {
    state.live.lastSeenTimes = { drop: 0, tease: 0, buildup: 0, descent: 0, anomaly: 0 };
  }
  const curTime = (f.sampleTimeMs > 0) ? f.sampleTimeMs : performance.now();
  if (f.dropDetected > 0) state.live.lastSeenTimes.drop = curTime;
  if (f.teaseDetected > 0) state.live.lastSeenTimes.tease = curTime;
  if (f.buildup > 0) state.live.lastSeenTimes.buildup = curTime;
  if (f.descent > 0) state.live.lastSeenTimes.descent = curTime;
  if (f.anomaly > 0) state.live.lastSeenTimes.anomaly = curTime;

  state.live.scene = f.scene;
  state.live.sceneFrozen = (f.lockedScene !== undefined && f.lockedScene >= 0);
  if (state.live.sceneFrozen) {
    state.live.selectedSceneIndex = f.lockedScene;
  } else if (f.sceneIndex !== undefined && f.sceneIndex >= 0) {
    state.live.selectedSceneIndex = f.sceneIndex;
  }
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
  state.live.episodes = f.episodes;
  state.live.displacement = f.displacement;
  state.live.arming = f.arming;
  state.live.dropConfidence = f.dropConfidence;
  state.live.dropConfirmed = f.dropConfirmed;
  drainEpisodeEvents();
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
    }

    if (f.teaseDetected > 0) {
      state.live.eventHold.tease = now + 2500;
    }

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
          pushLiveEvent('gate', hasPresence ? 'SOUND DETECTED' : 'SOUND LOST', hasPresence
            ? `silence gate opened: signal volume rose above 2x the noise floor (${f.noiseFloor.toFixed(4)})`
            : `silence gate closed: signal volume fell below 1.5x the noise floor (${f.noiseFloor.toFixed(4)})`);
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
      { name: 'loudness', key: 'loudness', max: 100, digits: 1,
        note: 'Smoothed loudness (0..100). The older loudness value. The animations use level; only the legacy mood history still reads this.' },
      { name: 'pixel level', key: 'pixelLevel', max: 1, digits: 2,
        note: 'level ^ BRIGHTNESS_GAMMA (0.5). The brightness curve most animations drive pixels from.' },
      { name: 'hsv level', key: 'hsvLevel', max: 1, digits: 2,
        note: 'sqrt(pixel level), which is level ^ 0.25. The value channel of HSV colours, used by the animations that pick a hue.' },
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
      { name: 'volume', key: 'volume', max: 0.5, sqrt: true, digits: 4,
        note: 'Raw physical RMS sample amplitude.\n\n' +
              '• Base data: 512 raw PCM input samples.\n' +
              '• Calculation: Root Mean Square: sqrt(sum(sample^2) / 512).\n' +
              '• Meaning: Unscaled physical signal amplitude (~0.006 RMS in quiet room). Dependent on mic hardware gain.' },
      { name: 'peak', key: 'peak', max: 1, sqrt: true, digits: 4,
        note: 'Raw physical peak sample amplitude.\n\n' +
              '• Base data: 512 raw PCM input samples.\n' +
              '• Calculation: Maximum absolute sample value: max(|sample|).\n' +
              '• Meaning: Instantaneous peak amplitude (~0.017 peak in quiet room). Indicates headroom and clipping.' },
      { name: 'average', key: 'average', max: 0.5, sqrt: true, digits: 4,
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
      { name: 'spectrum centroid', key: 'centroid', max: 255, digits: 1,
        note: 'Spectral center of mass (brightness).\n\n' +
              '• Base data: 256-bin FFT magnitudes.\n' +
              '• Calculation: Energy-weighted bin average: sum(k * mag[k]) / sum(mag[k]).\n' +
              '• Meaning: Average perceived frequency. Low bins indicate dark, bass-heavy audio; high bins indicate bright, airy sound.' },
      { name: 'dominant band', key: 'band', max: 255, digits: 0,
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
    cells.set(row.name, { num, nameEl: name });
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
    cells.set(row.name, { fill, num, meter, moving, nameEl: name });
  }

  if (row.note) {
    el.title = row.note;
  }
}

function formatAgo(timestampMs, sampleTimeMs) {
  if (!timestampMs || timestampMs === 0) return 'never';
  const cur = (sampleTimeMs > 0) ? sampleTimeMs : performance.now();
  const diff = Math.max(0, (cur - timestampMs) / 1000);
  if (diff < 1) return 'just now';
  if (diff < 60) return `${diff.toFixed(1)}s ago`;
  return `${(diff / 60).toFixed(1)}m ago`;
}

// Generates a compact, token-efficient diagnostics report in Markdown (<250 tokens)
// containing complete logic, rhythm, coordinates, structure, and events without raw spectrum bins.
export async function copyDiagnostics(button) {
  const l = state.live;
  const timeSec = (l.sampleTimeMs > 0) ? (l.sampleTimeMs / 1000).toFixed(1) : (performance.now() / 1000).toFixed(1);
  const fps = l.dtSeconds > 0 ? Math.round(1 / l.dtSeconds) : '—';
  const dtMs = (l.dtSeconds * 1000).toFixed(1);
  
  const layersStr = (l.layers && l.layers.length > 0)
    ? l.layers.map(layer => `${layer.name} (${(layer.elapsedMs / 1000).toFixed(1)}s)`).join(', ')
    : 'none';

  const coordsStr = l.coords ? [
    `  - Intensity: ${(l.coords.intensity.value * 100).toFixed(0)}% (conf ${(l.coords.intensity.conf * 100).toFixed(0)}%, trend ${l.coords.intensity.trend >= 0 ? '+' : ''}${l.coords.intensity.trend.toFixed(2)})`,
    `  - Activity: ${(l.coords.activity.value * 100).toFixed(0)}% (conf ${(l.coords.activity.conf * 100).toFixed(0)}%, trend ${l.coords.activity.trend >= 0 ? '+' : ''}${l.coords.activity.trend.toFixed(2)})`,
    `  - Brightness: ${(l.coords.brightness.value * 100).toFixed(0)}% (conf ${(l.coords.brightness.conf * 100).toFixed(0)}%, trend ${l.coords.brightness.trend >= 0 ? '+' : ''}${l.coords.brightness.trend.toFixed(2)})`,
    `  - Weight: ${(l.coords.weight.value * 100).toFixed(0)}% (conf ${(l.coords.weight.conf * 100).toFixed(0)}%, trend ${l.coords.weight.trend >= 0 ? '+' : ''}${l.coords.weight.trend.toFixed(2)})`,
    `  - Pulse: ${(l.coords.pulse.value * 100).toFixed(0)}% (conf ${(l.coords.pulse.conf * 100).toFixed(0)}%, trend ${l.coords.pulse.trend >= 0 ? '+' : ''}${l.coords.pulse.trend.toFixed(2)})`,
    `  - Tempo: ${(l.coords.tempo.value * 100).toFixed(0)}% (conf ${(l.coords.tempo.conf * 100).toFixed(0)}%, trend ${l.coords.tempo.trend >= 0 ? '+' : ''}${l.coords.tempo.trend.toFixed(2)})`,
    `  - Texture: ${(l.coords.texture.value * 100).toFixed(0)}% (conf ${(l.coords.texture.conf * 100).toFixed(0)}%, trend ${l.coords.texture.trend >= 0 ? '+' : ''}${l.coords.texture.trend.toFixed(2)})`,
    `  - Presence: ${(l.coords.presence.value * 100).toFixed(0)}% (conf ${(l.coords.presence.conf * 100).toFixed(0)}%, trend ${l.coords.presence.trend >= 0 ? '+' : ''}${l.coords.presence.trend.toFixed(2)})`
  ].join('\n') : '';

  const recentEvents = (l.events && l.events.length > 0)
    ? l.events.slice(0, 6).map(ev => `  - ${ev.time}: ${ev.label}${ev.detail ? ` (${ev.detail})` : ''}`).join('\n')
    : '  - none';

  const lines = [
    `### GlitchGlimmer Telemetry Diagnostics`,
    `- **Clock**: Source: ${l.source}${l.demoTrack ? ` (${l.demoTrack})` : ''} | Time: ${timeSec}s | Frame: ${l.sampleFrame} | dt: ${dtMs}ms (${fps} FPS)`,
    `- **Scene**: ${l.scene} [${l.sceneFrozen ? 'FROZEN' : 'AUTO'}] (elapsed ${l.sceneElapsed} / min ${l.sceneMin} / ideal ${l.sceneIdeal}) | Layers (${l.layers ? l.layers.length : 0}): ${layersStr}`,
    `- **Mood**: ${l.mood} | Predicted: ${l.predicted}`,
    `- **Audio Levels**: Vol: ${l.volume.toFixed(4)} | Peak: ${l.peak.toFixed(4)} | NoiseFloor: ${l.noiseFloor.toFixed(4)} | Gate: ${l.presence ? 'OPEN' : 'SHUT'} (gain ${l.gateGain.toFixed(2)}) | Energy: ${Math.round(l.energy)} | Level: ${(l.level * 100).toFixed(0)}% | Dynamics: ${(l.dynamics * 100).toFixed(0)}%`,
    `- **Rhythm**: BPM: ${l.bpm > 0 ? Math.round(l.bpm) : '—'} | Beat: ${l.beat ? 1 : 0} | Phase: ${((l.beatPhase || 0) * 100).toFixed(0)}% | Confidence: ${((l.beatConfidence || 0) * 100).toFixed(0)}%`,
    `- **Bands**: Bass: ${(l.bass * 100).toFixed(0)}% (drive ${(l.bassLevel * 100).toFixed(0)}%) | Mid: ${(l.mid * 100).toFixed(0)}% (drive ${(l.midLevel * 100).toFixed(0)}%) | Treble: ${(l.treble * 100).toFixed(0)}% (drive ${(l.trebleLevel * 100).toFixed(0)}%) | Centroid: ${l.centroid.toFixed(0)} | Flatness: ${(l.spectralFlatness * 100).toFixed(0)}%`,
    `- **8D Coordinates**:`,
    coordsStr,
    `- **Structure**: Buildup: ${l.buildup.toFixed(3)} (${formatAgo(l.lastSeenTimes?.buildup)}) | Descent: ${l.descent.toFixed(3)} (${formatAgo(l.lastSeenTimes?.descent)}) | Drop: ${l.dropDetected ? 1 : 0} (${formatAgo(l.lastSeenTimes?.drop)}) | Tease: ${l.teaseDetected ? 1 : 0} (${formatAgo(l.lastSeenTimes?.tease)}) | Anomaly: ${l.anomaly.toFixed(0)}`,
    `- **Recent Events**:`,
    recentEvents
  ];

  const text = lines.join('\n');
  console.log('[gg] diagnostics:\n' + text);
  try {
    await navigator.clipboard.writeText(text);
    if (button) flashButton(button, 'Copied!');
  } catch (err) {
    console.warn('[gg] clipboard write failed', err);
    if (button) flashButton(button, 'See console');
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
  // The structural windows. Starting guesses, since the only real capture is 2.3 s.
  // The firmware decides what each one means and the page only sends the number.
  { index: 11, name: 'section window', min: 1000, max: 10000, step: 250, value: 4000,
    format: asSeconds },
  { index: 12, name: 'tease window', min: 1000, max: 10000, step: 250, value: 4000,
    format: asSeconds },
  { index: 13, name: 'drop safety bound', min: 10000, max: 120000, step: 5000, value: 60000,
    format: asSeconds },
  { index: 14, name: 'drop hold fraction', min: 0.2, max: 1, step: 0.05, value: 0.6,
    format: (v) => v.toFixed(2) },
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
  // Snapshot and Record live in the header beside Copy Diagnostics, so this panel is
  // only readings.
  const copyButton = document.createElement('button');
  copyButton.type = 'button';
  copyButton.textContent = 'Copy snapshot';
  copyButton.title = 'Writes the current frame, the min and max of every value since load, the scene and mood history and the spectrum bars to the clipboard.';
  copyButton.addEventListener('click', () => copySnapshot(copyButton));
  const recordButton = document.createElement('button');
  recordButton.type = 'button';
  recordButton.textContent = 'Record';
  recordButton.title = 'Records the audio this page analyses into a .f32 file. Replay it offline with .pio/build/native/program --replay <file> to get min, max, mean and churn of every value.';
  recordButton.addEventListener('click', () => toggleRecording(recordButton));
  (document.getElementById('header-actions') || host).append(copyButton, recordButton);
  const fold = host;
  const pair = host;

  for (const group of STATE_GROUPS) {
    const box = document.createElement('div');
    box.className = group.title === 'Bands' ? 'state-group split' : 'state-group';
    const h = document.createElement('h3');
    h.textContent = group.title;
    box.append(h);
    (group.title === 'Output' ? pair : fold).append(box);
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
  clockBox.title = clockNote.textContent;
  clockNote.remove();
  (document.getElementById('clock-host') || pair).append(clockBox);

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
  (document.getElementById('tuning-host') || host).append(tuningBox);

  clockCell = cells.get(clockRow.name);
  stateBuilt = true;
}

let maxEnergySeen = 3000;

function paintState(f) {
  if (!stateBuilt) return;

  for (const group of STATE_GROUPS) {
    for (const row of group.rows) {
      const cell = cells.get(row.name);
      if (!cell) continue;
      const v = f[row.key];
      if (typeof v !== 'number') { cell.num.textContent = '—'; continue; }
      cell.num.textContent = v.toFixed(row.digits ?? 2);
      if (row.key === 'energy' && v > maxEnergySeen) {
        maxEnergySeen = v;
      }
      if (cell.fill) {
        const max = (row.key === 'energy') ? Math.max(row.max, maxEnergySeen) : row.max;
        const frac = row.sqrt ? Math.sqrt(Math.max(0, v) / max) : v / max;
        cell.fill.style.width = Math.max(0, Math.min(100, frac * 100)).toFixed(2) + '%';
      }

      if (cell.nameEl && group.title === 'Structure') {
        let ts = 0;
        if (row.key === 'buildup') ts = state.live.lastSeenTimes?.buildup;
        else if (row.key === 'descent') ts = state.live.lastSeenTimes?.descent;
        else if (row.key === 'dropDetected') ts = state.live.lastSeenTimes?.drop;
        else if (row.key === 'teaseDetected') ts = state.live.lastSeenTimes?.tease;
        else if (row.key === 'anomaly') ts = state.live.lastSeenTimes?.anomaly;
        
        if (ts && ts > 0) {
          const agoStr = formatAgo(ts, f.sampleTimeMs);
          cell.nameEl.textContent = `${row.name} (${agoStr})`;
        } else {
          cell.nameEl.textContent = row.name;
        }
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

export function lockScene(index) {
  if (!wasm) return;
  const idx = typeof index === 'number' ? index : parseInt(index, 10);
  if (idx >= 0) {
    if (wasm._gg_lock_scene) wasm._gg_lock_scene(idx);
    state.live.sceneFrozen = true;
    state.live.selectedSceneIndex = idx;
    if (wasm._gg_scene_name) state.live.scene = wasm.UTF8ToString(wasm._gg_scene_name());
  } else {
    unlockScene();
  }
}

export function unlockScene() {
  if (!wasm) return;
  if (wasm._gg_unlock_scene) wasm._gg_unlock_scene();
  state.live.sceneFrozen = false;
  state.live.selectedSceneIndex = wasm._gg_current_scene_index ? wasm._gg_current_scene_index() : -1;
  if (wasm._gg_scene_name) state.live.scene = wasm.UTF8ToString(wasm._gg_scene_name());
}

export function triggerLayer(index) {
  if (!wasm || !wasm._gg_trigger_layer) return false;
  // -1 releases the pick and hands the strip back to the director.
  return wasm._gg_trigger_layer(parseInt(index, 10)) === 1;
}

export function toggleSceneFreeze() {
  if (state.live.sceneFrozen) {
    unlockScene();
  } else {
    const cur = wasm && wasm._gg_current_scene_index ? wasm._gg_current_scene_index() : 0;
    lockScene(cur >= 0 ? cur : 0);
  }
}
