#pragma once

#include <stdint.h>
#include "../audio/Moods.h"

// One shape serves animations, episodes and layers: a sparse list of (key, fit)
// pairs, the key a mood or an episode and the fit between 0 and 1. See
// _architecture/plans/2026-09-28-base-layer-mood.md, "Fit lists".
//
// A fit of 1.0 means the thing was written for that key. An animation does not
// get 1.0 on several moods, so a generalist cannot win every mixed passage.
//
// Lists are padded with {0, 0}, which is Floaty at fit 0. A zero fit adds nothing
// to a score, so the padding needs no terminator.
enum FitKey : uint8_t {
    // 0 to MN_COUNT - 1 are the moods, by their MoodNature value.
    FK_BUILDUP = MN_COUNT,   // weight while a buildup is open
    FK_DESCENT,              // weight while a descent is open
    FK_DROP,                 // marks a drop animation: only these can be the drop base
    FK_COUNT
};

struct Fit {
    uint8_t key;
    float   fit;
};

static const int kMaxFits = 6;

struct FitList {
    Fit e[kMaxFits];

    // Fit for one key, 0 when the list has no entry for it.
    float of(uint8_t key) const {
        float best = 0.0f;
        for (int i = 0; i < kMaxFits; ++i) {
            if (e[i].fit > 0.0f && e[i].key == key && e[i].fit > best) best = e[i].fit;
        }
        return best;
    }
};
