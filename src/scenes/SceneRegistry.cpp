#include "SceneRegistry.h"
#include <Arduino.h>

void SceneRegistry::registerDefaultScenes() {
    scenes.clear();
    for (const auto& entry : animationCatalog) {
        SceneDefinition scene;
        scene.baseAnimation = entry.type;
        scene.layerTypes = { LayerType::OVERLAY, LayerType::REACTIVE }; // TODO: refine layer presets
        scene.preferredMoods = { entry.mood };
        scene.name = String(entry.name);
        scenes.push_back(scene);
    }
}

const SceneDefinition& SceneRegistry::pickSceneByMood(const SceneState& /*current*/, const MoodSnapshot& mood) const {
    std::vector<const SceneDefinition*> matches;
    
    // Determine the current mood type from the snapshot
    MoodType currentMood = MoodType::UNKNOWN;
    if (mood.energy > 0.8f && mood.dynamics > 0.5f) currentMood = MoodType::INTENSE;
    else if (mood.energy > 0.6f && mood.bpm > 100) currentMood = MoodType::ENERGETIC;
    else if (mood.energy < 0.3f && mood.dynamics < 0.2f) currentMood = MoodType::CALM;
    else if (mood.bpm < 80 && mood.energy > 0.4f) currentMood = MoodType::FLOATY;
    
    for (const auto& s : scenes) {
        if (s.supportsMood(currentMood)) {
            matches.push_back(&s);
        }
    }
    if (!matches.empty()) {
        return *matches[random(matches.size())];
    }
    return scenes[random(scenes.size())];
}

const SceneDefinition& SceneRegistry::get(size_t index) const {
    return scenes[index % scenes.size()];
}

size_t SceneRegistry::count() const {
    return scenes.size();
}

const std::vector<SceneDefinition>& SceneRegistry::getAll() const {
    return scenes;
}
