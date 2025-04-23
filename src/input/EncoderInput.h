#pragma once

#include <Arduino.h>
#include "../core/SettingsManager.h"

class EncoderInput {
public:
    EncoderInput(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, SettingsManager& settings)
        : pinA(pinA), pinB(pinB), buttonPin(buttonPin), settings(settings) {}



    void begin() {
        pinMode(pinA, INPUT_PULLUP);
        pinMode(pinB, INPUT_PULLUP);
        pinMode(buttonPin, INPUT_PULLUP);
    }

    void update() {
        int a = digitalRead(pinB);
        int b = digitalRead(pinA);
        bool pressed = digitalRead(buttonPin) == LOW;
        unsigned long now = millis();

        // Rotary detection (simple state change)
        if (a != lastA) {
            if (b != a) {
                settings.adjust(1);
            } else {
                settings.adjust(-1);
            }
            lastTurnTime = now;
        }
        lastA = a;

        // Button press
        if (pressed && !wasPressed && now - lastClick > debounce) {
            settings.next();
            lastClick = now;
        }
        wasPressed = pressed;

        /*if (display && now - lastTurnTime < displayTimeout) {
            display->showSetting(settings.getCurrentSetting(), settings.get(settings.getCurrentSetting()));
        }*/
    }

private:
    uint8_t pinA, pinB, buttonPin;
    int lastA = HIGH;
    bool wasPressed = false;

    unsigned long lastClick = 0;
    unsigned long lastTurnTime = 0;

    static constexpr unsigned long debounce = 250;
 //   static constexpr unsigned long displayTimeout = 3000;

    SettingsManager& settings;
    //DisplayManager* display = nullptr;
};
