
#pragma once
#include <Arduino.h>
#include "core/Debug.h"
#include "core/MainController.h"

MainController controller;

void setup() {
    Serial.begin(115200);
    delay(1000); // Let serial settle
    Debug::log(Debug::INFO, "Booting...");

    controller.begin();
    Debug::log(Debug::INFO, "Controller ready");
}

void loop() {
    controller.update();
}
