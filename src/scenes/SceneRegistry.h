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
    // Structural moods only: SILENT, TEASE, BUILDUP, DESCENT, DROP, WEIRD. A ladder mood
    // here would be inert, and worse than inert. Eight catalog scenes each
    // carrying their own animation's mood would each win their own mood outright
    // and the intensity axis would never be consulted, so CALM would always be
    // Alien Breath whatever the rest of the catalog grew into.
    std::vector<MoodType> structuralMoods;
    String name;

    bool isTaggedFor(MoodType mood) const {
        return std::find(structuralMoods.begin(), structuralMoods.end(), mood) != structuralMoods.end();
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
