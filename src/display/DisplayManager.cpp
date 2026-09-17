#include "DisplayManager.h"
#include "../core/Debug.h"
#include "widgets/Widget.h"
#include <TFT_eSPI.h>
#include "../config/Config.h"
#include "SettingIconRenderer.h"
#include "themes/ColorTheme.h"
#include "GridLayout.h"
#include <memory>

DisplayManager::DisplayManager(TFT_eSPI &display)
    : _tft(display), layout(DISPLAY_WIDTH, DISPLAY_HEIGHT),
      showSettingScreen(false), settingDisplayTime(0), loading(true), errorState(false) {
    // Constructor initialization only - don't initialize hardware here
}

void DisplayManager::setTheme(const WidgetColorTheme& newTheme) {
    // If you want to support dynamic theme switching, update widgets here
}

void DisplayManager::setupLayout() {
    Debug::log(Debug::INFO, "Setting up display layout and widgets...");
    const WidgetColorTheme& theme = getTheme();

    // Create persistent widgets using C++11 compatible approach
    // Instead of std::make_unique, use direct unique_ptr construction with new
    bassBar.reset(new VerticalBarWidget("BASS", 0.0f, theme.bassColor));
    midBar.reset(new VerticalBarWidget("MID", 0.0f, theme.midColor));
    trebleBar.reset(new VerticalBarWidget("TREB", 0.0f, theme.trebleColor));
    powerBar.reset(new VerticalBarWidget("PWR", 0.0f, theme.powerColor));
    bpmWidget.reset(new AcronymValueWidget("BPM", 0));
    powerValWidget.reset(new AcronymValueWidget("PWR", 0));
    waveformWidget.reset(new WaveformWidget(nullptr, 0, theme, false));

    // Add widgets to the layout
    layout.addWidget(bassBar.get());
    layout.addWidget(midBar.get());
    layout.addWidget(trebleBar.get());
    layout.addWidget(powerBar.get());
    layout.addWidget(bpmWidget.get());
    layout.addWidget(powerValWidget.get());
    layout.addWidget(waveformWidget.get());

    Debug::log(Debug::INFO, "Display layout setup complete.");
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
    // Initialize hardware
    pinMode(DISPLAY_PIN, OUTPUT);
    digitalWrite(DISPLAY_PIN, HIGH);
    delay(1000); // Give display time to stabilize
    
    // Set display rotation
    _tft.setRotation(1);
    _tft.fillScreen(TFT_BLACK);
    
    // Show startup screen
    showStartupScreen();
    
    // Initialize widgets and layout after startup screen
    setupLayout();
    
    // Initial draw of the empty layout
    _tft.fillScreen(TFT_BLACK);
    layout.draw(_tft);
    
    Serial.println("Display initialized");
}

// Simple update method (for compatibility)
void DisplayManager::update() {
    if (loading) return;
    if (errorState) {
        showError(errorMessage);
        return;
    }
    if (showSettingScreen) {
        drawSettingScreen();
        return;
    }

    // Simple display when no audio data available
    _tft.fillScreen(TFT_BLACK);
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setTextSize(2);
    int16_t x = (_tft.width() - _tft.textWidth(currentAnimationName)) / 2;
    int16_t y = _tft.height() / 2;
    _tft.setCursor(x, y);
    _tft.print(currentAnimationName);
}

// Main update with full data
void DisplayManager::update(const AudioFeatures& features, const String& animName) {
    if (loading) return;
    if (errorState) {
        showError(errorMessage);
        return;
    }
    if (showSettingScreen) {
        drawSettingScreen();
        return;
    }

    // Store current animation name
    currentAnimationName = animName;
    
    // Draw main dashboard using the persistent widgets
    drawMainScreen(features, animName);
}

// Legacy method (forwards to drawMainScreen)
void DisplayManager::updateAudioVisualization(const AudioFeatures& features) {
    // Forward to new method for backwards compatibility
    drawMainScreen(features, currentAnimationName);
}

// New method that updates existing widgets rather than recreating them
void DisplayManager::drawMainScreen(const AudioFeatures& features, const String& animName) {
    // Update data in existing widgets
    if (bassBar) bassBar->setValue(features.bass);
    if (midBar) midBar->setValue(features.mid);
    if (trebleBar) trebleBar->setValue(features.treble);
    // level, not loudness. loudness is volume * 100 and volume is an absolute RMS
    // that sits near 0.008 on the microphone in use, so both of these widgets
    // read zero through music.
    if (powerBar) powerBar->setValue(features.level);

    if (bpmWidget) bpmWidget->setValue(static_cast<int>(features.bpm), features.beatDetected);
    if (powerValWidget) powerValWidget->setValue(static_cast<int>(features.level * 100.0f));

    if (waveformWidget) {
        waveformWidget->updateData(features.waveform, NUM_SAMPLES, features.beatDetected);
    }

    // No screen clear: each widget repaints its own rect. Clearing here costs a
    // full-screen SPI write every frame for pixels that are immediately redrawn.
    layout.draw(_tft);

    // Animation name sits on top of the first widget's rect, so it ghosts when
    // the name changes unless the old text is painted out first.
    if (animName != drawnAnimName) {
        _tft.fillRect(0, 0, 120, 12, TFT_BLACK);
        drawnAnimName = animName;
    }
    _tft.setTextColor(getTheme().primary, TFT_BLACK);
    _tft.setTextSize(1);
    _tft.setCursor(5, 5);
    _tft.print(animName);
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

    int size = (4 + round((pulse -.0) * 8));
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
