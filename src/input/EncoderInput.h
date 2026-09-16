#pragma once // Or #ifndef/#define guards

#include <Arduino.h>
#include "driver/pcnt.h" // Assuming ESP32 PCNT driver is used
#include "../core/SettingsManager.h" // Include SettingsManager header
#include "../display/DisplayManager.h" // Include DisplayManager header

// Assuming these pins are defined elsewhere (e.g., Config.h)
#ifndef ENCODER_PIN_A
#define ENCODER_PIN_A 34 // Example pin
#endif
#ifndef ENCODER_PIN_B
#define ENCODER_PIN_B 35 // Example pin
#endif
#ifndef ENCODER_BTN_PIN
#define ENCODER_BTN_PIN 32 // Example pin
#endif


class EncoderInput {
private:
    // ... other members ...
    SettingsManager* settings; // It's a pointer
    DisplayManager& display; // Assuming it's a reference
    int encoderPinA;
    int encoderPinB;
    int buttonPin;
    // ... other members ...
    volatile long encoderValue = 0;
    long lastEncoderValue = 0;
    volatile bool buttonPressed = false;
    unsigned long lastButtonCheck = 0;


public:
    // Constructor taking SettingsManager pointer and DisplayManager reference
    EncoderInput(SettingsManager* sm, DisplayManager& dm, int pinA = ENCODER_PIN_A, int pinB = ENCODER_PIN_B, int btnPin = ENCODER_BTN_PIN)
        : settings(sm), display(dm), encoderPinA(pinA), encoderPinB(pinB), buttonPin(btnPin) {}

    // Method to handle encoder turns
    void processEncoderTurn() {
        long change = encoderValue - lastEncoderValue;
        bool changed = false;
        
        if (change > 0 && settings) {
            // Increase current setting value
            settings->adjust(1); // Use the existing adjust method
            changed = true;
        } else if (change < 0 && settings) {
            // Decrease current setting value
            settings->adjust(-1); // Use the existing adjust method
            changed = true;
        }
        
        lastEncoderValue = encoderValue;
    
        // Update display only if value changed and settings is valid
        if (changed && settings) {
            String currentSettingName = settings->getCurrentSetting();
            int currentSettingValue = settings->getValue(currentSettingName);
            display.showSetting(currentSettingName, currentSettingValue);
        }
    }
    
    // Method to handle button clicks
    void processButtonClick() {
       if (buttonPressed && settings) {
           // Cycle to the next setting
           settings->next(); // Use the existing next method
           
           String currentSettingName = settings->getCurrentSetting();
           int currentSettingValue = settings->getValue(currentSettingName);
           display.showSetting(currentSettingName, currentSettingValue);
           
           buttonPressed = false; // Reset flag
       }
    }
    
    // begin/initialize method
    void begin() {
        pinMode(buttonPin, INPUT_PULLUP);
        // Add initialization for ESP32 PCNT or attach interrupts here
        Serial.println("EncoderInput initialized.");
        
        // Initialize display with the current setting
        if(settings) {
            String currentSettingName = settings->getCurrentSetting();
            int currentSettingValue = settings->getValue(currentSettingName);
            display.showSetting(currentSettingName, currentSettingValue);
        } else {
            // showSetting expects a String name and an int value
            String setupName = "Setup"; // Convert to String explicitly
            display.showSetting(setupName, 0); // Now correctly passing String and int
        }
    }

    // update method
    void update() {
        // Read encoder value (e.g., from PCNT or interrupt-driven variable)
        // Read button state and handle debouncing
        // For simplicity, assuming buttonPressed and encoderValue are updated elsewhere (e.g., ISR)

        // Process turns based on value change
        // (Need to read encoder hardware state here to update encoderValue)
        processEncoderTurn();

        // Process clicks based on flag
        // (Need to read button hardware state here to update buttonPressed flag)
        processButtonClick();
    }

    // ... other methods ...
};