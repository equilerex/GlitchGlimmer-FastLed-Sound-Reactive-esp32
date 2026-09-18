#pragma once

#include <FastLED.h>
#include <cmath>

#include "VisualLayer.h"
#include "../../audio/AudioFeatures.h"
#include "../../config/Config.h"
#include "../../audio/AudioSnapshot.h"


// === Layer 4: Energy Pulse River ===
class EnergyPulseRiverLayer : public VisualLayer {
    float position = 0.0f;
    float speed = 0.0f;
    uint8_t hue = 0;

public:
    void update(const AudioFeatures& audio, const AudioHistory& snapshots) override {
        // level, not energy. energy is a raw FFT magnitude sum in the hundreds,
        // so energy * 0.5 moved position by hundreds of pixels a frame and the
        // river was a strobe rather than a flow, and energy * 255 truncated to
        // uint8_t made the hue noise.
        speed = audio.level * 8.0f;
        position += speed;
        hue = (uint8_t)(audio.level * 255);
    }

    void render(CRGB* leds, int count) override {
        if (count <= 0) return;

        // Reduced here, where the period is known. render() is the only place
        // that sees `count`, and the wave's period is exactly `count`, so
        // fmod(position, count) is identical to the unwrapped expression while
        // keeping the accumulator bounded. Left unreduced this eventually
        // reaches float's precision limit.
        position = fmodf(position, float(count));

        for (int i = 0; i < count; ++i) {
            float phase = fmodf(position + i * 0.1f, float(count));
            // sin8 already spans the whole byte, 0 to 255 with its middle at 128,
            // which is the 128 plus or minus 127 this was written as. Written out
            // that way it was an int reaching 32513 assigned to a uint8_t, so it
            // wrapped through the byte several times along the strip and the river
            // was a one-pixel strobe train rather than a wave. This is the base
            // layer of every scene.
            uint8_t bright = sin8((uint8_t)(phase));
            leds[i] += CHSV(hue, 255, bright);
        }
    }

    const char* getName() const override { return "EnergyPulseRiverLayer"; }

    // Test seam. The wrap lives in render(), where count is known, so this is
    // only meaningful after a render. Bound is `count`.
    float debugPosition() const { return position; }
};

// === Layer 5: Dominant Band Fire Trail ===
class DominantBandFireTrailLayer : public VisualLayer {
    float center = 0.0f;
    float heat = 0.0f;

public:
    void update(const AudioFeatures& audio, const AudioHistory&) override {
        center = map(audio.dominantBand, 0, NUM_SAMPLES / 2, 0, 255);
        // The bandLevels, not the shares. Both shares read under 0.05 for bass and
        // under 0.41 for treble on the microphone in use, so heat sat near zero and
        // every pixel was drawn at the base of its palette.
        heat = audio.bassLevel + audio.trebleLevel;
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
    void update(const AudioFeatures& audio, const AudioHistory&) override {
        // As a fraction of the highest the floor may reach, not as 80 hue units per
        // unit of floor. The floor is bounded by NOISE_FLOOR_MAX, so a fixed 80
        // units per unit left the whole term inside one hue unit and this mist was
        // the same colour on every frame of every scene.
        baseHue = uint8_t(160.0f + audio.noiseFloor / NOISE_FLOOR_MAX * 60.0f);
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            // 72 and not 20. FastLED squares CHSV's val on the way out, so a val of
            // 20 emits a duty of 20 * 20 / 255, which is 1.6 of 255, and this mist
            // contributed nothing on any frame of any scene. 72 is the val whose
            // square is 20, which is the emission the 20 was written as.
            leds[i] += CHSV(baseHue, 100, 72);
        }
    }

    const char* getName() const override { return "NoiseFloorMistLayer"; }
};

// === Layer 7: Dynamics Flicker Storm ===
class DynamicsFlickerStormLayer : public VisualLayer {
    // The share of pixels that flicker on a given frame. A member rather than
    // audio.dynamics read again in render(), so that what the flicker is drawn from
    // and what the compositor scales by cannot disagree.
    float density = 0.0f;

public:
    void update(const AudioFeatures& audio, const AudioHistory&) override {
        // opacity is the compositor's share of the light. Every other layer here sets
        // it to a constant and this one drove it from dynamics, which made it double as
        // an effect control: dynamics reads 0.00 to 0.26 on the real microphone, so the
        // layer sat black for most of a track and dim for the rest, at a share that
        // wandered with the loudness window rather than with anything the storm is.
        opacity = 0.6f;

        // dynamics is a fraction of the window it has itself measured, so it is already
        // normalised and the mapping needs no constant about this input's gain. The
        // floor is what stops the layer costing a frame while drawing nothing, and
        // gateGain is what stops it drawing in a silent room: the flicker is drawn at
        // random rather than scaled by level, so unlike the other layers it does not
        // inherit the gate for free.
        density = (0.4f + 0.6f * audio.dynamics) * audio.gateGain;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            if (random8() < density * 255) {
                leds[i] += CHSV(random8(), 200, random8(32, 128));
            }
        }
    }

    const char* getName() const override { return "DynamicsFlickerStormLayer"; }
};

class TriwaveBeatLayer : public VisualLayer {
    bool direction = true;

public:
    void update(const AudioFeatures& now, const AudioHistory&) override {
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
    void update(const AudioFeatures& now, const AudioHistory&) override {
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
    // A member, not a function-local static. As a static it was one array shared by
    // every instance and by both strips, so two strips running this layer wrote
    // through each other's trail.
    //
    // Sized to LED_0_NUM, which the firmware treats as the longest strip. That was an
    // assumption when the array was written and is now checked: the render loop is
    // bounded below, because indexing it by an arbitrary strip length overruns the
    // moment a strip is longer than 100.
    float heat[LED_0_NUM] = {};
    float decay = 0.9f;
    int   band  = 0;

public:
    void update(const AudioFeatures& now, const AudioHistory&) override {
        // The band is remembered rather than mapped here, because update() is not told
        // how long the strip is. Mapping it against LED_0_NUM put the head off the end
        // of any longer strip and in the wrong place on the ten-pixel one.
        band = now.dominantBand;
    }

    void render(CRGB* leds, int count) override {
        const int trail = count < LED_0_NUM ? count : LED_0_NUM;
        const int pos   = map(band, 0, NUM_SAMPLES / 2, 0, trail - 1);
        for (int i = 0; i < trail; ++i) {
            heat[i] *= decay;
        }
        if (pos >= 0 && pos < trail) {
            heat[pos] = 1.0f;
        }
        for (int i = 0; i < trail; ++i) {
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

    void update(const AudioFeatures& now, const AudioHistory&) override {
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

    void update(const AudioFeatures& now, const AudioHistory&) override {
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
    BassShockwaveLayer() {
        name = "BassShockwave";
        // Accent layer, so it sits on top of the base instead of replacing it. On an
        // additive layer this value is how much light the wave contributes; past
        // roughly 0.8 the ring clips to white and stops reading as a ring.
        opacity = 0.7f;
    }

    void update(const AudioFeatures& now, const AudioHistory&) override {
        if (now.beatDetected && now.bassLevel > 0.8f) {
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
    void update(const AudioFeatures& now, const AudioHistory&) override {
        // Wrapped at 255/40, which is this layer's own hue period: render() takes
        // fmod(angle * 40, 255), so subtracting that multiple leaves the output
        // bit-identical while keeping the accumulator bounded.
        offset += now.dynamics * 0.5f;
        if (offset >= 6.375f) offset = fmodf(offset, 6.375f);
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
    void update(const AudioFeatures& now, const AudioHistory&) override {
        // Stored curved, because the fog's brightness is the thing that was flat
        // at the level this microphone reports. The hue map below reads the same
        // value, so its 160..220 sweep moves with the curve as well; the band is
        // narrow enough that this changes how blue the fog is, not what colour it
        // is, and the brightness is the part that was wrong.
        energy = now.hsvLevel();
    }

    void render(CRGB* leds, int count) override {
        // Mapped against 0..1, not 0..2000. The old brightness was energy / 10
        // against a raw sum in the hundreds, so it sat at its 180 ceiling on
        // every frame and the fog was a flat wash.
        uint8_t hue = map(energy * 255, 0, 255, 160, 220);  // Bluish fog to white-hot
        uint8_t brightness = constrain(energy * 180, 0, 180);
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
    void update(const AudioFeatures& now, const AudioHistory&) override {
        // level, not loudness. loudness is volume * 100 and volume is an
        // absolute RMS near 0.008 on the microphone in use, so the old
        // `> 60.0f` test never fired and this layer never drew.
        lastLoudness = now.level;
    }

    void render(CRGB* leds, int count) override {
        if (lastLoudness > 0.6f && random(10) < 3) {
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
    void update(const AudioFeatures& now, const AudioHistory& history) override {
        if (history.size() < 10) return;
        float moodSum = 0;
        for (int i = 0; i < 10; ++i) {
            // level, not volume, so the arc tracks loudness at any gain. The
            // snapshot carries both now; volume alone sat near 0.008 on this
            // microphone and the arc never changed hue.
            moodSum += history[history.size() - 1 - i].level;
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
    void update(const AudioFeatures& now, const AudioHistory&) override {
        treble = now.trebleLevel;
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

    void update(const AudioFeatures& now, const AudioHistory& history) override {
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

    void update(const AudioFeatures&, const AudioHistory&) override {}

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

    void update(const AudioFeatures& now, const AudioHistory&) override {
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

    void update(const AudioFeatures& now, const AudioHistory&) override {
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
        void update(const AudioFeatures& now, const AudioHistory& snapshots) override {
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
    void update(const AudioFeatures& now, const AudioHistory& snapshots) override {
        hueBase = now.spectrumCentroid * 2;  // Map to hue
        // Wrapped at 256, the modulus render() already applies. Letting it grow
        // also pushed (int)flow past INT_MAX, which is undefined on conversion.
        flow += now.level * 3.0f;
        if (flow >= 256.0f) flow -= 256.0f;
    }

    void render(CRGB* leds, int count) override {
        for (int i = 0; i < count; ++i) {
            float wave = sin8((i * 4 + (int)flow) % 256);
            leds[i] += CHSV(hueBase, 255, wave);
        }
    }

    const char* getName() const override { return "CentroidColorFlowLayer"; }
};

