#pragma once

#include <Arduino.h>
#include <cmath>
#include "../audio/AudioFeatures.h"
#include "../audio/SnapshotRing.h"

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

    float bass;
    float mid;
    float treble;

    float spectrumCentroid;
    int dominantBand;
    float dynamics;
    float energy;
    float level;

    bool beatDetected;
    float bpm;
    int bassHits;

    float noiseFloor;
    bool signalPresence;

    float frequency;

    unsigned long timestamp;

    MoodSnapshot()
        : volume(0), loudness(0), peak(0), average(0),
          bass(0), mid(0), treble(0),
          spectrumCentroid(0), dominantBand(0), dynamics(0), energy(0), level(0),
          beatDetected(false), bpm(0), bassHits(0),
          noiseFloor(0), signalPresence(false),
          frequency(0), timestamp(0) {}
};

class MoodHistory {
private:
    // 150 snapshots at 76 bytes, one contiguous block. This was a std::deque,
    // which took and returned a 456-byte node every six frames and was one of
    // the loop's two throw paths. Nothing outside this class ever read the
    // container, only predictNextMood() below, and its sums are
    // order-independent, so a ring preserves the behaviour exactly.
    SnapshotRing<MoodSnapshot, 150> history;

    MoodSnapshot current;
    MoodType currentMood;
    MoodType predictedNextMood;

    // How many times the mood has actually changed. Counted here rather than at
    // the interface, because the page's trace only records with ?debug=1 on and
    // reported zero changes otherwise, which reads as a frozen classifier rather
    // than as an absent measurement.
    int moodChangeCount = 0;

    // The classifier reads these rather than the instantaneous values. Raw
    // per-frame values cross its thresholds several times a second on real audio,
    // which is the flicker the browser reported. A rate this slow moves most of the
    // way in about eight frames, so a genuine change of mood still lands inside a
    // third of a second.
    float smoothLevel    = 0.0f;
    float smoothDynamics = 0.0f;
    float smoothBpm      = 0.0f;

    // Minimum time a mood must be displayed, and minimum time a competing mood
    // must hold before it may replace it. Without the second the classifier
    // alternates whenever the signal sits on a threshold; without the first a
    // genuinely noisy signal changes mood several times a second, which is what
    // the browser reported as the mood value jumping like it had epilepsy.
    MoodType candidate = UNKNOWN;
    unsigned long candidateSince = 0;
    unsigned long moodSince      = 0;

    // The classifier's dynamics cut points move with the observed range rather
    // than sitting at 0.5 and 0.2. A fixed pair is a claim about one input's
    // spread: on the microphone in use the range a signal covers is narrow when the
    // room is steady and wide when it is not, so a fixed cut is either always
    // cleared or never cleared depending on the room, and dynamics could not
    // separate anything. dynSpan is what the classifier tests for usability, since
    // a signal with no drum in it has no range to split.
    //
    // The rates are per second rather than per frame. The device analyses a block
    // every 33 ms and the browser page steps once per animation frame, so a
    // per-frame rate would make the same window adapt at two different speeds.
    float dynLo = 0.0f;
    float dynHi = 0.0f;
    float dynSpan = 0.0f;
    bool  dynSeeded = false;
    unsigned long dynLastMs = 0;
    bool  smoothing      = false;

    // The dials the interface exposes. Defaults are the values this was tuned to;
    // the setters exist so the mood system can be adjusted against real audio from
    // the page rather than by editing a constant and rebuilding.
    float         smoothingRate = 0.05f;
    unsigned long minHoldMs     = 2000;
    unsigned long confirmMs     = 500;
    float         dynUpPerSec   = 1.5f;
    float         dynDownPerSec = 0.3f;
    static constexpr float DYN_MIN_SPAN = 0.08f;

public:
    MoodHistory() : currentMood(UNKNOWN), predictedNextMood(UNKNOWN) {}

    void setSmoothingRate(float rate) { smoothingRate = constrain(rate, 0.005f, 1.0f); }
    void setMinHoldMs(float ms)       { minHoldMs = (unsigned long)constrain(ms, 0.0f, 60000.0f); }
    void setConfirmMs(float ms)       { confirmMs = (unsigned long)constrain(ms, 0.0f, 10000.0f); }
    void setDynUpPerSec(float rate)   { dynUpPerSec = constrain(rate, 0.05f, 20.0f); }
    void setDynDownPerSec(float rate) { dynDownPerSec = constrain(rate, 0.01f, 20.0f); }

    float         getSmoothingRate() const { return smoothingRate; }
    float         getMinHoldMs() const { return float(minHoldMs); }
    float         getConfirmMs() const { return float(confirmMs); }
    float         getDynUpPerSec() const { return dynUpPerSec; }
    float         getDynDownPerSec() const { return dynDownPerSec; }

    void update(const AudioFeatures& f) {
        MoodSnapshot m;

        m.volume = f.volume;
        m.loudness = f.loudness;
        m.peak = f.peak;
        m.average = f.average;
        m.bass = f.bass;
        m.mid = f.mid;
        m.treble = f.treble;
        m.spectrumCentroid = f.spectrumCentroid;
        m.dominantBand = f.dominantBand;
        m.dynamics = f.dynamics;
        m.energy = f.energy;
        m.level = f.level;
        m.beatDetected = f.beatDetected;
        m.bpm = f.bpm;
        m.bassHits = f.bassHits;
        m.noiseFloor = f.noiseFloor;
        m.signalPresence = f.signalPresence;
        m.frequency = f.frequency;
        m.timestamp = millis();

        current = m;
        history.push_back(m);

        // Two stages. The inputs are smoothed first, so a value sitting on a
        // threshold does not carry the classification back and forth across it.
        // classifyMood then holds the running mood while its own condition still
        // holds, so two conditions that are true at once cannot alternate.
        if (!smoothing) {
            // Seeded from the first block rather than from zero, so the very first
            // frame is a classification of real audio instead of of silence.
            smoothLevel    = m.level;
            smoothDynamics = m.dynamics;
            smoothBpm      = m.bpm;
            smoothing      = true;
        } else {
            smoothLevel    += (m.level    - smoothLevel)    * smoothingRate;
            smoothDynamics += (m.dynamics - smoothDynamics) * smoothingRate;
            smoothBpm      += (m.bpm      - smoothBpm)      * smoothingRate;
        }

        MoodSnapshot smoothed = m;
        smoothed.level    = smoothLevel;
        smoothed.dynamics = smoothDynamics;
        smoothed.bpm      = smoothBpm;

        if (!dynSeeded) {
            dynLo = smoothDynamics;
            dynHi = smoothDynamics;
            dynSeeded = true;
        } else {
            // Clamped, so a stall between frames cannot move the window by a whole
            // step and a first frame from a zero timestamp cannot move it at all.
            float dt = float(m.timestamp - dynLastMs) * 0.001f;
            if (dt > 0.1f) dt = 0.1f;
            const float up   = 1.0f - expf(-dynUpPerSec * dt);
            const float down = 1.0f - expf(-dynDownPerSec * dt);
            if (smoothDynamics > dynHi) dynHi += (smoothDynamics - dynHi) * up;
            else                        dynHi -= (dynHi - smoothDynamics) * down;
            if (smoothDynamics < dynLo) dynLo += (smoothDynamics - dynLo) * up;
            else                        dynLo -= (dynLo - smoothDynamics) * down;
        }
        dynLastMs = m.timestamp;
        if (dynHi < dynLo) { const float swap = dynHi; dynHi = dynLo; dynLo = swap; }
        dynSpan = dynHi - dynLo;

        // Depleting UNKNOWN is not a mood change, it is the classifier arriving.
        // Adopted at once, because there is nothing to protect: a held mood that
        // must be outlasted exists only once a real one has been chosen, and
        // requiring a candidate to persist before the first adoption leaves the
        // classifier on UNKNOWN, which the interface displays as Calm and reads as
        // a stuck mood rather than as an unsettled one.
        //
        // Between two real moods both rules apply. The confirmation stops a signal
        // sitting on a threshold from alternating; the hold is the minimum dwell
        // the mood system did not have.
        const MoodType raw = classifyMood(smoothed, currentMood);
        if (raw == currentMood) {
            candidate = UNKNOWN;
        } else if (currentMood == UNKNOWN && raw != UNKNOWN) {
            currentMood = raw;
            moodSince   = m.timestamp;
            candidate   = UNKNOWN;
        } else if (raw != candidate) {
            candidate = raw;
            candidateSince = m.timestamp;
        } else if (m.timestamp - candidateSince >= confirmMs &&
                   m.timestamp - moodSince      >= minHoldMs) {
            currentMood = raw;
            moodSince   = m.timestamp;
            candidate   = UNKNOWN;
            // Only here. Both branches above leave the mood either unchanged or
            // arriving for the first time, and the comment above says why arriving
            // is not a change.
            ++moodChangeCount;
        }

        predictedNextMood = predictNextMood();
    }

    const MoodSnapshot& getCurrentSnapshot() const { return current; }
    MoodType getCurrentMood() const { return currentMood; }
    MoodType getPredictedNextMood() const { return predictedNextMood; }
    String getCurrentMoodName() const { return String(moodToString(currentMood)); }
    String getPredictedMoodName() const { return String(moodToString(predictedNextMood)); }
    size_t size() const { return history.size(); }

    // Read by the interface, so a session that never moved the mood reads as zero
    // changes rather than as no measurement. See the member's comment.
    int getMoodChangeCount() const { return moodChangeCount; }

    // The classifier's dynamics cut points move, so anything displaying them has
    // to read them rather than hardcode 0.2 and 0.5.
    float getDynamicsLow() const { return dynLo + dynSpan * 0.3f; }
    float getDynamicsHigh() const { return dynLo + dynSpan * 0.7f; }
    bool  dynamicsThresholdsActive() const { return dynSpan > DYN_MIN_SPAN; }

private:
    // `held` is the mood currently on display. Its own condition is tested first
    // and short-circuits, so a frame in which two conditions are both true keeps
    // the classification it already had instead of alternating between them.
    // The level thresholds are what they were written for, 0..1, and the field
    // they read is level rather than energy. energy is a sum of 255 magnitudes in
    // the hundreds, so every one of these was true on every frame: the classifier
    // could never reach CALM and returned INTENSE whenever dynamics cleared 0.5,
    // whatever was playing.
    MoodType classifyMood(const MoodSnapshot& m, MoodType held) const {
        // The dynamics cut points are 30 and 70 percent of the observed range.
        // Below DYN_MIN_SPAN the input has no dynamic variation worth reading, so
        // the dynamics clause is dropped rather than allowed to split its own
        // noise and report a mood on the strength of it.
        const bool  dynUsable = dynSpan > DYN_MIN_SPAN;
        const float dynHigh   = dynLo + dynSpan * 0.7f;
        const float dynLow    = dynLo + dynSpan * 0.3f;

        const bool intense   = m.level > 0.8f && (!dynUsable || m.dynamics > dynHigh);
        const bool energetic = m.level > 0.6f && m.bpm > 100;
        const bool calm      = m.level < 0.3f && (!dynUsable || m.dynamics < dynLow);
        const bool floaty    = m.bpm < 80 && m.level > 0.4f;

        switch (held) {
            case INTENSE:   if (intense)   return INTENSE;   break;
            case ENERGETIC: if (energetic) return ENERGETIC; break;
            case CALM:      if (calm)      return CALM;      break;
            case FLOATY:    if (floaty)    return FLOATY;    break;
            default: break;
        }

        if (intense)   return INTENSE;
        if (energetic) return ENERGETIC;
        if (calm)      return CALM;
        if (floaty)    return FLOATY;
        return UNKNOWN;
    }

    MoodType predictNextMood() const {
        if (history.size() < 10) return currentMood;

        float avgLevel = 0, avgBPM = 0, avgDynamics = 0;

        for (size_t i = 0; i < history.size(); ++i) {
            avgLevel += history[i].level;
            avgBPM += history[i].bpm;
            avgDynamics += history[i].dynamics;
        }

        avgLevel /= history.size();
        avgBPM /= history.size();
        avgDynamics /= history.size();

        MoodSnapshot temp;
        temp.level = avgLevel;
        temp.bpm = avgBPM;
        temp.dynamics = avgDynamics;

        return classifyMood(temp, UNKNOWN);
    }
};
