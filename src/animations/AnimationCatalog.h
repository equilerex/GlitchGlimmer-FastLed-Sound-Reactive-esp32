#pragma once

#include <array>
#include <functional>
#include <vector>
#include "../scenes/MoodHistory.h"
#include "Animation.h"

// Animation headers stay in AnimationCatalog.cpp. FastLED's fx/1d/pacifica.h
// defines members out of line with no inline, so including PacificaAnimation.h
// from this header made every TU that reached the catalog emit the same
// strong symbols and the device link failed.


enum class AnimationType {
    NONE = 0,
    PSYCHEDELIC_TUNNEL,
    ALIEN_BREATH,
    BASS_PULSE_STORM,
    NEON_BEAT_TUNNEL,
    MULTI_LAYERED_HYBRID,
    NEON_FLOW,
    PSYCHEDELIC_INK_SQUIRTS,
    ALIEN_PULSE,
    AURORA_DRIFT,
    PACIFICA,
    NOISE_WAVE,
    FIRE2012,
    GRADIENT_WASH,
    ETHERAL_PLASMA_DRIFT,
    TWILIGHT_RIPPLE,
    LAVA_LAMP,
    BEAT_SCANNER,
    GLITCHED_CYBER,
    BEAT_DROP,
    TWO_SIN,
    COLOR_WAVES,
    RISING_TENSION,
    STROBE_PULSE,
    POP_FADE,
    TOMORROWLAND_STAGE,
    COLOR_SLAM,
    HYPER_SPIN,
    SPACE_WIZARDS,
    PLAYA_CHAOS,
    THREE_SIN_TWO,
    HEARTBEAT,
    GENTLE_PULSE_WAVE,
    MOONLIGHT,
    FOREST_CANOPY,
    LAVA_CYBER_STORM,
    COUNT
};

struct AnimationMeta {
    AnimationType type;
    const char* name;
    MoodType mood;
    float preferredTempo;
    float intensity;
    std::function<Animation*()> create;
};

// Central registry of animations declared here (defined in .cpp)
extern const std::array<AnimationMeta, static_cast<size_t>(AnimationType::COUNT)> animationCatalog;

inline const char* animationTypeToString(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].name;
}

inline std::function<Animation*()> animationFactory(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].create;
}

inline MoodType animationMood(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].mood;
}

