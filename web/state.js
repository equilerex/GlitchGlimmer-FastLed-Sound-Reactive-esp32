import { reactive } from 'vue';

export const state = reactive({
  mode: 'live', // 'recording' | 'live'

  hwDrawerOpen: false,

  hw: {
    activeStrip: 0,
    surface: 'room',
    scale: 'fit',
    stageWidthM: 3,
    zoom: 1,
    panX: 0,
    panY: 0,
    ev: 0,
    spill: 1,
    grain: 0.14,
    pixelSize: 1.0,
    glowSize: 1.0,
    intensity: 1.0,
    strips: [],
    fit: null,
    inspect: null,
    drawMs: 0,
  },

  live: {
    scene: '—',
    mood: '—',
    predicted: '—',
    sceneElapsed: '0.0s',
    sceneMin: '0.0s',
    sceneIdeal: '0.0s',
    bpm: 0,
    beat: false,
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
    layers: [], // [{ name, elapsedMs }]
    lit: 0,
    litSum: 0,
    sceneChanges: 0,
    moodChanges: 0,
    telemetry: {} 
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
