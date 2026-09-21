#include "SceneState.h"
#include "MoodHistory.h"       // full definition for MoodSnapshot
#include "SceneRegistry.h"     // full definition for SceneDefinition
#include <Arduino.h>
#include <cmath>

// Initialize default values
SceneState::SceneState()
 : activeScene(nullptr), sceneStartMillis(0), sceneMinDurationMs(5000), sceneIdealDurationMs(12000), lastMood(), startMood(SILENT), sceneChangeCount(0), totalUptimeMs(0)
{
    for (int i = 0; i < kRecent; ++i) recent[i] = -1;
}

// Begin a new scene with baseline mood snapshot
void SceneState::beginScene(const SceneDefinition* def, const MoodSnapshot& moodNow, MoodType moodNowType) {
    if (activeScene && activeScene != def) {
        for (int i = kRecent - 1; i > 0; --i) recent[i] = recent[i - 1];
        recent[0] = static_cast<int>(activeScene->baseAnimation);
    }
    activeScene = def;
    sceneStartMillis = millis();
    lastMood = moodNow;
    startMood = moodNowType;
    sceneMinDurationMs = calculateMinDuration(moodNow);
    sceneIdealDurationMs = calculateIdealDuration(moodNow);
    sceneChangeCount++;
}

// Determine if it's time to transition based on mood shifts or max duration
bool SceneState::shouldTransition(const MoodSnapshot& moodNow, MoodType moodNowType) const {
    unsigned long now = millis();
    bool pastMin = (now - sceneStartMillis) > sceneMinDurationMs;
    // The shift is the classifier's mood, not a single field crossing a constant.
    // The old test compared the instantaneous level against the snapshot taken
    // when the scene began, and level is renormalised to the loudest recent block,
    // so a quarter of its range was crossed within a second or two of any real
    // audio: moodShift was true by the time the minimum elapsed and the scene
    // changed at its minimum duration on every single scene. The classified mood
    // is already smoothed, confirmed and held, so it is the stable signal this
    // test was always meant to read.
    bool moodShift = moodNowType != startMood
                  || fabs(moodNow.bpm - lastMood.bpm) > 15.0f;
    bool maxedOut = (now - sceneStartMillis) > sceneIdealDurationMs;
    return pastMin && (moodShift || maxedOut);
}

// Compute minimum duration scaled by tempo
float SceneState::calculateMinDuration(const MoodSnapshot& mood) const {
    float bpmFactor = constrain(mood.bpm / 130.0f, 0.6f, 1.4f);
    return minBaseMs * bpmFactor;
}

// Compute ideal duration scaled by energy & dynamics
float SceneState::calculateIdealDuration(const MoodSnapshot& mood) const {
    float energyFactor = constrain(mood.level, 0.2f, 1.0f);
    float dynFactor = constrain(mood.dynamics, 0.1f, 1.0f);
    return idealBaseMs + (idealSpanMs / (energyFactor + dynFactor));
}

void SceneState::refreshDurations() {
    sceneMinDurationMs = calculateMinDuration(lastMood);
    sceneIdealDurationMs = calculateIdealDuration(lastMood);
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
    Serial.print(lastMood.bpm);
    Serial.print(F(" | Level: "));
    Serial.print(lastMood.level, 2);
    Serial.print(F(" | Dynamics: "));
    Serial.print(lastMood.dynamics, 2);
    Serial.print(F(" | MinDur: "));
    Serial.print(sceneMinDurationMs);
    Serial.print(F(" | IdealDur: "));
    Serial.println(sceneIdealDurationMs);
}