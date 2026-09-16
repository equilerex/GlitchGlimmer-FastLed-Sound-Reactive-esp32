#pragma once

#include <Button2.h>
#include "../config/Config.h"

#include "../core/LEDStripController.h"
// Remove duplicate forward declaration since we already include the header
// class LEDStripController;

class ButtonInput {
private:

    unsigned long lastPressTime = 0;
    const unsigned long debounceDelay = 500; // Longer debounce to prevent rapid triggers
    Button2 nextModeBtn;
    Button2 autoModeBtn;

public:
    ButtonInput():


                                    nextModeBtn(BUTTON_PIN_1),
                                    autoModeBtn(BUTTON_PIN_2) {}

    void begin() {

        // Configure Button2 instances with debounce time
        nextModeBtn.setDebounceTime(50);
        autoModeBtn.setDebounceTime(50);

        // Use different event for more reliable operation
        nextModeBtn.setClickHandler([this](Button2 &btn) {
            unsigned long now = millis();
            // Only allow button press every 500ms to prevent rapid triggering
            if (now - lastPressTime > debounceDelay) {
                Serial.println("Button 1 pressed - switching animations");
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
