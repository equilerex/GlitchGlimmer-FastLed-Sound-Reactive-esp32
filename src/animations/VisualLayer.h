#pragma once

#include <FastLED.h>
#include <deque>
#include "../audio/AudioFeatures.h"
#include "../audio/AudioSnapshot.h"

class VisualLayer {
public:
    virtual ~VisualLayer() = default;

    float opacity = 1.0f;
    String name = "Unnamed";

    // Optional: How long should this layer remain active (ms)
    unsigned long lifetimeMs = 0;
    unsigned long activationTime = 0;

    // Optional category tagging
    bool persistent = false;

    virtual void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& history) = 0;
    virtual void render(CRGB* leds, int count) = 0;

    bool isExpired(unsigned long now) const {
        return lifetimeMs > 0 && now - activationTime >= lifetimeMs;
    }

    void resetLifetime(unsigned long now, unsigned long duration) {
        activationTime = now;
        lifetimeMs = duration;
    }
};
