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

public:
    inline SceneDirector(MoodHistory& m, SceneRegistry& r)
        : state(nullptr), mood(m), registry(r) {}

    /*-------------------- one-time wiring --------------------*/
    inline void attachState(SceneState* s) { state = s; }

    inline void begin() {
        if (!state) return;
        const SceneDefinition& first =
            registry.pickSceneByMood(*state, mood.getCurrentMood());
        state->beginScene(&first, mood.getCurrentSnapshot(), mood.getCurrentMood());
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

        // No mood.update() here, and no AudioFeatures argument to take one with.
        // `mood` is a reference to LEDStripController's own MoodHistory, and
        // LEDStripController::update() already advanced it a few lines earlier,
        // so this second call pushed the same snapshot twice per frame. The
        // argument is gone rather than ignored so the double update cannot come
        // back by accident.
        const MoodSnapshot& now = mood.getCurrentSnapshot();

        if (state->shouldTransition(now, mood.getCurrentMood())) {
            const SceneDefinition& nxt =
                registry.pickSceneByMood(*state, mood.getCurrentMood());
            state->beginScene(&nxt, now, mood.getCurrentMood());
        }
    }

    /*-------------------- reactive layer injection --------------------*/
    inline void maybeInjectReactiveLayer(LayerManager& lm,
                                         const AudioFeatures& af,
                                         unsigned long now)
    {
        static unsigned long lastBeat   = 0;
        static unsigned long lastEnergy = 0;
        constexpr int MAX_LAYERS = 4;

        if (lm.activeCount() >= MAX_LAYERS) return;

        if (af.beatDetected && now - lastBeat > 800) {
            if (random(100) < 70) lm.addLayerByType(LayerType::REACTIVE);
            lastBeat = now;
        }
        // level, not energy. energy is a raw FFT magnitude sum in the hundreds,
        // so the old `> 0.6f` was true on every frame and this injected an
        // OVERLAY layer on the 1500 ms timer regardless of the audio.
        if (af.level > 0.6f && now - lastEnergy > 1500) {
            if (random(100) < 40) lm.addLayerByType(LayerType::OVERLAY);
            lastEnergy = now;
        }
        if (random(1000) < 3) lm.addLayerByType(LayerType::MOOD_ARC);
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
        const SceneDefinition& nxt =
            registry.pickSceneByMood(*state, mood.getCurrentMood());
        state->beginScene(&nxt, mood.getCurrentSnapshot(), mood.getCurrentMood());
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
