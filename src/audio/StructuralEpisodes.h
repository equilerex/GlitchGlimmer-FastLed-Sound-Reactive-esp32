#pragma once
#include "../config/Config.h"
#include "EpisodeTypes.h"
#include "AudioFeatures.h"

// Turns the structural detectors' per-frame flags into episodes: a start, a state
// that develops, an end, a duration and the reason it ended. The firmware decides
// all of it. A reader (the page, the scene director) gets the state and the event
// ring and derives nothing.
//
// A flag says a condition holds this block and an episode says a section began and
// ended. They differ because the flags flicker: a buildup can thin out for a bar, a
// tease flag has no hold of its own. The windows in Config.h say how long a flag
// may stay false before the episode is called over.
//
// Time is the analyser's sample time. It is passed in and never read here, and a
// timestamp that repeats or goes backwards is clamped so no duration goes negative.
//
// Fixed storage, no heap, C++11: this is device-reachable.
class StructuralEpisodes {
public:
    static const int RING = 32;

    // Everything the tracker reads for one block. A plain struct so the harness can
    // script the detectors and test every rule without producing audio that happens
    // to trip them.
    struct Inputs {
        unsigned long now            = 0;
        float         level          = 0.0f;
        float         displacement   = 0.0f;

        bool          buildupActive  = false;
        unsigned long buildupHoldSince = 0;     // 0 = no hold in progress
        bool          descentActive  = false;
        unsigned long descentHoldSince = 0;

        bool          dropOnset      = false;
        float         dropArrival    = 0.0f;    // displacement at the onset

        bool          tease          = false;
        bool          anomaly        = false;
    };

    // The windows, settable at run time so the page can send a number and the
    // firmware decides what it means. Defaults are the Config.h starting guesses.
    unsigned long sectionWindowMs = STRUCT_SECTION_WINDOW_MS;
    unsigned long teaseWindowMs   = STRUCT_TEASE_WINDOW_MS;
    unsigned long dropMaxMs       = STRUCT_DROP_MAX_MS;
    float         dropHoldFraction = STRUCT_DROP_HOLD_FRACTION;

    StructuralEpisodes() { clearEpisodes(); }

    // Run one block. Order matters and is the precedence: an onset is handled before
    // the flags, so the section it ends never gets a frame as merely fading.
    void update(const Inputs& in) {
        unsigned long now = in.now < lastNow ? lastNow : in.now;
        lastNow = now;
        displacement = in.displacement;

        bool onsetNow = false;
        if (in.dropOnset) {
            onset(now, in.dropArrival);
            onsetNow = true;
        }

        section(SIG_BUILDUP, in.buildupActive, in.buildupHoldSince, BUILDUP_HOLD_MS, now);
        section(SIG_DESCENT, in.descentActive, in.descentHoldSince, DESCENT_HOLD_MS, now);
        dropWindow(in.level, now);

        // An arrival resolves a tease, so the payoff is not itself read as tension.
        // The flag stays true for TEASE_POST_DROP_MS after a drop, and without this a
        // tease would be closed by the onset and reopen on the next block.
        overlay(SIG_TEASE, in.tease && !dropOpen && !onsetNow, teaseWindowMs, END_WINDOW, now);
        overlay(SIG_ANOMALY, in.anomaly, 0, END_RELEASED, now);

        arming = 0.0f;
        for (int s = SIG_BUILDUP; s <= SIG_DESCENT; ++s) {
            if (ep[s].state == EP_ARMING && ep[s].armProgress > arming) arming = ep[s].armProgress;
        }
    }

    // The gate is not music, and a gate below GATE_SETTLED ends every open episode
    // so silence is not read as a section change. Also the source-change path, which
    // restarts the sample clock, so the clamp against a backwards timestamp is
    // released here.
    void clearEpisodes() {
        endAll(END_GATE);
        for (int s = 0; s < SIG_COUNT; ++s) {
            ep[s].state = EP_IDLE;
            ep[s].armProgress = 0.0f;
        }
        dropOpen = false;
        lastNow = 0;
        displacement = 0.0f;
        arming = 0.0f;
    }

    // Write the state into a feature block. `now` is the same sample time update()
    // was given. Safe to call on a block the gate closed, where update() did not run.
    void fill(AudioFeatures& f, unsigned long now) const {
        if (now < lastNow) now = lastNow;
        for (int s = 0; s < SIG_COUNT; ++s) {
            const Ep& e = ep[s];
            EpisodeStatus& o = f.episode[s];
            o.state          = e.state;
            o.episodeId      = e.id;
            o.lastEndReason  = e.lastReason;
            o.lastDurationMs = e.lastDuration;
            o.sinceEndMs     = e.lastReason != END_NONE ? span(e.lastEndAt, now) : 0;
            // Duration excludes the waiting window, so a fading episode reports up
            // to its last active frame.
            if (e.state == EP_ACTIVE)      o.elapsedMs = span(e.start, now);
            else if (e.state == EP_FADING) o.elapsedMs = span(e.start, e.lastActive);
            else                           o.elapsedMs = 0;
        }
        f.displacement   = displacement;
        f.arming         = arming;
        f.dropConfirmed  = dropOpen && dropConfirmedFlag;
        f.dropConfidence = dropOpen ? confidence() : 0.0f;
    }

    // ---- the event ring ------------------------------------------------------
    uint32_t newestSeq() const { return nextSeq - 1; }
    uint32_t oldestSeq() const {
        if (nextSeq == 1) return 0;
        return nextSeq > uint32_t(RING) ? nextSeq - uint32_t(RING) : 1;
    }
    // False when seq was never written or has already been overwritten.
    bool eventBySeq(uint32_t seq, EpisodeEvent& out) const {
        if (seq == 0 || seq >= nextSeq || seq < oldestSeq()) return false;
        out = ring[seq % uint32_t(RING)];
        return true;
    }

private:
    struct Ep {
        uint8_t       state       = EP_IDLE;
        uint32_t      id          = 0;
        unsigned long start       = 0;
        unsigned long lastActive  = 0;
        uint32_t      lastDuration = 0;
        unsigned long lastEndAt   = 0;
        uint8_t       lastReason  = END_NONE;
        float         armProgress = 0.0f;
    };

    static unsigned long span(unsigned long from, unsigned long to) {
        return to > from ? to - from : 0;
    }

    Ep ep[SIG_COUNT];
    EpisodeEvent ring[RING];
    uint32_t nextSeq = 1;
    unsigned long lastNow = 0;
    float displacement = 0.0f;
    float arming = 0.0f;

    // Drop window. `ep[SIG_DROP]` carries the shared fields, and these are what only
    // a drop window has.
    bool          dropOpen          = false;
    bool          dropConfirmedFlag = false;
    float         dropPreparation   = 0.0f;
    float         dropArrivalScore  = 0.0f;
    float         plateauSum        = 0.0f;
    int           plateauCount      = 0;
    bool          plateauKnown      = false;
    float         plateau           = 0.0f;
    unsigned long lastPayoffMs      = 0;      // last frame at or above STRUCT_DROP_CONFIRM_LEVEL

    void push(uint8_t signal, uint8_t kind, unsigned long at, unsigned long duration,
              uint8_t reason, uint8_t confirmed, float value) {
        EpisodeEvent& r = ring[nextSeq % uint32_t(RING)];
        r.seq        = nextSeq;
        r.signal     = signal;
        r.kind       = kind;
        r.reason     = reason;
        r.confirmed  = confirmed;
        r.atMs       = uint32_t(at);
        r.durationMs = uint32_t(duration);
        r.value      = value;
        ++nextSeq;
    }

    static bool isOpen(const Ep& e) { return e.state == EP_ACTIVE || e.state == EP_FADING; }

    // `at` is the stamp of the last active frame, not the moment the end was
    // decided, so the wait adds latency and not duration.
    void end(int s, unsigned long at, uint8_t reason) {
        Ep& e = ep[s];
        if (!isOpen(e)) return;
        if (at < e.start) at = e.start;
        e.lastDuration = uint32_t(span(e.start, at));
        e.lastEndAt    = at;
        e.lastReason   = reason;
        e.state        = EP_IDLE;
        e.armProgress  = 0.0f;
        push(uint8_t(s), EVT_ENDED, at, e.lastDuration, reason,
             s == SIG_DROP && dropConfirmedFlag ? 1 : 0, 0.0f);
        if (s == SIG_DROP) dropOpen = false;
    }

    void open(int s, unsigned long start, unsigned long now) {
        Ep& e = ep[s];
        if (start > now) start = now;
        e.state       = EP_ACTIVE;
        e.start       = start;
        e.lastActive  = now;
        e.armProgress = 0.0f;
        ++e.id;
        push(uint8_t(s), EVT_STARTED, start, 0, END_NONE, 0, 0.0f);
    }

    void endAll(uint8_t reason) {
        for (int s = 0; s < SIG_COUNT; ++s) end(s, ep[s].lastActive, reason);
    }

    // Buildup and descent. A new one replaces the other and a drop window, since a
    // section group has at most one open member.
    void section(int s, bool active, unsigned long holdSince, unsigned long holdMs,
                 unsigned long now) {
        Ep& e = ep[s];
        switch (e.state) {
        case EP_IDLE:
        case EP_ARMING:
            if (active) {
                const int other = s == SIG_BUILDUP ? SIG_DESCENT : SIG_BUILDUP;
                end(other, ep[other].lastActive, END_REPLACED);
                end(SIG_DROP, ep[SIG_DROP].lastActive, END_REPLACED);
                // A hold is only confirmable while it runs, so the episode is stamped
                // at the moment the hold began.
                open(s, holdSince != 0 ? holdSince : now, now);
            } else if (holdSince != 0) {
                e.state = EP_ARMING;
                const unsigned long held = span(holdSince, now);
                e.armProgress = held >= holdMs ? 1.0f : float(held) / float(holdMs);
            } else {
                e.state = EP_IDLE;
                e.armProgress = 0.0f;
            }
            break;
        case EP_ACTIVE:
            if (active) e.lastActive = now;
            else        e.state = EP_FADING;
            break;
        case EP_FADING:
            if (active) {
                e.state = EP_ACTIVE;
                e.lastActive = now;
            } else if (span(e.lastActive, now) >= sectionWindowMs) {
                end(s, e.lastActive, END_WINDOW);
            }
            break;
        }
    }

    // Tease and anomaly. They can overlap a section and are never ended by one.
    void overlay(int s, bool flag, unsigned long windowMs, uint8_t windowReason,
                 unsigned long now) {
        Ep& e = ep[s];
        switch (e.state) {
        case EP_IDLE:
        case EP_ARMING:
            if (flag) open(s, now, now);
            break;
        case EP_ACTIVE:
            if (flag) e.lastActive = now;
            else if (windowMs == 0) end(s, e.lastActive, windowReason);
            else e.state = EP_FADING;
            break;
        case EP_FADING:
            if (flag) {
                e.state = EP_ACTIVE;
                e.lastActive = now;
            } else if (span(e.lastActive, now) >= windowMs) {
                end(s, e.lastActive, windowReason);
            }
            break;
        }
    }

    // How prepared the arrival was: whether a section was open and how strong it had
    // been. Raises confidence and is never required.
    float preparation() const {
        const Ep& b = ep[SIG_BUILDUP];
        const Ep& d = ep[SIG_DESCENT];
        if (isOpen(b)) {
            const float held = float(span(b.start, b.lastActive));
            // Long enough to be a buildup and not the tail of a swell.
            const float lengthShare = held >= 6000.0f ? 1.0f : held / 6000.0f;
            return 0.6f + 0.4f * lengthShare;
        }
        // A breakdown before a drop is the other usual preparation, and a weaker one.
        if (isOpen(d)) return 0.4f;
        return 0.0f;
    }

    void onset(unsigned long now, float arrival) {
        dropPreparation = preparation();

        // The arrival ends the section it released, and resolves a tease.
        end(SIG_BUILDUP, ep[SIG_BUILDUP].lastActive, END_DROP);
        end(SIG_DESCENT, ep[SIG_DESCENT].lastActive, END_DROP);
        end(SIG_TEASE,   ep[SIG_TEASE].lastActive,   END_RESOLVED);
        end(SIG_DROP,    ep[SIG_DROP].lastActive,    END_REPLACED);

        const float over = (arrival - DROP_SCALE) / DROP_SCALE;
        dropArrivalScore = over < 0.0f ? 0.0f : (over > 1.0f ? 1.0f : over);

        push(SIG_DROP, EVT_TRIGGERED, now, 0, END_NONE, 0, dropPreparation);

        dropOpen          = true;
        dropConfirmedFlag = dropPreparation >= STRUCT_DROP_PREP_CONFIRM;
        plateauSum        = 0.0f;
        plateauCount      = 0;
        plateauKnown      = false;
        plateau           = 0.0f;
        lastPayoffMs      = now;
        open(SIG_DROP, now, now);
        ep[SIG_DROP].lastActive = now;
        // open() wrote the started record before the confirmation was known here.
        ring[(nextSeq - 1) % uint32_t(RING)].confirmed = dropConfirmedFlag ? 1 : 0;
    }

    // ---- the drop window's provisional tests ---------------------------------
    // Each is behind its own function so it can be replaced without touching the
    // state machine, and nothing outside this class depends on which is in use. The
    // first implementation compares `level` with the plateau it held in the seconds
    // after the onset. The intended judgement is by intensity, bass weight,
    // activity, pulse, spectral character and structural change.

    // Is the music still behaving like the payoff it opened with? Always true until
    // there is a plateau to compare against.
    bool payoffHolding(float level) const {
        return !plateauKnown || level >= plateau * dropHoldFraction;
    }

    // Was the payoff sustained, so an onset with weak preparation is a drop and not
    // an impact?
    bool payoffSustained() const {
        return plateauKnown && plateau >= STRUCT_DROP_CONFIRM_LEVEL;
    }

    void dropWindow(float level, unsigned long now) {
        if (!dropOpen) return;
        Ep& e = ep[SIG_DROP];

        if (level >= STRUCT_DROP_CONFIRM_LEVEL) lastPayoffMs = now;

        if (!plateauKnown) {
            plateauSum += level;
            ++plateauCount;
            if (span(e.start, now) >= STRUCT_DROP_PLATEAU_MS) {
                plateauKnown = true;
                plateau = plateauSum / float(plateauCount);
            }
        }

        if (span(e.start, now) >= dropMaxMs) {
            end(SIG_DROP, now, END_TIMEOUT);
            return;
        }

        if (plateauKnown && !dropConfirmedFlag) {
            if (payoffSustained()) {
                dropConfirmedFlag = true;
                push(SIG_DROP, EVT_CONFIRMED, now, span(e.start, now), END_NONE, 1, 0.0f);
            } else {
                // An arrival with no payoff. Stamped at the last frame that looked
                // like one, so the record is an impact and not a long window.
                end(SIG_DROP, lastPayoffMs < e.start ? e.start : lastPayoffMs, END_IMPACT);
                return;
            }
        }

        if (payoffHolding(level)) {
            e.state = EP_ACTIVE;
            e.lastActive = now;
        } else {
            e.state = EP_FADING;
            if (span(e.lastActive, now) >= sectionWindowMs) {
                end(SIG_DROP, e.lastActive, END_FADED);
            }
        }
    }

    // Provisional: how far the arrival cleared its conditions, how prepared it was,
    // and once known, how strong the payoff turned out to be. Weights are a first
    // guess and are settled against the harness and then a real capture.
    float confidence() const {
        const float evidence = plateauKnown ? (plateau > 1.0f ? 1.0f : plateau) : 0.0f;
        const float c = 0.25f * dropArrivalScore + 0.45f * dropPreparation + 0.30f * evidence;
        return c > 1.0f ? 1.0f : c;
    }
};
