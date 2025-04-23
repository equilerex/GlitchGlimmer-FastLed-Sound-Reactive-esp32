#pragma once

enum class LayerType {
    BASE,            // Main background or scene-defining visuals
    BACKGROUND,      // Ambient fills, fog, slow ripples
    OVERLAY,         // Rings, trails, motion streaks
    REACTIVE,        // Short pops triggered by beats or spikes
    HIGHLIGHT,       // Fast blink, flash, or spark on impact
    ENERGY,          // Energy-related pulses, noise
    MOOD_ARC,        // Smooth evolving accent layer
    TRANSITION,      // Temporary scene changers
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
        default: return "UNKNOWN";
    }
}
