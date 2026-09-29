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
// This table is the single source for each animation's intensity, which the
// emulator's scene catalog shows as the scene's own level. What moods and episodes
// an animation suits is in AnimationFit.h, and it is not a column here: a single
// mood tag per animation was what let one name pick the scene.
const std::array<AnimationMeta, static_cast<size_t>(AnimationType::COUNT)> animationCatalog = {{
    // Index must equal the AnimationType value. Every lookup is
    // animationCatalog[static_cast<size_t>(type)], and AnimationType::NONE is 0, so
    // without a slot here each entry is read one position early and the final one
    // is left value-initialised, with an empty std::function that aborts the moment
    // animationFactory() invokes it.
    { AnimationType::NONE, "None", 0.0f, []() -> Animation* { return nullptr; } },
    { AnimationType::PSYCHEDELIC_TUNNEL, "Psychedelic Tunnel", 0.35f, []() { return new PsychedelicTunnelAnimation(); } },
    { AnimationType::ALIEN_BREATH,       "Alien Breath",        0.18f, []() { return new AlienBreathAnimation(); } },
    { AnimationType::BASS_PULSE_STORM,   "Bass Pulse Storm",    0.97f, []() { return new BassPulseStormAnimation(); } },
    { AnimationType::NEON_BEAT_TUNNEL,   "Neon Beat Tunnel",    0.72f, []() { return new NeonBeatTunnelAnimation(); } },
    { AnimationType::MULTI_LAYERED_HYBRID,"Hybrid",              0.82f, []() { return new MultiLayeredHybridAnimation(); } },
    { AnimationType::NEON_FLOW,          "Neon Flow",           0.62f, []() { return new NeonFlowAnimation(); } },
    { AnimationType::PSYCHEDELIC_INK_SQUIRTS, "Squirt",           0.42f, []() { return new PsychedelicInkSquirtAnimation(); } },
    { AnimationType::ALIEN_PULSE,        "Alien Pulse",         0.88f, []() { return new AlienPulseAnimation(); } },
    { AnimationType::AURORA_DRIFT,       "Aurora Drift",        0.26f, []() { return new AuroraAnimation(); } },
    { AnimationType::PACIFICA,           "Pacifica",            0.30f, []() { return new PacificaAnimation(); } },
    { AnimationType::NOISE_WAVE,         "Noise Wave",          0.48f, []() { return new NoiseWaveAnimation(); } },
    { AnimationType::FIRE2012,           "Fire",                0.91f, []() { return new Fire2012Animation(); } },
    { AnimationType::GRADIENT_WASH,      "Gradient Wash",       0.06f, []() { return new GradientWashAnimation(); } },
    { AnimationType::ETHERAL_PLASMA_DRIFT,"Plasma Drift",       0.12f, []() { return new EtherealPlasmaDriftAnimation(); } },
    { AnimationType::TWILIGHT_RIPPLE,    "Twilight Ripple",     0.66f, []() { return new TwilightRippleAnimation(); } },
    { AnimationType::LAVA_LAMP,          "Lava Lamp",           0.45f, []() { return new LavaLampAnimation(); } },
    { AnimationType::BEAT_SCANNER,       "Beat Scanner",        0.60f, []() { return new BeatScannerAnimation(); } },
    { AnimationType::GLITCHED_CYBER,     "Glitched Cyber",      0.78f, []() { return new GlitchedCyberAnimation(); } },
    { AnimationType::BEAT_DROP,          "Beat Drop",           0.95f, []() { return new BeatDropAnimation(); } },
    { AnimationType::TWO_SIN,            "Two Sin",             0.52f, []() { return new TwoSinAnimation(); } },
    { AnimationType::COLOR_WAVES,        "Color Waves",         0.68f, []() { return new ColorWavesAnimation(); } },
    { AnimationType::RISING_TENSION,     "Rising Tension",      0.62f, []() { return new RisingTensionAnimation(); } },
    { AnimationType::STROBE_PULSE,       "Strobe Pulse",        0.64f, []() { return new StrobePulseAnimation(); } },
    { AnimationType::POP_FADE,           "Pop Fade",            0.58f, []() { return new PopFadeAnimation(); } },
    { AnimationType::TOMORROWLAND_STAGE, "Tomorrowland",        0.98f, []() { return new TomorrowlandStageAnimation(); } },
    { AnimationType::COLOR_SLAM,         "Color Slam",          0.94f, []() { return new ColorSlamAnimation(); } },
    { AnimationType::HYPER_SPIN,         "Hyper Spin",          0.96f, []() { return new HyperSpinAnimation(); } },
    { AnimationType::SPACE_WIZARDS,      "Space Wizards",       0.76f, []() { return new SpaceWizardsAndLizardsAnimation(); } },
    { AnimationType::PLAYA_CHAOS,        "Playa Chaos",         0.79f, []() { return new PlayaChaosAnimation(); } },
    { AnimationType::THREE_SIN_TWO,      "Three Sin Two",       0.75f, []() { return new ThreeSinTwoAnimation(); } },
    { AnimationType::HEARTBEAT,          "Heartbeat",           0.44f, []() { return new HeartbeatAnimation(); } },
    { AnimationType::GENTLE_PULSE_WAVE,  "Gentle Pulse",        0.46f, []() { return new GentlePulseWaveAnimation(); } },
    { AnimationType::MOONLIGHT,          "Moonlight",           0.05f, []() { return new MoonlightAnimation(); } },
    { AnimationType::FOREST_CANOPY,      "Forest Canopy",       0.04f, []() { return new ForestCanopyAnimation(); } },
    { AnimationType::LAVA_CYBER_STORM,   "Lava Cyber Storm",    0.65f, []() { return new LavaCyberStormAnimation(); } },
    // Ported from the Serenity themes folder. See ThemeAnimations.h.
    { AnimationType::JUGGLE,             "Juggle",              0.54f, []() { return new JuggleAnimation(); } },
    { AnimationType::SINELON,            "Sinelon",             0.40f, []() { return new SinelonAnimation(); } },
    { AnimationType::CONFETTI,           "Confetti",            0.47f, []() { return new ConfettiAnimation(); } },
    { AnimationType::TWINKLE_STARS,      "Twinkle Stars",       0.22f, []() { return new TwinkleStarsAnimation(); } },
    { AnimationType::RAINBOW_MARCH,      "Rainbow March",       0.57f, []() { return new RainbowMarchAnimation(); } },
    { AnimationType::BREATHING,          "Breathing",           0.08f, []() { return new BreathingAnimation(); } },
    { AnimationType::BEAT_TRAILS,        "Beat Trails",         0.36f, []() { return new BeatTrailsAnimation(); } },
    { AnimationType::BPM_STRIPES,        "BPM Stripes",         0.62f, []() { return new BpmAnimation(); } },
    { AnimationType::LIQUID_DREAM,       "Liquid Dream",        0.35f, []() { return new LiquidDreamAnimation(); } },
    { AnimationType::DREAMWAVE_AURORA,   "Dreamwave Aurora",    0.38f, []() { return new DreamwaveAuroraAnimation(); } },
    { AnimationType::FIRE_TRIBE,         "Fire Tribe",          0.68f, []() { return new FireTribeAnimation(); } },
    { AnimationType::COSMIC_CHAOS,       "Cosmic Chaos",        0.84f, []() { return new CosmicChaosAnimation(); } },
    { AnimationType::COSMIC_BEAST,       "Cosmic Beast",        0.92f, []() { return new CosmicBeastAnimation(); } },
    { AnimationType::TRIPPY_HIPPIE,      "Trippy Hippie",       0.58f, []() { return new TrippyHippieAnimation(); } },
    { AnimationType::PLASMA_EFFECT,      "Plasma Effect",       0.48f, []() { return new PlasmaEffectAnimation(); } },
    { AnimationType::PLASMA_EFFECT_TWO,  "Plasma Effect 2",     0.78f, []() { return new PlasmaEffectTwoAnimation(); } },
    { AnimationType::LAVA_LAMP_TWO,      "Lava Lamp 2",         0.52f, []() { return new LavaLampTwoAnimation(); } },
    { AnimationType::THREE_SIN,          "Three Sin",           0.64f, []() { return new ThreeSinAnimation(); } },
    { AnimationType::TWO_SIN_PSY,        "Two Sin nPsy",        0.72f, []() { return new TwoSinPsyAnimation(); } },
    { AnimationType::RAINBOW_GLITTER,    "Rainbow with Glitter",0.66f, []() { return new RainbowWithGlitterAnimation(); } }
}};
