#pragma once

#include <TFT_eSPI.h>
#include <vector>
#include <memory> // Required for std::unique_ptr
#include "../audio/AudioFeatures.h"
#include "widgets/Widget.h" // Include base class
#include "themes/ColorTheme.h"
#include "GridLayout.h"
#include "../config/Config.h"
#include "../core/Debug.h"
#include "SettingIconRenderer.h"

class DisplayManager {
private:
    TFT_eSPI& _tft;
    GridLayout layout;

    // --- Pointers to persistent widgets ---
    // Using unique_ptr for automatic memory management
    std::unique_ptr<VerticalBarWidget> bassBar;
    std::unique_ptr<VerticalBarWidget> midBar;
    std::unique_ptr<VerticalBarWidget> trebleBar;
    std::unique_ptr<VerticalBarWidget> powerBar;
    std::unique_ptr<AcronymValueWidget> bpmWidget;
    std::unique_ptr<AcronymValueWidget> powerValWidget;
    std::unique_ptr<WaveformWidget> waveformWidget;

    // Setting screen state
    bool showSettingScreen = false;
    bool loading = true;
    String activeSettingName = "";
    int activeSettingValue = 0;
    unsigned long settingDisplayTime = 0;
    bool errorState = false;
    String errorMessage;
    String currentAnimationName;
    String drawnAnimName;

    // --- Private Helper Methods ---
    void setupLayout(); // New method to initialize layout and widgets
    void drawMainScreen(const AudioFeatures& features, const String& animName); // Simplified signature

public:
    DisplayManager(TFT_eSPI& display);
    ~DisplayManager() = default; // Add default destructor for unique_ptr cleanup

    void setTheme(const WidgetColorTheme& newTheme);
    void begin();
    void showStartupScreen();
    
    // Simplified update methods
    void update(const AudioFeatures& features, const String& animName);
    
    // No-parameter update for when no audio data is available
    void update();
    
    // Legacy method for compatibility
    void updateAudioVisualization(const AudioFeatures& features);
    
    void showSetting(const String& name, int value);
    void drawSettingScreen();
    void showError(const String& message);
    // One line when heap pressure is not OK. A null label clears it.
    void showMemoryPressure(const char* label);
    void setCurrentAnimation(const String& name);
    void clearError();
    bool hasError() const { return errorState; }
};
