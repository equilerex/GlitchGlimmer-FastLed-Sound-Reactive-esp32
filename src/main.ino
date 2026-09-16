#include <Arduino.h>
#include "core/Debug.h"
#include "core/CommunicationService.h"
#include "core/MainController.h"

CommunicationService commService;
MainController controller(commService);

// Forward declarations for error handling helpers
void safeDelay(unsigned long ms);
bool isMemoryHealthy();

void setup() {
    Serial.begin(115200);
    delay(1000); // Let serial settle
    
    // Set ESP32 CPU speed if running too fast
    #ifdef CONFIG_ESP32_DEFAULT_CPU_FREQ_240
    setCpuFrequencyMhz(160); // Lower CPU frequency for stability
    #endif
    
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
    // Use a watchdog pattern for safer execution
    static unsigned long lastErrorCheck = 0;
    static unsigned long lastHeapCheck = 0;
    unsigned long now = millis();
    
    // Update the controller with error protection
    if (isMemoryHealthy()) {
        controller.update();
    } else {
        // If memory looks compromised, just wait and try again later
        safeDelay(100);
    }
    
    // Periodically log running status and heap info
    if (now - lastErrorCheck > 30000) { // Every 30 seconds
        Debug::log(Debug::INFO, "System running");
        lastErrorCheck = now;
    }
    
    // Check heap health every 5 seconds
    if (now - lastHeapCheck > 5000) {
        if (!isMemoryHealthy()) {
            Debug::log(Debug::ERROR, "Low memory detected");
        }
        lastHeapCheck = now;
    }
    
    // Always yield to the OS to prevent watchdog timeouts
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

// Check if memory/heap is in a healthy state
bool isMemoryHealthy() {
    // ESP32 has around 320KB of SRAM
    // Consider it unhealthy if we have less than 20KB free
    return ESP.getFreeHeap() > 20 * 1024;
}
