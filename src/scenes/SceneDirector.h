#pragma once

#include "../scenes/MoodHistory.h"
#include "../scenes/SceneRegistry.h"
#include "../scenes/SceneState.h"


struct SceneDefinition;
struct SceneState;
struct MoodHistory;

struct MoodInfo {
    float energy;
    float tempo;
    float dynamics;
    bool beatDetected;
};

class SceneDirector {
private:
    SceneState* state;
    MoodHistory& mood;
    SceneRegistry& registry;
    unsigned long lastScenePrint = 0;

public:
    SceneDirector(MoodHistory& moodRef, SceneRegistry& registryRef)
        : state(nullptr), mood(moodRef), registry(registryRef) {}

    void attachState(SceneState* s) { state = s; }

    void begin() {
        if (!state) return;
        const SceneDefinition& initial = registry.pickSceneByMood(*state, mood.getCurrentSnapshot());
        state->beginScene(&initial, mood.getCurrentSnapshot());
    }

    static inline MoodInfo convertToMoodInfo(const MoodSnapshot& m) {
        return MoodInfo{
            m.energy,
            m.bpm,
            m.dynamics,
            m.beatDetected
        };
    }

    void update(const AudioFeatures& features) {
        if (!state) return;
        mood.update(features);
        const MoodSnapshot& moodNow = mood.getCurrentSnapshot();
        const MoodSnapshot& predictedMood = mood.getPredictedNextMood();

        if (state->shouldTransition(moodNow)) {
            const SceneDefinition& nextScene = registry.pickSceneByMood(*state, predictedMood);
            state->beginScene(&nextScene, convertToMoodInfo(moodNow));
        }
    }

    void maybeInjectReactiveLayer(LayerManager& layerManager, const AudioFeatures& audio, unsigned long now) {
        static unsigned long lastBeatEffect = 0;
        static unsigned long lastEnergyEffect = 0;
        static const int maxActiveLayers = 4;

        if (layerManager.activeCount() >= maxActiveLayers) return;

        if (audio.beatDetected && now - lastBeatEffect > 800) {
            if (random(100) < 70) {
                layerManager.addLayerByType(LayerType::REACTIVE);
                lastBeatEffect = now;
            }
        }

        if (audio.energy > 0.6f && now - lastEnergyEffect > 1500) {
            if (random(100) < 40) {
                layerManager.addLayerByType(LayerType::OVERLAY);
                lastEnergyEffect = now;
            }
        }

        if (random(1000) < 3) {
            layerManager.addLayerByType(LayerType::MOOD_ARC);
        }
    }

    const SceneDefinition* getCurrentSceneForStrip(int index) const {
        return state.activeScene;
    }

    void forceNextScene() {
        if (!state) return;
        const MoodSnapshot& currentMood = mood.getCurrentSnapshot();
        const SceneDefinition& next = registry.pickSceneByMood(*state, currentMood);
        state->beginScene(&next, currentMood);
    }

    const SceneDefinition* getActiveScene() const {
        return state ? state->activeScene : nullptr;
    }

    String getCurrentSceneName() const {
        return state && state->activeScene ? String(state->activeScene->name) : "None";
    }

    void log() {
        if (millis() - lastScenePrint > 2000) {
            lastScenePrint = millis();
            Serial.print(F("[Scene] "));
            Serial.println(getCurrentSceneName());
        }
    }
};
