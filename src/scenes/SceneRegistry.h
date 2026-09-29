#pragma once

#include <vector>
#include <algorithm>
#include "../animations/AnimationCatalog.h"
#include "../audio/MusicState.h"
#include "Fit.h"
#include "LayerTypes.h"

// Forward declarations to avoid circular includes
struct SceneState;

struct SceneDefinition {
    AnimationType baseAnimation;
    // The fixed scene layers, from the animation's row in AnimationFit.h. Live
    // layers fill the remaining slots.
    std::vector<LayerType> layerTypes;
    String name;
    uint8_t role = 0;
    float intensity = 0.5f;
    // The moods and episodes this scene suits. Points into a static table.
    const FitList* fit = nullptr;

    float fitOf(uint8_t key) const { return fit ? fit->of(key) : 0.0f; }
    // A drop animation is marked by an FK_DROP entry. Only these can be the drop
    // base.
    bool isDropAnimation() const { return fitOf(FK_DROP) > 0.0f; }
};

// The numbers the selector is tuned with. Live dials in the emulator.
struct SelectionParams {
    float bucketMargin = 0.10f;     // score distance from the best that stays in the bucket
    int   bucketMin = 2;
    int   bucketMax = 5;
    float episodeWeight = 0.15f;    // weight of an open buildup or descent
    float toneWeight = 0.30f;       // share of the mood term that comes from tone moods
};

// Why a scene ranks where it does. One per registered scene, same order.
struct SceneScore {
    float mood = 0.0f;              // fit averaged over the moods the music has, 0 to 1
    float episode = 0.0f;           // bonus from an open buildup or descent
    float recency = 0.0f;           // penalty for a scene just left
    float score = 0.0f;             // mood + episode - recency
    bool  inBucket = false;
};

class SceneRegistry {
private:
    std::vector<SceneDefinition> scenes;
    // A mood some scene has a fit for. One no scene answers, such as a row just
    // added to the mood table, is left out of the average so it cannot dilute the
    // scores and move a pick.
    bool moodServed[MN_COUNT] = {};

public:
    void registerDefaultScenes();

    // The first term of the score: a fit list averaged over the moods the music
    // has, the strengths as weights, character and tone averaged apart and joined
    // as (1 - toneWeight) * character + toneWeight * tone. A tone is always partly
    // true of any music, so it shades the score and cannot drown the character.
    // Between 0 and 1 whatever the music.
    float moodTerm(const FitList& fit, const MusicState& music, float toneWeight = 0.30f) const;

    // Score of every scene for the music, into `out` (resized to the catalog).
    //
    //   score = sum over moods (strength * fit) / sum of strengths
    //         + episodeWeight * sum over open episodes (fit)
    //         - the recency penalty
    //
    // Loudness is not an input. The first term stays between 0 and 1 whatever the
    // music, because it is a fit averaged with the strengths as weights.
    void score(const SceneState& current, const MusicState& music,
               const SelectionParams& params, std::vector<SceneScore>& out) const;

    // The scenes within `bucketMargin` of the best score, at least bucketMin and at
    // most bucketMax, best first. Marks `inBucket` in `scores`.
    void buildBucket(std::vector<SceneScore>& scores, const SelectionParams& params,
                     std::vector<int>& bucket) const;

    // A draw from the bucket weighted by score, never the running scene while
    // another candidate exists. `rng` is the caller's xorshift state, so a harness
    // that fixes it gets the same scene every time.
    const SceneDefinition& drawFromBucket(const SceneState& current,
                                          const std::vector<SceneScore>& scores,
                                          const std::vector<int>& bucket,
                                          uint32_t& rng) const;

    // The drop animation with the highest mood score, not the running scene while
    // another drop animation exists. The catalog's first scene when none is marked.
    const SceneDefinition& pickDropBase(const SceneState& current,
                                        const std::vector<SceneScore>& scores) const;

    // Score, bucket and draw in one call, for callers that do not keep the
    // intermediate lists.
    const SceneDefinition& pickBase(const SceneState& current, const MusicState& music,
                                    const SelectionParams& params, uint32_t& rng) const;

    const SceneDefinition& get(size_t index) const;
    int findIndex(const SceneDefinition* scene) const;
    size_t count() const;
    const std::vector<SceneDefinition>& getAll() const;
};

// One step of the selector's generator. Same xorshift32 everywhere, so a fixed
// seed gives the same sequence on the device, the harness and the browser.
inline uint32_t nextRandom(uint32_t& state) {
    if (state == 0) state = 0x9E3779B9u;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

// Note: method implementations moved to SceneRegistry.cpp
