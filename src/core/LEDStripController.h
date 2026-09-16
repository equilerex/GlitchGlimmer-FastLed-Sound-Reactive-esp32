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
#include <deque>
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

    ~LEDStrip() { delete currentAnim; layerMgr.clearLayers(); }

    inline void init(int len, CRGB* buf) {
        length = len;  leds = buf;
        layerMgr.setLEDs(leds, length);
    }

    // creates a new object only when the type changes
    inline void setAnimation(AnimationType type, const AudioFeatures& af) {
        if (type == currentType && currentAnim) return;
        currentType = type;
        delete currentAnim;
        currentAnim = animationFactory(type)();
        if (currentAnim) currentAnim->update(leds, length, af);
    }

    inline void update(const AudioFeatures& af,
                       const std::deque<AudioSnapshot>& hist)
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
      sceneDirector(moodHistory, sceneRegistry),
      lastMemoryCheck(0),
      memoryCheckInterval(10000) // Check memory every 10 seconds
    {
#if defined(ENABLE_HEAP_MONITORING) && ENABLE_HEAP_MONITORING == true
        // Initialize memory monitoring
        minHeapSeen = ESP.getFreeHeap();
#endif
    }

    // -----------------------------------------------------------------------------
    //  Setup – call once from setup()
    // -----------------------------------------------------------------------------
    inline void begin() {
        sceneRegistry.registerDefaultScenes();
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
        // Only fade LEDs if there are valid LEDs registered with FastLED
        if (FastLED.leds() != nullptr && FastLED.size() > 0) {
            fadeToBlackBy(FastLED.leds(), FastLED.size(), 5);
        }

        moodHistory.update(audio);
        sceneDirector.update(audio);

        const SceneDefinition* scenePtr = sceneDirector.getActiveScene();
        if (scenePtr != nullptr) {
            const SceneDefinition& scene = *scenePtr;

            for (int i = 0; i < stripCount; ++i) {
                strips[i].setAnimation(scene.baseAnimation, audio);
                strips[i].layers().applySceneLayers(scene);       // custom helper
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

private:
    AudioFeatures&       audio;
    MoodHistory&         moodHistory;
    AudioHistoryTracker& audioHistory;
    SceneRegistry        sceneRegistry;
    SceneDirector        sceneDirector;

    LEDStrip strips[10];
    int      stripCount = 0;

    // Memory monitoring
    unsigned long lastMemoryCheck;
    const unsigned long memoryCheckInterval;
#if defined(ENABLE_HEAP_MONITORING) && ENABLE_HEAP_MONITORING == true
    int minHeapSeen;

    // Monitor memory and report if it gets too low
    inline void checkMemory() {
        unsigned long now = millis();
        if (now - lastMemoryCheck >= memoryCheckInterval) {
            lastMemoryCheck = now;

            int currentHeap = ESP.getFreeHeap();
            if (currentHeap < minHeapSeen) {
                minHeapSeen = currentHeap;
            }

            // Report critical memory condition
            if (currentHeap < MIN_FREE_HEAP) {
                Serial.println(F("WARNING: Memory critically low!"));
                Serial.print(F("Free heap: "));
                Serial.println(currentHeap);
            }
        }
    }
#else
    inline void checkMemory() {} // No-op if monitoring disabled
#endif

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
