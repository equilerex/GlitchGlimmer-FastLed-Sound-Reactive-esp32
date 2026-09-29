#pragma once

#include <Arduino.h>
#include "AnimationCatalog.h"
#include "../scenes/Fit.h"
#include "../scenes/LayerTypes.h"

// Which moods and episodes each animation suits, and the layers a scene made from
// it keeps. Replaces the single mood tag in the catalog and the 7-axis profile.
//
// Written by hand from what each animation draws and what the old tags and
// profiles said about intent. They are first estimates, meant to be moved with the
// emulator open on real audio. Old tags were evidence when writing these, not the
// answer: an old Intense tag is not a fit of 1.0 on Intense.
//
// Rules the lists follow (plan, "Fit lists"):
//   - 1.0 means written for that mood. No animation has 1.0 on two moods, so a
//     generalist cannot win every mixed passage.
//   - FK_BUILDUP and FK_DESCENT entries are the episode weights.
//   - An FK_DROP entry marks a drop animation. Only those can be the drop base.
//   - Heartbeat is a bad long base and Lava Lamp fits quiet or sparse, not rhythm.

// BED draws on its own clock and suits any beat structure. RHYTHM drives its
// motion off the beat clock. EVENT is written around a structural moment.
enum AnimationRole { ROLE_BED = 0, ROLE_RHYTHM, ROLE_EVENT };

static const LayerType kNoLayer = LayerType::COUNT;

struct AnimationFitRow {
    uint8_t  role;
    FitList  fit;
    // The fixed scene layers, tuned by hand. Live layers fill the free slots.
    LayerType layers[2];
};

// Order matches AnimationType.
inline const AnimationFitRow& animationFit(AnimationType type) {
    static const AnimationFitRow table[] = {
        /* NONE */ {ROLE_BED, {{}}, {kNoLayer, kNoLayer}},
        /* PSYCHEDELIC_TUNNEL */ {ROLE_BED, {{{MN_FLOWING, 1.0f}, {MN_WARM, 0.5f}, {MN_FLOATY, 0.5f}, {MN_BRIGHT, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* ALIEN_BREATH */ {ROLE_BED, {{{MN_CALM, 1.0f}, {MN_FLOATY, 0.7f}, {MN_SPARSE, 0.5f}, {MN_QUIET, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* BASS_PULSE_STORM */ {ROLE_RHYTHM, {{{MN_INTENSE, 1.0f}, {MN_HEAVY, 0.8f}, {MN_DRIVING, 0.6f}, {MN_FULL, 0.3f}}}, {LayerType::DOMINANT_BAND_FIRE_TRAIL, kNoLayer}},
        /* NEON_BEAT_TUNNEL */ {ROLE_RHYTHM, {{{MN_DRIVING, 1.00f}, {MN_RUSHING, 0.90f}, {MN_FULL, 0.30f}, {MN_BRIGHT, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* MULTI_LAYERED_HYBRID */ {ROLE_BED, {{{MN_FULL, 1.0f}, {MN_INTENSE, 0.7f}, {MN_DRIVING, 0.5f}, {MN_CHAOTIC, 0.3f}}}, {LayerType::CENTROID_COLOR_FLOW, kNoLayer}},
        /* NEON_FLOW */ {ROLE_RHYTHM, {{{MN_BRIGHT, 1.0f}, {MN_FLOWING, 0.7f}, {MN_DRIVING, 0.4f}, {MN_WARM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* PSYCHEDELIC_INK_SQUIRTS */ {ROLE_RHYTHM, {{{MN_WARM, 1.0f}, {MN_FLOWING, 0.6f}, {MN_BRIGHT, 0.3f}, {MN_CALM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* ALIEN_PULSE */ {ROLE_RHYTHM, {{{MN_HEAVY, 1.0f}, {MN_INTENSE, 0.7f}, {MN_DRIVING, 0.6f}, {MN_CHAOTIC, 0.3f}}}, {LayerType::BPM_WAVE_PULSE, kNoLayer}},
        /* AURORA_DRIFT */ {ROLE_BED, {{{MN_FLOATY, 1.0f}, {MN_CALM, 0.7f}, {MN_BRIGHT, 0.4f}, {MN_SPARSE, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* PACIFICA */ {ROLE_BED, {{{MN_CALM, 1.0f}, {MN_FLOATY, 0.7f}, {MN_WARM, 0.4f}, {MN_QUIET, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* NOISE_WAVE */ {ROLE_BED, {{{MN_WARM, 0.8f}, {MN_FLOWING, 0.7f}, {MN_CALM, 0.4f}, {MN_SPARSE, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* FIRE2012 */ {ROLE_BED, {{{MN_HEAVY, 1.0f}, {MN_INTENSE, 0.6f}, {MN_DRIVING, 0.4f}, {MN_WARM, 0.3f}}}, {LayerType::BPM_BEAT_FLASH, kNoLayer}},
        /* GRADIENT_WASH */ {ROLE_BED, {{{MN_QUIET, 1.0f}, {MN_FLOATY, 0.6f}, {MN_SPARSE, 0.5f}}}, {kNoLayer, kNoLayer}},
        /* ETHERAL_PLASMA_DRIFT */ {ROLE_BED, {{{MN_FLOATY, 0.9f}, {MN_QUIET, 0.6f}, {MN_SPARSE, 0.6f}, {MN_CALM, 0.5f}, {MN_BRIGHT, 0.2f}}}, {kNoLayer, kNoLayer}},
        /* TWILIGHT_RIPPLE */ {ROLE_BED, {{{FK_DESCENT, 1.0f}, {MN_CALM, 0.7f}, {MN_WARM, 0.6f}, {MN_FLOWING, 0.5f}}}, {kNoLayer, kNoLayer}},
        /* LAVA_LAMP */ {ROLE_BED, {{{MN_FLOATY, 1.0f}, {MN_QUIET, 0.7f}, {MN_SPARSE, 0.6f}, {MN_CALM, 0.5f}}}, {kNoLayer, kNoLayer}},
        /* BEAT_SCANNER */ {ROLE_RHYTHM, {{{FK_BUILDUP, 0.8f}, {MN_DRIVING, 0.7f}, {MN_FLOWING, 0.6f}, {MN_CALM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* GLITCHED_CYBER */ {ROLE_BED, {{{MN_CHAOTIC, 1.00f}, {MN_RUSHING, 0.80f}, {MN_INTENSE, 0.60f}, {MN_BRIGHT, 0.40f}}}, {LayerType::LOUDNESS_LIGHTNING, kNoLayer}},
        /* BEAT_DROP */ {ROLE_EVENT, {{{FK_DROP, 1.0f}, {MN_INTENSE, 0.7f}, {MN_HEAVY, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* TWO_SIN */ {ROLE_BED, {{{MN_FLOWING, 0.90f}, {MN_DRIVING, 0.60f}, {MN_WARM, 0.50f}, {MN_CALM, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* COLOR_WAVES */ {ROLE_BED, {{{MN_FLOWING, 0.95f}, {MN_BRIGHT, 0.60f}, {MN_DRIVING, 0.50f}, {MN_WARM, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* RISING_TENSION */ {ROLE_EVENT, {{{FK_BUILDUP, 1.0f}, {MN_DRIVING, 0.4f}, {MN_INTENSE, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* STROBE_PULSE */ {ROLE_EVENT, {{{FK_BUILDUP, 0.9f}, {MN_DRIVING, 0.5f}, {MN_RUSHING, 0.5f}, {MN_CHAOTIC, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* POP_FADE */ {ROLE_EVENT, {{{FK_BUILDUP, 0.7f}, {MN_DRIVING, 0.4f}, {MN_FLOWING, 0.4f}, {MN_WARM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* TOMORROWLAND_STAGE */ {ROLE_BED, {{{FK_DROP, 1.00f}, {MN_INTENSE, 0.70f}, {MN_FULL, 0.40f}, {MN_BRIGHT, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* COLOR_SLAM */ {ROLE_RHYTHM, {{{FK_DROP, 0.9f}, {MN_INTENSE, 0.6f}, {MN_DRIVING, 0.5f}, {MN_RUSHING, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* HYPER_SPIN */ {ROLE_BED, {{{FK_DROP, 0.9f}, {MN_RUSHING, 0.7f}, {MN_INTENSE, 0.6f}, {MN_CHAOTIC, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* SPACE_WIZARDS */ {ROLE_BED, {{{MN_CHAOTIC, 0.8f}, {MN_BRIGHT, 0.6f}, {MN_FLOWING, 0.4f}, {MN_WARM, 0.3f}}}, {LayerType::CENTROID_COLOR_FLOW, kNoLayer}},
        /* PLAYA_CHAOS */ {ROLE_BED, {{{MN_CHAOTIC, 0.9f}, {MN_INTENSE, 0.6f}, {MN_HEAVY, 0.4f}, {MN_DRIVING, 0.4f}}}, {LayerType::LOUDNESS_LIGHTNING, kNoLayer}},
        /* THREE_SIN_TWO */ {ROLE_BED, {{{MN_BRIGHT, 0.80f}, {MN_FLOWING, 0.70f}, {MN_FULL, 0.40f}, {MN_CHAOTIC, 0.40f}}}, {LayerType::CENTROID_COLOR_FLOW, kNoLayer}},
        /* HEARTBEAT */ {ROLE_RHYTHM, {{{FK_BUILDUP, 0.8f}, {MN_CALM, 0.3f}, {MN_SPARSE, 0.3f}, {MN_QUIET, 0.2f}}}, {kNoLayer, kNoLayer}},
        /* GENTLE_PULSE_WAVE */ {ROLE_RHYTHM, {{{MN_CALM, 0.9f}, {MN_SPARSE, 0.6f}, {MN_FLOATY, 0.5f}, {MN_WARM, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* MOONLIGHT */ {ROLE_BED, {{{MN_QUIET, 0.9f}, {MN_FLOATY, 0.7f}, {MN_CALM, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* FOREST_CANOPY */ {ROLE_BED, {{{MN_QUIET, 1.0f}, {MN_FLOATY, 0.6f}, {MN_SPARSE, 0.5f}, {MN_CALM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* LAVA_CYBER_STORM */ {ROLE_BED, {{{MN_HEAVY, 0.9f}, {MN_INTENSE, 0.5f}, {MN_CHAOTIC, 0.5f}, {FK_DESCENT, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* JUGGLE */ {ROLE_RHYTHM, {{{MN_DRIVING, 0.90f}, {MN_FLOWING, 0.60f}, {MN_WARM, 0.40f}, {MN_BRIGHT, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* SINELON */ {ROLE_RHYTHM, {{{MN_CALM, 0.90f}, {MN_FLOWING, 0.60f}, {MN_WARM, 0.50f}, {MN_SPARSE, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* CONFETTI */ {ROLE_BED, {{{MN_BRIGHT, 0.9f}, {MN_WARM, 0.6f}, {MN_FLOWING, 0.5f}, {MN_CHAOTIC, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* TWINKLE_STARS */ {ROLE_BED, {{{MN_SPARSE, 1.0f}, {MN_BRIGHT, 0.7f}, {MN_FLOATY, 0.6f}, {MN_QUIET, 0.4f}, {MN_CALM, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* RAINBOW_MARCH */ {ROLE_BED, {{{MN_WARM, 0.90f}, {MN_FLOWING, 0.70f}, {MN_BRIGHT, 0.50f}, {MN_DRIVING, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* BREATHING */ {ROLE_RHYTHM, {{{MN_FLOATY, 0.8f}, {MN_QUIET, 0.8f}, {MN_CALM, 0.6f}, {MN_SPARSE, 0.5f}}}, {kNoLayer, kNoLayer}},
        /* BEAT_TRAILS */ {ROLE_RHYTHM, {{{MN_FLOWING, 0.8f}, {MN_CALM, 0.6f}, {MN_DRIVING, 0.4f}, {MN_SPARSE, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* BPM_STRIPES */ {ROLE_RHYTHM, {{{MN_DRIVING, 0.95f}, {MN_FLOWING, 0.50f}, {MN_RUSHING, 0.50f}, {MN_WARM, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* LIQUID_DREAM */ {ROLE_BED, {{{MN_FLOATY, 0.9f}, {MN_CALM, 0.7f}, {MN_WARM, 0.4f}, {MN_BRIGHT, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* DREAMWAVE_AURORA */ {ROLE_BED, {{{MN_FLOATY, 0.8f}, {MN_BRIGHT, 0.6f}, {MN_CALM, 0.5f}, {MN_WARM, 0.4f}}}, {kNoLayer, kNoLayer}},
        /* FIRE_TRIBE */ {ROLE_RHYTHM, {{{MN_DRIVING, 0.90f}, {MN_HEAVY, 0.60f}, {MN_WARM, 0.50f}, {MN_INTENSE, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* COSMIC_CHAOS */ {ROLE_BED, {{{MN_CHAOTIC, 0.9f}, {MN_INTENSE, 0.8f}, {MN_RUSHING, 0.6f}, {MN_FULL, 0.4f}}}, {LayerType::WORMHOLE_VORTEX, kNoLayer}},
        /* COSMIC_BEAST */ {ROLE_RHYTHM, {{{MN_INTENSE, 1.00f}, {MN_DRIVING, 0.70f}, {MN_FULL, 0.40f}, {MN_RUSHING, 0.50f}, {MN_HEAVY, 0.40f}}}, {LayerType::BPM_BEAT_FLASH, kNoLayer}},
        /* TRIPPY_HIPPIE */ {ROLE_BED, {{{MN_FLOWING, 0.90f}, {MN_WARM, 0.70f}, {MN_FLOATY, 0.40f}, {MN_BRIGHT, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* PLASMA_EFFECT */ {ROLE_BED, {{{MN_CALM, 0.90f}, {MN_FLOWING, 0.60f}, {MN_WARM, 0.60f}, {MN_BRIGHT, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* PLASMA_EFFECT_TWO */ {ROLE_RHYTHM, {{{MN_INTENSE, 0.90f}, {MN_DRIVING, 0.70f}, {MN_BRIGHT, 0.60f}, {MN_FULL, 0.40f}, {MN_RUSHING, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* LAVA_LAMP_TWO */ {ROLE_BED, {{{MN_HEAVY, 0.7f}, {MN_FLOATY, 0.6f}, {MN_WARM, 0.5f}, {MN_CALM, 0.5f}, {MN_SPARSE, 0.3f}}}, {kNoLayer, kNoLayer}},
        /* THREE_SIN */ {ROLE_BED, {{{MN_DRIVING, 0.80f}, {MN_FLOWING, 0.60f}, {MN_WARM, 0.50f}, {MN_FULL, 0.30f}}}, {kNoLayer, kNoLayer}},
        /* TWO_SIN_PSY */ {ROLE_BED, {{{MN_RUSHING, 1.00f}, {MN_CHAOTIC, 0.60f}, {MN_BRIGHT, 0.60f}, {MN_DRIVING, 0.40f}}}, {kNoLayer, kNoLayer}},
        /* RAINBOW_GLITTER */ {ROLE_BED, {{{MN_BRIGHT, 1.0f}, {MN_FULL, 0.4f}, {MN_WARM, 0.4f}, {MN_DRIVING, 0.4f}}}, {kNoLayer, kNoLayer}},
    };
    static_assert(sizeof(table) / sizeof(table[0]) == static_cast<size_t>(AnimationType::COUNT),
                  "one fit row per AnimationType");
    return table[static_cast<size_t>(type)];
}

// The live layers: layers the director may add into a free slot for the current
// moods. Only layers with no reserved meaning are candidates. Reserved, and so
// absent here: HIGHLIGHT with ENERGY (the drop onset), MOOD_ARC (tease),
// DYNAMICS_FLICKER_STORM (anomaly), ENERGY_SPIRAL (drop window), BUILDUP_SWELL,
// DESCENT_COOL, CENTROID_GLOW_WIPE and TRANSITION (episode edges), REACTIVE (the
// beat pop) and BASE.
struct LiveLayerFit {
    LayerType type;
    FitList   fit;
};

static const LiveLayerFit kLiveLayerFits[] = {
    {LayerType::BACKGROUND,               {{{MN_QUIET, 0.7f}, {MN_FLOATY, 0.7f}, {MN_SPARSE, 0.6f}}}},
    {LayerType::OVERLAY,                  {{{MN_BRIGHT, 0.7f}, {MN_WARM, 0.6f}, {MN_FLOWING, 0.5f}}}},
    {LayerType::DOMINANT_BAND_FIRE_TRAIL, {{{MN_HEAVY, 0.9f}, {MN_INTENSE, 0.6f}}}},
    {LayerType::TRIWAVE_BEAT,             {{{MN_DRIVING, 0.7f}, {MN_RUSHING, 0.7f}}}},
    {LayerType::DOMINANT_BAND_TRAIL,      {{{MN_FLOWING, 0.7f}, {MN_WARM, 0.6f}, {MN_CALM, 0.4f}}}},
    {LayerType::WAVEFORM_SCRIBBLE,        {{{MN_WARM, 0.9f}, {MN_SPARSE, 0.5f}, {MN_CALM, 0.4f}}}},
    {LayerType::WORMHOLE_VORTEX,          {{{MN_CHAOTIC, 0.8f}, {MN_INTENSE, 0.6f}, {MN_RUSHING, 0.5f}}}},
    {LayerType::LOUDNESS_LIGHTNING,       {{{MN_INTENSE, 0.9f}, {MN_CHAOTIC, 0.7f}, {MN_HEAVY, 0.4f}}}},
    {LayerType::BPM_WAVE_PULSE,           {{{MN_FLOWING, 0.8f}, {MN_DRIVING, 0.7f}, {MN_CALM, 0.4f}}}},
    {LayerType::BPM_BEAT_FLASH,           {{{MN_DRIVING, 0.9f}, {MN_RUSHING, 0.8f}}}},
    {LayerType::CENTROID_COLOR_FLOW,      {{{MN_WARM, 0.7f}, {MN_BRIGHT, 0.6f}, {MN_FLOWING, 0.6f}}}},
};
static const int kLiveLayerFitCount = sizeof(kLiveLayerFits) / sizeof(kLiveLayerFits[0]);
