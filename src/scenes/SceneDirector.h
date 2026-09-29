#pragma once

/*--------------------------------------------------------------------
 *  Includes – only the headers that give COMPLETE definitions
 *------------------------------------------------------------------*/
#include <vector>
#include "../audio/AudioFeatures.h"
#include "../config/Config.h"
#include "../scenes/SceneRegistry.h"   // gives SceneDefinition
#include "../scenes/SceneState.h"      // gives SceneState
#include "../scenes/LayerManager.h"    // needed for feedLayers
#include "../animations/AnimationFit.h"

/*--------------------------------------------------------------------
 *  Why the running scene is up. Shown by the emulator.
 *------------------------------------------------------------------*/
enum SceneReason : uint8_t {
    REASON_START = 0,    // the first pick
    REASON_ROTATION,     // the scene ran its ideal duration and the bucket was drawn again
    REASON_EARLY,        // the scene sat outside the bucket long enough to be replaced
    REASON_DROP,         // a confirmed drop took the base
    REASON_DROP_END,     // the drop base ended and the bucket was drawn
    REASON_FORCED,       // switchAllAnimations, the page's next button
    REASON_LOCKED        // the page locked a scene
};

/*--------------------------------------------------------------------
 *  SceneDirector – decides which Scene runs and feeds layers
 *
 *  The base scene follows the moods the firmware measures. Each frame every
 *  scene is scored against them (SceneRegistry::score), the best few form the
 *  bucket, and the scene is drawn from it. It rotates at the ideal duration,
 *  changes early when it has left the bucket, and is replaced by a drop
 *  animation only on the rising edge of a confirmed drop. Loudness chooses
 *  nothing. See _architecture/plans/2026-09-28-base-layer-mood.md.
 *------------------------------------------------------------------*/
class SceneDirector {
public:
    // Dials for the drop base, live in the emulator.
    struct DropParams {
        unsigned long holdMinMs = 4500;     // the drop base holds at least this long
        unsigned long holdCapMs = 12000;    // and is forced to end by this
        unsigned long spacingMs = DROP_COOLDOWN_MS;  // least between two drop bases
    };

    // What the emulator reads about the drop base.
    struct DropHoldStatus {
        bool          holding = false;
        bool          shiftSeen = false;
        unsigned long minLeftMs = 0;
        unsigned long capLeftMs = 0;
    };

private:
    SceneState*    state    = nullptr;   // owned elsewhere
    SceneRegistry& registry;
    unsigned long  lastScenePrint = 0;

    SelectionParams selection;
    DropParams      dropParams;
    uint32_t        rng = 0x1F2E3D4Cu;

    // Recomputed every update, so the emulator can list them and the early-change
    // rule can ask whether the running scene is in the bucket.
    std::vector<SceneScore> scores;
    std::vector<int>        bucket;

    MusicState     lastMusic;             // for the draws that are not made from update()
    float          lastBpm = 0.0f;
    float          lastLevel = 0.0f;
    float          lastDynamics = 0.0f;

    int            lockedSceneIndex = -1;
    SceneReason    reason = REASON_START;
    bool           reasonQuiet = false;

    // How long the running scene has been out of the bucket.
    bool           outOfBucket = false;
    unsigned long  outSince = 0;

    // The drop base.
    bool           prevDropConfirmed = false;
    bool           haveDropBase = false;
    unsigned long  lastDropBaseMs = 0;
    bool           dropHolding = false;
    unsigned long  dropStartMs = 0;
    bool           dropShiftSeen = false;
    float          dropRefActivity = 0.0f;
    float          dropRefPulse = 0.0f;
    float          dropRefBpm = 0.0f;

    // Episode edges. A new open buildup or descent is a start, its return to idle
    // an end. `edgeSeq` counts frames that had an edge and each strip's manager
    // remembers the last it answered.
    bool           edgeOpen[2] = {false, false};
    uint8_t        edgePrevState[2] = {EP_IDLE, EP_IDLE};
    uint8_t        edgeMask = 0;            // bit 0/1 start of buildup/descent, bit 2/3 end
    uint32_t       edgeSeq = 0;

    // A joint jump in the mood strengths (concept 14). The strengths are compared
    // with a slow copy of themselves.
    float          moodSlow[MN_COUNT] = {};
    bool           moodSlowSeeded = false;
    unsigned long  moodSlowMs = 0;
    bool           jumpPrev = false;
    unsigned long  lastJumpMs = 0;
    uint32_t       jumpSeq = 0;

    static constexpr unsigned long kChallengeMs   = 1500;   // how long a scene may sit outside the bucket
    static constexpr unsigned long kJumpCooldownMs = 2500;
    static constexpr unsigned long kLiveCooldownMs = 6000;
    static constexpr unsigned long kLiveDurationMs = 5000;
    static constexpr float         kLiveMinScore = 0.60f;

    static inline bool episodeOpen(const EpisodeStatus& e) {
        return e.state == EP_ACTIVE || e.state == EP_FADING;
    }

    inline void switchTo(const SceneDefinition& next, SceneReason why) {
        state->beginScene(&next, lastBpm, lastLevel, lastDynamics);
        reason = why;
        reasonQuiet = lastMusic.mood[MN_QUIET] >= 0.5f;
        outOfBucket = false;
    }

    inline void remember(const AudioFeatures& f) {
        lastMusic = f.music;
        lastBpm = f.bpm;
        lastLevel = f.level;
        lastDynamics = f.dynamics;
    }

    inline void rescore() {
        registry.score(*state, lastMusic, selection, scores);
        registry.buildBucket(scores, selection, bucket);
    }

    inline void drawBase(SceneReason why) {
        switchTo(registry.drawFromBucket(*state, scores, bucket, rng), why);
    }

    // The drop base looks at the sound as it was when the drop was confirmed.
    inline void startDropBase(const AudioFeatures& f, unsigned long t) {
        switchTo(registry.pickDropBase(*state, scores), REASON_DROP);
        dropHolding = true;
        dropStartMs = t;
        haveDropBase = true;
        lastDropBaseMs = t;
        dropShiftSeen = false;
        dropRefActivity = f.music.activity.value;
        dropRefPulse = f.music.pulse.value;
        dropRefBpm = f.bpm;
    }

    // A drastic shift is a joint change across readings (concept 14): the sound
    // cuts off, the rhythm breaks, or the tempo moves.
    inline bool dropShift(const AudioFeatures& f) const {
        const bool cutOff = f.gateGain < 0.4f ||
            (dropRefActivity > 0.3f && f.music.activity.value < dropRefActivity * 0.35f);
        const bool broke = dropRefPulse > 0.4f && f.music.pulse.value < dropRefPulse * 0.5f;
        const bool moved = dropRefBpm > 1.0f && f.bpm > 1.0f && fabsf(f.bpm - dropRefBpm) > 15.0f;
        return cutOff || broke || moved;
    }

    inline void trackEdges(const AudioFeatures& f) {
        edgeMask = 0;
        const uint8_t sig[2] = {SIG_BUILDUP, SIG_DESCENT};
        for (int i = 0; i < 2; ++i) {
            const EpisodeStatus& e = f.episode[sig[i]];
            if (episodeOpen(e) && !edgeOpen[i]) {
                edgeOpen[i] = true;
                edgeMask |= uint8_t(1u << i);
            } else if (e.state == EP_IDLE && edgePrevState[i] != EP_IDLE && edgeOpen[i]) {
                edgeOpen[i] = false;
                // An episode a drop ended already has the drop's own layers, and one
                // the gate ended is silence, which needs no accent.
                if (e.lastEndReason != END_DROP && e.lastEndReason != END_GATE) {
                    edgeMask |= uint8_t(4u << i);
                }
            } else if (e.state == EP_IDLE) {
                edgeOpen[i] = false;
            }
            edgePrevState[i] = e.state;
        }
        if (edgeMask != 0) ++edgeSeq;
    }

    inline void trackMoodJump(const AudioFeatures& f, unsigned long t) {
        if (!moodSlowSeeded) {
            for (int m = 0; m < MN_COUNT; ++m) moodSlow[m] = f.music.mood[m];
            moodSlowSeeded = true;
            moodSlowMs = t;
            return;
        }
        float dt = float(t - moodSlowMs) * 0.001f;
        moodSlowMs = t;
        if (dt > 0.1f) dt = 0.1f;
        const float alpha = 1.0f - expf(-dt / 1.5f);
        float total = 0.0f;
        int moved = 0;
        for (int m = 0; m < MN_COUNT; ++m) {
            const float d = fabsf(f.music.mood[m] - moodSlow[m]);
            total += d;
            if (d >= 0.20f) ++moved;
            moodSlow[m] += (f.music.mood[m] - moodSlow[m]) * alpha;
        }
        const bool jump = total >= 0.60f && moved >= 2;
        if (jump && !jumpPrev && t - lastJumpMs >= kJumpCooldownMs) {
            lastJumpMs = t;
            ++jumpSeq;
        }
        jumpPrev = jump;
    }

public:
    inline SceneDirector(SceneRegistry& r)
        : state(nullptr), registry(r) {}

    /*-------------------- one-time wiring --------------------*/
    inline void attachState(SceneState* s) { state = s; }

    inline void begin() {
        if (!state) return;
        rescore();
        drawBase(REASON_START);
    }

    // Forget what was measured against audio that has stopped: the drop base, the
    // episode edges and the slow copy of the mood strengths. Called with the
    // analysis reset, on a change of input.
    inline void reset() {
        prevDropConfirmed = false;
        haveDropBase = false;
        dropHolding = false;
        outOfBucket = false;
        edgeOpen[0] = edgeOpen[1] = false;
        edgePrevState[0] = edgePrevState[1] = EP_IDLE;
        edgeMask = 0;
        moodSlowSeeded = false;
        jumpPrev = false;
    }

    /*-------------------- tuning and inspection --------------------*/
    inline void seed(uint32_t value) { rng = value == 0 ? 0x1F2E3D4Cu : value; }
    inline SelectionParams& selectionForTuning() { return selection; }
    inline const SelectionParams& selectionParams() const { return selection; }
    inline DropParams& dropForTuning() { return dropParams; }
    inline const DropParams& dropParamsRef() const { return dropParams; }

    inline const std::vector<SceneScore>& sceneScores() const { return scores; }
    inline const std::vector<int>& bucketIndices() const { return bucket; }
    inline SceneReason sceneReason() const { return reason; }
    inline bool reasonWasQuiet() const { return reasonQuiet; }

    inline const char* reasonText() const {
        switch (reason) {
            case REASON_START:    return reasonQuiet ? "start, quiet" : "start";
            case REASON_ROTATION: return reasonQuiet ? "rotation, quiet" : "rotation";
            case REASON_EARLY:    return reasonQuiet ? "left the bucket, quiet" : "left the bucket";
            case REASON_DROP:     return "drop";
            case REASON_DROP_END: return reasonQuiet ? "drop ended, quiet" : "drop ended";
            case REASON_FORCED:   return "next";
            case REASON_LOCKED:   return "locked";
        }
        return "";
    }

    inline DropHoldStatus dropHold() const {
        DropHoldStatus s;
        s.holding = dropHolding;
        if (dropHolding && state) {
            const unsigned long el = millis() - dropStartMs;
            s.shiftSeen = dropShiftSeen;
            s.minLeftMs = el >= dropParams.holdMinMs ? 0 : dropParams.holdMinMs - el;
            s.capLeftMs = el >= dropParams.holdCapMs ? 0 : dropParams.holdCapMs - el;
        }
        return s;
    }

    /*-------------------- regular update --------------------*/
    // Reads the frame's features. It never advances anything the controller owns.
    inline void update(const AudioFeatures& f) {
        if (!state) return;
        remember(f);
        rescore();
        trackEdges(f);
        trackMoodJump(f, millis());

        if (lockedSceneIndex >= 0) {
            outOfBucket = false;
            return;
        }

        const unsigned long t  = millis();
        const unsigned long el = t - state->sceneStartMillis;

        // The drop. Only a confirmed drop changes the base, on its rising edge, and
        // not twice inside the spacing. While the window is open and unconfirmed the
        // drop adds layers and the base stays.
        const bool rising = f.dropConfirmed && !prevDropConfirmed;
        prevDropConfirmed = f.dropConfirmed;
        if (rising && !dropHolding &&
            (!haveDropBase || t - lastDropBaseMs >= dropParams.spacingMs)) {
            startDropBase(f, t);
            return;
        }

        if (dropHolding) {
            const unsigned long held = t - dropStartMs;
            if (dropShift(f)) dropShiftSeen = true;
            if (held >= dropParams.holdCapMs ||
                (dropShiftSeen && held >= dropParams.holdMinMs)) {
                dropHolding = false;
                drawBase(REASON_DROP_END);
            }
            return;
        }

        if (el <= (unsigned long)state->sceneMinDurationMs) {
            outOfBucket = false;
            return;
        }

        // Past the minimum. A scene that has left the bucket is replaced once it
        // has stayed out for kChallengeMs.
        const int running = registry.findIndex(state->activeScene);
        const bool inBucket = running >= 0 && running < int(scores.size()) && scores[running].inBucket;
        if (!inBucket) {
            if (!outOfBucket) {
                outOfBucket = true;
                outSince = t;
            } else if (t - outSince >= kChallengeMs) {
                drawBase(REASON_EARLY);
                return;
            }
        } else {
            outOfBucket = false;
        }

        // A scene that is still in the bucket may stay, but not forever.
        if (el > (unsigned long)state->sceneIdealDurationMs) drawBase(REASON_ROTATION);
    }

    /*-------------------- episode layers --------------------*/
    // Attach the layer an open episode wants, once. The drop onset is the exception:
    // its one-shots run their own attack, hold and decay and are not bound to
    // anything, so they are fired once per episode per strip and left to expire.
    inline void attachEpisodeLayers(LayerManager& lm, const AudioFeatures& af) {
        const EpisodeStatus& drop = af.episode[SIG_DROP];

        if (drop.episodeId != 0 && drop.episodeId != lm.impactFiredFor()) {
            // Scaled by how sure the firmware is at the onset. Confidence is
            // provisional, so it only sets how hard the flash hits.
            const float hit = 0.6f + 0.4f * af.dropConfidence;
            lm.addOwnedLayerByType(LayerType::HIGHLIGHT, LayerClass::IMPACT, -1, 0, hit, 2500);
            lm.addOwnedLayerByType(LayerType::ENERGY, LayerClass::IMPACT, -1, 0, hit, 3500);
            lm.markImpactFired(drop.episodeId);
        }

        const EpisodeStatus& build = af.episode[SIG_BUILDUP];
        if (episodeOpen(build) && !lm.hasOwned(SIG_BUILDUP, build.episodeId)) {
            lm.addOwnedLayerByType(LayerType::BUILDUP_SWELL, LayerClass::SECTION,
                                   SIG_BUILDUP, build.episodeId);
        }

        const EpisodeStatus& fall = af.episode[SIG_DESCENT];
        if (episodeOpen(fall) && !lm.hasOwned(SIG_DESCENT, fall.episodeId)) {
            lm.addOwnedLayerByType(LayerType::DESCENT_COOL, LayerClass::SECTION,
                                   SIG_DESCENT, fall.episodeId);
        }

        // The sustained payoff, only once the window is confirmed. A window closed
        // as an impact never gets one, and a provisional window drives the one-shots
        // alone.
        if (episodeOpen(drop) && af.dropConfirmed && !lm.hasOwned(SIG_DROP, drop.episodeId)) {
            lm.addOwnedLayerByType(LayerType::ENERGY_SPIRAL, LayerClass::SECTION,
                                   SIG_DROP, drop.episodeId);
        }

        const EpisodeStatus& tease = af.episode[SIG_TEASE];
        if (episodeOpen(tease) && !lm.hasOwned(SIG_TEASE, tease.episodeId)) {
            lm.addOwnedLayerByType(LayerType::MOOD_ARC, LayerClass::OVERLAY,
                                   SIG_TEASE, tease.episodeId);
        }

        const EpisodeStatus& odd = af.episode[SIG_ANOMALY];
        if (episodeOpen(odd) && !lm.hasOwned(SIG_ANOMALY, odd.episodeId)) {
            lm.addOwnedLayerByType(LayerType::DYNAMICS_FLICKER_STORM, LayerClass::OVERLAY,
                                   SIG_ANOMALY, odd.episodeId);
        }
    }

    /*-------------------- edge, beat, shift and mood layers --------------------*/
    // What the director adds to one strip each frame, on top of the scene's fixed
    // layers. One meaning per layer: MOOD_ARC is tease, DYNAMICS_FLICKER_STORM is
    // anomaly, HIGHLIGHT with ENERGY is the drop onset, a glow wipe opens a
    // buildup or descent and a sparkle closes it.
    inline void feedLayers(LayerManager& lm, const AudioFeatures& af, unsigned long now) {
        constexpr int MAX_LAYERS = 4;

        // Structural layers first, and ahead of the cap test: a full manager makes
        // room for a layer that outranks something in it, so an episode is answered
        // even when the scene already has its layers up.
        attachEpisodeLayers(lm, af);

        // The edges of a buildup or a descent, once per strip.
        if (lm.edgeSeenSeq != edgeSeq) {
            lm.edgeSeenSeq = edgeSeq;
            for (int bit = 0; bit < 2; ++bit) {
                if (edgeMask & (1u << bit)) {
                    lm.addLayerByType(LayerType::CENTROID_GLOW_WIPE, 700, LayerClass::ACCENT, LayerWhy::EDGE);
                }
                if (edgeMask & (4u << bit)) {
                    lm.addLayerByType(LayerType::TRANSITION, 600, LayerClass::ACCENT, LayerWhy::EDGE);
                }
            }
        }

        // A joint jump in the mood strengths, in place of the contrast accent that
        // read the level.
        if (lm.jumpSeenSeq != jumpSeq) {
            lm.jumpSeenSeq = jumpSeq;
            lm.addLayerByType(LayerType::HIGHLIGHT, 450, LayerClass::ACCENT, LayerWhy::SHIFT);
        }

        if (lm.activeCount() >= MAX_LAYERS) return;

        // A beat accent. Trusted only when the tracker has a solid lock (>= 0.70
        // confidence), a punchy 450 ms pop that expires promptly. No coin flip: the
        // lock is the gate.
        if (af.beatDetected && af.beatConfidence >= 0.70f && now - lm.lastBeatMs > 1500) {
            lm.addLayerByType(LayerType::REACTIVE, 450, LayerClass::ACCENT, LayerWhy::BEAT);
            lm.lastBeatMs = now;
        }

        // A free slot goes to the live layer that fits the moods best, when one fits
        // well enough and it is not already up.
        if (lm.activeCount() < MAX_LAYERS - 1 && now - lm.lastLiveMs > kLiveCooldownMs) {
            int best = -1;
            float bestScore = kLiveMinScore;
            for (int i = 0; i < kLiveLayerFitCount; ++i) {
                if (lm.hasActiveLayerOfType(kLiveLayerFits[i].type)) continue;
                const float s = registry.moodTerm(kLiveLayerFits[i].fit, af.music, selection.toneWeight);
                if (s > bestScore) { bestScore = s; best = i; }
            }
            if (best >= 0) {
                lm.addLayerByType(kLiveLayerFits[best].type, kLiveDurationMs,
                                  LayerClass::ACCENT, LayerWhy::MOOD);
            }
            lm.lastLiveMs = now;
        }
    }

    /*-------------------- convenience getters --------------------*/
    inline const SceneDefinition* getActiveScene() const {
        return state ? state->activeScene : nullptr;
    }
    inline String getCurrentSceneName() const {
        return state && state->activeScene
               ? String(state->activeScene->name) : F("None");
    }
    inline void forceNextScene() {
        if (!state) return;
        rescore();
        drawBase(REASON_FORCED);
    }

    /*-------------------- scene lock / freeze --------------------*/
    inline void lockScene(int index) {
        if (index >= 0 && index < static_cast<int>(registry.count())) {
            lockedSceneIndex = index;
            dropHolding = false;
            switchTo(registry.get(index), REASON_LOCKED);
        } else {
            unlockScene();
        }
    }

    inline void unlockScene() {
        if (lockedSceneIndex >= 0 && state) {
            state->sceneStartMillis = millis();
        }
        lockedSceneIndex = -1;
        outOfBucket = false;
    }

    inline int getLockedSceneIndex() const {
        return lockedSceneIndex;
    }

    inline bool isSceneLocked() const {
        return lockedSceneIndex >= 0;
    }

    inline int getCurrentSceneIndex() const {
        if (!state || !state->activeScene) return -1;
        return registry.findIndex(state->activeScene);
    }

    /*-------------------- serial logging --------------------*/
    inline void log() {
        if (millis() - lastScenePrint > 2000) {
            lastScenePrint = millis();
            Serial.print(F("[Scene] "));
            Serial.println(getCurrentSceneName());
        }
    }
};
