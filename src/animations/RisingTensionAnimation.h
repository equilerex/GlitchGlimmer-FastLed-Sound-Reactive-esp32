#pragma once
#include "Animation.h"
#include <FastLED.h>

// Buildup riser animation: pulse frequency, convergence speed, and brightness
// escalate over time and track f.buildup, accelerating from a steady pulse
// into a tight, rapid strobe before releasing into the drop.
class RisingTensionAnimation : public Animation {
    uint16_t step;
    uint8_t  gHue;

public:
    RisingTensionAnimation() : step(0), gHue(160) {}

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        fadeToBlackBy(leds, n, 40);

        // Tension ramps over a 16-second cycle (500 frames at ~30ms)
        step = (step + 1) % 512;
        const float tensionProgress = float(step) / 512.0f;
        const float tension = f.buildup > 0.05f ? max(tensionProgress, f.buildup) : tensionProgress;

        // Accelerate BPM from 60 to 240 as tension mounts
        const uint8_t bpm = uint8_t(60.0f + tension * 180.0f);
        const uint8_t pulse = beatsin8(bpm, 0, 255);

        // Scanner heads converge toward center with increasing speed
        const uint16_t headPos = beatsin16(bpm, 0, n / 2);
        const uint16_t left = headPos;
        const uint16_t right = (n - 1) - headPos;

        const uint8_t valHsv = uint8_t(255.0f * f.hsvLevel());
        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        // Color shifts from warm amber (30) up through violet (190) to electric cyan (140)
        const uint8_t hue = uint8_t(30.0f + tension * 130.0f);

        leds[left]  += CHSV(hue, 240, valHsv);
        leds[right] += CHSV(hue + 32, 240, valHsv);

        // Flashes tighten and intensify near the peak of tension
        if (tension > 0.65f && pulse > 210) {
            const uint8_t flashVal = scale8(pulse, brightAudio);
            leds[n / 2] += CRGB(flashVal, flashVal, flashVal);
        }

        EVERY_N_MILLISECONDS(50) { gHue++; }
    }
};
