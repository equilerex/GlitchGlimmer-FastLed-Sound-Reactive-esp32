// AudioHistoryTracker.h
#pragma once

#include <deque>
#include <Arduino.h>
#include "AudioFeatures.h"
#include "../audio/AudioSnapshot.h"

class AudioHistoryTracker {
private:
    std::deque<AudioSnapshot> history;
    const size_t maxHistorySize = 1500; // ~60s @ 25fps

public:
    void addSnapshot(const AudioFeatures& f) {
        AudioSnapshot s = {
            .volume = f.volume,
            .bass = f.bass,
            .mid = f.mid,
            .treble = f.treble,
            .centroid = f.spectrumCentroid,
            .bpm = f.bpm,
            .energy = f.energy,
            .dynamics = f.dynamics,
            .beat = f.beatDetected,
            .timestamp = millis()
        };
        history.push_back(s);
        if (history.size() > maxHistorySize) history.pop_front();
    }

    std::deque<AudioSnapshot> getRecent(unsigned long ms) const {
        std::deque<AudioSnapshot> recent;
        unsigned long now = millis();
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (now - it->timestamp > ms) break;
            recent.push_front(*it);
        }
        return recent;
    }
    
    // Add method to get the complete history
    const std::deque<AudioSnapshot>& getHistory() const {
        return history;
    }
};

// MoodClassifier.h
#pragma once
#include <String.h>
#include "AudioHistoryTracker.h"

class MoodClassifier {
public:
    String classify(const std::deque<AudioSnapshot>& recent) {
        if (recent.empty()) return "Unknown";

        float avgVol = 0, avgEnergy = 0, avgCentroid = 0, beatRate = 0;
        int beatCount = 0;

        for (const auto& snap : recent) {
            avgVol += snap.volume;
            avgEnergy += snap.energy;
            avgCentroid += snap.centroid;
            if (snap.beat) beatCount++;
        }

        int n = recent.size();
        avgVol /= n;
        avgEnergy /= n;
        avgCentroid /= n;
        beatRate = beatCount * 1000.0f / (recent.back().timestamp - recent.front().timestamp + 1);

        if (avgVol < 0.1 && avgEnergy < 50) return "Calm";
        if (avgCentroid > 150 && avgEnergy > 1000 && avgVol > 0.5) return "Drop";
        if (avgEnergy > 900 && beatRate > 1.5f) return "Groove";
        if (avgEnergy > 800 && avgCentroid > 120) return "Buildup";
        if (avgEnergy > 1100 && avgCentroid > 140) return "Chaos";

        return "Idle";
    }
};
