#pragma once

#include <Arduino.h>
#include <cmath>

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
