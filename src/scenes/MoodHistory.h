#pragma once

#include <Arduino.h>
#include <cmath>
#include "../audio/AudioFeatures.h"
#include "../audio/SnapshotRing.h"

// Display order, and deliberately not intensity order. SILENT, TEASE, BUILDUP,
// DROP, DESCENT and WEIRD name the shape of a passage rather than how loud it is,
// so they sit between rungs rather than on one, and comparing two MoodType values
// does not compare intensity. Every rank comparison goes through ladderRank().
//
// The order walks the archetypal arc and returns to where it started: silence,
// then up through the rungs with the structural moods at the points they happen,
// the peak, and DESCENT closing the loop back onto SILENT. WEIRD is deliberately
// the one that sits outside the arc, since an eclectic passage can be at any
// intensity.
//
// MOOD_COUNT is a sentinel. classifyMood never returns it, the picker never
// selects it, and moodToString renders it as "?". It exists because the mood
// count was hardcoded as 5 in three places in the harness, so adding a mood
// silently left two of them behind.
enum MoodType {
    SILENT, FLOATY, CALM, TEASE, DANCY,
    BUILDUP, ENERGETIC, DROP, INTENSE, WEIRD,
    DESCENT,
    MOOD_COUNT
};

static const char* moodToString(MoodType mood) {
    switch (mood) {
        case SILENT:    return "Silent";
        case FLOATY:    return "Floaty";
        case CALM:      return "Calm";
        case TEASE:     return "Tease";
        case DANCY:     return "Dancy";
        case BUILDUP:   return "Buildup";
        case ENERGETIC: return "Energetic";
        case DROP:      return "DROP";
        case INTENSE:   return "Intense";
        case WEIRD:     return "Weeeeird";
        case DESCENT:   return "Descent";
        case MOOD_COUNT: break;
    }
    return "?";
}

// The five rungs the ladder can return, quietest first. -1 means the mood is
// not on the ladder at all: a structural mood, or MOOD_COUNT.
inline int ladderRank(MoodType mood) {
    switch (mood) {
        case FLOATY:    return 0;
        case CALM:      return 1;
        case DANCY:     return 2;
        case ENERGETIC: return 3;
        case INTENSE:   return 4;
        default:        return -1;
    }
}

inline MoodType ladderMood(int rank) {
    if (rank <= 0) return FLOATY;
    if (rank == 1) return CALM;
    if (rank == 2) return DANCY;
    if (rank == 3) return ENERGETIC;
    return INTENSE;
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

    // What the structural half of the classifier reads. gateGain, buildup, the
    // two flags and the anomaly count, and deliberately not spectralFlatness:
    // that one is an input to the anomaly count rather than something the
    // classifier tests, so carrying it here would be a field with no reader.
    //
    // gateGain is the reason this is a copy rather than a reference. The
    // prediction averages the ring, and a snapshot left at the struct default
    // would make a prediction about a silent window claim signal.
    float gateGain;
    float buildup;
    float descent;
    bool  dropDetected;
    float anomaly;
    bool  teaseDetected;

    MusicState music;

    float frequency;

    unsigned long timestamp;

    // Listed explicitly rather than given in-class initialisers, so there is one
    // site that says what a zero snapshot is. The default is the normal-reading
    // one, not the off one: a hand-built snapshot must classify as a rung rather
    // than as SILENT. See AudioFeatures::gateGain.
    MoodSnapshot()
        : volume(0), loudness(0), peak(0), average(0),
          bass(0), mid(0), treble(0),
          spectrumCentroid(0), dominantBand(0), dynamics(0), energy(0), level(0),
          beatDetected(false), bpm(0), bassHits(0),
          noiseFloor(0), signalPresence(false),
          gateGain(1.0f), buildup(0), descent(0), dropDetected(false),
          anomaly(0), teaseDetected(false),
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
    MoodType candidate = SILENT;
    unsigned long candidateSince = 0;
    unsigned long moodSince      = 0;

    // Arriving at a first verdict is not a mood change. currentMood starts at
    // SILENT because the interface needs something to display, which makes the
    // boot value and a genuinely silent passage the same value, so this flag is
    // what separates "no verdict yet" from "classified as silent". Without it the
    // counter would report a change on the first frame of every session, which is
    // the one transition that is certainly not the mood moving.
    bool haveVerdict = false;

    // Until when the display reads DROP, after one has fired. DROP is the only
    // mood that is an event rather than a condition: dropDetected is true for one
    // block, and the confirmation window would reject a single block before it
    // could ever be shown, so a real drop would be the one mood the system could
    // never report. The pin is the confirmation window's job done by hand, for
    // the one case where the condition cannot last long enough to earn it.
    //
    // Lives here rather than in classifyMood, which stays a pure function of the
    // snapshot so the harness can sweep the whole input space through it. A pin
    // is a fact about the clock, not about the audio.
    unsigned long dropPinUntil = 0;

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

    // The ladder's four edges, and the reason the classifier is total. See
    // ladderRankFrom() for what the four overlapping booleans they replaced cost.
    static constexpr float LADDER_EDGE_0 = 0.20f;   // FLOATY    | CALM
    static constexpr float LADDER_EDGE_1 = 0.40f;   // CALM      | DANCY
    static constexpr float LADDER_EDGE_2 = 0.60f;   // DANCY     | ENERGETIC
    static constexpr float LADDER_EDGE_3 = 0.80f;   // ENERGETIC | INTENSE
    static constexpr int   LADDER_RUNGS  = 5;

    // What bpm has to clear to nudge a rung, either way. Not page-tunable: a
    // tempo nudge is a property of the ladder, not a dial on the signal.
    static constexpr float BPM_NUDGE_UP   = 120.0f;
    static constexpr float BPM_NUDGE_DOWN = 80.0f;

public:
    MoodHistory() : currentMood(SILENT), predictedNextMood(SILENT) {}

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
        m.gateGain = f.gateGain;
        m.buildup = f.buildup;
        m.descent = f.descent;
        m.dropDetected = f.dropDetected;
        m.anomaly = f.anomaly;
        m.teaseDetected = f.teaseDetected;
        m.music = f.music;
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
        // The classifier now consumes the coordinate state. Keep its
        // intensity and tempo coordinates aligned with the legacy smoothed
        // fields so step 3 preserves the existing smoothing behavior.
        smoothed.music.intensity.value = smoothLevel;
        smoothed.music.tempo.value = constrain(smoothBpm / 240.0f, 0.0f, 1.0f);
        smoothed.music.presence.value = constrain(m.gateGain, 0.0f, 1.0f);
        smoothed.music.presence.confidence = smoothed.music.presence.value;
        smoothed.music.initialized = true;

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

        // Two rules, and they do different jobs. Arriving at a first verdict is
        // adopted at once, because there is nothing to protect: a held mood that
        // must be outlasted exists only once a real one has been chosen.
        //
        // Between two real moods both apply. The confirmation stops a signal
        // sitting on a threshold from alternating; the hold is the minimum dwell
        // the mood system did not have.
        //
        // Neither is a step limiter. The ladder is an ordering used for matching,
        // never a path the classifier walks, so a passage may jump from FLOATY
        // straight to INTENSE and that is correct, because a track can do exactly
        // that. The abruptness of such a jump is not answered here.
        // A drop pins the display rather than being confirmed into it. Stamped
        // before the verdict so the frame that fires the drop is already inside
        // the window it opens.
        if (m.dropDetected) dropPinUntil = m.timestamp + DROP_PIN_MS;

        MoodType raw = classifyMood(smoothed);
        if (m.timestamp < dropPinUntil) raw = DROP;

        if (!haveVerdict) {
            currentMood    = raw;
            candidate      = raw;
            moodSince      = m.timestamp;
            candidateSince = m.timestamp;
            haveVerdict    = true;
        } else if (raw == currentMood) {
            candidate = currentMood;
        } else if (raw != candidate) {
            candidate = raw;
            candidateSince = m.timestamp;
        } else if (m.timestamp - candidateSince >= confirmMs &&
                   m.timestamp - moodSince      >= minHoldMs) {
            currentMood = raw;
            moodSince   = m.timestamp;
            candidate   = raw;
            // Only here. The branches above leave the mood either unchanged or
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

    // Forget the window and every learned reference, so the next snapshot is
    // classified on its own terms.
    //
    // Everything here is a statistic of audio that was playing. The classifier
    // reads smoothed values, so it keeps testing the new input against a level the
    // old one settled at. The dynamics window it adapts is the old input's spread.
    // The hold clock and the candidate are a mood half-arrived at. And
    // predictNextMood averages the whole 150-snapshot ring, so the prediction is a
    // verdict on a signal that has stopped. The page switches between a synthetic
    // signal and the microphone, and the two are about forty times apart in
    // amplitude, so none of those references describe the input actually in use.
    // Called from the same place as AudioProcessor::resetTracking, for the same
    // reason.
    void reset() {
        history.clear();
        current           = MoodSnapshot();
        currentMood       = SILENT;
        predictedNextMood = SILENT;
        moodChangeCount   = 0;
        smoothLevel       = 0.0f;
        smoothDynamics    = 0.0f;
        smoothBpm         = 0.0f;
        candidate         = SILENT;
        candidateSince    = 0;
        moodSince         = 0;
        haveVerdict       = false;
        dropPinUntil      = 0;
        dynLo             = 0.0f;
        dynHi             = 0.0f;
        dynSpan           = 0.0f;
        dynSeeded         = false;
        dynLastMs         = 0;
        smoothing         = false;
    }

    // Read by the interface, so a session that never moved the mood reads as zero
    // changes rather than as no measurement. See the member's comment.
    int getMoodChangeCount() const { return moodChangeCount; }

    // The classifier's dynamics cut points move, so anything displaying them has
    // to read them rather than hardcode 0.2 and 0.5.
    float getDynamicsLow() const { return dynLo + dynSpan * 0.3f; }
    float getDynamicsHigh() const { return dynLo + dynSpan * 0.7f; }
    bool  dynamicsThresholdsActive() const { return dynSpan > DYN_MIN_SPAN; }

private:
    MusicState stateFor(const MoodSnapshot& m) const {
        if (m.music.initialized) return m.music;
        MusicState s;
        s.intensity.value = constrain(m.level, 0.0f, 1.0f);
        s.tempo.value = constrain(m.bpm / 240.0f, 0.0f, 1.0f);
        s.presence.value = constrain(m.gateGain, 0.0f, 1.0f);
        s.buildup = m.buildup > 0.0f;
        s.descent = m.descent > 0.0f;
        s.dropDetected = m.dropDetected;
        s.teaseDetected = m.teaseDetected;
        s.anomaly = m.anomaly >= WEIRD_ANOMALY_MIN;
        return s;
    }

    // A total partition over level, with bpm and dynamics as nudges of at most
    // one rung each. The nudge is the whole fix.
    //
    // This replaced four overlapping booleans that fell through to UNKNOWN.
    // Enumerating their coverage leaves four bands with no mood at all: level
    // 0.30-0.40, 0.40-0.60 at bpm >= 80, 0.60-0.80 at bpm <= 100, and level above
    // 0.8 with a dynamics window too narrow to clear the cut. UNKNOWN then
    // reached a picker whose fallback was a uniform draw over the whole catalog,
    // so a third of the input range selected a scene at random while the
    // interface read "Calm", because moodToString mapped UNKNOWN to Calm.
    //
    // `energetic = level > 0.6 && bpm > 100` is the shape of the bug: a gate ANDs
    // a band away, so level 0.5 was unreachable whatever was playing. A gate
    // removes a band from the ladder, and a removed band is exactly a dead band.
    // A nudge of at most one rung cannot create one.
    //
    // The level thresholds are what they were always written for, 0..1, and the
    // field they read is level rather than energy. energy is a sum of 255 FFT
    // magnitudes, in the hundreds, so every one of the old threshold tests was
    // true on every frame: the classifier could never reach CALM and returned
    // INTENSE whenever dynamics cleared its cut, whatever was playing.
    int ladderRankFrom(const MoodSnapshot& m) const {
        const MusicState s = stateFor(m);
        int rung = 0;
        if      (s.intensity.value >= LADDER_EDGE_3) rung = 4;
        else if (s.intensity.value >= LADDER_EDGE_2) rung = 3;
        else if (s.intensity.value >= LADDER_EDGE_1) rung = 2;
        else if (s.intensity.value >= LADDER_EDGE_0) rung = 1;

        // bpm > 1 is required rather than defensive. bpm is exactly 0 whenever no
        // tempo is known, which is every beatless passage, so a bare `bpm < 80`
        // would push each of those down a rung for as long as it lasted.
        const float bpm = s.tempo.value * 240.0f;
        if (bpm > BPM_NUDGE_UP) rung += 1;
        else if (bpm > 1.0f && bpm < BPM_NUDGE_DOWN) rung -= 1;

        // Only when the observed range is wide enough to split. Below DYN_MIN_SPAN
        // the input has no dynamic variation worth reading, and splitting its own
        // noise would report a mood on the strength of it.
        if (dynSpan > DYN_MIN_SPAN) {
            if      (m.dynamics > dynLo + dynSpan * 0.7f) rung += 1;
            else if (m.dynamics < dynLo + dynSpan * 0.3f) rung -= 1;
        }

        if (rung < 0)                 rung = 0;
        if (rung > LADDER_RUNGS - 1)  rung = LADDER_RUNGS - 1;
        return rung;
    }

    // Two stages. The structural moods answer first, because each is a claim
    // about the shape of a passage rather than about its loudness, and the same
    // level is a hush before a drop or an ambient drift depending entirely on
    // what came before it. The ladder answers everything else.
    //
    // The order among the structural ones is by how tightly each is tied to
    // something that just happened. SILENT is the gate, the one condition that can
    // say there is no audio here at all. DROP is an event and outranks
    // everything. TEASE is anchored to an event as well, since its post-drop
    // trigger is a drop within the last twelve seconds, and the aftermath of an
    // event is named by the event. DESCENT and BUILDUP are standing measurements
    // of a movement with nothing event-shaped behind them, and they take over
    // once no event is in recent memory. WEIRD is the loosest, needing two of
    // three, so it loses to all of them.
    //
    // TEASE ahead of DESCENT is the ordering that changed on review, and it
    // matters because the two genuinely overlap: a drop is always followed by a
    // fall, so the twelve seconds after one are both. Those twelve seconds go to
    // TEASE, which is what was asked for, and DESCENT catches every wind-down
    // that is not a drop's aftermath.
    //
    // No structural mood is reachable from every rung. The two that the ladder
    // gates are gated in mirror image: BUILDUP needs the rung below ENERGETIC,
    // because a climb that starts at the top is just loud music, and DESCENT
    // needs it above CALM, because a fall that starts at the bottom is just
    // quiet.
    MoodType classifyMood(const MoodSnapshot& m) const {
        const MusicState s = stateFor(m);
        if (s.presence.value < SILENT_GATE) return SILENT;
        if (s.dropDetected)                 return DROP;
        if (s.teaseDetected)                return TEASE;

        const int rung = ladderRankFrom(m);

        if (s.buildup && rung < 3)                         return BUILDUP;
        if (s.descent && rung > 1)                         return DESCENT;
        if (s.anomaly && rung >= 2)                        return WEIRD;
        return ladderMood(rung);
    }

public:
    // The harness sweeps the whole input space through this, without the dwell.
    // Asserting the partition is total needs to reach every input rather than
    // whichever ones the hold and the confirmation happen to let through.
    MoodType classifyForTest(const MoodSnapshot& m) const { return classifyMood(m); }

private:
    MoodType predictNextMood() const {
        if (history.size() < 10) return currentMood;

        float avgLevel = 0, avgBPM = 0, avgDynamics = 0, avgGate = 0;

        for (size_t i = 0; i < history.size(); ++i) {
            avgLevel += history[i].level;
            avgBPM += history[i].bpm;
            avgDynamics += history[i].dynamics;
            avgGate += history[i].gateGain;
        }

        avgLevel /= history.size();
        avgBPM /= history.size();
        avgDynamics /= history.size();
        avgGate /= history.size();

        // gateGain is averaged and the rest of the structural fields are left at
        // the snapshot default, which is the same distinction the smoothing above
        // makes. The gate is a continuous quantity, so a window that was mostly
        // silent is a silent window and the prediction should say so. The two
        // flags and the two displacements are not: a one-block pulse has no mean,
        // and a rising or falling passage averages to a mean that describes
        // neither half, so a prediction built from them would claim a movement
        // that stopped.
        MoodSnapshot temp;
        temp.level = avgLevel;
        temp.bpm = avgBPM;
        temp.dynamics = avgDynamics;
        temp.gateGain = avgGate;

        return classifyMood(temp);
    }
};
