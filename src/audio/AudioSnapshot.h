
#pragma once

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
