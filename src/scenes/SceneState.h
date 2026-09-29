#pragma once

#include <Arduino.h>
#include "../audio/AudioFeatures.h"

// Forward declaration for SceneDefinition only (pointer in header)
struct SceneDefinition;

struct SceneState {
    const SceneDefinition* activeScene;
    unsigned long sceneStartMillis;
    float sceneMinDurationMs;
    float sceneIdealDurationMs;

    // What the scene clock was scaled by when this scene began. Tempo scales the
    // minimum, level and dynamics scale the ideal length. They set how long a scene
    // runs, and not which one runs.
    float startBpm = 0.0f;
    float startLevel = 0.0f;
    float startDynamics = 0.0f;

    // The last few base animations, newest first, so the selector can lean away
    // from a scene it just left. Without it two scenes with nearly equal score
    // trade places every dwell, which reads as a loop of two looks.
    static const int kRecent = 3;
    int recent[kRecent];

    // Base durations, adjustable at run time so the scene clock can be tuned
    // against real audio from the page rather than by rebuilding. The tempo and
    // level factors still scale them.
    float minBaseMs   = 4000.0f;
    float idealBaseMs = 9000.0f;
    float idealSpanMs = 5000.0f;

    int sceneChangeCount;
    unsigned long totalUptimeMs;

    SceneState();

    void beginScene(const SceneDefinition* def, float bpm, float level, float dynamics);
    void beginScene(const SceneDefinition* def, const AudioFeatures& now) {
        beginScene(def, now.bpm, now.level, now.dynamics);
    }
    float calculateMinDuration(float bpm) const;
    float calculateIdealDuration(float level, float dynamics) const;
    unsigned long elapsed() const;

    // Recomputes the running scene's own thresholds from what it began with.
    // Without this a duration change would only take effect on the next scene.
    void refreshDurations();

    void setMinBaseMs(float ms);
    void setIdealBaseMs(float ms);
    void setIdealSpanMs(float ms);
    float getMinBaseMs() const { return minBaseMs; }
    float getIdealBaseMs() const { return idealBaseMs; }
    float getIdealSpanMs() const { return idealSpanMs; }

    void debug() const;
};

// Note: method implementations moved to SceneState.cpp
