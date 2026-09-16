#include "SceneState.h"
#include "MoodHistory.h"       // full definition for MoodSnapshot
#include "SceneRegistry.h"     // full definition for SceneDefinition
#include <Arduino.h>
#include <cmath>

// Initialize default values
SceneState::SceneState()
 : activeScene(nullptr), sceneStartMillis(0), sceneMinDurationMs(5000), sceneIdealDurationMs(12000), lastMood(), sceneChangeCount(0), totalUptimeMs(0)
{}

// Begin a new scene with baseline mood snapshot
void SceneState::beginScene(const SceneDefinition* def, const MoodSnapshot& moodNow) {
    activeScene = def;
    sceneStartMillis = millis();
    lastMood = moodNow;
    sceneMinDurationMs = calculateMinDuration(moodNow);
    sceneIdealDurationMs = calculateIdealDuration(moodNow);
    sceneChangeCount++;
}

// Determine if it's time to transition based on mood shifts or max duration
bool SceneState::shouldTransition(const MoodSnapshot& moodNow) const {
    unsigned long now = millis();
    bool pastMin = (now - sceneStartMillis) > sceneMinDurationMs;
    bool moodShift = fabs(moodNow.energy - lastMood.energy) > 0.25f
                  || fabs(moodNow.bpm - lastMood.bpm) > 15.0f
                  || fabs(moodNow.dynamics - lastMood.dynamics) > 0.2f;
    bool maxedOut = (now - sceneStartMillis) > sceneIdealDurationMs;
    return pastMin && (moodShift || maxedOut);
}

// Compute minimum duration scaled by tempo
float SceneState::calculateMinDuration(const MoodSnapshot& mood) const {
    float bpmFactor = constrain(mood.bpm / 130.0f, 0.6f, 1.4f);
    return 4000.0f * bpmFactor;
}

// Compute ideal duration scaled by energy & dynamics
float SceneState::calculateIdealDuration(const MoodSnapshot& mood) const {
    float energyFactor = constrain(mood.energy, 0.2f, 1.0f);
    float dynFactor = constrain(mood.dynamics, 0.1f, 1.0f);
    return 9000.0f + (5000.0f / (energyFactor + dynFactor));
}

// Return elapsed time since scene start
unsigned long SceneState::elapsed() const {
    return millis() - sceneStartMillis;
}

// Debug print current state over serial
void SceneState::debug() const {
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