// ====== AlienSquirtTrailLayer.h ======
#pragma once

#include <FastLED.h>
#include <deque>

#include "../animations/VisualLayer.h"
#include "../audio/AudioFeatures.h"
#include "../config/Config.h"
#include "../audio/AudioSnapshot.h"


// === Layer 4: Energy Pulse River ===
class EnergyPulseRiverLayer : public VisualLayer {
    float position = 0.0f;
    float speed = 0.0f;
    uint8_t hue = 0;

public:
    void update(const AudioFeatures& audio, const std::deque<AudioSnapshot>& snapshots) override {
        speed = audio.energy * 0.5f;
        position += speed;
        hue = (uint8_t)(audio.energy * 255);
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float phase = fmod(position + i * 0.1f, count);
            uint8_t bright = 128 + 127 * sin8((uint8_t)(phase));
            leds[i] += CHSV(hue, 255, bright);
        }
    }

    const char* getName() const override { return "EnergyPulseRiverLayer"; }
};

// === Layer 5: Dominant Band Fire Trail ===
class DominantBandFireTrailLayer : public VisualLayer {
    float center = 0.0f;
    float heat = 0.0f;

public:
    void update(const AudioFeatures& audio, const std::deque<AudioSnapshot>&) override {
        center = map(audio.dominantBand, 0, NUM_SAMPLES / 2, 0, 255);
        heat = audio.bass + audio.treble;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float dist = abs(i - center * count / 255.0f);
            float intensity = max(0.0f, 1.0f - dist / (count * 0.2f));
            leds[i] += CHSV(20 + heat * 40, 255, intensity * 255);
        }
    }

    const char* getName() const override { return "DominantBandFireTrailLayer"; }
};

// === Layer 6: NoiseFloor Mist ===
class NoiseFloorMistLayer : public VisualLayer {
    uint8_t baseHue = 160;

public:
    void update(const AudioFeatures& audio, const std::deque<AudioSnapshot>&) override {
        baseHue = 160 + audio.noiseFloor * 80;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            leds[i] += CHSV(baseHue, 100, 20);
        }
    }

    const char* getName() const override { return "NoiseFloorMistLayer"; }
};

// === Layer 7: Dynamics Flicker Storm ===
class DynamicsFlickerStormLayer : public VisualLayer {
public:
    void update(const AudioFeatures& audio, const std::deque<AudioSnapshot>&) override {
        opacity = audio.dynamics * 1.0f;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            if (random8() < opacity * 255) {
                leds[i] += CHSV(random8(), 200, random8(32, 128));
            }
        }
    }

    const char* getName() const override { return "DynamicsFlickerStormLayer"; }
};

class TriwaveBeatLayer : public VisualLayer {
    bool direction = true;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        if (now.beatDetected) direction = !direction;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; i++) {
            float pos = (float)i / count;
            float tri = direction
                ? abs(fmod(pos * 2.0, 1.0f) * 2.0f - 1.0f)
                : abs(fmod((1.0f - pos) * 2.0, 1.0f) * 2.0f - 1.0f);
            uint8_t brightness = tri * 255;
            leds[i] += CHSV(200, 255, brightness);
        }
    }

    const char* getName() const override { return "TriwaveBeatLayer"; }
};

class EnergySpiralLayer : public VisualLayer {
public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        // No dynamic state needed, just reacts
    }

    void render(CRGB* leds, int count) override {
        float hueOffset = fmod(millis() / 50.0, 255);
        for (int i = 0; i < count; ++i) {
            float phase = float(i) / count * 6.2831f; // 2π
            float amp = sin(phase + millis() / 200.0) * 0.5 + 0.5;
            leds[i] += CHSV(hueOffset + amp * 100, 255, amp * 100);
        }
    }

    const char* getName() const override { return "EnergySpiralLayer"; }
};
class DominantBandTrailLayer : public VisualLayer {
    int pos = 0;
    float decay = 0.9f;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        pos = map(now.dominantBand, 0, NUM_SAMPLES / 2, 0, LED_0_NUM - 1); // assuming LED_0_NUM is longest strip
    }

    void render(CRGB* leds, int count) override {
        static float heat[LED_0_NUM] = {};
        for (int i = 0; i < count; ++i) {
            heat[i] *= decay;
        }
        if (pos >= 0 && pos < count) {
            heat[pos] = 1.0f;
        }
        for (int i = 0; i < count; ++i) {
            leds[i] += CHSV(140, 255, heat[i] * 255);
        }
    }

    const char* getName() const override { return "DominantBandTrailLayer"; }
};

class WaveformScribbleLayer : public VisualLayer {
private:
    int16_t localWaveform[256]; // Store a local copy instead of just a pointer
    int waveformSize = 0;
    unsigned long lastUpdateTime = 0;

public:
    // Constructor
    WaveformScribbleLayer() : VisualLayer() {
        // Initialize local waveform buffer
        memset(localWaveform, 0, sizeof(localWaveform));
        name = "WaveformScribble"; // Set a name for this layer
        waveformSize = 0;
        lastUpdateTime = 0;
    }

    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        // Only update at most every 50ms to avoid rapid memory accesses
        unsigned long currentTime = millis();
        if (currentTime - lastUpdateTime < 50) {
            return;
        }
        
        lastUpdateTime = currentTime;
        
        // Safely copy waveform data to our local buffer
        if (now.waveform != nullptr && now.waveformSize > 0) {
            int sizeToCopy = min(now.waveformSize, (int)sizeof(localWaveform)/sizeof(localWaveform[0]));
            memcpy(localWaveform, now.waveform, sizeToCopy * sizeof(int16_t));
            waveformSize = sizeToCopy;
        }
    }

    void render(CRGB* leds, int count) override {
        if (waveformSize <= 0) return;

        for (int i = 0; i < count; ++i) {
            // Safe mapping with bounds checking
            int waveformIndex = map(i, 0, count - 1, 0, waveformSize - 1);
            waveformIndex = constrain(waveformIndex, 0, waveformSize - 1);
            
            // Safe normalization
            float value = localWaveform[waveformIndex] / 32768.0f; // normalize to -1..1
            uint8_t brightness = constrain(abs(value) * 255, 0, 255);
            leds[i] += CHSV(map(i, 0, count, 0, 255), 255, brightness);
        }
    }

    const char* getName() const override { return "WaveformScribbleLayer"; }
};

class CentroidRadianceLayer : public VisualLayer {
    float ripplePhase = 0;
    float spectrumCentroid = 0;

public:
    CentroidRadianceLayer() {
        name = "CentroidRadiance";
        opacity = 0.7f;
        ripplePhase = 0;
        spectrumCentroid = 0;
    }

    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        ripplePhase += 0.1f;
        spectrumCentroid = now.spectrumCentroid;
    }

    void render(CRGB* leds, int count) override {
        int center = map(spectrumCentroid, 0, NUM_SAMPLES / 2, 0, count - 1);
        for (int i = 0; i < count; ++i) {
            float dist = abs(i - center);
            float pulse = sin(dist * 0.3f + ripplePhase);
            uint8_t bright = constrain((pulse + 1.0f) * 128, 0, 255);
            leds[i] += CHSV(center, 255, bright);
        }
    }

    const char* getName() const override { return "CentroidRadianceLayer"; }
};

class BassShockwaveLayer : public VisualLayer {
    int frame = 999;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        if (now.beatDetected && now.bass > 0.8f) {
            frame = 0;
        } else {
            frame++;
        }
    }

    void render(CRGB* leds, int count) override {
        float radius = frame * 0.8f;
        for (int i = 0; i < count; ++i) {
            float dist = abs(i - count / 2);
            float wave = exp(-pow((dist - radius) / 5.0f, 2));
            uint8_t brightness = wave * 255;
            leds[i] += CHSV(0, 255, brightness);
        }
    }

    const char* getName() const override { return "BassShockwaveLayer"; }
};


class WormholeVortexLayer : public VisualLayer {
    float offset = 0;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        offset += now.dynamics * 0.5f;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float angle = offset + i * 0.15f;
            uint8_t hue = fmod(angle * 40, 255);
            leds[i] += CHSV(hue, 255, 80);
        }
    }

    const char* getName() const override { return "WormholeVortexLayer"; }
};

class EnergyFogLayer : public VisualLayer {
public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        energy = now.energy; // Store energy from audio features
    }

    void render(CRGB* leds, int count) override {
        uint8_t hue = map(energy, 0, 2000, 160, 220);  // Bluish fog to white-hot
        uint8_t brightness = constrain(energy / 10, 0, 180);
        for (int i = 0; i < count; ++i) {
            leds[i] += CHSV(hue, 40, brightness);
        }
    }

    const char* getName() const override { return "EnergyFogLayer"; }
private:
    float energy = 0;
};
class LoudnessLightningLayer : public VisualLayer {
    float lastLoudness = 0;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        lastLoudness = now.loudness;
    }

    void render(CRGB* leds, int count) override {
        if (lastLoudness > 60.0f && random(10) < 3) {
            int start = random(0, count - 10);
            int length = random(5, 15);
            for (int i = start; i < start + length && i < count; ++i) {
                leds[i] += CHSV(180 + random(50), 50 + random(100), 255);
            }
        }
    }

    const char* getName() const override { return "LoudnessLightningLayer"; }
};
class MoodMemoryArcLayer : public VisualLayer {
    float avgMood = 0;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& history) override {
        if (history.size() < 10) return;
        float moodSum = 0;
        for (int i = 0; i < 10; ++i) {
            moodSum += history[history.size() - 1 - i].volume;
        }
        avgMood = moodSum / 10.0f;
    }

    void render(CRGB* leds, int count) override {
        uint8_t hue = map(avgMood * 100, 0, 100, 0, 255);
        for (int i = count / 4; i < count * 3 / 4; ++i) {
            leds[i] += CHSV(hue, 180, 80);
        }
    }

    const char* getName() const override { return "MoodMemoryArcLayer"; }
};
class TrebleSparkleLayer : public VisualLayer {
private:
    float treble = 0.0f;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        treble = now.treble;
    }

    void render(CRGB* leds, int count) override {
        int numSparks = map(treble * 100, 0, 100, 0, 10);
        for (int i = 0; i < numSparks; ++i) {
            int pos = random(count);
            leds[pos] += CHSV(200 + random(55), 255, 180 + random(75));
        }
    }

    const char* getName() const override { return "TrebleSparkleLayer"; }
};


class CentroidGlowWipeLayer : public VisualLayer {
private:
    float pos = 0.0f;

public:
    CentroidGlowWipeLayer() {
        name = "CentroidGlowWipe";
        opacity = 0.6f;
    }

    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& history) override {
        pos = now.spectrumCentroid / float(NUM_SAMPLES / 2); // normalized 0–1
    }

    void render(CRGB* leds, int count) override {
        int center = int(pos * count);
        for (int i = 0; i < count; ++i) {
            float dist = fabs(i - center);
            uint8_t brightness = qsub8(128, dist * 6);
            leds[i] += CHSV(170, 200, brightness);
        }
    }

    const char* getName() const override { return "CentroidGlowWipeLayer"; }
};

class SpectralRibbonLayer : public VisualLayer {
public:
    SpectralRibbonLayer() {
        name = "SpectralRibbon";
        opacity = 0.4f;
    }

    void update(const AudioFeatures&, const std::deque<AudioSnapshot>&) override {}

    void render(CRGB* leds, int count) override {
        int bands = 16;
        int ledsPerBand = count / bands;
        for (int i = 0; i < bands; ++i) {
            int start = i * ledsPerBand;
            CRGB color = CHSV(i * (255 / bands), 255, 80);
            for (int j = start; j < start + ledsPerBand && j < count; ++j) {
                leds[j] += color;
            }
        }
    }

    const char* getName() const override { return "SpectralRibbonLayer"; }
};



class BPMWavePulseLayer : public VisualLayer {
private:
    float position = 0.0f;
    unsigned long lastUpdate = 0;

public:
    BPMWavePulseLayer() {
        name = "BPMWavePulse";
        opacity = 0.5f;
    }

    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        unsigned long nowMillis = millis();
        float interval = now.bpm > 0.0f ? 60000.0f / now.bpm : 500.0f;

        if (nowMillis - lastUpdate > interval) {
            lastUpdate = nowMillis;
            position = 0.0f;
        }

        position += 0.05f;  // Move pulse forward
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float dist = fabs(i - (position * count));
            uint8_t brightness = qsub8(255, dist * 15);
            if (brightness > 0) {
                leds[i] += CHSV(200, 255, brightness);
            }
        }
    }

    const char* getName() const override { return "BPMWavePulseLayer"; }
};


class BeatFlashSparkLayer : public VisualLayer {
private:
    uint8_t cooldown = 0;

public:
    BeatFlashSparkLayer() {
        name = "BeatFlashSpark";
        opacity = 0.7f;
    }

    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>&) override {
        if (now.beatDetected) {
            cooldown = 10;
        } else if (cooldown > 0) {
            cooldown--;
        }
    }

    void render(CRGB* leds, int count) override {
        if (cooldown == 0) return;
        for (int i = 0; i < count; ++i) {
            if (random8() < 20) {
                leds[i] += CHSV(random8(), 255, 255);
            }
        }
    }

    const char* getName() const override { return "BeatFlashSparkLayer"; }
};

class BPMBeatFlashLayer : public VisualLayer {
    private:
        int flashTime = 0;
        float lastBPM = 0;
    
    public:
        void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& snapshots) override {
            if (now.beatDetected) {
                flashTime = 5;
                lastBPM = now.bpm;
            }
            else if (flashTime > 0) {
                flashTime--;
            }
        }
    
        void render(CRGB* leds, int count) override {
            if (flashTime > 0) {
                CHSV color = CHSV((int)lastBPM % 255, 255, 100);
                for (int i = 0; i < count; ++i) {
                    leds[i] += color;
                }
            }
        }

        const char* getName() const override { return "BPMBeatFlashLayer"; }
    };


class CentroidColorFlowLayer : public VisualLayer {
private:
    float flow = 0.0f;
    float hueBase = 0;

public:
    void update(const AudioFeatures& now, const std::deque<AudioSnapshot>& snapshots) override {
        hueBase = now.spectrumCentroid * 2;  // Map to hue
        flow += now.volume * 3.0f;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float wave = sin8((i * 4 + (int)flow) % 256);
            leds[i] += CHSV(hueBase, 255, wave);
        }
    }

    const char* getName() const override { return "CentroidColorFlowLayer"; }
};

