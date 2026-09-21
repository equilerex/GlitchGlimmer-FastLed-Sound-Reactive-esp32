#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class BassPulseStormAnimation : public Animation {
public:
    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        static uint8_t hue = 0;
        // A continuous drift as well as the jump on a beat. With only the beat
        // term this was a fixed colour whenever the detector was quiet, which
        // reads as the animation having stopped.
        hue += 1 + uint8_t(f.pixelLevel() * 3.0f);
        if (f.beatDetected) hue += 32;

        // bassLevel is the bass band against its own recent peak, the same 0..1
        // question level asks of loudness, so neither term needs a device-scale
        // constant. The previous expression added peak * 128, which at a peak of
        // 0.02 put nothing on top of a bass share that is small whenever the music
        // is broadband, and the storm sat on its 50 floor. The share itself is no
        // better: measured at 0.001 to 0.049 on the microphone in use.
        //
        // Curved, because at the level this microphone reports the drive came out
        // 0.13 and the whole strip sat at 69 of 255 through a track.
        const float weight = f.music.initialized ? f.music.weight.value : f.bassLevel;
        // beatPhase pulse accentuating the bass impact
        const float phasePulse = 1.0f - f.beatPhase;
        const float drive = f.hsvLevel() * 0.4f + weight * 0.4f + phasePulse * f.beatConfidence * 0.2f;
        uint8_t brightness = constrain(40 + drive * 215.0f, 40, 255);
        fill_solid(leds, count, CHSV(hue, 255, brightness));
    }
};
