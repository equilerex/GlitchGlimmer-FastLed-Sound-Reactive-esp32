#pragma once

#include <Arduino.h>
#include <map>
#include "Debug.h"
#include "../config/Config.h"

enum class Setting {
    BRIGHTNESS = DEFAULT_BRIGHTNESS,
    SPEED,
    HUE,
    SATURATION,
    COUNT
};

class SettingsManager {
public:
    SettingsManager() {
        values[Setting::BRIGHTNESS] = 128;
        values[Setting::SPEED] = 100;
        values[Setting::HUE] = 0;
        values[Setting::SATURATION] = 255;
        currentSetting = Setting::BRIGHTNESS;  
    }

    int get(Setting setting) const {
        auto it = values.find(setting);
        return it != values.end() ? it->second : 0;
    }

    void set(Setting setting, int value) {
        values[setting] = value;
        Debug::logf(Debug::DEBUG, "SettingsManager: Set %s (%d) to %d\n", settingName(setting).c_str(), static_cast<int>(setting), value);
    }

    void adjust(int delta) {
        values[currentSetting] = constrain(values[currentSetting] + delta, 0, 255);
        Debug::logf(Debug::DEBUG, "SettingsManager: Adjusted %s (%d) to %d\n", settingName(currentSetting).c_str(), static_cast<int>(currentSetting), values[currentSetting]);
    }

    void next() {
        currentSetting = static_cast<Setting>((static_cast<int>(currentSetting) + 1) % static_cast<int>(Setting::COUNT));
        Debug::logf(Debug::DEBUG, "SettingsManager: Switched to setting %s (%d)", settingName(currentSetting).c_str(), static_cast<int>(currentSetting));
    }

    Setting getCurrentSetting() const {
        return currentSetting;
    }

    static String settingName(Setting setting) {
        switch (setting) {
            case Setting::BRIGHTNESS: return "BRIGHTNESS";
            case Setting::SPEED: return "SPEED";
            case Setting::HUE: return "HUE";
            case Setting::SATURATION: return "SATURATION";
            default: return "UNKNOWN";
        }
    }

private:
    std::map<Setting, int> values;
    Setting currentSetting;
};

extern SettingsManager settingsManager;
