// What a strip physically is, in millimetres.
//
// There is no per-type renderer anywhere in viz/. A COB bar looks continuous
// because its diffusion radius is larger than its pitch, not because a branch
// somewhere draws a tube for it. Keeping the difference in the numbers is why
// adding a strip type is a row in this table and nothing else.

// Densities are the catalogue's LEDs (or pixels) per metre; pitch is 1000 / density.
const SMD_BODY = { fill: 'package', len: 5.0, wid: 5.0, pcb: { w: 10, color: '#0b0b0d', pads: true } };
const smd = (density, note) => ({
  id: `ws${density}`, name: `WS2812B ${density}/m`,
  pitch: 1000 / density, die: 5.0, sigma: 2.4, casing: 'smd', burst: 0,
  // Every WS2812B strip is the same 10 mm ribbon carrying the same 5050
  // package. Only the cadence differs, so nothing here varies with density.
  body: SMD_BODY,
  visual: { pixelSize: 1.0, glowSize: 1.0, intensity: 1.0 },
  note,
});
const fcob = (density, w, pcbW, note) => ({
  id: `fcob${density}`, name: `FCOB ${density} px/m`,
  pitch: 1000 / density, die: 6.0, sigma: 3.0, casing: 'cob', burst: 0,
  body: { fill: 'phosphor', len: 1000 / density, wid: w, blend: 0.22, dot: 0,
          pcb: { w: pcbW, color: '#eeece7', pads: true } },
  visual: { pixelSize: 1.0, glowSize: 1.0, intensity: 1.0 },
  note,
});
const pebble = (mm, note) => ({
  id: `peb${mm}`, name: `Pebble ${mm / 10} cm`,
  pitch: mm, die: 5.0, sigma: 4.0, casing: 'pip', burst: 0,
  body: { fill: 'bead', len: 9, wid: 6, pcb: { w: 2.4, color: '#050506', pads: false } },
  visual: { pixelSize: 1.0, glowSize: 0.8, intensity: 0.9 },
  note,
});

export const PROFILES = [
  smd(30, 'Same 5050 as every WS2812B, half the density. Dots read individually.'),
  smd(60, 'The standard strip. Hard dots, visible gaps past a couple of metres.'),
  smd(100, 'Dense enough to start reading as a line at arm’s length.'),
  smd(144, 'Ultra density: packages nearly touch, a line beyond arm’s length.'),
  {
    id: 'sil', name: 'IP65 silicone 60/m',
    pitch: 1000 / 60, die: 5.0, sigma: 11.0, casing: 'sleeve', burst: 0,
    body: { ...SMD_BODY, sleeve: 12 },
    visual: { pixelSize: 1.0, glowSize: 1.5, intensity: 1.0 },
    note: 'A 60/m strip inside a milky sleeve. Much softer, still not seamless.',
  },
  {
    id: 'cob', name: 'COB 480/m',
    pitch: 1000 / 480, die: 1.8, sigma: 3.4, casing: 'cob', burst: 0,
    body: { fill: 'phosphor', len: 1000 / 480, wid: 3.4, blend: 0, dot: 1000 / 480,
            pcb: { w: 8, color: '#e8e6e0', pads: false } },
    visual: { pixelSize: 0.8, glowSize: 0.7, intensity: 0.9 },
    note: 'Diffusion wider than the pitch, so the pixels fuse into one bar.',
  },
  fcob(160, 4.0, 8, 'Flip-chip COB, 5 V, one pixel per LED. Seamless at any distance.'),
  fcob(32, 5.0, 12, 'RGBCCT 24 V. One IC drives a 31 mm bar of 60 LEDs.'),
  fcob(28, 5.0, 10, 'RGBCCT 24 V. One IC drives a 36 mm bar of 60 LEDs.'),
  fcob(20, 5.0, 10, 'RGB 12/24 V. A 50 mm bar per pixel.'),
  fcob(14, 5.0, 10, 'RGBW 24 V. A 71 mm bar per pixel.'),
  pebble(25, 'Ultra-high pebble string, 40 per metre. Beads nearly touch.'),
  pebble(50, 'Standard tree-lighting pebble string, 20 per metre.'),
  pebble(100, 'Wide pebble string, 10 per metre. Beads read as separate points on a black cable.'),
  pebble(200, 'Very wide pebble string, 5 per metre, for draping.'),
  {
    id: 'bul', name: 'WS2811 bullet 20/m',
    pitch: 1000 / 20, die: 9.0, sigma: 5.5, casing: 'bulb', burst: 0.35,
    body: { fill: 'bead', len: 12, wid: 12, pcb: { w: 2.4, color: '#0c0c0e', pads: false } },
    visual: { pixelSize: 1.5, glowSize: 1.8, intensity: 1.2 },
    note: '12 mm epoxy bullets. Bright point, dark cable between.',
  },
  {
    id: 'fairy', name: 'Fairy pip 20/m',
    pitch: 1000 / 20, die: 2.0, sigma: 3.0, casing: 'pip', burst: 1.0,
    body: { fill: 'bead', len: 4, wid: 4, pcb: { w: 0.8, color: '#5a4a3a', pads: false } },
    visual: { pixelSize: 1.0, glowSize: 2.0, intensity: 1.1 },
    note: 'A tiny die in clear epoxy, so it scatters into a starburst.',
  },
];

export const DEFAULT_PROFILE_ID = 'ws60';

export function densityOf(pitchMm) {
  return pitchMm > 0 ? 1000 / pitchMm : 0;
}

export function profileById(id) {
  return PROFILES.find((p) => p.id === id)
      || PROFILES.find((p) => p.id === DEFAULT_PROFILE_ID);
}

export function stripLengthMm(count, profile) {
  return count * profile.pitch;
}

export function countForLength(lengthM, pitchMm, capacity = Infinity) {
  const length = Number.isFinite(lengthM) ? Math.max(0.1, lengthM) : 0.1;
  const pitch = Number.isFinite(pitchMm) ? Math.max(0.5, pitchMm) : 16.7;
  return Math.max(1, Math.min(capacity, Math.round(length * 1000 / pitch)));
}

export function fuses(profile) {
  return profile.sigma > profile.pitch;
}

// The old sidebar stored a density string. Kept so a browser that has one in
// localStorage, or a state object written before this change, lands on the
// right profile instead of silently resetting.
const LEGACY = {
  144: 'ws144', 60: 'ws60', 30: 'ws30',
  fairy: 'fairy', bullet: 'bul', cob: 'cob', none: 'ws60',
};

export function migratePreset(preset) {
  return LEGACY[preset] || DEFAULT_PROFILE_ID;
}
