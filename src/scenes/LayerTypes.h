#pragma once

// A layer slot, in two kinds.
//
// The first eight are roles: a compositing job a scene asks for without naming an
// implementation, and the factory picks one written for that job. The rest name a
// concrete layer, which is how a scene asks for a specific one of the alternatives
// that share a role. A scene may list either kind, in any mix.
//
// The split exists because the alternative layers were unreachable. Twelve classes
// were written in VisualLayers.h and constructed by nothing, so half the library
// could not be composited and every scene looked like every other at the same
// intensity, whatever its own base animation was doing.
enum class LayerType {
    // Roles.
    BASE,            // Main background or scene-defining visuals
    BACKGROUND,      // Ambient fills, fog, slow ripples
    OVERLAY,         // Rings, trails, motion streaks
    REACTIVE,        // Short pops triggered by beats or spikes
    HIGHLIGHT,       // Fast blink, flash, or spark on impact
    ENERGY,          // Energy-related pulses, noise
    MOOD_ARC,        // Smooth evolving accent layer
    TRANSITION,      // Temporary scene changers
    // Concrete layers.
    DOMINANT_BAND_FIRE_TRAIL,   // Fire trail behind the loudest band
    DYNAMICS_FLICKER_STORM,     // Random spark, denser as the loudness window widens
    TRIWAVE_BEAT,               // Triangle wave that reverses on every beat
    ENERGY_SPIRAL,              // Spiral that tightens with level
    DOMINANT_BAND_TRAIL,        // Decaying trail behind the loudest band
    WAVEFORM_SCRIBBLE,          // The waveform drawn over the strip
    WORMHOLE_VORTEX,            // Perspective tunnel from the centroid
    LOUDNESS_LIGHTNING,         // Bolts whose reach follows loudness
    CENTROID_GLOW_WIPE,         // Wipe from the spectral centroid's position
    BPM_WAVE_PULSE,             // Pulse whose rate is the detected tempo
    BPM_BEAT_FLASH,             // Flash on the beat, scaled by tempo
    CENTROID_COLOR_FLOW,        // Hue travelling with the centroid
    // Written for a structural episode, and bound to one by the director.
    BUILDUP_SWELL,              // Fills in from both ends for as long as a buildup lasts
    DESCENT_COOL,               // Cool comet falling toward the start for a descent
    COUNT            // Helper for random choice, etc.
};

inline const char* layerTypeToString(LayerType type) {
    switch (type) {
        case LayerType::BASE: return "BASE";
        case LayerType::BACKGROUND: return "BACKGROUND";
        case LayerType::OVERLAY: return "OVERLAY";
        case LayerType::REACTIVE: return "REACTIVE";
        case LayerType::HIGHLIGHT: return "HIGHLIGHT";
        case LayerType::ENERGY: return "ENERGY";
        case LayerType::MOOD_ARC: return "MOOD_ARC";
        case LayerType::TRANSITION: return "TRANSITION";
        case LayerType::DOMINANT_BAND_FIRE_TRAIL: return "DOMINANT_BAND_FIRE_TRAIL";
        case LayerType::DYNAMICS_FLICKER_STORM: return "DYNAMICS_FLICKER_STORM";
        case LayerType::TRIWAVE_BEAT: return "TRIWAVE_BEAT";
        case LayerType::ENERGY_SPIRAL: return "ENERGY_SPIRAL";
        case LayerType::DOMINANT_BAND_TRAIL: return "DOMINANT_BAND_TRAIL";
        case LayerType::WAVEFORM_SCRIBBLE: return "WAVEFORM_SCRIBBLE";
        case LayerType::WORMHOLE_VORTEX: return "WORMHOLE_VORTEX";
        case LayerType::LOUDNESS_LIGHTNING: return "LOUDNESS_LIGHTNING";
        case LayerType::CENTROID_GLOW_WIPE: return "CENTROID_GLOW_WIPE";
        case LayerType::BPM_WAVE_PULSE: return "BPM_WAVE_PULSE";
        case LayerType::BPM_BEAT_FLASH: return "BPM_BEAT_FLASH";
        case LayerType::CENTROID_COLOR_FLOW: return "CENTROID_COLOR_FLOW";
        case LayerType::BUILDUP_SWELL: return "BUILDUP_SWELL";
        case LayerType::DESCENT_COOL: return "DESCENT_COOL";
        case LayerType::COUNT: return "COUNT";
    }
    return "UNKNOWN";
}
