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

// Central registry of animations:
inline const std::array<AnimationMeta, static_cast<size_t>(AnimationType::COUNT)> animationCatalog = {{
    { AnimationType::PSYCHEDELIC_TUNNEL, "Psychedelic Tunnel", MoodType::FLOATY, 0.6f, 0.7f, []() { return new PsychedelicTunnelAnimation(); }},
    { AnimationType::ALIEN_BREATH, "Alien Breath", MoodType::CALM, 0.3f, 0.4f, []() { return new AlienBreathAnimation(); }},
    { AnimationType::BASS_PULSE_STORM, "Bass Pulse Storm", MoodType::INTENSE, 0.8f, 0.9f, []() { return new BassPulseStormAnimation(); }},
    { AnimationType::NEON_BEAT_TUNNEL, "Neon Beat Tunnel", MoodType::ENERGETIC, 1.0f, 0.8f, []() { return new NeonBeatTunnelAnimation(); }},
    { AnimationType::MULTI_LAYERED_HYBRID, "Hybrid", MoodType::ENERGETIC, 1.0f, 0.9f, []() { return new MultiLayeredHybridAnimation(); }},
    { AnimationType::NEON_FLOW, "Neon Flow", MoodType::ENERGETIC, 1.0f, 0.8f, []() { return new NeonFlowAnimation(); }},
    { AnimationType::PSYCHEDELIC_INK_SQUIRTS, "Squirt", MoodType::FLOATY, 0.5f, 0.7f, []() { return new PsychedelicInkSquirtAnimation(); }},
    { AnimationType::ALIEN_PULSE, "Alien Pulse", MoodType::INTENSE, 0.9f, 0.8f, []() { return new AlienPulseAnimation(); }},

}};

inline const char* animationTypeToString(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].name;
}

inline std::function<Animation*()> animationFactory(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].create;
}

inline MoodType animationMood(AnimationType type) {
    return animationCatalog[static_cast<size_t>(type)].mood;
}

