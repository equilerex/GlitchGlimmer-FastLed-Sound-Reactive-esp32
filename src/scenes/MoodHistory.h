#pragma once

#include <deque>
#include <Arduino.h>
#include "../audio/AudioFeatures.h"

enum MoodType {
    CALM,
    ENERGETIC,
    INTENSE,
    FLOATY,
    UNKNOWN
};

static const char* moodToString(MoodType mood) {
    switch (mood) {
        case CALM: return "Calm";
        case ENERGETIC: return "Energetic";
        case INTENSE: return "Intense";
        case FLOATY: return "Floaty";
        case UNKNOWN: return "Calm";
    }
    return "Unknown";
}

struct MoodSnapshot {
    float volume;
    float loudness;
    float peak;
    float average;
    float agcLevel;

    float bass;
    float mid;
    float treble;

    float spectrumCentroid;
    int dominantBand;
    float dynamics;
    float energy;

    bool beatDetected;
    float bpm;
    int bassHits;

    float noiseFloor;
    bool signalPresence;

    float frequency;

    unsigned long timestamp;

    MoodSnapshot()
        : volume(0), loudness(0), peak(0), average(0), agcLevel(1),
          bass(0), mid(0), treble(0),
          spectrumCentroid(0), dominantBand(0), dynamics(0), energy(0),
          beatDetected(false), bpm(0), bassHits(0),
          noiseFloor(0), signalPresence(false),
          frequency(0), timestamp(0) {}
};

class MoodHistory {
private:
    std::deque<MoodSnapshot> history;
    size_t maxSize;

    MoodSnapshot current;
    MoodType currentMood;
    MoodType predictedNextMood;

public:
    MoodHistory() : maxSize(150), currentMood(UNKNOWN), predictedNextMood(UNKNOWN) {}

    void update(const AudioFeatures& f) {
        MoodSnapshot m;

        m.volume = f.volume;
        m.loudness = f.loudness;
        m.peak = f.peak;
        m.average = f.average;
        m.agcLevel = f.agcLevel;
        m.bass = f.bass;
        m.mid = f.mid;
        m.treble = f.treble;
        m.spectrumCentroid = f.spectrumCentroid;
        m.dominantBand = f.dominantBand;
        m.dynamics = f.dynamics;
        m.energy = f.energy;
        m.beatDetected = f.beatDetected;
        m.bpm = f.bpm;
        m.bassHits = f.bassHits;
        m.noiseFloor = f.noiseFloor;
        m.signalPresence = f.signalPresence;
        m.frequency = f.frequency;
        m.timestamp = millis();

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
        if (m.energy > 0.8f && m.dynamics > 0.5f) return INTENSE;
        if (m.energy > 0.6f && m.bpm > 100) return ENERGETIC;
        if (m.energy < 0.3f && m.dynamics < 0.2f) return CALM;
        if (m.bpm < 80 && m.energy > 0.4f) return FLOATY;
        return UNKNOWN;
    }

    MoodType predictNextMood() const {
        if (history.size() < 10) return currentMood;

        float avgEnergy = 0, avgBPM = 0, avgDynamics = 0;

        for (size_t i = 0; i < history.size(); ++i) {
            avgEnergy += history[i].energy;
            avgBPM += history[i].bpm;
            avgDynamics += history[i].dynamics;
        }

        avgEnergy /= history.size();
        avgBPM /= history.size();
        avgDynamics /= history.size();

        MoodSnapshot temp;
        temp.energy = avgEnergy;
        temp.bpm = avgBPM;
        temp.dynamics = avgDynamics;

        return classifyMood(temp);
    }
};
