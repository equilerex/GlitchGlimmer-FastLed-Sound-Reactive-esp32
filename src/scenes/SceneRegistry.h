#pragma once

#include <vector>
#include <algorithm>
#include "../animations/AnimationCatalog.h"
#include "LayerTypes.h"

// Forward declarations to avoid circular includes
struct SceneState;

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
    // Takes the classified mood rather than a snapshot to classify. See the
    // definition for why the second classifier was removed.
    const SceneDefinition& pickSceneByMood(const SceneState& current, MoodType mood) const;
    const SceneDefinition& get(size_t index) const;
    size_t count() const;
    const std::vector<SceneDefinition>& getAll() const;
};

// Note: method implementations moved to SceneRegistry.cpp
