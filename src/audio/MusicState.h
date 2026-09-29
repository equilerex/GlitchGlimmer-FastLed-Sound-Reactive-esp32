#pragma once

#include <Arduino.h>
#include <cmath>

#include "Moods.h"

// A coordinate is deliberately not a mood or a rank. Confidence says whether
// the value can be estimated; it must not be inferred from value == 0.
struct MusicCoord {
    float value = 0.0f;
    float confidence = 0.0f;
    float trend = 0.0f;
};

struct MusicState {
    MusicCoord intensity;
    MusicCoord activity;
    MusicCoord brightness;
    MusicCoord weight;
    MusicCoord pulse;
    MusicCoord tempo;
    MusicCoord texture;
    MusicCoord presence;
    // Band-balance and low-end readings behind the moods. Tilt is 0 for a
    // bass-heavy spectrum and 1 for a bright one.
    MusicCoord tilt;
    MusicCoord evenness;
    MusicCoord punch;
    MusicCoord body;

    // Degree to which the music has each mood, 0..1, smoothed. Measured only.
    float mood[MN_COUNT] = {};

    bool buildup = false;
    bool descent = false;
    bool dropDetected = false;
    bool teaseDetected = false;
    bool anomaly = false;

    // False is reserved for legacy hand-built snapshots. The analyser sets it
    // true after filling the block, allowing the strangler classifier to retain
    // old fixture construction while reading the new state when present.
    bool initialized = false;
};

struct MusicCoordTracker {
    MusicCoord output;
    float fast = 0.0f;
    float slow = 0.0f;
    bool seeded = false;
    float fastTau = 1.0f;
    float slowTau = 8.0f;

    void reset() {
        output = MusicCoord();
        fast = slow = 0.0f;
        seeded = false;
    }

    void update(float value, float confidence, float dtSeconds) {
        value = constrain(value, 0.0f, 1.0f);
        confidence = constrain(confidence, 0.0f, 1.0f);
        if (!seeded) {
            fast = slow = value;
            seeded = true;
        } else {
            const float dt = fmaxf(0.0f, dtSeconds);
            const float fastAlpha = 1.0f - expf(-dt / fastTau);
            const float slowAlpha = 1.0f - expf(-dt / slowTau);
            fast += (value - fast) * fastAlpha;
            slow += (value - slow) * slowAlpha;
        }
        output.value = fast;
        output.confidence = confidence;
        output.trend = dtSeconds > 1e-5f ? (fast - slow) / dtSeconds : 0.0f;
    }
};

// The readings the moods are measured from, taken from a finished music state.
// Loudness is not one of them. Presence is the level test itself, so its
// confidence is 1 and a quiet passage can still be Quiet. Shared by the analyser
// and the replay, so a diagnosis reads exactly what the firmware read.
inline MoodReadings moodReadingsOf(const MusicState& m, float bpm, float dynamics, float gateGain) {
    const float gate = gateGain < 0.0f ? 0.0f : (gateGain > 1.0f ? 1.0f : gateGain);
    MoodReadings r;
    r.value[RD_PULSE]      = m.pulse.value;      r.confidence[RD_PULSE]      = m.pulse.confidence;
    r.value[RD_ACTIVITY]   = m.activity.value;   r.confidence[RD_ACTIVITY]   = m.activity.confidence;
    r.value[RD_TEXTURE]    = m.texture.value;    r.confidence[RD_TEXTURE]    = m.texture.confidence;
    r.value[RD_BRIGHTNESS] = m.brightness.value; r.confidence[RD_BRIGHTNESS] = m.brightness.confidence;
    r.value[RD_TEMPO]      = bpm;                r.confidence[RD_TEMPO]      = m.tempo.confidence;
    r.value[RD_PUNCH]      = m.punch.value;      r.confidence[RD_PUNCH]      = m.punch.confidence;
    r.value[RD_BODY]       = m.body.value;       r.confidence[RD_BODY]       = m.body.confidence;
    r.value[RD_DYNAMICS]   = dynamics;           r.confidence[RD_DYNAMICS]   = gate;
    r.value[RD_PRESENCE]   = gateGain;           r.confidence[RD_PRESENCE]   = 1.0f;
    r.value[RD_TILT]       = m.tilt.value;       r.confidence[RD_TILT]       = m.tilt.confidence;
    r.value[RD_EVENNESS]   = m.evenness.value;   r.confidence[RD_EVENNESS]   = m.evenness.confidence;
    return r;
}
