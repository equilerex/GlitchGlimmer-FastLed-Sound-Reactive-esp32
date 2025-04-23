// ====== AlienSquirtTrailLayer.h ======
#pragma once

#include <FastLED.h>
#include <deque>
#include "../audio/AudioFeatures.h"
#include "../audio/AudioSnapshot.h"
#include "../config/Config.h"

struct SquirtParticle {
    int position;
    float velocity;
    uint8_t hue;
    float life;
};

class AlienSquirtTrailLayer {
private:
    std::vector<SquirtParticle> particles;
    float spawnCooldown = 0;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& history) {
        spawnCooldown -= 1.0f;

        // Trigger new squirt on beat
        if (now.beatDetected && spawnCooldown <= 0) {
            SquirtParticle p;
            p.position = random(NUM_LEDS);
            p.velocity = random(-3, 4);
            p.hue = random(160, 200);  // Alien blue-violet
            p.life = 1.0f;
            particles.push_back(p);
            spawnCooldown = 10; // frames between squirts
        }

        // Update existing particles
        for (auto& p : particles) {
            p.position += p.velocity;
            p.velocity *= 0.9f;
            p.life -= 0.03f;
        }

        // Remove dead ones
        particles.erase(
            std::remove_if(particles.begin(), particles.end(), [](const SquirtParticle& p) {
                return p.life <= 0 || p.position < 0 || p.position >= NUM_LEDS;
            }),
            particles.end()
        );
    }

    void render(CRGB* leds, int count) {
        for (const auto& p : particles) {
            if (p.position >= 0 && p.position < count) {
                uint8_t brightness = p.life * 255;
                leds[p.position] += CHSV(p.hue, 255, brightness);
            }
        }
    }
};

