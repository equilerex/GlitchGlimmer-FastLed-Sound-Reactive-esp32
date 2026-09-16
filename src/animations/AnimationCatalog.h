#pragma once

#include <array>
#include <functional>
#include <vector>
#include "../scenes/MoodHistory.h"
#include "../animations/Animation.h"

// Include all your animations here:
#include "../animations/AlienPulse.h"
#include "../animations/MultiLayeredHybridAnimation.h"
#include "../animations/neonFlow.h"
#include "../animations/PsychedelicInkSquirtAnimation.h"
#include "../animations/PsychedelicTunnelAnimation.h"
#include "../animations/AlienBreathAnimation.h"
#include "../animations/BassPulseStormAnimation.h"
#include "../animations/NeonBeatTunnelAnimation.h"
#include "../scenes/MoodHistory.h"


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

