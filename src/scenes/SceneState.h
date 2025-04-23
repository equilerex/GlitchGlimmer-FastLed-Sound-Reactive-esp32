#pragma once

#include <Arduino.h>

#include "../scenes/SceneRegistry.h"

struct SceneState {
    const SceneDefinition* activeScene = nullptr;
    unsigned long sceneStartMillis = 0;
    float sceneMinDurationMs = 5000;
    float sceneIdealDurationMs = 12000;

    MoodSnapshot lastMood;

    int sceneChangeCount = 0;
    unsigned long totalUptimeMs = 0;

    void beginScene(const SceneDefinition* def, const MoodSnapshot& moodNow) {
        activeScene = def;
        sceneStartMillis = millis();
        lastMood = moodNow;
        sceneMinDurationMs = calculateMinDuration(moodNow);
        sceneIdealDurationMs = calculateIdealDuration(moodNow);
        sceneChangeCount++;
    }

    bool shouldTransition(const MoodSnapshot& moodNow) const {
        unsigned long now = millis();
        bool pastMin = (now - sceneStartMillis) > sceneMinDurationMs;
        bool moodShift = fabs(moodNow.energy - lastMood.energy) > 0.25f
                      || fabs(moodNow.bpm - lastMood.bpm) > 15.0f
                      || fabs(moodNow.dynamics - lastMood.dynamics) > 0.2f;
        bool maxedOutTime = (now - sceneStartMillis) > sceneIdealDurationMs;
        return pastMin && (moodShift || maxedOutTime);
    }

    float calculateMinDuration(const MoodSnapshot& mood) const {
        float bpmFactor = constrain(mood.bpm / 130.0f, 0.6f, 1.4f);
        return 4000.0f * bpmFactor;
    }

    float calculateIdealDuration(const MoodSnapshot& mood) const {
        float energyFactor = constrain(mood.energy, 0.2f, 1.0f);
        float dynamicFactor = constrain(mood.dynamics, 0.1f, 1.0f);
        return 9000 + (5000.0f / (energyFactor + dynamicFactor));
    }

    unsigned long elapsed() const {
        return millis() - sceneStartMillis;
    }

    void debug() const {
        Serial.print(F("[SceneState] Scene: "));
        Serial.print(activeScene ? activeScene->name : "None");
        Serial.print(F(" | Time: "));
        Serial.print(elapsed());
        Serial.print(F("ms | BPM: "));
        Serial.print(lastMood.bpm);
        Serial.print(F(" | Energy: "));
        Serial.print(lastMood.energy, 2);
        Serial.print(F(" | Dynamics: "));
        Serial.print(lastMood.dynamics, 2);
        Serial.print(F(" | MinDur: "));
        Serial.print(sceneMinDurationMs);
        Serial.print(F(" | IdealDur: "));
        Serial.println(sceneIdealDurationMs);
    }
};
