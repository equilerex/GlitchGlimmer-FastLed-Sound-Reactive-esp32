#pragma once

#include <Button2.h>

#include "../core/LEDStripController.h"
// Remove duplicate forward declaration since we already include the header
// class LEDStripController;

class ButtonInput {
private:
    uint8_t buttonPin1;
    uint8_t buttonPin2;
    unsigned long lastPressTime = 0;
    const unsigned long debounceDelay = 500; // Longer debounce to prevent rapid triggers
    Button2 nextModeBtn;
    Button2 autoModeBtn;
    LEDStripController& lEDStripController;

public:
    ButtonInput(LEDStripController& hybrid, uint8_t pin1, uint8_t pin2) :
                                    lEDStripController(hybrid),
                                    buttonPin1(pin1),
                                    buttonPin2(pin2),
                                    nextModeBtn(pin1),
                                    autoModeBtn(pin2) {}

    void begin() {
        // Configure internal pullup resistors for the buttons
        pinMode(buttonPin1, INPUT_PULLUP);
        pinMode(buttonPin2, INPUT_PULLUP);

        // Configure Button2 instances with debounce time
        nextModeBtn.setDebounceTime(50);
        autoModeBtn.setDebounceTime(50);

        // Use different event for more reliable operation
        nextModeBtn.setClickHandler([this](Button2 &btn) {
            unsigned long now = millis();
            // Only allow button press every 500ms to prevent rapid triggering
            if (now - lastPressTime > debounceDelay) {
                Serial.println("Button 1 pressed - switching animations");
                lEDStripController.switchAllAnimations();
                lastPressTime = now;
            }
        });

        autoModeBtn.setClickHandler([this](Button2 &btn) {
            Serial.println("Button 2 pressed");
            // Uncomment when function is implemented
            // lEDStripController.toggleAuto();
        });
    }

    bool update() {
        // Only call loop on the button objects
        nextModeBtn.loop();
        autoModeBtn.loop();
        return true;
    }
};
