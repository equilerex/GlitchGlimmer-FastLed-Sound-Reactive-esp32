// AudioHistoryTracker.h
#pragma once

#include <Arduino.h>
#include "AudioFeatures.h"
#include "../audio/AudioSnapshot.h"

class AudioHistoryTracker {
private:
    AudioHistory history;

public:
    void addSnapshot(const AudioFeatures& f) {
        // Ordered, not designated. The device env is -std=gnu++11, where
        // `.volume = ...` compiles only as a GNU extension, so naming the
        // members built by accident of compiler version rather than by the
        // standard. The order below is the declaration order in AudioSnapshot.
        const AudioSnapshot s = {
            f.volume,
            f.level,
            f.bass, f.mid, f.treble,
            f.spectrumCentroid,
            f.bpm,
            f.energy,
            f.dynamics,
            f.beatDetected,
            millis()
        };
        history.push_back(s);
    }

    // Add method to get the complete history
    const AudioHistory& getHistory() const {
        return history;
    }
};
