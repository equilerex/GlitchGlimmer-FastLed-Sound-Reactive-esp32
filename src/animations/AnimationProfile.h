// AnimationProfile.h
#pragma once
#include <Arduino.h>
#include "AnimationCatalog.h"
#include "../audio/MusicState.h"

// Where in music space an animation belongs, so the selector ranks scenes by how
// near the music is to what the animation was written for.
//
// A target of kAny means the animation does not care about that axis, and the
// axis is left out of the distance rather than scored as a mismatch. Most
// animations are indifferent to most axes, and a fully specified profile would
// invent preferences nobody wrote into the animation.
//
// Axes are the MusicState coordinates in this order. presence is not an axis: the
// silence gate is handled before selection, not by ranking.
enum ProfileAxis {
    AX_INTENSITY = 0, AX_ACTIVITY, AX_BRIGHTNESS, AX_WEIGHT, AX_PULSE, AX_TEMPO, AX_TEXTURE,
    AX_COUNT
};

// BED: a base that draws on its own clock and suits any beat structure.
// RHYTHM: drives its motion off the beat clock, so it only earns its place when
//         the pulse coordinate says a beat is trackable.
// EVENT: written around a structural moment (build, drop). Chosen for the event
//        and scored on the same axes as the rest.
enum AnimationRole { ROLE_BED = 0, ROLE_RHYTHM, ROLE_EVENT };

static const float kAny = -1.0f;

struct AnimationProfile {
    float target[AX_COUNT];
    uint8_t role;
};

// Order matches AnimationType. Values are where the music sits when the animation
// looks right, judged from what each one draws and which inputs it reads. They
// are first estimates, meant to be moved with the visualiser open on real audio.
//
//                          int   act   bri   wgt   pulse tempo tex
inline const AnimationProfile& animationProfile(AnimationType type) {
    static const AnimationProfile table[] = {
        /* NONE                  */ {{kAny, kAny, kAny, kAny, kAny, kAny, kAny}, ROLE_BED},
        /* PSYCHEDELIC_TUNNEL    */ {{0.35f, 0.40f, kAny, 0.40f, kAny, 0.50f, kAny}, ROLE_BED},
        /* ALIEN_BREATH          */ {{0.18f, 0.10f, kAny, kAny, kAny, 0.30f, kAny}, ROLE_BED},
        /* BASS_PULSE_STORM      */ {{0.95f, 0.70f, kAny, 0.90f, kAny, kAny, kAny}, ROLE_RHYTHM},
        /* NEON_BEAT_TUNNEL      */ {{0.70f, 0.60f, kAny, kAny, 0.80f, 0.60f, kAny}, ROLE_RHYTHM},
        /* MULTI_LAYERED_HYBRID  */ {{0.80f, 0.80f, 0.60f, 0.60f, kAny, kAny, kAny}, ROLE_BED},
        /* NEON_FLOW             */ {{0.60f, 0.70f, 0.70f, 0.50f, kAny, kAny, 0.50f}, ROLE_RHYTHM},
        /* PSYCHEDELIC_INK       */ {{0.42f, 0.50f, 0.50f, 0.50f, kAny, 0.40f, kAny}, ROLE_RHYTHM},
        /* ALIEN_PULSE           */ {{0.85f, 0.70f, 0.40f, 0.70f, kAny, kAny, 0.60f}, ROLE_RHYTHM},
        /* AURORA_DRIFT          */ {{0.25f, 0.15f, 0.50f, 0.20f, kAny, 0.25f, 0.20f}, ROLE_BED},
        /* PACIFICA              */ {{0.30f, 0.10f, 0.35f, 0.35f, kAny, 0.30f, 0.20f}, ROLE_BED},
        /* NOISE_WAVE            */ {{0.48f, 0.40f, kAny, kAny, kAny, 0.50f, 0.40f}, ROLE_BED},
        /* FIRE2012              */ {{0.90f, 0.60f, 0.30f, 0.80f, kAny, kAny, 0.60f}, ROLE_BED},
        /* GRADIENT_WASH         */ {{0.06f, 0.03f, kAny, kAny, kAny, 0.20f, kAny}, ROLE_BED},
        /* ETHERAL_PLASMA_DRIFT  */ {{0.12f, 0.10f, 0.50f, 0.20f, kAny, 0.25f, 0.20f}, ROLE_BED},
        /* TWILIGHT_RIPPLE       */ {{0.50f, 0.30f, 0.30f, 0.40f, kAny, 0.40f, 0.30f}, ROLE_BED},
        /* LAVA_LAMP             */ {{0.45f, 0.20f, 0.20f, 0.50f, kAny, 0.35f, 0.30f}, ROLE_BED},
        /* BEAT_SCANNER          */ {{0.60f, 0.50f, kAny, kAny, 0.80f, 0.60f, kAny}, ROLE_RHYTHM},
        /* GLITCHED_CYBER        */ {{0.78f, 0.80f, 0.80f, kAny, 0.30f, 0.70f, 0.80f}, ROLE_BED},
        /* BEAT_DROP             */ {{0.95f, 0.80f, kAny, 0.90f, kAny, kAny, kAny}, ROLE_EVENT},
        /* TWO_SIN               */ {{0.52f, 0.50f, kAny, kAny, kAny, 0.60f, 0.30f}, ROLE_BED},
        /* COLOR_WAVES           */ {{0.68f, 0.50f, 0.50f, kAny, kAny, 0.60f, 0.30f}, ROLE_BED},
        /* RISING_TENSION        */ {{0.62f, 0.70f, kAny, kAny, kAny, 0.70f, kAny}, ROLE_EVENT},
        /* STROBE_PULSE          */ {{0.64f, 0.80f, kAny, kAny, 0.60f, 0.80f, kAny}, ROLE_EVENT},
        /* POP_FADE              */ {{0.58f, 0.70f, kAny, kAny, kAny, 0.60f, kAny}, ROLE_EVENT},
        /* TOMORROWLAND_STAGE    */ {{0.98f, 0.90f, 0.70f, 0.80f, kAny, 0.80f, kAny}, ROLE_BED},
        /* COLOR_SLAM            */ {{0.94f, 0.80f, kAny, kAny, 0.80f, 0.70f, kAny}, ROLE_RHYTHM},
        /* HYPER_SPIN            */ {{0.96f, 0.85f, kAny, kAny, kAny, 0.80f, kAny}, ROLE_BED},
        /* SPACE_WIZARDS         */ {{0.76f, 0.60f, 0.70f, kAny, kAny, kAny, 0.50f}, ROLE_BED},
        /* PLAYA_CHAOS           */ {{0.79f, 0.60f, kAny, 0.50f, kAny, kAny, 0.60f}, ROLE_BED},
        /* THREE_SIN_TWO         */ {{0.75f, 0.50f, 0.60f, kAny, kAny, kAny, 0.40f}, ROLE_BED},
        /* HEARTBEAT             */ {{0.44f, 0.20f, kAny, 0.70f, 0.80f, 0.30f, kAny}, ROLE_RHYTHM},
        /* GENTLE_PULSE_WAVE     */ {{0.46f, 0.20f, kAny, 0.40f, 0.70f, 0.40f, kAny}, ROLE_RHYTHM},
        /* MOONLIGHT             */ {{0.05f, 0.05f, 0.50f, kAny, kAny, 0.25f, kAny}, ROLE_BED},
        /* FOREST_CANOPY         */ {{0.04f, 0.05f, 0.30f, kAny, kAny, 0.25f, kAny}, ROLE_BED},
        /* LAVA_CYBER_STORM      */ {{0.65f, 0.50f, kAny, 0.60f, kAny, 0.50f, 0.40f}, ROLE_BED},
        /* JUGGLE                */ {{0.55f, 0.60f, kAny, kAny, kAny, 0.60f, kAny}, ROLE_RHYTHM},
        /* SINELON               */ {{0.40f, 0.40f, kAny, kAny, 0.70f, 0.45f, kAny}, ROLE_RHYTHM},
        /* CONFETTI              */ {{0.47f, 0.60f, 0.60f, kAny, kAny, 0.50f, 0.40f}, ROLE_BED},
        /* TWINKLE_STARS         */ {{0.22f, 0.30f, 0.70f, 0.10f, kAny, 0.30f, 0.30f}, ROLE_BED},
        /* RAINBOW_MARCH         */ {{0.57f, 0.50f, 0.60f, kAny, kAny, 0.60f, 0.20f}, ROLE_BED},
        /* BREATHING             */ {{0.10f, 0.05f, kAny, kAny, 0.60f, 0.20f, kAny}, ROLE_RHYTHM},
        /* BEAT_TRAILS           */ {{0.36f, 0.40f, kAny, kAny, 0.80f, 0.50f, kAny}, ROLE_RHYTHM},
        /* BPM_STRIPES           */ {{0.62f, 0.50f, kAny, 0.50f, 0.80f, 0.60f, kAny}, ROLE_RHYTHM},
        /* LIQUID_DREAM          */ {{0.35f, 0.20f, 0.50f, 0.30f, kAny, 0.25f, 0.20f}, ROLE_BED},
        /* DREAMWAVE_AURORA      */ {{0.38f, 0.25f, 0.60f, 0.25f, kAny, 0.30f, 0.30f}, ROLE_BED},
        /* FIRE_TRIBE            */ {{0.68f, 0.60f, 0.40f, 0.70f, 0.70f, 0.55f, 0.40f}, ROLE_RHYTHM},
        /* COSMIC_CHAOS          */ {{0.84f, 0.80f, 0.75f, 0.50f, 0.40f, 0.75f, 0.85f}, ROLE_BED},
        /* COSMIC_BEAST          */ {{0.92f, 0.85f, 0.60f, 0.80f, 0.80f, 0.85f, 0.60f}, ROLE_RHYTHM},
        /* TRIPPY_HIPPIE         */ {{0.58f, 0.50f, 0.70f, 0.30f, 0.60f, 0.45f, 0.40f}, ROLE_BED},
        /* PLASMA_EFFECT         */ {{0.48f, 0.40f, 0.60f, 0.40f, kAny, 0.40f, 0.30f}, ROLE_BED},
        /* PLASMA_EFFECT_TWO     */ {{0.78f, 0.70f, 0.70f, 0.50f, 0.70f, 0.65f, 0.40f}, ROLE_RHYTHM},
        /* LAVA_LAMP_TWO         */ {{0.52f, 0.30f, 0.30f, 0.75f, kAny, 0.35f, 0.30f}, ROLE_BED},
        /* THREE_SIN             */ {{0.64f, 0.55f, 0.60f, 0.50f, kAny, 0.50f, 0.40f}, ROLE_BED},
        /* TWO_SIN_PSY           */ {{0.72f, 0.65f, 0.80f, 0.40f, 0.60f, 0.60f, 0.70f}, ROLE_BED},
        /* RAINBOW_GLITTER       */ {{0.66f, 0.60f, 0.80f, 0.30f, 0.60f, 0.55f, 0.30f}, ROLE_BED},
    };
    static_assert(sizeof(table) / sizeof(table[0]) == static_cast<size_t>(AnimationType::COUNT),
                  "one profile per AnimationType");
    return table[static_cast<size_t>(type)];
}

// Distance from the music to an animation's profile, 0 = perfect, about 1 = far.
//
// Each axis counts in proportion to how sure the analyser is of it, floored so a
// low-confidence axis still counts a little: a coordinate that has not settled yet
// reads near 0 and would otherwise let any profile that asks for 0 win on a guess.
// Intensity counts double, since loudness is the axis every viewer reads first.
inline float profileDistance(const AnimationProfile& p, const MusicState& m) {
    static const float axisWeight[AX_COUNT] = {2.0f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    const MusicCoord* coord[AX_COUNT] = {
        &m.intensity, &m.activity, &m.brightness, &m.weight, &m.pulse, &m.tempo, &m.texture
    };
    float sum = 0.0f, wsum = 0.0f;
    for (int a = 0; a < AX_COUNT; ++a) {
        if (p.target[a] < 0.0f) continue;
        const float c = constrain(coord[a]->confidence, 0.0f, 1.0f);
        const float w = axisWeight[a] * (0.35f + 0.65f * c);
        const float d = p.target[a] - coord[a]->value;
        sum  += w * d * d;
        wsum += w;
    }
    return wsum > 0.0f ? sqrtf(sum / wsum) : 1.0f;
}
