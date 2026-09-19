// What a strip physically is, in millimetres.
//
// There is no per-type renderer anywhere in viz/. A COB bar looks continuous
// because its diffusion radius is larger than its pitch, not because a branch
// somewhere draws a tube for it. Keeping the difference in the numbers is why
// adding a strip type is a row in this table and nothing else.

export const PROFILES = [
  {
    id: 'ws60', name: 'WS2812B 60/m',
    pitch: 1000 / 60, die: 5.0, sigma: 2.4, casing: 'smd', burst: 0,
    visual: { pixelSize: 1.0, glowSize: 1.0, intensity: 1.0 },
    note: 'Bare 5050 SMD. Hard dots, visible gaps past a couple of metres.',
  },
  {
    id: 'ws144', name: 'WS2812B 144/m',
    pitch: 1000 / 144, die: 3.5, sigma: 1.7, casing: 'smd', burst: 0,
    visual: { pixelSize: 0.75, glowSize: 0.8, intensity: 0.9 },
    note: 'Dense 3535. Reads as a line at arm’s length, dotty up close.',
  },
  {
    id: 'ws30', name: 'WS2812B 30/m',
    pitch: 1000 / 30, die: 5.0, sigma: 2.4, casing: 'smd', burst: 0,
    visual: { pixelSize: 1.2, glowSize: 1.2, intensity: 1.1 },
    note: 'Same 5050 die, half the density. Dots read individually.',
  },
  {
    id: 'sil', name: 'IP65 silicone',
    pitch: 1000 / 60, die: 5.0, sigma: 11.0, casing: 'sleeve', burst: 0,
    visual: { pixelSize: 1.1, glowSize: 1.5, intensity: 1.0 },
    note: 'A 60/m strip inside a milky sleeve. Much softer, still not seamless.',
  },
  {
    id: 'cob', name: 'COB 480/m',
    pitch: 1000 / 480, die: 1.8, sigma: 3.4, casing: 'cob', burst: 0,
    visual: { pixelSize: 0.8, glowSize: 0.7, intensity: 0.9 },
    note: 'Diffusion wider than the pitch, so the pixels fuse into one bar.',
  },
  {
    id: 'bul', name: 'WS2811 bullet',
    pitch: 1000 / 20, die: 9.0, sigma: 5.5, casing: 'bulb', burst: 0.35,
    visual: { pixelSize: 1.5, glowSize: 1.8, intensity: 1.2 },
    note: '12 mm epoxy bullets. Bright point, dark cable between.',
  },
  {
    id: 'fairy', name: 'Fairy pip',
    pitch: 1000 / 20, die: 2.0, sigma: 3.0, casing: 'pip', burst: 1.0,
    visual: { pixelSize: 1.0, glowSize: 2.0, intensity: 1.1 },
    note: 'A tiny die in clear epoxy, so it scatters into a starburst.',
  },
];

export const DEFAULT_PROFILE_ID = 'ws60';

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
