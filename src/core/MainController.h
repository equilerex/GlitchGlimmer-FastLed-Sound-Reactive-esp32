#pragma once


#include <FastLED.h>
#include <TFT_eSPI.h>
#include "../audio/AudioProcessor.h"
#include "../audio/AudioHistoryTracker.h"
#include "../input/EncoderInput.h"
#include "../input/ButtonInput.h"
#include "../display/DisplayManager.h"
#include "../core/LEDStripController.h"
#include "../core/SettingsManager.h"
#include "../scenes/SceneDirector.h"
#include "../scenes/MoodHistory.h"


class MainController {
private:
    AudioFeatures audioFeatures;        // Must come before ledController
    AudioHistoryTracker audioHistory;   // Stores recent audio snapshots
    MoodHistory moodHistory;
    SceneDirector sceneDirector;
    AudioProcessor audioProcessor;
    LEDStripController ledController;   // Uses audioFeatures and audioHistory
    TFT_eSPI tft = TFT_eSPI();                       // Must come before displayManager
    EncoderInput encoderInput;
    ButtonInput buttonInput;
    DisplayManager displayManager;

public:
    MainController()
        : ledController(audioFeatures),
          encoderInput(ENCODER_PIN_A, ENCODER_PIN_B, ENCODER_BTN_PIN, settingsManager),
          buttonInput(ledController, BUTTON_PIN_1, BUTTON_PIN_2),
          displayManager(tft)
    {}

    void begin() {

        sceneDirector.begin();
        sceneDirector.begin();
        audioProcessor.begin();
        displayManager.begin();
        ledController.begin();
        encoderInput.begin();
        buttonInput.begin();
        FastLED.setBrightness(DEFAULT_BRIGHTNESS);
        FastLED.clear();
        FastLED.show();
    }

    void update() {
        static unsigned long lastFrame = 0;
        const unsigned long frameInterval = 10; // ~30 FPS

        unsigned long now = millis();
        if (now - lastFrame < frameInterval) {
            return; // Skip this update, not enough time has passed
        }
        lastFrame = now;

        audioProcessor.captureAudio();

        // Analyze and store into audioFeatures
        audioFeatures = audioProcessor.analyzeAudio();

        // Track the current frame in rolling history
        audioHistory.addSnapshot(audioFeatures);


        // Update all components
        encoderInput.update();
        buttonInput.update();
        ledController.update();

        String debugInfo =
    "Mood: " + moodHistory.getCurrentMoodName() +
    "\nBPM: " + String(audioFeatures.bpm, 1) +
    "\nEnergy: " + String(audioFeatures.energy, 1) +
    "\nDynamics: " + String(audioFeatures.dynamics, 2) +
    "\nScene: " + sceneDirector.getCurrentSceneName();

        displayManager.update(
            audioFeatures,
            debugInfo,
            1,
            4,
            false,
            String("")
        );

        FastLED.show();
    }
};
