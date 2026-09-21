#pragma once
#include "Animation.h"
#include "BeatClock.h"
#include <FastLED.h>

class PlayaChaosAnimation : public Animation {
    uint8_t       gHue;
    uint8_t       currentMood;
    HoldSelect    moodSelect;
    uint32_t      lastMoodChange;
    uint16_t      dustOffset;
    uint8_t       artCarPos;
    uint8_t       artCarSpeed;
    CRGBPalette16 moodPalettes[3];

public:
    PlayaChaosAnimation()
        : gHue(0), currentMood(0), lastMoodChange(0), dustOffset(0),
          artCarPos(0), artCarSpeed(2) {
        moodPalettes[0] = CRGBPalette16(CRGB::Black, CRGB::Red, CRGB::Orange, CRGB::Yellow);
        moodPalettes[1] = CRGBPalette16(CRGB::Black, CRGB::Purple, CRGB::Blue, CRGB::Indigo);
        moodPalettes[2] = CRGBPalette16(CRGB::Black, CRGB::Green, CRGB::Cyan, CRGB::Lime);
    }

    void update(CRGB* leds, int n, const AudioFeatures& f) override {
        if (n <= 0) return;

        const uint32_t now = millis();
        // The palette follows the sound: dark and heavy is fire, mid is dusk,
        // bright is cool. It used to rotate every 8 s regardless of the music.
        const float bri = f.music.brightness.value;
        const float scores[3] = {
            0.6f * f.music.weight.value + 0.4f * (1.0f - bri),
            0.8f * (1.0f - 2.0f * fabsf(bri - 0.5f)),
            bri
        };
        const uint8_t pick = uint8_t(moodSelect.update(scores, 3, 0.12f, 5000));
        if (pick != currentMood) {
            currentMood = pick;
            lastMoodChange = now;
        }

        EVERY_N_MILLISECONDS(40) { ++gHue; dustOffset += 2; }

        fadeToBlackBy(leds, n, 30);

        const uint8_t brightAudio = uint8_t(255.0f * f.pixelLevel());

        // Base dust storm noise
        for (int i = 0; i < n; ++i) {
            const uint8_t noise = inoise8(i * 14, dustOffset);
            leds[i] = ColorFromPalette(moodPalettes[currentMood], noise + gHue, brightAudio, LINEARBLEND);
        }

        // Mutant art cars racing across the strip with trails
        artCarPos = (artCarPos + artCarSpeed) % n;
        leds[artCarPos] = CHSV(gHue + 128, 240, uint8_t(255.0f * f.hsvLevel()));

        // Thunderstorm flash on weird anomalies
        if (f.anomaly > 1.0f || random8() < 4) {
            const int p = random16(n);
            leds[p] = CRGB(brightAudio, brightAudio, brightAudio);
        }
    }
};
