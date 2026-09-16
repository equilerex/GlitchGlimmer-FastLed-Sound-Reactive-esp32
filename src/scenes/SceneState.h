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

    int sceneChangeCount;
    unsigned long totalUptimeMs;

    SceneState();

    void beginScene(const SceneDefinition* def, const MoodSnapshot& moodNow);
    bool shouldTransition(const MoodSnapshot& moodNow) const;
    float calculateMinDuration(const MoodSnapshot& mood) const;
    float calculateIdealDuration(const MoodSnapshot& mood) const;
    unsigned long elapsed() const;

    void debug() const;
};

// Note: method implementations moved to SceneState.cpp
