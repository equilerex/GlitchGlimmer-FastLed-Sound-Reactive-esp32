#pragma once

#include "../scenes/MoodHistory.h"
#include "../scenes/SceneState.h"
#include "../scenes/SceneRegistry.h"
#include "../scenes/LayerManager.h"
#include "../animations/AnimationCatalog.h"

class SceneDirector {
private:
    SceneState state;
    MoodHistory& mood;
    SceneRegistry& registry;
    unsigned long lastScenePrint = 0;

public:
    SceneDirector(MoodHistory& moodRef, SceneRegistry& registryRef)
        : mood(moodRef), registry(registryRef) {}

    void begin() {
        const SceneDefinition& initial = registry.pickSceneByMood(state, mood.getCurrentSnapshot());
        state.beginScene(&initial, mood.getCurrentSnapshot());
    }

    void update(const AudioFeatures& features) {
        mood.update(features);
        const MoodSnapshot& moodNow = mood.getCurrentSnapshot();

        if (state.shouldTransition(moodNow)) {
            const SceneDefinition& next = registry.pickSceneByMood(state, mood.getPredictedSnapshot());
            state.beginScene(&next, moodNow);
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
        const MoodSnapshot& currentMood = mood.getCurrentSnapshot();
        const SceneDefinition& next = registry.pickSceneByMood(state, currentMood);
        state.beginScene(&next, currentMood);
    }

    const SceneDefinition* getActiveScene() const {
        return state.activeScene;
    }

    String getCurrentSceneName() const {
        return state.activeScene ? String(state.activeScene->name) : "None";
    }

    void log() {
        if (millis() - lastScenePrint > 2000) {
            lastScenePrint = millis();
            Serial.print(F("[Scene] "));
            Serial.println(getCurrentSceneName());
        }
    }
};
