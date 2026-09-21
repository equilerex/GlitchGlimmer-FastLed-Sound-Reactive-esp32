#pragma once

/*--------------------------------------------------------------------
 *  Includes – only the headers that give COMPLETE definitions
 *------------------------------------------------------------------*/
#include "../audio/AudioFeatures.h"
#include "../scenes/MoodHistory.h"     // gives MoodSnapshot
#include "../scenes/SceneRegistry.h"   // gives SceneDefinition
#include "../scenes/SceneState.h"      // gives SceneState
#include "../scenes/LayerManager.h"    // needed for maybeInjectReactiveLayer

/*--------------------------------------------------------------------
 *  Forward declarations – good style when we only need a pointer/ref
 *------------------------------------------------------------------*/
struct SceneDefinition;   // already defined via SceneRegistry, but explicit
struct MoodSnapshot;      // comes from MoodHistory

/*--------------------------------------------------------------------
 *  Lightweight POD used when we only care about a few mood fields
 *------------------------------------------------------------------*/
struct MoodInfo {
    float energy       = 0.0f;
    float tempo        = 0.0f;
    float dynamics     = 0.0f;
    bool  beatDetected = false;
};

/*--------------------------------------------------------------------
 *  SceneDirector – decides which Scene runs and injects layers
 *------------------------------------------------------------------*/
class SceneDirector {
private:
    SceneState*    state    = nullptr;   // owned elsewhere
    MoodHistory&   mood;
    SceneRegistry& registry;
    unsigned long  lastScenePrint = 0;
    // Cooldown stamps for the accent layers. Members and not function statics, so two
    // directors do not share a cooldown and a fresh one starts clear. The structural
    // layers have none: the firmware's episodes say when they start and end.
    unsigned long  lastBeat   = 0;
    unsigned long  lastEnergy = 0;

    // The scene that has been a better fit than the running one, and since when.
    // A challenger has to stay ahead for kChallengeMs before it takes over, so a
    // passage hovering between two looks does not flip between them.
    const SceneDefinition* challenger = nullptr;
    unsigned long          challengerSince = 0;
    int                    lockedSceneIndex = -1;

    static constexpr float         kSwitchMargin = 0.12f;  // distance the challenger must win by
    static constexpr unsigned long kChallengeMs  = 1500;   // how long it must keep winning
    static constexpr unsigned long kEventDwellMs = 1500;   // least a scene runs before an event cuts it

    // The event the music is reporting, or MOOD_COUNT when it is on the ladder.
    static inline MoodType structuralOf(MoodType m) {
        return ladderRank(m) < 0 ? m : MOOD_COUNT;
    }

    inline const SceneDefinition& pick(MoodType structural) {
        const MoodSnapshot& now = mood.getCurrentSnapshot();
        if (now.music.initialized) {
            return registry.pickSceneByMusic(*state, now.music, structural);
        }
        return registry.pickSceneByMood(*state, mood.getCurrentMood());
    }

    inline void switchTo(const SceneDefinition& next) {
        state->beginScene(&next, mood.getCurrentSnapshot(), mood.getCurrentMood());
        challenger = nullptr;
    }

public:
    inline SceneDirector(MoodHistory& m, SceneRegistry& r)
        : state(nullptr), mood(m), registry(r) {}

    /*-------------------- one-time wiring --------------------*/
    inline void attachState(SceneState* s) { state = s; }

    inline void begin() {
        if (!state) return;
        switchTo(pick(structuralOf(mood.getCurrentMood())));
    }

    /*-------------------- helpers --------------------*/
    static inline MoodInfo convertToMoodInfo(const MoodSnapshot& m) {
        // Initialize each field individually to avoid brace-initialization errors
        MoodInfo info;
        info.energy = m.energy;
        info.tempo = m.bpm;        // MoodInfo uses 'tempo' while MoodSnapshot uses 'bpm'
        info.dynamics = m.dynamics;
        info.beatDetected = m.beatDetected;
        return info;
    }

    /*-------------------- regular update --------------------*/
    inline void update() {
        if (!state) return;
        if (lockedSceneIndex >= 0) {
            challenger = nullptr;
            return;
        }

        // No mood.update() here, and no AudioFeatures argument to take one with.
        // `mood` is a reference to LEDStripController's own MoodHistory, and
        // LEDStripController::update() already advanced it a few lines earlier,
        // so this second call pushed the same snapshot twice per frame. The
        // argument is gone rather than ignored so the double update cannot come
        // back by accident.
        const MoodSnapshot& now = mood.getCurrentSnapshot();

        // Snapshots built by hand carry no musical coordinates, so the mood
        // ladder stays the selector for them.
        if (!now.music.initialized) {
            if (state->shouldTransition(now, mood.getCurrentMood())) {
                const SceneDefinition& nxt =
                    registry.pickSceneByMood(*state, mood.getCurrentMood());
                state->beginScene(&nxt, now, mood.getCurrentMood());
            }
            return;
        }

        const unsigned long t  = millis();
        const unsigned long el = t - state->sceneStartMillis;
        const MoodType structural = structuralOf(mood.getCurrentMood());

        // A structural passage (build, drop, tease, descent, weird) is a moment
        // the classifier has already confirmed and held, so it cuts in after a
        // short dwell without waiting out the challenge. A scene already written
        // for that moment is left alone.
        if (structural != MOOD_COUNT && structural != state->startMood &&
            el > kEventDwellMs && state->activeScene &&
            !state->activeScene->isTaggedFor(structural)) {
            switchTo(pick(structural));
            return;
        }

        if (el <= (unsigned long)state->sceneMinDurationMs) {
            challenger = nullptr;
            return;
        }

        // Past the minimum, the running scene is judged against the best other on
        // the same distance. It must lose by a margin, for a sustained time.
        const SceneDefinition& best = registry.pickSceneByMusic(*state, now.music, structural);
        const float incumbent = state->activeScene
            ? registry.sceneDistance(*state, *state->activeScene, now.music) : 1e9f;
        const float rival = registry.sceneDistance(*state, best, now.music);

        if (incumbent - rival >= kSwitchMargin) {
            if (challenger != &best) {
                challenger = &best;
                challengerSince = t;
            } else if (t - challengerSince >= kChallengeMs) {
                switchTo(best);
                return;
            }
        } else {
            challenger = nullptr;
        }

        // A scene that is still the best fit may stay, but not forever.
        if (el > (unsigned long)(state->sceneIdealDurationMs * 2.0f)) switchTo(best);
    }

    /*-------------------- episode layers --------------------*/
    static inline bool episodeOpen(const EpisodeStatus& e) {
        return e.state == EP_ACTIVE || e.state == EP_FADING;
    }

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

    /*-------------------- reactive layer injection --------------------*/
    inline void maybeInjectReactiveLayer(LayerManager& lm,
                                         const AudioFeatures& af,
                                         unsigned long now)
    {
        constexpr int MAX_LAYERS = 4;

        // Structural layers first, and ahead of the cap test: a full manager makes
        // room for a layer that outranks something in it, so an episode is answered
        // even when the scene already has its layers up.
        //
        // Each is bound to the firmware's episode. It is attached while the episode
        // is open and released by the manager when the firmware ends it, so nothing
        // here has a timer or a cooldown, and a layer lost to a scene change comes
        // back on the next frame for as long as its episode is still open.
        attachEpisodeLayers(lm, af);
        if (lm.activeCount() >= MAX_LAYERS) return;

        // A beat accent. Trusted only when the tracker has a solid lock (>= 0.70 confidence),
        // punchy transient 450ms pop that expires promptly.
        if (af.beatDetected && af.beatConfidence >= 0.70f && now - lastBeat > 1500) {
            if (random(100) < 50) {
                lm.addLayerByType(LayerType::REACTIVE, 450);
            }
            lastBeat = now;
        }
        // High-energy dynamic surge: level > 0.85 with dynamic range > 0.35, brief 1200ms flare
        if (af.level > 0.85f && af.dynamics > 0.35f && now - lastEnergy > 4000) {
            if (random(100) < 40) {
                lm.addLayerByType(LayerType::OVERLAY, 1200, LayerClass::OVERLAY);
            }
            lastEnergy = now;
        }
        // Rare mood arc sweep across scene
        if (random(1000) < 2 && now - lastBeat > 6000) {
            lm.addLayerByType(LayerType::MOOD_ARC, 4000);
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
        switchTo(pick(structuralOf(mood.getCurrentMood())));
    }

    /*-------------------- scene lock / freeze --------------------*/
    inline void lockScene(int index) {
        if (index >= 0 && index < static_cast<int>(registry.count())) {
            lockedSceneIndex = index;
            switchTo(registry.get(index));
        } else {
            unlockScene();
        }
    }

    inline void unlockScene() {
        if (lockedSceneIndex >= 0 && state) {
            state->sceneStartMillis = millis();
        }
        lockedSceneIndex = -1;
        challenger = nullptr;
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
