#include "SceneRegistry.h"
#include "SceneState.h"
#include <Arduino.h>

void SceneRegistry::registerDefaultScenes() {
    scenes.clear();
    for (const auto& entry : animationCatalog) {
        // NONE exists only so the catalog indices match the enum. It has no
        // animation, so it has nothing to build a scene around.
        if (entry.type == AnimationType::NONE) continue;

        SceneDefinition scene;
        scene.baseAnimation = entry.type;
        // Three roles, one layer each. The base animation is the motion, so the
        // layers supply the field underneath it and the accent on top. Stacking a
        // second full-strip motion layer (OVERLAY) only fought the animation.
        scene.layerTypes = { LayerType::BACKGROUND, LayerType::REACTIVE };
        scene.preferredMoods = { entry.mood };
        scene.name = String(entry.name);
        scenes.push_back(scene);
    }
}

const SceneDefinition& SceneRegistry::pickSceneByMood(const SceneState& current, MoodType mood) const {
    std::vector<const SceneDefinition*> matches;

    // The mood arrives classified rather than being derived here. This used to
    // repeat MoodHistory::classifyMood's thresholds, which is two places for one
    // answer to drift apart, and they had drifted: the classifier's dynamics cut
    // points move with the observed range while the copy's sat at 0.5 and 0.2, and
    // the copy read the raw per-frame snapshot while the classifier reads smoothed
    // values. The picker could therefore choose a scene for a mood the display never
    // named, and it did so exactly at the thresholds the adaptive cuts were
    // introduced to fix.
    for (const auto& s : scenes) {
        if (s.supportsMood(mood)) {
            matches.push_back(&s);
        }
    }

    // The scene already running is not a transition. SceneState::beginScene would
    // reset the clock and nothing on screen would change, while the next real
    // transition is pushed out by a full scene duration. Only drop it while
    // another candidate remains, so a single match still returns and does not
    // fall through to the mood-blind branch below.
    if (matches.size() > 1 && current.activeScene != nullptr) {
        matches.erase(std::remove(matches.begin(), matches.end(), current.activeScene),
                      matches.end());
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
