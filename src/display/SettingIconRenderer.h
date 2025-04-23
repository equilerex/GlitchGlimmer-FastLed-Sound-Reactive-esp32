#pragma once
#include <TFT_eSPI.h>
#include <cmath>
#include <cmath>

class SettingIconRenderer {
public:
    static void draw(const String& name, TFT_eSPI& tft, int x, int y, int size, uint16_t color) {
        if (name == "BRIGHT") drawBrightness(tft, x, y, size, color);
        else if (name == "SPEED") drawSpeed(tft, x, y, size, color);
        else if (name == "HUE") drawHue(tft, x, y, size, color);
        else if (name == "SAT") drawSaturation(tft, x, y, size, color);
        else drawFallbackText(name, tft, x, y, color);
    }

private:
    static void drawBrightness(TFT_eSPI& tft, int x, int y, int size, uint16_t color) {
        tft.drawCircle(x, y, size, color);
        for (int i = 0; i < 360; i += 45) {
            float rad = radiansf(i);
            int x1 = x + cos(rad) * (size + 2);
            int y1 = y + sin(rad) * (size + 2);
            int x2 = x + cos(rad) * (size + 6);
            int y2 = y + sin(rad) * (size + 6);
            tft.drawLine(x1, y1, x2, y2, color);
        }
    }

    static void drawSpeed(TFT_eSPI& tft, int x, int y, int size, uint16_t color) {
        tft.fillTriangle(
            x - size / 2, y - size / 2,
            x + size / 2, y,
            x - size / 2, y + size / 2,
            color
        );
    }

    static void drawHue(TFT_eSPI& tft, int x, int y, int size, uint16_t color) {
        for (int i = 0; i < 360; i += 30) {
            tft.drawLine(
                x, y,
                x + cos(radiansf(i)) * size,
                y + sin(radiansf(i)) * size,
                color
            );
        }
        tft.drawCircle(x, y, size / 2, color);
    }

    static void drawSaturation(TFT_eSPI& tft, int x, int y, int size, uint16_t color) {
        tft.drawRect(x - size / 2, y - size / 2, size, size, color);
        tft.drawLine(x - size / 2, y, x + size / 2, y, color);
        tft.drawLine(x, y - size / 2, x, y + size / 2, color);
    }

    static void drawFallbackText(const String& label, TFT_eSPI& tft, int x, int y, uint16_t color) {
        tft.setTextSize(2);
        tft.setTextColor(color);
        int tw = tft.textWidth(label);
        tft.setCursor(x - tw / 2, y - 8);
        tft.print(label);
    }

    static float radiansf(int deg) {
        return deg * 3.14159265f / 180.0f;
    }
};
