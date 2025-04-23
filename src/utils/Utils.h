#pragma once
#include <Arduino.h>

inline float smooth(float current, float target, float factor) {
    return factor * current + (1 - factor) * target;
}

inline String formatFloat(float value, int decimals = 2) {
    char buffer[16];
    dtostrf(value, 1, decimals, buffer);
    return String(buffer);
}


// Map a float from one range to another
inline float fmap(float x, float in_min, float in_max, float out_min, float out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// Constrain a float between 0.0 and 1.0
inline float constrain01(float value) {
    return constrain(value, 0.0f, 1.0f);
}

// Convert boolean to human-readable string
inline const char* boolToStr(bool val) {
    return val ? "ON" : "OFF";
}

// Helper for rotating a string for marquee effect
inline String scrollText(const String& text, int offset, int visibleLength) {
    String doubled = text + "   " + text;
    return doubled.substring(offset % doubled.length(), offset % doubled.length() + visibleLength);
}
