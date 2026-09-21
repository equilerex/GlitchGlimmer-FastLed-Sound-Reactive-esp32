#include "AnimationCatalog.h"
#include "AlienPulse.h"
#include "MultiLayeredHybridAnimation.h"
#include "neonFlow.h"
#include "PsychedelicInkSquirtAnimation.h"
#include "PsychedelicTunnelAnimation.h"
#include "AlienBreathAnimation.h"
#include "BassPulseStormAnimation.h"
#include "NeonBeatTunnelAnimation.h"
#include "AuroraAnimation.h"
#include "PacificaAnimation.h"
#include "NoiseWaveAnimation.h"
#include "Fire2012Animation.h"
#include "GradientWashAnimation.h"
#include "EtherealPlasmaDriftAnimation.h"
#include "TwilightRippleAnimation.h"
#include "LavaLampAnimation.h"
#include "BeatScannerAnimation.h"
#include "GlitchedCyberAnimation.h"
#include "BeatDropAnimation.h"
#include "TwoSinAnimation.h"
#include "ColorWavesAnimation.h"
#include "RisingTensionAnimation.h"
#include "StrobePulseAnimation.h"
#include "PopFadeAnimation.h"
#include "TomorrowlandStageAnimation.h"
#include "ColorSlamAnimation.h"
#include "HyperSpinAnimation.h"
#include "SpaceWizardsAndLizardsAnimation.h"
#include "PlayaChaosAnimation.h"
#include "ThreeSinTwoAnimation.h"
#include "HeartbeatAnimation.h"
#include "GentlePulseWaveAnimation.h"
#include "MoonlightAnimation.h"
#include "ForestCanopyAnimation.h"
#include "LavaCyberStormAnimation.h"
#include "ThemeAnimations.h"

// Definition of the central animation catalog (was extern in header)
//
// This table is the single source for each animation's tempo and intensity. Four
// of the animation headers used to carry their own `static constexpr` pair, read
// by nothing and already disagreeing with the literals here in three of the four
// cases. They are gone, because two statements of one number that can drift apart
// is the same defect as a Config.h macro with no call site.
//
// Both fields are load-bearing now: SceneRegistry::pickSceneByMood ranks the
// catalog by |intensity - the mood's target| and breaks ties on preferredTempo.
// intensity is 0..1 and has to be spread widely enough that the five ladder rungs
// land on five different scenes, which the old cluster at 0.8-0.9 could not do.
// preferredTempo is in the same units the picker compares it in, bpm / 130, so a
// value near 0.5 asks for a slow track and one near 1.0 for a fast one.
const std::array<AnimationMeta, static_cast<size_t>(AnimationType::COUNT)> animationCatalog = {{
    // Index must equal the AnimationType value. Every lookup is
    // animationCatalog[static_cast<size_t>(type)], and AnimationType::NONE is 0, so
    // without a slot here each entry is read one position early and the final one
    // is left value-initialised, with an empty std::function that aborts the moment
    // animationFactory() invokes it. Its mood is MOOD_COUNT, the "not a mood"
    // sentinel, since it has no animation to have a character.
    { AnimationType::NONE, "None", MOOD_COUNT, 0.0f, 0.0f, []() -> Animation* { return nullptr; } },
    { AnimationType::PSYCHEDELIC_TUNNEL, "Psychedelic Tunnel", MoodType::FLOATY, 0.60f, 0.35f, []() { return new PsychedelicTunnelAnimation(); } },
    { AnimationType::ALIEN_BREATH,       "Alien Breath",        MoodType::CALM,   0.45f, 0.18f, []() { return new AlienBreathAnimation(); } },
    { AnimationType::BASS_PULSE_STORM,   "Bass Pulse Storm",    MoodType::INTENSE,1.00f, 0.97f, []() { return new BassPulseStormAnimation(); } },
    { AnimationType::NEON_BEAT_TUNNEL,   "Neon Beat Tunnel",    MoodType::ENERGETIC,0.82f,0.72f, []() { return new NeonBeatTunnelAnimation(); } },
    { AnimationType::MULTI_LAYERED_HYBRID,"Hybrid",              MoodType::ENERGETIC,0.90f,0.82f, []() { return new MultiLayeredHybridAnimation(); } },
    { AnimationType::NEON_FLOW,          "Neon Flow",           MoodType::ENERGETIC,0.80f,0.62f, []() { return new NeonFlowAnimation(); } },
    { AnimationType::PSYCHEDELIC_INK_SQUIRTS, "Squirt",           MoodType::FLOATY, 0.65f, 0.42f, []() { return new PsychedelicInkSquirtAnimation(); } },
    { AnimationType::ALIEN_PULSE,        "Alien Pulse",         MoodType::INTENSE,0.85f, 0.88f, []() { return new AlienPulseAnimation(); } },
    { AnimationType::AURORA_DRIFT,       "Aurora Drift",        MoodType::CALM,   0.40f, 0.26f, []() { return new AuroraAnimation(); } },
    { AnimationType::PACIFICA,           "Pacifica",            MoodType::CALM,   0.45f, 0.30f, []() { return new PacificaAnimation(); } },
    { AnimationType::NOISE_WAVE,         "Noise Wave",          MoodType::DANCY,  0.70f, 0.48f, []() { return new NoiseWaveAnimation(); } },
    { AnimationType::FIRE2012,           "Fire",                MoodType::INTENSE,0.90f, 0.91f, []() { return new Fire2012Animation(); } },
    { AnimationType::GRADIENT_WASH,      "Gradient Wash",       MoodType::SILENT, 0.35f, 0.06f, []() { return new GradientWashAnimation(); } },
    { AnimationType::ETHERAL_PLASMA_DRIFT,"Plasma Drift",       MoodType::FLOATY, 0.50f, 0.12f, []() { return new EtherealPlasmaDriftAnimation(); } },
    { AnimationType::TWILIGHT_RIPPLE,    "Twilight Ripple",     MoodType::DESCENT,0.60f, 0.66f, []() { return new TwilightRippleAnimation(); } },
    { AnimationType::LAVA_LAMP,          "Lava Lamp",           MoodType::TEASE,  0.55f, 0.45f, []() { return new LavaLampAnimation(); } },
    { AnimationType::BEAT_SCANNER,       "Beat Scanner",        MoodType::BUILDUP,0.90f, 0.60f, []() { return new BeatScannerAnimation(); } },
    { AnimationType::GLITCHED_CYBER,     "Glitched Cyber",      MoodType::WEIRD,  0.80f, 0.78f, []() { return new GlitchedCyberAnimation(); } },
    { AnimationType::BEAT_DROP,          "Beat Drop",           MoodType::DROP,   0.98f, 0.95f, []() { return new BeatDropAnimation(); } },
    { AnimationType::TWO_SIN,            "Two Sin",             MoodType::DANCY,  0.75f, 0.52f, []() { return new TwoSinAnimation(); } },
    { AnimationType::COLOR_WAVES,        "Color Waves",         MoodType::ENERGETIC,0.85f,0.68f, []() { return new ColorWavesAnimation(); } },
    { AnimationType::RISING_TENSION,     "Rising Tension",      MoodType::BUILDUP,0.92f, 0.62f, []() { return new RisingTensionAnimation(); } },
    { AnimationType::STROBE_PULSE,       "Strobe Pulse",        MoodType::BUILDUP,0.95f, 0.64f, []() { return new StrobePulseAnimation(); } },
    { AnimationType::POP_FADE,           "Pop Fade",            MoodType::BUILDUP,0.85f, 0.58f, []() { return new PopFadeAnimation(); } },
    { AnimationType::TOMORROWLAND_STAGE, "Tomorrowland",        MoodType::DROP,   1.00f, 0.98f, []() { return new TomorrowlandStageAnimation(); } },
    { AnimationType::COLOR_SLAM,         "Color Slam",          MoodType::DROP,   0.95f, 0.94f, []() { return new ColorSlamAnimation(); } },
    { AnimationType::HYPER_SPIN,         "Hyper Spin",          MoodType::DROP,   0.98f, 0.96f, []() { return new HyperSpinAnimation(); } },
    { AnimationType::SPACE_WIZARDS,      "Space Wizards",       MoodType::WEIRD,  0.75f, 0.76f, []() { return new SpaceWizardsAndLizardsAnimation(); } },
    { AnimationType::PLAYA_CHAOS,        "Playa Chaos",         MoodType::WEIRD,  0.82f, 0.79f, []() { return new PlayaChaosAnimation(); } },
    { AnimationType::THREE_SIN_TWO,      "Three Sin Two",       MoodType::WEIRD,  0.70f, 0.75f, []() { return new ThreeSinTwoAnimation(); } },
    { AnimationType::HEARTBEAT,          "Heartbeat",           MoodType::TEASE,  0.50f, 0.44f, []() { return new HeartbeatAnimation(); } },
    { AnimationType::GENTLE_PULSE_WAVE,  "Gentle Pulse",        MoodType::TEASE,  0.52f, 0.46f, []() { return new GentlePulseWaveAnimation(); } },
    { AnimationType::MOONLIGHT,          "Moonlight",           MoodType::SILENT, 0.30f, 0.05f, []() { return new MoonlightAnimation(); } },
    { AnimationType::FOREST_CANOPY,      "Forest Canopy",       MoodType::SILENT, 0.32f, 0.04f, []() { return new ForestCanopyAnimation(); } },
    { AnimationType::LAVA_CYBER_STORM,   "Lava Cyber Storm",    MoodType::DESCENT,0.62f, 0.65f, []() { return new LavaCyberStormAnimation(); } },
    // Ported from the Serenity themes folder. See ThemeAnimations.h.
    { AnimationType::JUGGLE,             "Juggle",              MoodType::DANCY,  0.60f, 0.54f, []() { return new JuggleAnimation(); } },
    { AnimationType::SINELON,            "Sinelon",             MoodType::CALM,   0.55f, 0.40f, []() { return new SinelonAnimation(); } },
    { AnimationType::CONFETTI,           "Confetti",            MoodType::DANCY,  0.65f, 0.47f, []() { return new ConfettiAnimation(); } },
    { AnimationType::TWINKLE_STARS,      "Twinkle Stars",       MoodType::FLOATY, 0.40f, 0.22f, []() { return new TwinkleStarsAnimation(); } },
    { AnimationType::RAINBOW_MARCH,      "Rainbow March",       MoodType::DANCY,  0.70f, 0.57f, []() { return new RainbowMarchAnimation(); } },
    { AnimationType::BREATHING,          "Breathing",           MoodType::FLOATY, 0.30f, 0.08f, []() { return new BreathingAnimation(); } },
    { AnimationType::BEAT_TRAILS,        "Beat Trails",         MoodType::CALM,   0.50f, 0.36f, []() { return new BeatTrailsAnimation(); } },
    { AnimationType::BPM_STRIPES,        "BPM Stripes",         MoodType::ENERGETIC, 0.75f, 0.62f, []() { return new BpmAnimation(); } }
}};
