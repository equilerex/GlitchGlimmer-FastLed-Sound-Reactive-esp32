#pragma once
#include "../animations/Animation.h"
#include <FastLED.h>

class BassPulseStormAnimation : public Animation {
public:
    static constexpr MoodType mood = MoodType::INTENSE;
    static constexpr float preferredTempo = 1.0f;
    static constexpr float intensity = 1.0f;

    void update(CRGB* leds, int count, const AudioFeatures& f) override {
        if (count <= 0) return;

        static uint8_t hue = 0;
        // A continuous drift as well as the jump on a beat. With only the beat
        // term this was a fixed colour whenever the detector was quiet, which
        // reads as the animation having stopped.
        hue += 1 + uint8_t(f.pixelLevel() * 3.0f);
        if (f.beatDetected) hue += 32;

        // bass is a share of the spectrum and level is the loudness against the
        // recent peak, so both are 0..1 and neither needs a device-scale constant.
        // The previous expression added peak * 128, which at a peak of 0.02 put
        // nothing on top of a bass share that is small whenever the music is
        // broadband, and the storm sat on its 50 floor.
        //
        // Curved, because at the level this microphone reports the drive came out
        // 0.13 and the whole strip sat at 69 of 255 through a track.
        const float drive = f.hsvLevel() * 0.6f + f.bass * 0.4f;
        uint8_t brightness = constrain(40 + drive * 215.0f, 40, 255);
        fill_solid(leds, count, CHSV(hue, 255, brightness));
    }
};
