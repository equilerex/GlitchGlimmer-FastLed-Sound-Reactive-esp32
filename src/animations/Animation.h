// Animation.h
#pragma once
#include <FastLED.h>
#include "../audio/AudioFeatures.h"

class Animation {
public:
    virtual ~Animation() = default;
    virtual void begin() {}
    virtual void update(CRGB* leds, int n, const AudioFeatures& features) = 0;
};