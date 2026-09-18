import { reactive } from 'vue';

export const state = reactive({
  mode: 'live', // 'recording' | 'live'
  
  hw: {
    activeStrip: 0,
    surface: 'room',
    scale: 'fit',
    stageWidthM: 3,
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
    bpm: '—',
    level: 0,
    beat: false,
    source: 'mic',
    note: '',
    // The telemetry rows built dynamically
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
