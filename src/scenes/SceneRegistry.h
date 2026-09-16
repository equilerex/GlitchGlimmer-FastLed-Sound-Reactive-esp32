#pragma once

#include <vector>
#include <algorithm>
#include "../animations/AnimationCatalog.h"
#include "LayerTypes.h"

// Forward declarations to avoid circular includes
struct SceneState;
struct MoodSnapshot;

struct SceneDefinition {
    AnimationType baseAnimation;
    std::vector<LayerType> layerTypes;
    std::vector<MoodType> preferredMoods;
    String name;

    bool supportsMood(MoodType mood) const {
        return std::find(preferredMoods.begin(), preferredMoods.end(), mood) != preferredMoods.end();
    }
};

class SceneRegistry {
private:
    std::vector<SceneDefinition> scenes;

public:
    void registerDefaultScenes();
    const SceneDefinition& pickSceneByMood(const SceneState& current, const MoodSnapshot& mood) const;
    const SceneDefinition& get(size_t index) const;
    size_t count() const;
    const std::vector<SceneDefinition>& getAll() const;
};

// Note: method implementations moved to SceneRegistry.cpp
