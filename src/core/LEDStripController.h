#pragma once

#include <FastLED.h>
#include <functional>
#include <array>
#include "../audio/AudioFeatures.h"
#include "../config/Config.h"
#include "../animations/Animation.h"
#include "../animations/AnimationCatalog.h"
#include "../scenes/LayerManager.h"
#include "../audio/AudioHistoryTracker.h"
#include "../scenes/MoodHistory.h"
#include "../scenes/SceneRegistry.h"
#include "../scenes/SceneDirector.h"

#ifdef LED_0_PIN
CRGB ledStrip_0[LED_0_NUM];
#endif
#ifdef LED_1_PIN
CRGB ledStrip_1[LED_1_NUM];
#endif
#ifdef LED_2_PIN
CRGB ledStrip_2[LED_2_NUM];
#endif
#ifdef LED_3_PIN
CRGB ledStrip_3[LED_3_NUM];
#endif
#ifdef LED_4_PIN
CRGB ledStrip_4[LED_4_NUM];
#endif
#ifdef LED_5_PIN
CRGB ledStrip_5[LED_5_NUM];
#endif
#ifdef LED_6_PIN
CRGB ledStrip_6[LED_6_NUM];
#endif
#ifdef LED_7_PIN
CRGB ledStrip_7[LED_7_NUM];
#endif
#ifdef LED_8_PIN
CRGB ledStrip_8[LED_8_NUM];
#endif
#ifdef LED_9_PIN
CRGB ledStrip_9[LED_9_NUM];
#endif

class LEDStrip {
public:
    int index = -1;
    int length = 0;
    CRGB* leds = nullptr;
    Animation* currentAnimation = nullptr;
    LayerManager layerManager;

    ~LEDStrip() {
        if (currentAnimation) delete currentAnimation;
        layerManager.clearLayers();
    }

    void init(int len, CRGB* buffer) {
        length = len;
        leds = buffer;
        layerManager.setLEDs(leds, length);
    }

    void setAnimation(AnimationType type, const AudioFeatures& audio) {
        if (currentAnimation) delete currentAnimation;
        currentAnimation = animationFactory(type)(); // Use the animation factory
        if (currentAnimation) currentAnimation->update(leds, length, audio);
    }

    void update(const AudioFeatures& audio, const std::deque<AudioSnapshot>& history) {
        if (currentAnimation && leds)
            currentAnimation->update(leds, length, audio);
        layerManager.updateLayers(audio, history);
        layerManager.renderLayers(); // Remove extra arguments if not needed
    }

    LayerManager& getLayerManager() { return layerManager; }
};

class LEDStripController {
private:
    AudioFeatures& audio;
    MoodHistory& moodHistory;
    AudioHistoryTracker& audioHistory;
    SceneRegistry sceneRegistry;
    SceneDirector sceneDirector;
    LEDStrip strips[10];
    int stripCount = 0;

public:
LEDStripController(AudioFeatures& af, MoodHistory& mh, AudioHistoryTracker& ah)
  : audio(audio), moodHistory(moodHistory), audioHistory(audioHistory), sceneDirector(moodHistory, sceneRegistry) {}

    void begin() {
        sceneRegistry.registerDefaultScenes();
        sceneDirector.begin();

        #ifdef LED_0_PIN
        FastLED.addLeds<WS2812B, LED_0_PIN, GRB>(ledStrip_0, LED_0_NUM);
        strips[stripCount].init(LED_0_NUM, ledStrip_0);
        ++stripCount;
        #endif
        #ifdef LED_1_PIN
        FastLED.addLeds<WS2812B, LED_1_PIN, GRB>(ledStrip_1, LED_1_NUM);
        strips[stripCount].init(LED_1_NUM, ledStrip_1);
        ++stripCount;
        #endif
        #ifdef LED_2_PIN
        FastLED.addLeds<WS2812B, LED_2_PIN, GRB>(ledStrip_2, LED_2_NUM);
        strips[stripCount].init(LED_2_NUM, ledStrip_2);
        ++stripCount;
        #endif
        #ifdef LED_3_PIN
        FastLED.addLeds<WS2812B, LED_3_PIN, GRB>(ledStrip_3, LED_3_NUM);
        strips[stripCount].init(LED_3_NUM, ledStrip_3);
        ++stripCount;
        #endif
        #ifdef LED_4_PIN
        FastLED.addLeds<WS2812B, LED_4_PIN, GRB>(ledStrip_4, LED_4_NUM);
        strips[stripCount].init(LED_4_NUM, ledStrip_4);
        ++stripCount;
        #endif
        #ifdef LED_5_PIN
        FastLED.addLeds<WS2812B, LED_5_PIN, GRB>(ledStrip_5, LED_5_NUM);
        strips[stripCount].init(LED_5_NUM, ledStrip_5);
        ++stripCount;
        #endif
        #ifdef LED_6_PIN
        FastLED.addLeds<WS2812B, LED_6_PIN, GRB>(ledStrip_6, LED_6_NUM);
        strips[stripCount].init(LED_6_NUM, ledStrip_6);
        ++stripCount;
        #endif
        #ifdef LED_7_PIN
        FastLED.addLeds<WS2812B, LED_7_PIN, GRB>(ledStrip_7, LED_7_NUM);
        strips[stripCount].init(LED_7_NUM, ledStrip_7);
        ++stripCount;
        #endif
        #ifdef LED_8_PIN
        FastLED.addLeds<WS2812B, LED_8_PIN, GRB>(ledStrip_8, LED_8_NUM);
        strips[stripCount].init(LED_8_NUM, ledStrip_8);
        ++stripCount;
        #endif
        #ifdef LED_9_PIN
        FastLED.addLeds<WS2812B, LED_9_PIN, GRB>(ledStrip_9, LED_9_NUM);
        strips[stripCount].init(LED_9_NUM, ledStrip_9);
        ++stripCount;
        #endif

        FastLED.setBrightness(DEFAULT_BRIGHTNESS);
        FastLED.show();
    }

    void update() {
        audioHistory.addSnapshot(audio);
        moodHistory.update(audio);
        sceneDirector.update(audio);

        for (int i = 0; i < stripCount; ++i) {
            const SceneDefinition* scene = &sceneDirector.getCurrentScene().getActiveScene();
            if (scene) {
                strips[i].setAnimation(scene->baseAnimation, audio);
                strips[i].getLayerManager().applySceneLayers(*scene);
            }
            strips[i].update(audio, audioHistory.getHistory());
        }

        static unsigned long lastDebugPrint = 0;
        unsigned long now = millis();

        if (stripCount > 0 && now - lastDebugPrint > 1000) {
            lastDebugPrint = now;

            Serial.println(F("----- Debug [Strip 0] -----"));
            Serial.print(F("Active Scene: "));
            Serial.println(sceneDirector.getCurrentSceneName());
            Serial.print(F("Mood: "));
            Serial.println(moodHistory.getCurrentMoodName());

        }
        FastLED.show();
    }

    void switchAllAnimations() {
        sceneDirector.forceNextScene();
    }

    int getStripCount() const {
        return stripCount;
    }
};
