#include "DisplayManager.h"
#include "../core/Debug.h" 
#include "widgets/Widget.h"
#include <TFT_eSPI.h>
#include "../config/Config.h"
#include "SettingIconRenderer.h"
#include "themes/ColorTheme.h"
#include "GridLayout.h"

DisplayManager::DisplayManager(TFT_eSPI &display)
    : _tft(display), layout(DISPLAY_WIDTH, DISPLAY_HEIGHT),
      showSettingScreen(false), settingDisplayTime(0), loading(true), errorState(false) {
    // Initialize with a default theme
}

void DisplayManager::setTheme(const WidgetColorTheme& newTheme) {
    // If you want to support dynamic theme switching, you must implement it via config or getTheme().
}

void DisplayManager::showStartupScreen() {
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setTextSize(2);

    String title = "GlitchGlimmer";
    int16_t x = (_tft.width() - _tft.textWidth(title)) / 2;
    int16_t y = _tft.height() / 4;
    _tft.setCursor(x, y);
    _tft.print(title);

    delay(2000);

    _tft.setTextSize(1);
    String subtitle = "Loading visual cortex...";
    x = (_tft.width() - _tft.textWidth(subtitle)) / 2;
    y = _tft.height() / 2;
    _tft.setCursor(x, y);
    _tft.print(subtitle);

    delay(1000);
    loading = false;
}

void DisplayManager::begin() {
    pinMode(DISPLAY_PIN, OUTPUT);
    digitalWrite(DISPLAY_PIN, HIGH);
    delay(1000); // Wait for 2 seconds
    Serial.println("Display initialized");

    _tft.setRotation(1);
    _tft.fillScreen(TFT_BLACK); 
    showStartupScreen();
}

void DisplayManager::update(const AudioFeatures& features, const String& animName,
                          int animIndex, int animCount, bool autoSwitch, const String& keepReason) {
    if (loading) return;
    if (errorState) {
        showError(errorMessage);
        return;
    }
    if (showSettingScreen) {
        drawSettingScreen();
        return;
    }
    
    currentAnimationName = animName;
    updateAudioVisualization(features, animIndex, animCount, autoSwitch, keepReason);
    
    // Add animation name display
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setTextSize(1);
    _tft.setCursor(5, 5);
    _tft.print(currentAnimationName);
}

void DisplayManager::showSetting(const String& name, int value) {
    showSettingScreen = true;
    settingDisplayTime = millis();
    activeSettingName = name;
    activeSettingValue = value;
}

void DisplayManager::drawSettingScreen() {
    unsigned long elapsed = millis() - settingDisplayTime;
    if (elapsed > 3000) {
        showSettingScreen = false;
        return;
    }

    float pulse = 1.0 + 0.1 * sin(elapsed / 150.0);
    _tft.fillScreen(TFT_BLACK);

    String icon = activeSettingName;
    if (icon == "BRIGHT") icon = "\xF0\x9F\x94\x8A";
    else if (icon == "SPEED") icon = "\xE2\x9A\xA1";
    else if (icon == "HUE") icon = "\xF0\x9F\x8C\x88";
    else if (icon == "SAT") icon = "\xF0\x9F\x92\xA1";

    _tft.setTextSize(2);
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setCursor(10, _tft.height() / 2 - 10); _tft.print("<");
    _tft.setCursor(_tft.width() - 20, _tft.height() / 2 - 10); _tft.print(">");

    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setTextSize(2);
    int nameWidth = _tft.textWidth(icon);
    _tft.setCursor((_tft.width() - nameWidth) / 2, 40);
    _tft.print(icon);

    int size = (4 + round((pulse - 1.0) * 8));
    _tft.setTextSize(size);
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    String valStr = String(activeSettingValue);
    int valWidth = _tft.textWidth(valStr);
    _tft.setCursor((_tft.width() - valWidth) / 2, _tft.height() / 2 + 20);
    _tft.print(valStr);

    _tft.setTextSize(1);
    _tft.setTextColor(getTheme().secondary, TFT_BLACK);
    _tft.setCursor((_tft.width() - _tft.textWidth("press knob for more")) / 2, _tft.height() - 16);
    _tft.print("press knob for more");
}

void DisplayManager::updateAudioVisualization(const AudioFeatures& features, 
                                            int animIndex, int animCount, 
                                            bool autoSwitch, String keepReason) {
 

    _tft.fillScreen(TFT_BLACK);
    layout = GridLayout(_tft.width(), _tft.height());

    layout.addWidget(new VerticalBarWidget("BASS", features.bass, getTheme().bassColor));
    layout.addWidget(new VerticalBarWidget("MID", features.mid, getTheme().midColor));
    layout.addWidget(new VerticalBarWidget("TREB", features.treble, getTheme().trebleColor));
    layout.addWidget(new VerticalBarWidget("PWR", features.loudness / 100.0f, getTheme().powerColor));

    layout.addWidget(new AcronymValueWidget("BPM", static_cast<int>(features.bpm), features.beatDetected));
    layout.addWidget(new AcronymValueWidget("PWR", static_cast<int>(features.loudness)));
    layout.addWidget(new AcronymValueWidget("IDX", animIndex ? animIndex + 1 : 0));
    layout.addWidget(new AcronymValueWidget("TOT", animCount ? animCount : 0));
    layout.addWidget(new AcronymValueWidget("AUTO", autoSwitch));
    layout.addWidget(new AcronymValueWidget("KEEP", String(keepReason)));

    layout.addWidget(new WaveformWidget(features.waveform, NUM_SAMPLES, getTheme(), features.beatDetected));
    layout.draw(_tft);
}

void DisplayManager::showError(const String& message) {
    errorState = true;
    errorMessage = message;
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(TFT_RED, TFT_BLACK);
    _tft.setTextSize(1);
    _tft.setCursor(10, _tft.height()/2);
    _tft.print("ERROR: " + errorMessage);
}

void DisplayManager::setCurrentAnimation(const String& name) {
    currentAnimationName = name;
}

void DisplayManager::clearError() {
    errorState = false;
    errorMessage = "";
}
