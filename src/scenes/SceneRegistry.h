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
    MoodType mood = MOOD_COUNT;
    uint8_t role = 0;
    float intensity = 0.5f;

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

    // Ranks the catalog by distance to the music's own coordinates instead of by
    // one loudness axis. `structural` is the event the music is reporting, or
    // MOOD_COUNT for none: a build, a drop and the like narrow the field to scenes
    // written for them, as the mood picker does. Never returns the running scene
    // while another candidate exists. Deterministic: ties fall to catalog order.
    const SceneDefinition& pickSceneByMusic(const SceneState& current, const MusicState& music,
                                            MoodType structural) const;

    // Distance of one scene from the music, lower is nearer. Includes the recency
    // penalty, so it is the number the selection itself compares and the director
    // can judge a challenger against the running scene on the same scale.
    float sceneDistance(const SceneState& current, const SceneDefinition& scene,
                        const MusicState& music) const;
    const SceneDefinition& get(size_t index) const;
    int findIndex(const SceneDefinition* scene) const;
    size_t count() const;
    const std::vector<SceneDefinition>& getAll() const;
};

// Note: method implementations moved to SceneRegistry.cpp
