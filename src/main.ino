#include <Arduino.h>
#include <esp_heap_caps.h>
#include "core/Debug.h"
#include "core/CommunicationService.h"
#include "core/MainController.h"
#include "core/MemoryGuard.h"

CommunicationService commService;
MainController controller(commService);

void safeDelay(unsigned long ms);

void setup() {
    Serial.begin(115200);
    delay(1000); // Let serial settle
    
    // CPU frequency comes from the build config. Do not downclock here: the
    // FFT and display work both need the cycles, and 240 MHz is the board default.

    Debug::log(Debug::INFO, "Booting...");
    
    // Allocate more stack for the main task
    // This helps prevent stack overflows during initialization
    #if CONFIG_ARDUINO_RUNNING_CORE == 1
    // Increase stack depth on core 1
    delay(100);
    #endif
    
    // Initialize the controller with safety delays
    safeDelay(500);
    controller.begin();
    safeDelay(500);
    Debug::log(Debug::INFO, "Controller started");
    
    // Add a post-initialization delay to let everything settle
    safeDelay(1000);
}

void loop() {
    static unsigned long lastErrorCheck = 0;
    unsigned long now = millis();

    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const uint32_t minFree = ESP.getMinFreeHeap();
    const MemoryGuardSample sample = memoryGuard().update(freeHeap, largest, now);
    if (sample.changed) {
        Debug::logf(Debug::INFO, "heap %s free=%u largest=%u min=%u",
                    memoryPressureName(sample.pressure),
                    static_cast<unsigned>(freeHeap),
                    static_cast<unsigned>(largest),
                    static_cast<unsigned>(minFree));
    }
    if (sample.restart) {
        Debug::logf(Debug::ERROR, "heap critical for %u ms, restarting",
                    static_cast<unsigned>(HEAP_CRITICAL_RESTART_MS));
        ESP.restart();
    }

    // Defence in depth, not the fix. The two history buffers no longer
    // allocate and the misplaced handlers that used to sit around memcpy are
    // gone, so nothing in this call is expected to throw. But loop() has no
    // caller: an exception escaping it reaches std::terminate and reboots the
    // board, and a dropped frame is cheaper than a reboot.
    try {
        controller.update();
    } catch (...) {
        Debug::log(Debug::ERROR, "Exception escaped controller.update()");
        safeDelay(100);
    }

    // Periodic status only while healthy. Pressure transitions log themselves.
    if (memoryGuard().pressure() == MemoryPressure::OK && now - lastErrorCheck > 30000) {
        Debug::log(Debug::INFO, "System running");
        lastErrorCheck = now;
    }

    yield();
}

// Safe delay function that yields periodically 
void safeDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        delay(10);
        yield();
    }
}

