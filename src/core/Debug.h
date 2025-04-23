#pragma once

#include <Arduino.h>
#include "../config/Config.h"
#include <esp_system.h>
#include <esp_task_wdt.h>

// Debug utility class for more control over logging
class Debug {
public:
    enum LogLevel {
        ERROR = 0,
        INFO = 1,
        DEBUG = 2
    };

    static LogLevel currentLevel;

    static void begin(unsigned long baudRate) {
        Serial.begin(baudRate);
        while (!Serial && millis() < 3000); // Wait for serial to connect or timeout
        Serial.println(F("\n--- Debug System Started ---"));

        // Setup crash handler
        esp_task_wdt_init(10, true); // 10 second timeout, panic on timeout
        setupCrashHandler();

        // Log initial memory state
        logMemory("Initial memory state", "", 0);
    }

    static void log(LogLevel level, const char* message) {
        if (!shouldLog(level)) return;
        if (!message) {
            Serial.println(F("WARNING: Null message passed to log()"));
            return;
        }

        const char* prefix = getPrefix(level);
        unsigned long timestamp = millis();
        Serial.print(timestamp);
        Serial.print(F(" ["));
        Serial.print(prefix);
        Serial.print(F("] "));
        Serial.println(message);
    }

    template<typename... Args>
    static void logf(LogLevel level, const char* format, Args... args) {
        if (!shouldLog(level)) return;
        if (!format) {
            Serial.println(F("WARNING: Null format string passed to logf()"));
            return;
        }

        const char* prefix = getPrefix(level);
        unsigned long timestamp = millis();
        Serial.print(timestamp);
        Serial.print(F(" ["));
        Serial.print(prefix);
        Serial.print(F("] "));
        Serial.printf(format, args...);
        Serial.println();
    }

    static LogLevel getLogLevel() {
        return static_cast<LogLevel>(DEBUG_LEVEL);
    }

    static void logMemory(const char* label, const char* file, int line) {
        if (!shouldLog(INFO)) return;

        size_t freeHeap = ESP.getFreeHeap();
        size_t largestFreeBlock = ESP.getMaxAllocHeap(); // ESP32 specific

        if (file && strlen(file) > 0) {
            logf(INFO, "MEMORY [%s] at %s:%d - Free: %u bytes, Largest block: %u bytes",
                 label, file, line, freeHeap, largestFreeBlock);
        } else {
            logf(INFO, "MEMORY [%s] - Free: %u bytes, Largest block: %u bytes",
                 label, freeHeap, largestFreeBlock);
        }
    }

    static void logPointer(LogLevel level, const char* name, const void* ptr, const char* file, int line) {
        if (!shouldLog(level)) return;

        if (ptr == nullptr) {
            logf(ERROR, "NULL POINTER: %s is NULL at %s:%d", name, file, line);
        } else {
            logf(level, "POINTER: %s = 0x%p at %s:%d", name, ptr, file, line);
        }
    }

    static void logAnimationError(const char* animationName, const char* error) {
        logf(ERROR, "Animation creation failed - %s: %s", animationName, error);
    }

    // Add new method for tracking animation transitions
    static void logAnimationTransition(const char* fromAnim, const char* toAnim) {
        if (!shouldLog(INFO)) return;
        if (!fromAnim || !toAnim) {
            log(ERROR, "Invalid animation name in transition");
            return;
        }
        logf(INFO, "Animation transition: %s -> %s", fromAnim, toAnim);
        logMemory("Animation transition", "", 0);
    }

    // Enhanced crash handler with animation context
    static void setupCrashHandler() {
        #if CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT || CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT
        static char lastAnimation[32] = {0};
        
        esp_err_t err = esp_register_shutdown_handler([]() {
            Serial.println(F("\n!!! CRASH DETECTED !!!\n"));
            
            // Last known animation context
            if (lastAnimation[0]) {
                Serial.printf("Last animation: %s\n", lastAnimation);
            }
            
            // System state
            Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
            Serial.printf("Uptime: %lu ms\n", millis());
            
            // Hardware info
            Serial.printf("CPU Frequency: %u MHz\n", ESP.getCpuFreqMHz());
            Serial.printf("Chip revision: %d\n", ESP.getChipRevision());
            Serial.printf("SDK version: %s\n", ESP.getSdkVersion());
            
            // Memory analysis
            Serial.printf("Heap info:\n");
            Serial.printf("  Size: %u bytes\n", ESP.getHeapSize());
            Serial.printf("  Free: %u bytes\n", ESP.getFreeHeap());
            Serial.printf("  Min Free: %u bytes\n", ESP.getMinFreeHeap());
            Serial.printf("  Max Alloc: %u bytes\n", ESP.getMaxAllocHeap());
            
            // Print full register dump
            Serial.println(F("\nRegister dump:"));
            // Note: Additional register dump code would go here
            
            //delay(1000);
        });

        if (err != ESP_OK) {
            log(ERROR, "Failed to register crash handler");
        }
        #endif
    }

    // Track current animation
    static void setCurrentAnimation(const char* animName) {
        if (!animName) return;
        strncpy(lastAnimation, animName, sizeof(lastAnimation) - 1);
        lastAnimation[sizeof(lastAnimation) - 1] = '\0';
    }

    static void logMicrophoneError() {
        static unsigned long lastMicErrorLog = 0;
        const unsigned long LOG_INTERVAL = 5000;  // Log every 5 seconds
        
        unsigned long now = millis();
        if (now - lastMicErrorLog >= LOG_INTERVAL) {
            log(ERROR, "Microphone error detected - no audio input");
            lastMicErrorLog = now;
        }
    }

private:
    static bool shouldLog(LogLevel level) {
        #if DEBUG_ENABLED
            return level <= static_cast<LogLevel>(DEBUG_LEVEL);
        #else
            return false;
        #endif
    }

    static const char* getPrefix(LogLevel level) {
        switch (level) {
            case ERROR: return "ERROR";
            case INFO:  return "INFO";
            case DEBUG: return "DEBUG";
            default:    return "UNKNOWN";
        }
    }

    static char lastAnimation[32];  // Only declare here, don't initialize
};

// Remove the initialization from here
