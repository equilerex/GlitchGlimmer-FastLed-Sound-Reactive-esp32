#pragma once
#include <TFT_eSPI.h>
#include <Arduino.h>
#include "../../core/Debug.h" 
#include <functional>
#include "../themes/ColorTheme.h"



class Widget {
public:
    virtual ~Widget() = default;
    virtual void draw(TFT_eSPI& tft, int x, int y, int width, int height) = 0;
    virtual int getMinWidth() const { return 40; }
    virtual int getMinHeight() const { return 20; }
};



// --- VerticalBarWidget (simple) ---
class VerticalBarWidget : public Widget {
private:
    String label;
    float normalizedValue;
    uint16_t barColor;
    WidgetColorTheme theme;

public:
    VerticalBarWidget(String l, float val, uint16_t col = TFT_GREEN)
        : label(l), normalizedValue(val), barColor(col), theme(CyberpunkTheme) {}

    void draw(TFT_eSPI& tft, int x, int y, int width, int height) override {
        int barHeight = normalizedValue * height;
        tft.fillRect(x, y, width, height, theme.barBg);
        tft.fillRect(x, y + height - barHeight, width, barHeight, barColor);

        tft.setTextColor(theme.text);
        tft.setTextSize(1);
        tft.setRotation(1);
        tft.setCursor(y + 4, x + 4);
        tft.print(label);
        tft.setRotation(0);
    }

    int getMinWidth() const override { return 135; }
    int getMinHeight() const override { return 30; }
};

// --- WaveformWidget (advanced) ---
class WaveformWidget : public Widget {
private:
    const int16_t* waveform;
    int samples;
    WidgetColorTheme theme;
    bool beatPulse;
    float pulseIntensity = 0.0f;

    bool isValidWaveform() const {
        if (!waveform) return false;
        if (samples <= 1) return false;
        uint32_t ptr = (uint32_t)waveform;
        return (ptr >= 0x3FF80000 && ptr < 0x40000000);
    }

public:
    WaveformWidget(const int16_t* wf, int samp, const WidgetColorTheme& themeRef, bool pulseOnBeat = false)
        : waveform(wf), samples(samp), theme(themeRef), beatPulse(pulseOnBeat) {
        //Debug::logPointer(Debug::DEBUG, "WaveformWidget", wf, __FILE__, __LINE__);
    }

    void draw(TFT_eSPI& tft, int x, int y, int width, int height) override {
 
        
        // Reduced drawing frequency
        static unsigned long lastDrawTime = 0;
        if (millis() - lastDrawTime < 50) { // Max 20 FPS
            return;
        }
        lastDrawTime = millis();

        tft.drawRect(x, y, width, height, theme.secondary);

        if (!isValidWaveform()) {
            drawNoSignal(tft, x, y, width, height);
            return;
        }

        // Draw waveform with reduced resolution
        int step = width > 100 ? 2 : 1;
        int baseY = y + height / 2;
        uint16_t waveColor = beatPulse ? theme.powerColor : theme.primary;

        if (beatPulse) pulseIntensity = min(1.0f, pulseIntensity + 0.2f);
        else pulseIntensity = max(0.0f, pulseIntensity - 0.1f);

        int lastY = baseY;
        for (int i = 0; i < width; i += step) {
            float idx = (float)i * samples / width;
            int index = (int)idx;
            
            float sample = waveform[index];
            int y1 = map(sample, -32768, 32767, -height / 2, height / 2);
            int currentY = baseY + y1;
            
            tft.drawLine(x + i, baseY, x + i, currentY, waveColor);
            if (i > 0) tft.drawLine(x + i - step, lastY, x + i, currentY, waveColor);
            lastY = currentY;

     
        }

        if (pulseIntensity > 0) {
            uint8_t alpha = pulseIntensity * 128;
            tft.drawRect(x, y, width, height, theme.powerColor);
        }
    }

    void triggerPulse() {
        pulseIntensity = 1.0f;
    }

    int getMinWidth() const override { return 100; }
    int getMinHeight() const override { return 20; }

private:
    void drawNoSignal(TFT_eSPI& tft, int x, int y, int width, int height) {
        int centerX = x + width / 2;
        int centerY = y + height / 2;
        tft.drawLine(x, y, x + width, y + height, theme.powerColor);
        tft.drawLine(x, y + height, x + width, y, theme.powerColor);
        tft.setTextColor(theme.powerColor);
        tft.setTextSize(1);
        tft.setCursor(centerX - 20, centerY - 3);
        tft.print("No Audio");
    }
};


class AcronymValueWidget : public Widget {
public:
    // String value constructor
    AcronymValueWidget(const String& label, const String& value, bool highlight = false)
        : label(label.isEmpty() ? "---" : label), stringValue(value), isStringValue(true), highlight(highlight) {}
    
    // Integer value constructor
    AcronymValueWidget(const String& label, int value, bool highlight = false)
        : label(label.isEmpty() ? "---" : label), intValue(value), isStringValue(false), highlight(highlight) {}

    void draw(TFT_eSPI& tft, int x, int y, int width, int height) override {
        const uint16_t bgColor = highlight ? TFT_BLACK : TFT_BLACK;
        tft.fillRoundRect(x, y, width, height, 4, bgColor);

        tft.setTextSize(1);
        tft.setTextColor(TFT_WHITE);
        tft.setCursor(x + 4, y + 2);
        tft.print(label);

        tft.setTextSize(2);
        tft.setTextColor(highlight ? TFT_RED : TFT_CYAN);
        int valueY = y + (height - 16) / 2;
        int valueX;
        
        if (isStringValue) {
            valueX = x + (width - stringValue.length() * 12) / 2;
            tft.setCursor(valueX, valueY);
            tft.print(stringValue);
        } else {
            valueX = x + (width - String(intValue).length() * 12) / 2;
            tft.setCursor(valueX, valueY);
            tft.print(intValue);
        }
    }

    int getMinWidth() const override { return 60; }
    int getMinHeight() const override { return 40; }

private:
    String label;
    String stringValue;
    int intValue;
    bool isStringValue;
    bool highlight;
};




class ScrollingTextWidget : public Widget {
public:
    ScrollingTextWidget(std::function<String()> textFn)
        : getText(textFn) {}

    void draw(TFT_eSPI& tft, int x, int y, int width, int height) override {
        String text = getText();
        tft.setTextSize(1);
        tft.setTextColor(TFT_GREEN);
        tft.setCursor(x + scrollOffset, y + (height / 2) - 4);
        tft.print(text);

        // Scroll offset update
        scrollCounter++;
        if (scrollCounter > 5) {
            scrollOffset--;
            if (scrollOffset < -text.length() * 6) scrollOffset = width;
            scrollCounter = 0;
        }
    }

    int getMinWidth() const override { return 100; }
    int getMinHeight() const override { return 20; }

private:
    std::function<String()> getText;
    int scrollOffset = 0;
    int scrollCounter = 0;
};