#pragma once

#include <TFT_eSPI.h>
#include <vector>
#include "../audio/AudioFeatures.h"
#include "widgets/Widget.h"
#include "themes/ColorTheme.h"
#include "GridLayout.h"
#include "../config/Config.h"
#include "../core/Debug.h"
#include "SettingIconRenderer.h"

class SettingIconWidget : public Widget {
    String icon;
    String value;
    float pulse;
    uint16_t primary;
    uint16_t secondary;
public:
    SettingIconWidget(const String& icon, const String& value, float pulse, uint16_t primary, uint16_t secondary)
        : icon(icon), value(value), pulse(pulse), primary(primary), secondary(secondary) {}

    void draw(TFT_eSPI& tft, int x, int y, int w, int h) override {
        int centerX = tft.width() / 2;
        int iconY = tft.height() / 2 - 20;
        int valueY = tft.height() / 2 + 20;

        // Draw icon using SettingIconRenderer
        SettingIconRenderer::draw(icon, tft, centerX, iconY, 18 * pulse, primary);

        // Draw value
        tft.setTextSize(2 * pulse);
        tft.setTextColor(primary, TFT_BLACK);
        int valWidth = tft.textWidth(value);
        tft.setCursor(centerX - valWidth / 2, valueY);
        tft.print(value);

        // Draw hint text
        tft.setTextSize(1);
        tft.setTextColor(secondary, TFT_BLACK);
        String hint = "press knob for more";
        int hintWidth = tft.textWidth(hint);
        tft.setCursor(centerX - hintWidth / 2, tft.height() - 16);
        tft.print(hint);
    }
};
#pragma once

#include <TFT_eSPI.h>
#include "../audio/AudioProcessor.h"
#include "../display/GridLayout.h"
#include "../display/themes/ColorTheme.h"

 
class DisplayManager  {
private:
    TFT_eSPI& _tft;
    GridLayout layout;

    // Setting screen state
    bool showSettingScreen = false;
    bool loading = true;
    String activeSettingName = "";
    int activeSettingValue = 0;
    unsigned long settingDisplayTime = 0;
    bool errorState = false;
    String errorMessage;
    String currentAnimationName;

public:
DisplayManager(TFT_eSPI& display);

    void setTheme(const WidgetColorTheme& newTheme);
    void begin();
    void showStartupScreen();
    void update(const AudioFeatures& features, const String& animName,
                int animIndex, int animCount, bool autoSwitch, const String& keepReason);
    void updateAudioVisualization(const AudioFeatures& features, int animIndex, int animCount, bool autoSwitch, String keepReason);
    void showSetting(const String& name, int value);
    void drawSettingScreen();
    void showError(const String& message);
    void setCurrentAnimation(const String& name);
    void clearError();
    bool hasError() const { return errorState; }
};
