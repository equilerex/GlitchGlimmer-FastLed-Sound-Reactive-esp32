#pragma once

#include <vector>
#include <algorithm> 
#include "../scenes/SceneState.h"
#include "../scenes/LayerTypes.h"
#include "../scenes/MoodHistory.h"
#include "../animations/AnimationCatalog.h"

struct SceneDefinition {
    AnimationType baseAnimation;
    std::vector<LayerType> layerTypes;
    std::vector<MoodType> preferredMoods;
    String name;
    const SceneDefinition* activeScene = nullptr;

    bool supportsMood(MoodType mood) const {
        return std::find(preferredMoods.begin(), preferredMoods.end(), mood) != preferredMoods.end();
    }
};

class SceneRegistry {
private:
    std::vector<SceneDefinition> scenes;

public:
void registerDefaultScenes() {
    for (const auto& entry : animationCatalog) {
        scenes.push_back({
            entry.type,
            { LayerType::OVERLAY, LayerType::REACTIVE }, // placeholder
            { entry.mood },
            entry.name
        });
    }
}

    const SceneDefinition& pickSceneByMood(const SceneState& current, const MoodSnapshot& mood) const {
        std::vector<const SceneDefinition*> moodMatches;

        for (const auto& scene : scenes) {
            if (scene.supportsMood(mood.mood)) {
                moodMatches.push_back(&scene);
            }
        }

        if (!moodMatches.empty()) {
            return *moodMatches[random(moodMatches.size())];
        }

        // fallback
        return scenes[random(scenes.size())];
    }

    const SceneDefinition& get(size_t index) const {
        return scenes[index % scenes.size()];
    }

    size_t count() const {
        return scenes.size();
    }

    const std::vector<SceneDefinition>& getAll() const {
        return scenes;
    }
};
