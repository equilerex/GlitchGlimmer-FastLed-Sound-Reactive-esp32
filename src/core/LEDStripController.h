#pragma once

/*
* ---------------------------------------------------------------------------
*  FastLED and ESP32 Memory Management Best Practices
* ---------------------------------------------------------------------------
*  1. **FastLED Safety Guidelines**:
*     - Always check FastLED.leds() for nullptr before use
*     - Verify FastLED.size() > 0 before manipulating LED arrays
*     - Use compile-time configuration for LED strips (as enforced by project guidelines)
*     - Minimize heap allocations in animation loops
*
*  2. **LED Controller Performance Optimizations**:
*     - Use FastLED's parallel output when controlling multiple strips
*     - Limit animation frame rate to conserve CPU for audio processing
*     - Use FastLED's built-in power management features for stability
*     - Consider using FASTLED_ALLOW_INTERRUPTS_OFF for smooth animations
*
*  3. **Memory Safety**:
*     - Avoid dynamic allocations during real-time processing
*     - Check all pointers before dereferencing
*     - Implement timeouts and fallbacks for all operations
*     - Use stack-allocated buffers where possible
*
*  4. **ESP32-Specific Considerations**:
*     - Be aware of FreeRTOS task priorities (audio processing is priority)
*     - Monitor heap fragmentation with ESP.getMinFreeHeap()
*     - Consider using IRAM_ATTR for time-critical functions
*     - Use both cores effectively (audio on one, LED on the other)
* ---------------------------------------------------------------------------
*/

#include <FastLED.h>
#include <array>
#include "../config/Config.h"
#include "../audio/AudioFeatures.h"
#include "../audio/AudioHistoryTracker.h"
#include "../animations/Animation.h"
#include "../animations/AnimationCatalog.h"
#include "../scenes/LayerManager.h"
#include "../scenes/MoodHistory.h"
#include "../scenes/SceneRegistry.h"
#include "../scenes/SceneDirector.h"

// -----------------------------------------------------------------------------
//   LED strip buffers – one array per physical string
// -----------------------------------------------------------------------------
#ifdef LED_0_PIN
    extern CRGB ledStrip_0[LED_0_NUM];
#endif
#ifdef LED_1_PIN
    extern CRGB ledStrip_1[LED_1_NUM];
#endif
#ifdef LED_2_PIN
    extern CRGB ledStrip_2[LED_2_NUM];
 #endif
#ifdef LED_3_PIN
    extern CRGB ledStrip_3[LED_3_NUM];
 #endif
#ifdef LED_4_PIN
   extern CRGB ledStrip_4[LED_4_NUM];
 #endif

// -----------------------------------------------------------------------------
//   Helper struct wrapping one physical strip
// -----------------------------------------------------------------------------
struct LEDStrip {
    int     index     = -1;
    int     length    = 0;
    CRGB*   leds      = nullptr;

    Animation*   currentAnim  = nullptr;
    AnimationType currentType = AnimationType::NONE;   // *** NEW ***
    LayerManager  layerMgr;
    const SceneDefinition* currentScene = nullptr;

    ~LEDStrip() { delete currentAnim; layerMgr.clearLayers(); }

    inline void init(int len, CRGB* buf) {
        length = len;  leds = buf;
        layerMgr.setLEDs(leds, length);
    }

    // Rebuild the layer list only when the scene actually changes. Scene layers
    // have no duration, so building them every frame allocates without bound.
    inline void setScene(const SceneDefinition& scene) {
        if (&scene == currentScene) return;
        currentScene = &scene;
        layerMgr.clearLayers();
        layerMgr.applySceneLayers(scene);
    }

    // creates a new object only when the type changes
    inline void setAnimation(AnimationType type, const AudioFeatures& af) {
        if (type == currentType && currentAnim) return;
        currentType = type;
        delete currentAnim;
        currentAnim = animationFactory(type)();
        if (currentAnim) {
            // The one moment an animation's clock can be started, since the object is
            // rebuilt on every type change rather than reused. An animation that
            // measures its own delta from millis() would otherwise read the time since
            // boot as its first frame's elapsed time and integrate it.
            currentAnim->begin();
            currentAnim->update(leds, length, af);
        }
    }

    inline void update(const AudioFeatures& af,
                       const AudioHistory& hist)
    {
        if (currentAnim) currentAnim->update(leds, length, af);
        layerMgr.updateLayers(af, hist);
        layerMgr.renderLayers();                                  // alpha blend
    }

    inline LayerManager& layers() { return layerMgr; }
};

// -----------------------------------------------------------------------------
//   High-level controller coordinating all strips & scenes
// -----------------------------------------------------------------------------
class LEDStripController {
public:
    inline LEDStripController(AudioFeatures& af,
                              MoodHistory&   mh,
                              AudioHistoryTracker& ah)
    : audio(af),
      moodHistory(mh),
      audioHistory(ah),
      sceneDirector(moodHistory, sceneRegistry)
    {
    }

    // -----------------------------------------------------------------------------
    //  Setup – call once from setup()
    // -----------------------------------------------------------------------------
    inline void begin() {
        sceneRegistry.registerDefaultScenes();
        // SceneDirector holds a pointer, not an instance, so something has to own
        // the state. Without this, state stays null and every scene call early-returns.
        sceneDirector.attachState(&sceneState);
        sceneDirector.begin();

        #ifdef LED_0_PIN
                 FastLED.addLeds<WS2812B, LED_0_PIN, GRB>(ledStrip_0, LED_0_NUM);
                strips[stripCount].index = stripCount;
                strips[stripCount].init(LED_0_NUM, ledStrip_0);
                ++stripCount;
        #endif
        #ifdef LED_1_PIN
                FastLED.addLeds<WS2812B, LED_1_PIN, GRB>(ledStrip_1, LED_1_NUM);
                strips[stripCount].index = stripCount;
                strips[stripCount].init(LED_1_NUM, ledStrip_1);
                ++stripCount;
        #endif
        #ifdef LED_2_PIN
                FastLED.addLeds<WS2812B, LED_2_PIN, GRB>(ledStrip_2, LED_2_NUM);
                strips[stripCount].init(LED_2_NUM, ledStrip_2);
                strips[stripCount].index = stripCount;
                ++stripCount;
        #endif
        #ifdef LED_3_PIN
                FastLED.addLeds<WS2812B, LED_3_PIN, GRB>(ledStrip_3, LED_3_NUM);
                strips[stripCount].init(LED_3_NUM, ledStrip_3);
                strips[stripCount].index = stripCount;
                ++stripCount;
        #endif
        #ifdef LED_4_PIN
                FastLED.addLeds<WS2812B, LED_4_PIN, GRB>(ledStrip_4, LED_4_NUM);
                strips[stripCount].init(LED_4_NUM, ledStrip_4);
                strips[stripCount].index = stripCount;
                ++stripCount;
        #endif

        FastLED.setBrightness(DEFAULT_BRIGHTNESS);
        FastLED.show();
    }

    //­­­­­­­­­­­­­­­­­------------------------------------------------------------
    //  Per-frame update – call from loop()
    //­­­­­­­­­­­­­­­­­------------------------------------------------------------
    inline void update() {
        // No fade here. renderLayers() already fades each strip's own buffer, and
        // FastLED.leds() addresses only the first registered controller -- so this
        // decayed strip 0 twice per frame and never touched any other strip.
        moodHistory.update(audio);
        sceneDirector.update();

        const SceneDefinition* scenePtr = sceneDirector.getActiveScene();
        if (scenePtr != nullptr) {
            const SceneDefinition& scene = *scenePtr;

            for (int i = 0; i < stripCount; ++i) {
                strips[i].setAnimation(scene.baseAnimation, audio);
                strips[i].setScene(scene);                        // rebuild only on change
                strips[i].update(audio, audioHistory.getHistory());
            }
        }

        if (stripCount > 0) {
            FastLED.show();
        }

        debugPrint();
    }

    inline void switchAllAnimations() { sceneDirector.forceNextScene(); }
    inline int  getStripCount() const { return stripCount; }

    // The live director is this one, not any other instance -- only this object
    // holds the SceneState that makes the director do anything.
    inline String getCurrentSceneName() const { return sceneDirector.getCurrentSceneName(); }

    // Test seams. The layer list and the scene clock are the two things the host
    // harness under sim/ has to observe, and both are otherwise private.
    inline int layerCount(int strip) const {
        return (strip >= 0 && strip < stripCount) ? strips[strip].layerMgr.activeCount() : -1;
    }
    inline int getSceneChangeCount() const { return sceneState.sceneChangeCount; }

    // The scene clock. A transition needs both a minimum elapsed time and a mood
    // shift, so elapsed-against-minimum is the only way to tell a scene that is
    // holding from one that is about to be replaced.
    inline unsigned long sceneElapsedMs() const { return sceneState.elapsed(); }
    inline float sceneMinMs() const { return sceneState.sceneMinDurationMs; }
    inline float sceneIdealMs() const { return sceneState.sceneIdealDurationMs; }

    // Mutable, for the tuning controls. A non-const reference is deliberate: the
    // setters recompute the running scene's own thresholds, so a change lands on
    // the scene in front of the viewer rather than on the one after it.
    inline SceneState& sceneStateForTuning() { return sceneState; }

private:
    AudioFeatures&       audio;
    MoodHistory&         moodHistory;
    AudioHistoryTracker& audioHistory;
    SceneRegistry        sceneRegistry;
    SceneState           sceneState;      // owned here; SceneDirector only points at it
    SceneDirector        sceneDirector;

    LEDStrip strips[10];
    int      stripCount = 0;

    // No heap monitor here. checkMemory() had no call sites and its low-water
    // mark was write-only, and loop() already gates on ESP.getFreeHeap() every
    // frame with a logged warning every 5s. A second health check with a
    // different threshold would only give two answers to one question.

    // optional serial debug every second
    inline void debugPrint() {
    #ifdef SERIAL_DEBUG
        static unsigned long last = 0;
        unsigned long now = millis();
        if (now - last > 1000 && stripCount) {
            last = now;
            Serial.println(F("------ LED Debug ------"));
            Serial.print(F("Scene : "));
            if (sceneDirector.getActiveScene() != nullptr) {
                Serial.println(sceneDirector.getCurrentSceneName());
            } else {
                Serial.println(F("None"));
            }
            Serial.print(F("Mood  : "));
            Serial.println(moodHistory.getCurrentMoodName());
        }
#endif
    }
};
