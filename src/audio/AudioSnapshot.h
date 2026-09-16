
#pragma once

#include "SnapshotRing.h"

struct AudioSnapshot {
    float volume;
    float bass, mid, treble;
    float centroid;
    float bpm;
    float energy;
    float dynamics;
    bool beat;
    unsigned long timestamp;
};

// The audio history's type, named once. It reaches the layers as a parameter
// through LEDStripController, LayerManager and every VisualLayer subclass, so
// keeping the name here is what stops the storage change from being a signature
// change at thirty call sites.
//
// The capacity is the cap: push_back evicts the oldest element once the ring is
// full, which is what the deque's separate maxHistorySize check used to do.
using AudioHistory = SnapshotRing<AudioSnapshot, 1500>;
