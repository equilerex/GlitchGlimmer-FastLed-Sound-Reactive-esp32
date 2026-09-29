import { reactive } from 'vue';
import { DEFAULT_STRIP_CONFIGS } from './viz/hwStore.js';
import { emptyAttention } from './attention.js';

export const COORD_INFO = {
  intensity: {
    label: 'Intensity',
    base: 'Time-domain RMS sample volume enveloped through attack/release followers, normalized against 20s rolling peak reference.',
    formula: 'Ratio: (levelEnv - noiseFloor) / (levelRef - noiseFloor), constrained 0..1, scaled by gateGain. Filtered with dual EMA (fast tau 0.3s, slow tau 3.0s).',
    desc: 'Loudness and acoustic energy: tracks the dynamic loudness of the room, climbing on loud drops and falling during quiet sections.'
  },
  activity: {
    label: 'Activity',
    base: '256-bin FFT spectral flux (half-wave rectified difference between consecutive magnitude spectrums).',
    formula: 'Positive spectral difference: sum(max(0, mag[k] - prevMag[k])) normalized against rolling peak flux. Filtered with dual EMA (fast tau 0.3s, slow tau 3.0s).',
    desc: 'Event density and transient rate: high on rapid arpeggios, fast hi-hats, drum rolls; low on sustained drones, ambient pads, or static chords.'
  },
  brightness: {
    label: 'Brightness',
    base: '256-bin FFT magnitude spectrum and frequency bin center frequencies (0 to 22.05 kHz).',
    formula: 'Spectral centroid: sum(k * mag[k]) / sum(mag[k]), normalized across spectrum bins. Filtered with dual EMA (fast tau 0.3s, slow tau 3.0s).',
    desc: 'Timbre color and spectral balance: high values indicate crisp cymbals, hi-hats, vocal sibilance; low values indicate bass, sub, or muffled tones.'
  },
  weight: {
    label: 'Weight',
    base: 'FFT magnitude bins in the low-frequency sub/bass region (< 200 Hz).',
    formula: 'Bass band energy sum normalized against its own 20s rolling bass peak envelope. Filtered with dual EMA (fast tau 0.3s, slow tau 3.0s).',
    desc: 'Low-frequency acoustic weight: measures sustained bass and kick presence independently of overall track volume.'
  },
  pulse: {
    label: 'Pulse regularity',
    base: 'Ring buffer of the last 12 detected inter-beat intervals in milliseconds and continuous autocorrelation periodicity.',
    formula: 'Periodicity regularity fused with autocorrelation confidence: max(1.0 - (MAD / Median), beatConfidence). Filtered with dual EMA.',
    desc: 'Metric regularity: near 1.0 for steady electronic beats (techno/house/four-on-the-floor); near 0 for syncopated breaks, speech, ambient, or live rubato.'
  },
  tempo: {
    label: 'Tempo',
    base: 'Continuous autocorrelation lag tracking fused with median inter-beat interval.',
    formula: 'BPM = 60000 / medianInterval, normalized as value = clamp((BPM - 60.0) / 120.0, 0, 1). Decays exponentially during prolonged beat silence.',
    desc: 'Speed coordinate: track tempo normalized to 0..1 (0 = 60 BPM, 0.5 = 120 BPM, 1.0 = 180+ BPM). Confidence reflects autocorrelation peak prominence.'
  },
  texture: {
    label: 'Texture',
    base: '256 FFT magnitude bins across the full spectrum.',
    formula: 'Spectral flatness (Wiener entropy): geometric mean divided by arithmetic mean = exp(mean(ln(mag + eps))) / (mean(mag) + eps). Fast tau 1.0s, slow tau 8.0s.',
    desc: 'Tone vs noise: near 0 for pure harmonic pitches and synth tones; near 1 for white noise, heavy distortion, snare splash, breath, or crowd applause.'
  },
  presence: {
    label: 'Presence',
    base: 'Time-domain RMS sample volume vs adaptive silence noise floor follower (tracking quietest blocks over 10s).',
    formula: 'Schmitt trigger. Opens when block RMS > 2.5 * noiseFloor + 0.001. Closes after a 350 ms hangover when block RMS <= 1.5 * noiseFloor + 0.0005. gateGain slews toward that flag at 0.04 per block.',
    desc: 'Acoustic presence gate: smoothly transitions between quiet background room ambience (0) and active sound/music playback (1).'
  },
  tilt: {
    label: 'Tilt',
    base: 'Eight log-spaced sub-band shares of the spectrum (subBands).',
    formula: '0.5 + 0.5 * (share of 2 kHz and up minus share of 20 to 250 Hz). 0 is bass-heavy, 1 is bright.',
    desc: 'Band balance: low for dark, low-end heavy music, high for airy or thin-bottomed music. Room-coloured, so compare against your own calibration.'
  },
  evenness: {
    label: 'Evenness',
    base: 'The same eight sub-band shares.',
    formula: 'Entropy of the shares over ln 8. 1 is every band equally full.',
    desc: 'How full the mix is across the range. Per band, where texture is per bin and reads noise.'
  },
  punch: {
    label: 'Punch',
    base: 'Rise of the sub-bass plus bass share from one block to the next.',
    formula: 'max(0, lowShare - previousLowShare) over its own decaying maximum (0.995 per block).',
    desc: 'The kick attack: high for a struck low end, low for a rolling or sustained bassline. Hypothesis grade, the recordings decide whether it stays.'
  },
  body: {
    label: 'Body',
    base: 'Sub-band shares 2 to 4 (250 Hz to 2 kHz).',
    formula: 'Sum of the three shares.',
    desc: 'Where guitars, keys and vocals sit. High for melodic, organic music.'
  }
};

export const state = reactive({
  mode: 'live', // 'recording' | 'live'

  // Open by default where it docks beside the telemetry; on narrower screens it
  // is a slide-over that would cover the stage.
  hwDrawerOpen: window.innerWidth >= 1101,
  // Right-panel tab: 'led' or 'tuning'.
  hwTab: 'led',

  hw: {
    activeStrip: 0,
    surface: 'room',
    look: 'realistic',
    scale: 'physical',
    stageWidthM: 3,
    zoom: 1,
    panX: 0,
    panY: 0,
    ev: 0,
    spill: 1,
    roomGlow: 2.5,
    grain: 0.14,
    pixelSize: 1.0,
    glowSize: 1.0,
    intensity: 1.0,
    strips: DEFAULT_STRIP_CONFIGS.map((s) => ({
      ...s,
      pts: s.pts.map((p) => p.slice()),
    })),
    fit: null,
    inspect: null,
    drawMs: 0,
  },

  live: {
    scene: '—',
    sceneFrozen: false,
    selectedSceneIndex: -1,
    sceneCatalog: [], // [{ index, name }]
    layerCatalog: [], // [{ index, name }] every layer type the manual trigger can add
    triggerLayerIndex: -1,
    mood: '—',
    predicted: '—',
    sceneElapsed: '0.0s',
    sceneMin: '0.0s',
    sceneIdeal: '0.0s',
    bpm: 0,
    beat: false,
    beatPhase: 0,
    beatConfidence: 0,
    level: 0,
    volume: 0,
    loudness: 0,
    peak: 0,
    energy: 0,
    dynamics: 0,
    noiseFloor: 0,
    presence: false,
    bass: 0,
    mid: 0,
    treble: 0,
    bassLevel: 0,
    midLevel: 0,
    trebleLevel: 0,
    centroid: 0,
    dominantBand: 0,
    source: 'mic',
    note: '',
    demoTrack: 'edm',
    hasCustomAudio: false,
    audioFileName: '',
    audioPlaying: false,
    audioMuted: false,
    layers: [], // [{ name, elapsedMs }]
    lit: 0,
    litSum: 0,
    sceneChanges: 0,
    coords: {
      intensity: { value: 0, conf: 0, trend: 0 },
      activity: { value: 0, conf: 0, trend: 0 },
      brightness: { value: 0, conf: 0, trend: 0 },
      weight: { value: 0, conf: 0, trend: 0 },
      pulse: { value: 0, conf: 0, trend: 0 },
      tempo: { value: 0, conf: 0, trend: 0 },
      texture: { value: 0, conf: 0, trend: 0 },
      presence: { value: 0, conf: 0, trend: 0 },
      tilt: { value: 0, conf: 0, trend: 0 },
      evenness: { value: 0, conf: 0, trend: 0 },
      punch: { value: 0, conf: 0, trend: 0 },
      body: { value: 0, conf: 0, trend: 0 },
    },
    // One entry per mood row in src/audio/Moods.h, read from the firmware. The
    // page holds no mood names. [{ name, strength, inSelector, tone }]
    moods: [],
    // The selector as the firmware reports it: why the running scene is up, the
    // drop base's hold and the bucket. See readSelection in live.js.
    selection: { reason: '', drop: { holding: false, shiftSeen: false, minLeftMs: 0, capLeftMs: 0 }, bucket: [] },
    sampleFrame: 0,
    sampleTimeMs: 0,
    dtSeconds: 0,
    buildup: 0,
    descent: 0,
    dropDetected: false,
    teaseDetected: false,
    anomaly: 0,
    gateGain: 1.0,
    spectralFlatness: 0,
    // Episode state as the firmware reports it. Held, never derived.
    episodes: {},
    displacement: 0,
    arming: 0,
    dropConfidence: 0,
    dropConfirmed: false,
    events: [],
    hideGateEvents: false,
    attention: emptyAttention(),
    eventHold: {
      drop: 0,
      tease: 0,
      buildup: 0,
      descent: 0,
      anomaly: 0,
    },
    lastSeenTimes: {
      drop: 0,
      tease: 0,
      buildup: 0,
      descent: 0,
      anomaly: 0,
    }
  },

  recording: {
    scene: '—',
    mood: '—',
    time: '0.0s',
    note: '',
    playing: false,
    scrub: 0,
    maxScrub: 0,
    buttons: []
  }
});
