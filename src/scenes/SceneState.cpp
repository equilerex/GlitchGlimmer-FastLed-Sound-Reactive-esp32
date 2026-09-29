#include "SceneState.h"
#include "SceneRegistry.h"     // full definition for SceneDefinition
#include <Arduino.h>
#include <cmath>

// Initialize default values
SceneState::SceneState()
 : activeScene(nullptr), sceneStartMillis(0), sceneMinDurationMs(5000), sceneIdealDurationMs(12000), sceneChangeCount(0), totalUptimeMs(0)
{
    for (int i = 0; i < kRecent; ++i) recent[i] = -1;
}

// Begin a new scene, stamping what its clock is scaled by
void SceneState::beginScene(const SceneDefinition* def, float bpm, float level, float dynamics) {
    if (activeScene && activeScene != def) {
        for (int i = kRecent - 1; i > 0; --i) recent[i] = recent[i - 1];
        recent[0] = static_cast<int>(activeScene->baseAnimation);
    }
    activeScene = def;
    sceneStartMillis = millis();
    startBpm = bpm;
    startLevel = level;
    startDynamics = dynamics;
    sceneMinDurationMs = calculateMinDuration(startBpm);
    sceneIdealDurationMs = calculateIdealDuration(startLevel, startDynamics);
    sceneChangeCount++;
}

// Compute minimum duration scaled by tempo
float SceneState::calculateMinDuration(float bpm) const {
    float bpmFactor = constrain(bpm / 130.0f, 0.6f, 1.4f);
    return minBaseMs * bpmFactor;
}

// Compute ideal duration scaled by energy & dynamics
float SceneState::calculateIdealDuration(float level, float dynamics) const {
    float energyFactor = constrain(level, 0.2f, 1.0f);
    float dynFactor = constrain(dynamics, 0.1f, 1.0f);
    return idealBaseMs + (idealSpanMs / (energyFactor + dynFactor));
}

void SceneState::refreshDurations() {
    sceneMinDurationMs = calculateMinDuration(startBpm);
    sceneIdealDurationMs = calculateIdealDuration(startLevel, startDynamics);
}

void SceneState::setMinBaseMs(float ms) {
    minBaseMs = constrain(ms, 500.0f, 30000.0f);
    refreshDurations();
}

void SceneState::setIdealBaseMs(float ms) {
    idealBaseMs = constrain(ms, 1000.0f, 60000.0f);
    refreshDurations();
}

void SceneState::setIdealSpanMs(float ms) {
    idealSpanMs = constrain(ms, 0.0f, 60000.0f);
    refreshDurations();
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
    Serial.print(startBpm);
    Serial.print(F(" | MinDur: "));
    Serial.print(sceneMinDurationMs);
    Serial.print(F(" | IdealDur: "));
    Serial.println(sceneIdealDurationMs);
}
