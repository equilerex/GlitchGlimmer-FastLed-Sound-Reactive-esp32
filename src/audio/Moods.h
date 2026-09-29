#pragma once

#include <Arduino.h>
#include <cmath>
#include <cstdio>
#include <cstdint>

// Moods, measured. A mood is a named nature of the music, defined by readings
// (`MusicState` coordinates, tempo in BPM). Music has each mood to a degree 0..1,
// several at once. See _architecture/plans/2026-09-28-base-layer-mood.md.
//
// Measure only: nothing selects a scene from these yet.
//
// Every breakpoint below is a draft, unvalidated, and must be set on a recording
// made with the real microphone, never on the synthetic signal. The table is the
// one place the numbers live. The emulator edits a runtime copy and `Copy mood
// table` writes it back out in exactly this shape.

enum MoodNature : uint8_t {
    MN_FLOATY,
    MN_CALM,
    MN_FLOWING,
    MN_DRIVING,
    MN_RUSHING,
    MN_WARM,
    MN_INTENSE,
    MN_HEAVY,
    MN_BRIGHT,
    MN_FULL,
    MN_CHAOTIC,
    MN_SPARSE,
    MN_QUIET,
    MN_SYNCOPATED,
    MN_COUNT
};

// What a condition reads. Tilt is 0 for a bass-heavy spectrum and 1 for a bright
// one, so "tilt low" in the plan's table means dark and low-end heavy.
enum MoodReading : uint8_t {
    RD_NONE,
    RD_PULSE,
    RD_ACTIVITY,
    RD_TEXTURE,
    RD_BRIGHTNESS,
    RD_TEMPO,       // BPM, not 0..1
    RD_PUNCH,
    RD_BODY,
    RD_DYNAMICS,
    RD_PRESENCE,
    RD_TILT,
    RD_EVENNESS,
    RD_COUNT
};

// Zero below `lo`, one above `hi`, linear between. lo > hi is a falling ramp
// ("low"). Two ramps on one reading make a band ("mid", "110 to 128 BPM").
struct MoodCondition {
    MoodReading reading;
    float lo;
    float hi;
};

static const int kMoodMaxConditions = 8;

struct MoodRow {
    const char* name;
    bool inSelector;        // false: a target that cannot be measured yet
    // Tone describes the colour of the sound (warm, heavy, bright, full, sparse).
    // It is always partly true of any music, so it shades a scene's score and does
    // not compete with the character moods, which say what the music does.
    bool tone;
    float tauSeconds;       // smoothing time constant of the strength
    MoodCondition cond[kMoodMaxConditions];
};

// The mood table. Concept numbers are those of docs/music-research/MODEL.md.
//
// Two families. Character moods say what the music does: Floaty, Calm, Flowing,
// Driving, Rushing, Intense, Chaotic, Quiet. Tone moods say what colour the sound
// is: Warm, Heavy, Bright, Full, Sparse. A tone is always partly true, so the two
// are averaged apart when a scene is scored, and a tone at 100% cannot drown the
// character. The board and the weight board keep them apart too.
//
// Pulse, tempo (BPM) and presence are read as they come. Every other reading
// (activity, punch, dynamics, texture, brightness, tilt, evenness, body) depends on
// the room, the microphone and the music, so it is read against a per-device
// calibration (kRoomDefault): 0.5 is what this device usually hears and a
// breakpoint is on that scale. Recordings from this microphone put tilt anywhere
// from 0.32 to 0.84, texture from 0.02 to 0.34 and body from 0.2 to 0.7 between
// sessions, so a fixed cut pins one mood on and leaves its opposite at zero.
// The emulator's `Measure this music` sets the calibration.
//
// Where a mood begins and ends is the owner's, set by ear on recordings of each
// kind of music. These numbers are a first placement so that every mood can rise
// on music that has it.
static const MoodRow kMoodTable[MN_COUNT] = {
    { "Floaty", true, false, 2.0f, {
        {RD_PULSE, 0.75f, 0.45f}, {RD_ACTIVITY, 0.40f, 0.25f}, {RD_DYNAMICS, 0.40f, 0.25f} } },
    { "Calm", true, false, 2.0f, {
        {RD_PULSE, 0.40f, 0.60f}, {RD_TEMPO, 110.0f, 90.0f}, {RD_ACTIVITY, 0.40f, 0.25f},
        {RD_TILT, 0.30f, 0.42f} } },
    { "Flowing", true, false, 2.0f, {
        {RD_PULSE, 0.75f, 0.90f}, {RD_TEMPO, 100.0f, 110.0f}, {RD_TEMPO, 135.0f, 128.0f},
        {RD_ACTIVITY, 0.25f, 0.35f}, {RD_ACTIVITY, 0.75f, 0.60f}, {RD_TILT, 0.50f, 0.35f},
        {RD_PUNCH, 0.65f, 0.45f} } },
    { "Driving", true, false, 2.0f, {
        {RD_PULSE, 0.75f, 0.90f}, {RD_TEMPO, 118.0f, 125.0f}, {RD_TEMPO, 155.0f, 150.0f},
        {RD_ACTIVITY, 0.45f, 0.65f}, {RD_PUNCH, 0.50f, 0.70f} } },
    { "Rushing", true, false, 2.0f, {
        {RD_TEMPO, 150.0f, 160.0f}, {RD_ACTIVITY, 0.55f, 0.75f}, {RD_TILT, 0.50f, 0.35f} } },
    { "Warm", true, true, 2.0f, {
        {RD_BODY, 0.58f, 0.75f}, {RD_EVENNESS, 0.50f, 0.65f}, {RD_BRIGHTNESS, 0.30f, 0.42f},
        {RD_BRIGHTNESS, 0.70f, 0.58f}, {RD_PULSE, 0.50f, 0.65f}, {RD_PULSE, 0.95f, 0.80f} } },
    { "Intense", true, false, 2.0f, {
        {RD_ACTIVITY, 0.60f, 0.80f}, {RD_EVENNESS, 0.62f, 0.78f}, {RD_BRIGHTNESS, 0.62f, 0.78f},
        {RD_DYNAMICS, 0.55f, 0.75f} } },
    { "Heavy", true, true, 2.0f, {
        {RD_TILT, 0.38f, 0.22f}, {RD_BRIGHTNESS, 0.45f, 0.30f} } },
    { "Bright", true, true, 2.0f, {
        {RD_TILT, 0.62f, 0.80f}, {RD_BRIGHTNESS, 0.62f, 0.80f} } },
    { "Full", true, true, 2.0f, {
        {RD_EVENNESS, 0.65f, 0.85f} } },
    { "Chaotic", true, false, 2.0f, {
        {RD_TEXTURE, 0.65f, 0.82f}, {RD_ACTIVITY, 0.60f, 0.80f}, {RD_PULSE, 0.75f, 0.45f},
        {RD_PRESENCE, 0.50f, 0.80f} } },
    { "Sparse", true, true, 2.0f, {
        {RD_ACTIVITY, 0.40f, 0.25f}, {RD_TEXTURE, 0.40f, 0.25f}, {RD_PULSE, 0.40f, 0.60f} } },
    // Presence low only. The plan adds "no periodicity in the onsets", which is
    // not built, so a quiet passage with a beat still reads as Quiet here.
    { "Quiet", true, false, 2.0f, {
        {RD_PRESENCE, 0.50f, 0.20f} } },
    // Needs a beat grid to see accents displaced from the pulse. No conditions,
    // so it stays at 0 and the selector will not read it.
    { "Syncopated", false, false, 2.0f, {} },
};

// What this device usually hears in the eight room-dependent readings: the centre,
// and the spread that maps to one sixth of the 0..1 scale either side of it.
// Defaults are the pooled medians of five of this microphone's captures (the
// demo track goes into the page digitally, with no room, so it is not a stand-in
// for the microphone). Set your own with
// `Measure this music` in the emulator and paste `Copy mood table` here.
struct RoomCal {
    MoodReading reading;
    float center;
    float spread;
};

static const int kRoomCount = 8;
static const RoomCal kRoomDefault[kRoomCount] = {
    {RD_ACTIVITY,   0.280f, 0.100f},
    {RD_PUNCH,      0.190f, 0.080f},
    {RD_DYNAMICS,   0.260f, 0.090f},
    {RD_TEXTURE,    0.100f, 0.040f},
    {RD_BRIGHTNESS, 0.135f, 0.028f},
    {RD_TILT,       0.670f, 0.110f},
    {RD_EVENNESS,   0.800f, 0.050f},
    {RD_BODY,       0.520f, 0.130f},
};

// The readings a strength is computed from, one value and one confidence each.
struct MoodReadings {
    float value[RD_COUNT] = {};
    float confidence[RD_COUNT] = {};
};

inline const char* moodReadingName(MoodReading r) {
    static const char* const names[RD_COUNT] = {
        "none", "pulse", "activity", "texture", "brightness", "tempo", "punch", "body",
        "dynamics", "presence", "tilt", "evenness" };
    return r < RD_COUNT ? names[r] : "?";
}

// Mutable copy of the table's numbers, the smoothed strengths, and the flat
// parameter list the emulator edits.
class MoodModel {
public:
    MoodModel() { reset(); loadRoomDefaults(); loadDefaults(); }

    void loadDefaults() {
        for (int m = 0; m < MN_COUNT; ++m) {
            tau_[m] = kMoodTable[m].tauSeconds;
            for (int c = 0; c < kMoodMaxConditions; ++c) {
                lo_[m][c] = kMoodTable[m].cond[c].lo;
                hi_[m][c] = kMoodTable[m].cond[c].hi;
            }
        }
    }

    // A change of input restarts the strengths. A calibration the owner set or
    // measured stays. One the firmware measured itself is for the music that was
    // playing, so it is measured again for the new input.
    void reset() {
        for (int m = 0; m < MN_COUNT; ++m) strength_[m] = 0.0f;
        if (roomMode_ != ROOM_MANUAL) {
            roomMode_ = ROOM_WAIT;
            measuring_ = false;
            autoSeconds_ = 0.0f;
        }
    }

    // ---- room calibration --------------------------------------------------------
    static bool roomColoured(MoodReading r) {
        return r == RD_ACTIVITY || r == RD_PUNCH || r == RD_DYNAMICS || r == RD_TEXTURE ||
               r == RD_BRIGHTNESS || r == RD_TILT || r == RD_EVENNESS || r == RD_BODY;
    }

    void loadRoomDefaults() {
        for (int i = 0; i < kRoomCount; ++i) {
            center_[kRoomDefault[i].reading] = kRoomDefault[i].center;
            spread_[kRoomDefault[i].reading] = kRoomDefault[i].spread;
        }
        roomMode_ = ROOM_WAIT;
        measuring_ = false;
        autoSeconds_ = 0.0f;
    }

    // Where the calibration came from. The moods read the readings against it, so
    // until it fits the input they read near zero, which is why the firmware
    // measures it itself: the first kAutoRoomSeconds of music after a change of
    // input, taken once presence has settled, then it is fixed. It is not adaptive.
    // The owner's own measurement or edit outranks it and is never overwritten.
    enum RoomMode : uint8_t {
        ROOM_WAIT = 0,      // defaults, waiting for music to measure
        ROOM_MEASURING,     // measuring the first seconds of music
        ROOM_AUTO,          // measured by the firmware from the music that played
        ROOM_MANUAL         // set by the owner: measured on request, or edited
    };
    static constexpr float kAutoRoomSeconds = 20.0f;
    RoomMode roomMode() const { return roomMode_; }
    float roomAutoSeconds() const { return autoSeconds_; }

    // A room-coloured reading on the scale the mood table reads it: 0.5 is the
    // centre, and three spreads either side reach 0 and 1.
    float normalized(MoodReading rd, float v) const {
        if (!roomColoured(rd)) return v;
        const float n = 0.5f + (v - center_[rd]) / (6.0f * fmaxf(spread_[rd], 1e-4f));
        return n < 0.0f ? 0.0f : (n > 1.0f ? 1.0f : n);
    }

    // Take another model's calibration, for a replay that measures a file first.
    void copyRoomFrom(const MoodModel& other) {
        for (int i = 0; i < RD_COUNT; ++i) { center_[i] = other.center_[i]; spread_[i] = other.spread_[i]; }
        roomMode_ = ROOM_MANUAL;
    }

    // Ten parameters, a centre then a spread for each room reading.
    int roomParamCount() const { return kRoomCount * 2; }
    const char* roomParamName(int i) {
        if (i < 0 || i >= roomParamCount()) return "";
        snprintf(nameBuf_, sizeof(nameBuf_), "room %s %s", moodReadingName(kRoomDefault[i / 2].reading),
                 (i % 2) == 0 ? "centre" : "spread");
        return nameBuf_;
    }
    float roomParam(int i) const {
        if (i < 0 || i >= roomParamCount()) return 0.0f;
        const MoodReading rd = kRoomDefault[i / 2].reading;
        return (i % 2) == 0 ? center_[rd] : spread_[rd];
    }
    void setRoomParam(int i, float v) {
        if (i < 0 || i >= roomParamCount()) return;
        const MoodReading rd = kRoomDefault[i / 2].reading;
        if ((i % 2) == 0) center_[rd] = v;
        else spread_[rd] = v < 0.001f ? 0.001f : v;
        roomMode_ = ROOM_MANUAL;
    }

    // Measure the room. While on, every frame's raw readings feed a running mean
    // and variance. Ending takes the mean as each centre and the standard deviation
    // as each spread, never under a quarter of the default so a steady tone cannot
    // make a hair trigger. Returns the frames used, and leaves the calibration
    // alone when there were fewer than 30.
    void measureBegin() {
        measuring_ = true;
        autoMeasure_ = false;
        measureFrames_ = 0;
        for (int i = 0; i < RD_COUNT; ++i) { mean_[i] = 0.0; m2_[i] = 0.0; }
    }
    bool measuring() const { return measuring_; }
    int measuredFrames() const { return measureFrames_; }
    int measureEnd() {
        const bool wasAuto = autoMeasure_;
        measuring_ = false;
        autoMeasure_ = false;
        if (measureFrames_ < 30) {
            if (wasAuto) roomMode_ = ROOM_WAIT;
            return measureFrames_;
        }
        for (int i = 0; i < kRoomCount; ++i) {
            const MoodReading rd = kRoomDefault[i].reading;
            center_[rd] = float(mean_[rd]);
            const float sd = sqrtf(float(m2_[rd] / double(measureFrames_)));
            spread_[rd] = fmaxf(sd, 0.25f * kRoomDefault[i].spread);
        }
        roomMode_ = wasAuto ? ROOM_AUTO : ROOM_MANUAL;
        return measureFrames_;
    }

    float strength(int m) const { return (m >= 0 && m < MN_COUNT) ? strength_[m] : 0.0f; }
    const char* name(int m) const { return (m >= 0 && m < MN_COUNT) ? kMoodTable[m].name : ""; }
    bool inSelector(int m) const { return m >= 0 && m < MN_COUNT && kMoodTable[m].inSelector; }
    bool isTone(int m) const { return m >= 0 && m < MN_COUNT && kMoodTable[m].tone; }

    // One condition of one mood: how far its reading is through the ramp, 0 to 1.
    // Public so a replay can say which condition holds a mood down.
    static int conditionCount(int m) {
        int n = 0;
        while (n < kMoodMaxConditions && kMoodTable[m].cond[n].reading != RD_NONE) ++n;
        return n;
    }
    MoodReading conditionReading(int m, int c) const { return kMoodTable[m].cond[c].reading; }
    float conditionRamp(int m, int c, const MoodReadings& r) const {
        const MoodReading rd = kMoodTable[m].cond[c].reading;
        return ramp(normalized(rd, r.value[rd]), lo_[m][c], hi_[m][c]);
    }

    // Instantaneous strength of one mood: the minimum of its ramps, times the
    // lowest confidence among the readings it uses.
    float raw(int m, const MoodReadings& r) const {
        const MoodRow& row = kMoodTable[m];
        float lowest = 1.0f;
        float sum = 0.0f;
        int n = 0;
        float confidence = 1.0f;
        for (int c = 0; c < kMoodMaxConditions; ++c) {
            const MoodReading rd = row.cond[c].reading;
            if (rd == RD_NONE) continue;
            const float ramp01 = ramp(normalized(rd, r.value[rd]), lo_[m][c], hi_[m][c]);
            lowest = fminf(lowest, ramp01);
            sum += ramp01;
            ++n;
            confidence = fminf(confidence, r.confidence[rd]);
        }
        if (n == 0) return 0.0f;
        // A mood with five conditions would almost never be all true at once, while
        // one with a single condition is true whenever that one is, so a strict
        // minimum makes the many-condition moods read near zero beside the others.
        // Partial credit: the minimum, plus `credit` of the way to the mean, so a
        // near miss on one condition costs strength without erasing the mood.
        const float mean = sum / float(n);
        const float strength = lowest + credit_ * (mean - lowest);
        return strength * confidence;
    }

    // How much a near miss counts, 0 (every condition must hold) to 1 (the average
    // of them). Starts at 0.35.
    float credit() const { return credit_; }
    void setCredit(float v) { credit_ = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

    void update(const MoodReadings& r, float dtSeconds) {
        const float dt = fmaxf(0.0f, dtSeconds);
        const bool present = r.value[RD_PRESENCE] >= 0.95f;

        // The firmware's own measurement starts when music is present and runs for
        // kAutoRoomSeconds of it. Silence in the window is not counted.
        if (roomMode_ == ROOM_WAIT && present) {
            measureBegin();
            autoMeasure_ = true;
            roomMode_ = ROOM_MEASURING;
        }
        if (measuring_ && present) {
            ++measureFrames_;
            for (int i = 0; i < RD_COUNT; ++i) {
                const double d = double(r.value[i]) - mean_[i];
                mean_[i] += d / double(measureFrames_);
                m2_[i] += d * (double(r.value[i]) - mean_[i]);
            }
            if (autoMeasure_) {
                autoSeconds_ += dt;
                if (autoSeconds_ >= kAutoRoomSeconds) measureEnd();
            }
        }
        for (int m = 0; m < MN_COUNT; ++m) {
            const float target = raw(m, r);
            const float tau = fmaxf(tau_[m], 1e-3f);
            strength_[m] += (target - strength_[m]) * (1.0f - expf(-dt / tau));
        }
    }

    // Flat parameter list: per mood its time constant, then lo and hi of each
    // condition. Rebuilt on demand from the static table, so it needs no storage.
    int paramCount() const {
        int n = 0;
        for (int m = 0; m < MN_COUNT; ++m) n += 1 + 2 * conditionCount(m);
        return n;
    }

    // Name of parameter i, valid until the next call.
    const char* paramName(int i) {
        int m, c, part;
        if (!locate(i, m, c, part)) return "";
        if (part == 0) {
            snprintf(nameBuf_, sizeof(nameBuf_), "%s time constant s", kMoodTable[m].name);
        } else {
            snprintf(nameBuf_, sizeof(nameBuf_), "%s %s %s", kMoodTable[m].name,
                     moodReadingName(kMoodTable[m].cond[c].reading), part == 1 ? "from" : "to");
        }
        return nameBuf_;
    }

    float param(int i) const {
        int m, c, part;
        if (!locate(i, m, c, part)) return 0.0f;
        return part == 0 ? tau_[m] : (part == 1 ? lo_[m][c] : hi_[m][c]);
    }

    void setParam(int i, float v) {
        int m, c, part;
        if (!locate(i, m, c, part)) return;
        if (part == 0) tau_[m] = v < 0.05f ? 0.05f : v;
        else if (part == 1) lo_[m][c] = v;
        else hi_[m][c] = v;
    }

    // The current values as the C++ rows of `kMoodTable`, for pasting back in.
    int formatTable(char* out, size_t size) const {
        size_t n = 0;
        out[0] = 0;
        for (int m = 0; m < MN_COUNT && n + 1 < size; ++m) {
            n += snprintf(out + n, size - n, "    { \"%s\", %s, %s, %.2ff, {", kMoodTable[m].name,
                          kMoodTable[m].inSelector ? "true" : "false",
                          kMoodTable[m].tone ? "true" : "false", tau_[m]);
            bool first = true;
            for (int c = 0; c < conditionCount(m) && n + 1 < size; ++c) {
                n += snprintf(out + n, size - n, "%s{RD_%s, %.3ff, %.3ff}", first ? " " : ", ",
                              upper(moodReadingName(kMoodTable[m].cond[c].reading)), lo_[m][c], hi_[m][c]);
                first = false;
            }
            n += snprintf(out + n, size - n, " } },\n");
        }
        n += snprintf(out + n, size - n, "\n");
        for (int i = 0; i < kRoomCount && n + 1 < size; ++i) {
            const MoodReading rd = kRoomDefault[i].reading;
            n += snprintf(out + n, size - n, "    {RD_%s, %.3ff, %.3ff},\n", upper(moodReadingName(rd)),
                          center_[rd], spread_[rd]);
        }
        return int(n);
    }

private:
    static float ramp(float v, float lo, float hi) {
        if (hi == lo) return v >= hi ? 1.0f : 0.0f;
        const float t = (v - lo) / (hi - lo);
        return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }

    // part 0: time constant, 1: lo, 2: hi
    static bool locate(int i, int& m, int& c, int& part) {
        if (i < 0) return false;
        for (m = 0; m < MN_COUNT; ++m) {
            const int span = 1 + 2 * conditionCount(m);
            if (i < span) {
                if (i == 0) { c = 0; part = 0; }
                else { c = (i - 1) / 2; part = 1 + ((i - 1) % 2); }
                return true;
            }
            i -= span;
        }
        return false;
    }

    const char* upper(const char* s) const {
        size_t k = 0;
        for (; s[k] && k + 1 < sizeof(upperBuf_); ++k) {
            upperBuf_[k] = (s[k] >= 'a' && s[k] <= 'z') ? char(s[k] - 32) : s[k];
        }
        upperBuf_[k] = 0;
        return upperBuf_;
    }

    float credit_ = 0.35f;
    float center_[RD_COUNT] = {};
    float spread_[RD_COUNT] = {};
    bool measuring_ = false;
    bool autoMeasure_ = false;
    RoomMode roomMode_ = ROOM_WAIT;
    float autoSeconds_ = 0.0f;
    int measureFrames_ = 0;
    double mean_[RD_COUNT] = {};
    double m2_[RD_COUNT] = {};
    float tau_[MN_COUNT];
    float lo_[MN_COUNT][kMoodMaxConditions];
    float hi_[MN_COUNT][kMoodMaxConditions];
    float strength_[MN_COUNT];
    char nameBuf_[64];
    mutable char upperBuf_[16];
};
