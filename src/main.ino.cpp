# 1 "C:\\Users\\Joosep\\AppData\\Local\\Temp\\tmpnytyhi9b"
#include <Arduino.h>
# 1 "D:/ESP32 projects/GlitchGlimmer-FastLed-Sound-Reactive-esp32/src/main.ino"
#include <Arduino.h>
#include "core/Debug.h"
#include "core/CommunicationService.h"
#include "core/MainController.h"

CommunicationService commService;
MainController controller(commService);


void safeDelay(unsigned long ms);
bool isMemoryHealthy();
void setup();
void loop();
#line 13 "D:/ESP32 projects/GlitchGlimmer-FastLed-Sound-Reactive-esp32/src/main.ino"
void setup() {
    Serial.begin(115200);
    delay(1000);




    Debug::log(Debug::INFO, "Booting...");



    #if CONFIG_ARDUINO_RUNNING_CORE == 1

    delay(100);
    #endif


    safeDelay(500);
    controller.begin();
    safeDelay(500);
    Debug::log(Debug::INFO, "Controller started");


    safeDelay(1000);
}

void loop() {

    static unsigned long lastErrorCheck = 0;
    static unsigned long lastHeapCheck = 0;
    unsigned long now = millis();


    if (isMemoryHealthy()) {





        try {
            controller.update();
        } catch (...) {
            Debug::log(Debug::ERROR, "Exception escaped controller.update()");
            safeDelay(100);
        }
    } else {

        safeDelay(100);
    }


    if (now - lastErrorCheck > 30000) {
        Debug::log(Debug::INFO, "System running");
        lastErrorCheck = now;
    }


    if (now - lastHeapCheck > 5000) {
        if (!isMemoryHealthy()) {
            Debug::log(Debug::ERROR, "Low memory detected");
        }
        lastHeapCheck = now;
    }


    yield();
}


void safeDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        delay(10);
        yield();
    }
}


bool isMemoryHealthy() {


    return ESP.getFreeHeap() > 20 * 1024;
}