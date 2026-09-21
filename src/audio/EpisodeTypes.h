#pragma once
#include <stdint.h>

// The vocabulary of a structural episode, shared by the tracker that decides them
// (StructuralEpisodes.h) and the feature block that reports them (AudioFeatures.h).
// Values only, no logic, so both can include it without one depending on the other.

// The five structural signals. Buildup, descent and the drop window are the section
// group and at most one of them is open. Tease and anomaly are overlays.
enum StructSignal : uint8_t {
    SIG_BUILDUP = 0,
    SIG_DESCENT,
    SIG_DROP,
    SIG_TEASE,
    SIG_ANOMALY,
    SIG_COUNT
};

enum EpisodeState : uint8_t {
    EP_IDLE = 0,
    EP_ARMING,      // the hold is accumulating and nothing has been confirmed yet
    EP_ACTIVE,
    EP_FADING       // the condition is false, inside the window that still counts as one episode
};

enum EpisodeEnd : uint8_t {
    END_NONE = 0,   // has not ended yet
    END_WINDOW,     // the condition stayed false for the whole window
    END_DROP,       // a drop arrived and ended the section
    END_REPLACED,   // another section took over
    END_GATE,       // the gate closed, or the source changed
    END_FADED,      // the payoff stopped holding
    END_TIMEOUT,    // the safety bound
    END_RESOLVED,   // a tease whose withheld arrival happened
    END_IMPACT,     // an onset that was not followed by a payoff
    END_RELEASED    // an overlay whose own hold released
};

enum EventKind : uint8_t {
    EVT_STARTED = 0,
    EVT_ENDED,
    EVT_TRIGGERED,  // a point event: the drop onset
    EVT_CONFIRMED   // a provisional drop window turned out to be a payoff
};

// One record in the ring. `seq` counts up from 1 and never goes backwards, so a
// reader that remembers the newest seq it has seen never repeats or loses one that
// is still in the ring. `atMs` is sample time and restarts from zero on a source
// change, which is why seq and not atMs is the ordering key.
struct EpisodeEvent {
    uint32_t seq        = 0;
    uint8_t  signal     = SIG_BUILDUP;
    uint8_t  kind       = EVT_STARTED;
    uint8_t  reason     = END_NONE;     // meaningful on an end
    uint8_t  confirmed  = 0;            // drop window: confirmed at the end, or at the start if known
    uint32_t atMs       = 0;
    uint32_t durationMs = 0;            // on an end, and zero otherwise
    float    value      = 0.0f;         // preparation on a drop onset, zero otherwise
};

// What a reader can see of one signal. Defaults are an idle signal that has never
// ended, so a hand-built AudioFeatures in the harness is valid without setting it.
struct EpisodeStatus {
    uint8_t  state         = EP_IDLE;
    uint8_t  lastEndReason = END_NONE;
    uint32_t episodeId     = 0;         // changes on every new episode, 0 before the first
    uint32_t elapsedMs     = 0;         // the open episode, and zero when idle
    uint32_t lastDurationMs = 0;
    uint32_t sinceEndMs    = 0;         // since the last end, zero when it has never ended
};
