#pragma once

#include <Arduino.h>
#include "MoodHistory.h"  // for MoodSnapshot definition

// Forward declaration for SceneDefinition only (pointer in header)
struct SceneDefinition;

struct SceneState {
    const SceneDefinition* activeScene;
    unsigned long sceneStartMillis;
    float sceneMinDurationMs;
    float sceneIdealDurationMs;
    MoodSnapshot lastMood;
    MoodType startMood;

    // The last few base animations, newest first, so the selector can lean away
    // from a scene it just left. Without it two scenes with nearly equal distance
    // to the music trade places every dwell, which reads as a loop of two looks.
    static const int kRecent = 3;
    int recent[kRecent];

    // Base durations, adjustable at run time so the scene clock can be tuned
    // against real audio from the page rather than by rebuilding. The tempo and
    // mood factors still scale them.
    float minBaseMs   = 4000.0f;
    float idealBaseMs = 9000.0f;
    float idealSpanMs = 5000.0f;

    int sceneChangeCount;
    unsigned long totalUptimeMs;

    SceneState();

    void beginScene(const SceneDefinition* def, const MoodSnapshot& moodNow, MoodType moodNowType);
    bool shouldTransition(const MoodSnapshot& moodNow, MoodType moodNowType) const;
    float calculateMinDuration(const MoodSnapshot& mood) const;
    float calculateIdealDuration(const MoodSnapshot& mood) const;
    unsigned long elapsed() const;

    // Recomputes the running scene's own thresholds from the mood it began in.
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
