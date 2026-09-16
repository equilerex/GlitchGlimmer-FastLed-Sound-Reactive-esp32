#pragma once

#include <Arduino.h>

// Helper class for an individual setting
class Setting {
private:
    String name;
    int value;
    int minValue;
    int maxValue;
    
public:
    Setting(const String& name, int defaultValue, int min, int max) 
        : name(name), value(defaultValue), minValue(min), maxValue(max) {}
    
    String getName() const { return name; }
    int getValue() const { return value; }
    
    void setValue(int newValue) {
        value = constrain(newValue, minValue, maxValue);
    }
    
    void adjust(int direction) {
        setValue(value + direction);
    }
};

// Settings manager to handle multiple settings
class SettingsManager {
private:
    // Fixed-size array of setting names and values for simplicity
    static const int maxSettings = 5;
    String settingNames[maxSettings] = {"BRIGHT", "SPEED", "HUE", "SAT", "VAL"};
    int settingValues[maxSettings] = {50, 50, 50, 50, 50}; // Default values
    int currentSettingIndex = 0;
    
public:
    SettingsManager() {}
    
    // Cycle to the next setting
    void next() {
        currentSettingIndex = (currentSettingIndex + 1) % maxSettings;
        #ifdef DEBUG_SETTINGS
        Serial.print("Setting changed to: ");
        Serial.println(getCurrentSetting());
        #endif
    }
    
    // Cycle to the previous setting
    void previous() {
        currentSettingIndex = (currentSettingIndex > 0) ? 
            (currentSettingIndex - 1) : (maxSettings - 1);
        #ifdef DEBUG_SETTINGS
        Serial.print("Setting changed to: ");
        Serial.println(getCurrentSetting());
        #endif
    }
    
    // Adjust the current setting by the specified amount
    void adjust(int direction) {
        settingValues[currentSettingIndex] = constrain(
            settingValues[currentSettingIndex] + direction, 
            0, 100);
        #ifdef DEBUG_SETTINGS
        Serial.print("Setting ");
        Serial.print(getCurrentSetting());
        Serial.print(" adjusted to: ");
        Serial.println(getValue(getCurrentSetting()));
        #endif
    }
    
    // Get the current setting's name
    String getCurrentSetting() {
        return settingNames[currentSettingIndex];
    }
    
    // Get a setting's value by name
    int getValue(const String& name) {
        // Find the setting by name
        for (int i = 0; i < maxSettings; i++) {
            if (settingNames[i] == name) {
                return settingValues[i];
            }
        }
        // If not found, return current setting value
        return settingValues[currentSettingIndex];
    }
    
    // Get setting value by index directly (for internal use)
    int getValueByIndex(int index) {
        if (index >= 0 && index < maxSettings) {
            return settingValues[index];
        }
        return 0;
    }
};
