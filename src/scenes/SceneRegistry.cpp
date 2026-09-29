#include "SceneRegistry.h"
#include "SceneState.h"
#include "../animations/AnimationFit.h"
#include <Arduino.h>
#include <cmath>

void SceneRegistry::registerDefaultScenes() {
    scenes.clear();
    for (const auto& entry : animationCatalog) {
        // NONE exists only so the catalog indices match the enum. It has no
        // animation, so it has nothing to build a scene around.
        if (entry.type == AnimationType::NONE) continue;

        const AnimationFitRow& row = animationFit(entry.type);
        SceneDefinition scene;
        scene.baseAnimation = entry.type;
        for (LayerType t : row.layers) {
            if (t != kNoLayer) scene.layerTypes.push_back(t);
        }
        scene.name = String(entry.name);
        scene.role = row.role;
        scene.intensity = entry.intensity;
        scene.fit = &row.fit;
        scenes.push_back(scene);
    }

    for (int m = 0; m < MN_COUNT; ++m) {
        moodServed[m] = false;
        for (const SceneDefinition& s : scenes) {
            if (s.fitOf(uint8_t(m)) > 0.0f) { moodServed[m] = true; break; }
        }
    }
}

float SceneRegistry::moodTerm(const FitList& fit, const MusicState& music, float toneWeight) const {
    // The ballast keeps an average defined when a family has no mood at all, and
    // collapses it to 0.5 for every scene then, so the other family, the episode
    // and the recency terms decide instead of a division by nothing.
    const float ballast = 0.05f;
    float sum[2] = {0.0f, 0.0f};
    float weighted[2] = {0.0f, 0.0f};
    for (int m = 0; m < MN_COUNT; ++m) {
        if (!moodServed[m] || music.mood[m] <= 0.0f) continue;
        const int f = kMoodTable[m].tone ? 1 : 0;
        sum[f] += music.mood[m];
        weighted[f] += music.mood[m] * fit.of(uint8_t(m));
    }
    const float character = (weighted[0] + 0.5f * ballast) / (sum[0] + ballast);
    const float tone = (weighted[1] + 0.5f * ballast) / (sum[1] + ballast);
    const float w = toneWeight < 0.0f ? 0.0f : (toneWeight > 1.0f ? 1.0f : toneWeight);
    return (1.0f - w) * character + w * tone;
}

void SceneRegistry::score(const SceneState& current, const MusicState& music,
                          const SelectionParams& params, std::vector<SceneScore>& out) const {
    out.assign(scenes.size(), SceneScore());

    for (size_t i = 0; i < scenes.size(); ++i) {
        const SceneDefinition& s = scenes[i];
        SceneScore& o = out[i];

        o.mood = scenes[i].fit ? moodTerm(*scenes[i].fit, music, params.toneWeight) : 0.5f;

        if (music.buildup) o.episode += params.episodeWeight * s.fitOf(FK_BUILDUP);
        if (music.descent) o.episode += params.episodeWeight * s.fitOf(FK_DESCENT);

        // A scene left within the last few picks is nudged away, newest strongest.
        // The running scene is exempt: it is the incumbent.
        if (current.activeScene != &s) {
            for (int r = 0; r < SceneState::kRecent; ++r) {
                if (current.recent[r] == static_cast<int>(s.baseAnimation)) {
                    o.recency = 0.10f - 0.03f * r;
                    break;
                }
            }
        }
        o.score = o.mood + o.episode - o.recency;
    }
}

void SceneRegistry::buildBucket(std::vector<SceneScore>& scores, const SelectionParams& params,
                                std::vector<int>& bucket) const {
    bucket.clear();
    for (auto& s : scores) s.inBucket = false;
    if (scores.empty()) return;

    std::vector<int> ranked(scores.size());
    for (size_t i = 0; i < ranked.size(); ++i) ranked[i] = int(i);
    // Best first, ties to catalog order, so the same scores give the same bucket.
    std::stable_sort(ranked.begin(), ranked.end(), [&](int a, int b) {
        return scores[a].score > scores[b].score;
    });

    const int lo = std::max(1, std::min(params.bucketMin, params.bucketMax));
    const int hi = std::max(lo, params.bucketMax);
    const float floorScore = scores[ranked.front()].score - params.bucketMargin;
    int n = 0;
    while (n < int(ranked.size()) && n < hi &&
           (n < lo || scores[ranked[n]].score >= floorScore)) {
        ++n;
    }
    for (int i = 0; i < n; ++i) {
        bucket.push_back(ranked[i]);
        scores[ranked[i]].inBucket = true;
    }
}

const SceneDefinition& SceneRegistry::drawFromBucket(const SceneState& current,
                                                     const std::vector<SceneScore>& scores,
                                                     const std::vector<int>& bucket,
                                                     uint32_t& rng) const {
    // Only reachable from a registry that was never registered, or a bucket built
    // from nothing. Every caller needs a reference to bind.
    if (bucket.empty()) return scenes.front();

    // The scene already running is not a transition. Dropped only while another
    // candidate remains, so a bucket of one still returns.
    std::vector<int> pool;
    for (int idx : bucket) {
        if (&scenes[idx] != current.activeScene) pool.push_back(idx);
    }
    if (pool.empty()) pool = bucket;

    float total = 0.0f;
    for (int idx : pool) total += std::max(scores[idx].score, 0.01f);
    float pick = (float(nextRandom(rng) & 0xFFFFFF) / float(0x1000000)) * total;
    for (int idx : pool) {
        pick -= std::max(scores[idx].score, 0.01f);
        if (pick < 0.0f) return scenes[idx];
    }
    return scenes[pool.back()];
}

const SceneDefinition& SceneRegistry::pickDropBase(const SceneState& current,
                                                   const std::vector<SceneScore>& scores) const {
    const SceneDefinition* best = nullptr;
    float bestScore = 0.0f;
    for (size_t i = 0; i < scenes.size() && i < scores.size(); ++i) {
        if (!scenes[i].isDropAnimation()) continue;
        if (&scenes[i] == current.activeScene) continue;
        if (best == nullptr || scores[i].mood > bestScore) {
            best = &scenes[i];
            bestScore = scores[i].mood;
        }
    }
    if (best != nullptr) return *best;
    // Only the running scene is a drop animation, or none is. Keep the running one.
    if (current.activeScene != nullptr) return *current.activeScene;
    return scenes.front();
}

const SceneDefinition& SceneRegistry::pickBase(const SceneState& current, const MusicState& music,
                                               const SelectionParams& params, uint32_t& rng) const {
    std::vector<SceneScore> scores;
    std::vector<int> bucket;
    score(current, music, params, scores);
    buildBucket(scores, params, bucket);
    return drawFromBucket(current, scores, bucket, rng);
}

const SceneDefinition& SceneRegistry::get(size_t index) const {
    return scenes[index % scenes.size()];
}

int SceneRegistry::findIndex(const SceneDefinition* scene) const {
    if (!scene) return -1;
    for (size_t i = 0; i < scenes.size(); ++i) {
        if (&scenes[i] == scene) return static_cast<int>(i);
    }
    return -1;
}

size_t SceneRegistry::count() const {
    return scenes.size();
}

const std::vector<SceneDefinition>& SceneRegistry::getAll() const {
    return scenes;
}
