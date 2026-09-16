#include "AnimationCatalog.h"

// Definition of the central animation catalog (was extern in header)
const std::array<AnimationMeta, static_cast<size_t>(AnimationType::COUNT)> animationCatalog = {{
    { AnimationType::PSYCHEDELIC_TUNNEL, "Psychedelic Tunnel", MoodType::FLOATY, 0.6f, 0.7f, []() { return new PsychedelicTunnelAnimation(); } },
    { AnimationType::ALIEN_BREATH,       "Alien Breath",        MoodType::CALM,   0.3f, 0.4f, []() { return new AlienBreathAnimation(); } },
    { AnimationType::BASS_PULSE_STORM,   "Bass Pulse Storm",    MoodType::INTENSE,0.8f, 0.9f, []() { return new BassPulseStormAnimation(); } },
    { AnimationType::NEON_BEAT_TUNNEL,   "Neon Beat Tunnel",    MoodType::ENERGETIC,1.0f,0.8f, []() { return new NeonBeatTunnelAnimation(); } },
    { AnimationType::MULTI_LAYERED_HYBRID,"Hybrid",              MoodType::ENERGETIC,1.0f,0.9f, []() { return new MultiLayeredHybridAnimation(); } },
    { AnimationType::NEON_FLOW,          "Neon Flow",           MoodType::ENERGETIC,1.0f,0.8f, []() { return new NeonFlowAnimation(); } },
    { AnimationType::PSYCHEDELIC_INK_SQUIRTS, "Squirt",           MoodType::FLOATY, 0.5f, 0.7f, []() { return new PsychedelicInkSquirtAnimation(); } },
    { AnimationType::ALIEN_PULSE,        "Alien Pulse",         MoodType::INTENSE,0.9f, 0.8f, []() { return new AlienPulseAnimation(); } }
}};
