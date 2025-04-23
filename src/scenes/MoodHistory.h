#pragma once

#include <deque>
#include <Arduino.h>
#include "../audio/AudioFeatures.h"

enum class MoodType {
    CALM,
    ENERGETIC,
    INTENSE,
    FLOATY,
    UNKNOWN
};

inline const char* moodToString(MoodType mood) {
    switch (mood) {
        case MoodType::CALM: return "Calm";
        case MoodType::ENERGETIC: return "Energetic";
        case MoodType::INTENSE: return "Intense";
        case MoodType::FLOATY: return "Floaty";
        default: return "Unknown";
    }
}

// Rich snapshot of mood-relevant audio
struct MoodSnapshot {
    float volume = 0;
    float loudness = 0;
    float peak = 0;
    float average = 0;
    float agcLevel = 1;

    float bass = 0;
    float mid = 0;
    float treble = 0;

    float spectrumCentroid = 0;
    int dominantBand = 0;
    float dynamics = 0;
    float energy = 0;

    bool beatDetected = false;
    float bpm = 0;
    int bassHits = 0;

    float noiseFloor = 0;
    bool signalPresence = false;

    float frequency = 0;

    unsigned long timestamp = 0;
};

class MoodHistory {
private:
    std::deque<MoodSnapshot> history;
    size_t maxSize = 150; // ~6 seconds @ 25 FPS

    MoodSnapshot current;
    MoodType currentMood = MoodType::UNKNOWN;
    MoodType predictedNextMood = MoodType::UNKNOWN;

public:
    void update(const AudioFeatures& f) {
        MoodSnapshot m = {
            .volume = f.volume,
            .loudness = f.loudness,
            .peak = f.peak,
            .average = f.average,
            .agcLevel = f.agcLevel,
            .bass = f.bass,
            .mid = f.mid,
            .treble = f.treble,
            .spectrumCentroid = f.spectrumCentroid,
            .dominantBand = f.dominantBand,
            .dynamics = f.dynamics,
            .energy = f.energy,
            .beatDetected = f.beatDetected,
            .bpm = f.bpm,
            .bassHits = f.bassHits,
            .noiseFloor = f.noiseFloor,
            .signalPresence = f.signalPresence,
            .frequency = f.frequency,
            .timestamp = millis()
        };

        current = m;
        history.push_back(m);
        if (history.size() > maxSize) history.pop_front();

        currentMood = classifyMood(m);
        predictedNextMood = predictNextMood();
    }

    const MoodSnapshot& getCurrentSnapshot() const { return current; }
    MoodType getCurrentMood() const { return currentMood; }
    MoodType getPredictedNextMood() const { return predictedNextMood; }
    String getCurrentMoodName() const { return String(moodToString(currentMood)); }
    String getPredictedMoodName() const { return String(moodToString(predictedNextMood)); }

    const std::deque<MoodSnapshot>& getHistory() const { return history; }

private:
    MoodType classifyMood(const MoodSnapshot& m) const {
        if (m.energy > 0.8f && m.dynamics > 0.5f) return MoodType::INTENSE;
        if (m.energy > 0.6f && m.bpm > 100) return MoodType::ENERGETIC;
        if (m.energy < 0.3f && m.dynamics < 0.2f) return MoodType::CALM;
        if (m.bpm < 80 && m.energy > 0.4f) return MoodType::FLOATY;
        return MoodType::UNKNOWN;
    }

    MoodType predictNextMood() const {
        if (history.size() < 10) return currentMood;

        float avgEnergy = 0, avgBPM = 0, avgDynamics = 0;

        for (const auto& m : history) {
            avgEnergy += m.energy;
            avgBPM += m.bpm;
            avgDynamics += m.dynamics;
        }

        avgEnergy /= history.size();
        avgBPM /= history.size();
        avgDynamics /= history.size();

        return classifyMood({.energy = avgEnergy, .bpm = avgBPM, .dynamics = avgDynamics});
    }
};
