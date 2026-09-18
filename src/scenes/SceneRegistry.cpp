#include "SceneRegistry.h"
#include "SceneState.h"
#include <Arduino.h>
#include <cmath>

namespace {

// Where each mood sits on the catalog's 0..1 intensity axis.
//
// A ladder mood's target is the middle of its rung, so the four edges and this
// table are two views of one scale: the edges partition level, and the midpoints
// are what a scene is ranked against. That the table is the midpoints is
// load-bearing and not a preference. The values here were once hand-picked against
// an eight-entry catalog to make the five rungs land on five scenes, and the picks
// they produced were FLOATY over CALM, so crossing level 0.20 dropped the scene's
// intensity while the level rose, and CALM to DANCY was a step of 0.44 where the
// others were under 0.18. The rung targets are derived from the edges now, so the
// spacing follows the ladder by construction and a catalog that grows cannot
// reorder them.
//
// A structural mood is not on the ladder, so it takes a target of its own rather
// than a midpoint. It still needs one, so a structural mood with no scene tagged
// for it picks something instead of being skipped. Skipping is for an empty
// catalog, not for an unserved mood.
float targetIntensity(MoodType mood) {
    switch (mood) {
        case SILENT:    return 0.05f;
        case FLOATY:    return 0.10f;
        case CALM:      return 0.30f;
        case DANCY:     return 0.50f;
        case ENERGETIC: return 0.70f;
        case INTENSE:   return 0.90f;
        // Structural moods are not on the ladder, so these are positions rather than
        // midpoints, and unlike the rungs they may share a scene. Six moods against an
        // eight-entry catalog have no choice about that, and a structural mood is
        // meant to be answered by a scene tagged for it, so its target is the
        // fallback for when none is.
        case TEASE:     return 0.45f;
        case BUILDUP:   return 0.60f;
        // Between the two rungs a descent travels rather than beside its mirror.
        // BUILDUP's 0.60 would be the symmetric choice, and it would also make the
        // two movements select the same scene at every level, which would leave the
        // pair indistinguishable in the one place they differ: their direction.
        case DESCENT:   return 0.66f;
        case WEIRD:     return 0.78f;
        case DROP:      return 0.95f;
        case MOOD_COUNT: break;
    }
    return 0.5f;
}

}  // namespace

// The layer set a scene composites, by the band its animation's intensity sits in.
//
// Every scene used to carry the same two layers, so two scenes at different
// intensities composited the same way and the only thing between them was the base
// animation underneath. The band decides which accent suits it: a slow drone under
// the quiet end, a steady pulse and a streak through the groove, a full base with
// impact layers at the peak.
//
// Cost is bounded by kMaxLayers in addLayer, which refuses past the fourth. The
// loudest band lists exactly four.
std::vector<LayerType> layersForIntensity(float intensity) {
    if (intensity < 0.30f) return { LayerType::BACKGROUND, LayerType::MOOD_ARC };
    if (intensity < 0.50f) return { LayerType::BACKGROUND, LayerType::TRIWAVE_BEAT };
    if (intensity < 0.75f) return { LayerType::BACKGROUND, LayerType::OVERLAY, LayerType::REACTIVE };
    if (intensity < 0.90f) return { LayerType::BASE, LayerType::REACTIVE, LayerType::HIGHLIGHT };
    return { LayerType::BASE, LayerType::REACTIVE, LayerType::HIGHLIGHT, LayerType::ENERGY };
}

void SceneRegistry::registerDefaultScenes() {
    scenes.clear();
    for (const auto& entry : animationCatalog) {
        // NONE exists only so the catalog indices match the enum. It has no
        // animation, so it has nothing to build a scene around.
        if (entry.type == AnimationType::NONE) continue;

        SceneDefinition scene;
        scene.baseAnimation = entry.type;
        // The base animation is the motion, so the layers supply the field underneath
        // it and the accent on top. The set is the band's rather than the same two for
        // every scene, which is what stops two scenes at different intensities from
        // compositing identically.
        scene.layerTypes = layersForIntensity(entry.intensity);
        // A structural tag is copied, a ladder tag is not. See
        // SceneDefinition::structuralMoods for why a ladder tag here would be
        // worse than useless.
        if (entry.mood != MOOD_COUNT && ladderRank(entry.mood) < 0) {
            scene.structuralMoods.push_back(entry.mood);
        }
        scene.name = String(entry.name);
        scenes.push_back(scene);
    }
}

const SceneDefinition& SceneRegistry::pickSceneByMood(const SceneState& current, MoodType mood) const {
    // A structural mood names the shape of a passage rather than its loudness, so
    // intensity cannot rank its candidates and a scene written for it wins.
    std::vector<const SceneDefinition*> candidates;
    if (ladderRank(mood) < 0) {
        for (const auto& s : scenes) {
            if (s.isTaggedFor(mood)) candidates.push_back(&s);
        }
        if (candidates.size() == 1 && candidates.front() == current.activeScene) {
            candidates.clear();
        }
    }

    // Otherwise intensity is the axis, and the whole catalog competes. This used
    // to be a mood match with a uniform draw over the whole catalog as its
    // fallback, which is where the "Calm" label met an intense scene: the
    // classifier's dead bands returned UNKNOWN, UNKNOWN matched nothing, and the
    // fallback chose at random.
    if (candidates.empty()) {
        for (const auto& s : scenes) candidates.push_back(&s);
    }

    // Only reachable from a registry that was never registered, since
    // registerDefaultScenes() is the only thing that fills the vector. Every
    // caller needs a reference to bind, so there is nothing better to return.
    if (candidates.empty()) return scenes.front();

    // The scene already running is not a transition. SceneState::beginScene would
    // reset the clock and nothing on screen would change, while the next real
    // transition is pushed out by a full scene duration. Only dropped while
    // another candidate remains, so a single match still returns.
    if (candidates.size() > 1 && current.activeScene != nullptr) {
        candidates.erase(std::remove(candidates.begin(), candidates.end(), current.activeScene),
                         candidates.end());
    }

    // Nearest intensity, then tempo, then catalog order. Every tie-break is a
    // number rather than a draw, so the same state always returns the same scene
    // and both this and the no-self-pick rule can be asserted on.
    const float target = targetIntensity(mood);
    // preferredTempo is 0..1 and bpm is 60..180, so this is the same /130 the
    // scene clock uses. Comparing them raw would make the tie-break a constant,
    // which is a silent way of making it do nothing.
    const float tempo = current.lastMood.bpm / 130.0f;
    const SceneDefinition* best       = nullptr;
    float                  bestGap     = 0.0f;
    float                  bestTempoGap = 0.0f;

    for (const SceneDefinition* s : candidates) {
        const AnimationMeta& meta = animationCatalog[static_cast<size_t>(s->baseAnimation)];
        const float gap      = std::fabs(meta.intensity - target);
        const float tempoGap = std::fabs(meta.preferredTempo - tempo);
        const bool  better   = best == nullptr || gap < bestGap ||
                               (gap == bestGap && tempoGap < bestTempoGap);
        if (better) {
            best         = s;
            bestGap      = gap;
            bestTempoGap = tempoGap;
        }
    }

    return *best;
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
