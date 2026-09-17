#pragma once

#include <Arduino.h>
#include <arduinoFFT.h>
#include "AudioFeatures.h"
#include "../config/Config.h"

// AudioProcessor: captures audio via I2S and performs FFT-based feature extraction
class AudioProcessor {
private:
    // Audio sample buffers for FFT and waveform.
    // float, not double: the ESP32 has a single-precision FPU and no double unit,
    // so double runs in software and costs roughly an order of magnitude more.
    float vReal[NUM_SAMPLES];
    float vImag[NUM_SAMPLES];
    int16_t buffer[NUM_SAMPLES];

    // FFT engine instance
    ArduinoFFT<float>* FFT;

    // State for smoothing, level detection, and beat timing
    float gainSmoothing   = 0.85f;
    float volume          = 0.0f;
    float previousVolume  = 0.0f;
    float loudness        = 0.0f;
    float peak            = 0.0f;
    unsigned long lastBeatTime = 0;
    float currentBPM      = 0.0f;

    // Silence tracking. noiseFloor is a slow follower of the quietest recent
    // block, signalPresence is the hysteresis gate over it, and gateGain is that
    // gate ramped, because a hard 0/1 switch strobes a signal sitting on the
    // threshold.
    float noiseFloor     = 0.0f;
    bool  signalPresence = false;
    float gateGain       = 0.0f;

    // dynamics is a single-sample maximum against a block average, so the raw
    // value moves several tenths between frames on a signal that is not changing.
    float smoothedDynamics = 0.0f;
    bool  dynamicsSeeded   = false;

    // The loudest block seen recently, which is what features.level is a fraction
    // of. This is what makes the level independent of the microphone's gain.
    float levelRef = 0.0f;

    // features.level is the ratio above, followed by an asymmetric filter. Raw, the
    // ratio is a spike train: any block louder than the last sets it to 1 for that
    // one frame, which at this block rate is most of a second's worth of spikes.
    float levelSmoothed = 0.0f;
    bool  levelSeeded   = false;

    // The peak the three band shares are normalised against. One reference for all
    // three, not one per band: shares already carry the relationship between the
    // bands, and a per-band reference would normalise a noise-only band to 1
    // against its own noise, so a pure tone would report all three at full. The
    // shared reference lifts the set so the dominant band fills its bar, which the
    // shares alone never did.
    float bandsRef = 0.0f;

    // A single 512-sample block's band magnitudes jitter by several percent
    // between blocks of a signal that is not changing, and the animations colour
    // with these directly, so that jitter reads as a strobing hue. Smoothed
    // asymmetrically: fast on the way up so a hit still lands on the frame it
    // arrives, slower on the way down so the colour decays instead of flickering.
    float smoothBass   = 0.0f;
    float smoothMid    = 0.0f;
    float smoothTreble = 0.0f;
    bool  bandsSeeded  = false;

public:
    AudioProcessor();            // Construct and initialize FFT resources
    ~AudioProcessor();           // Clean up allocated resources

    void begin();                // Initialize the I2S hardware for audio capture
    void captureAudio();         // Read raw audio samples into internal buffers
    AudioFeatures analyzeAudio(); // Analyze buffered audio data and return computed features

    // Fill the analysis buffers from already-captured samples, each normalised to
    // -1..1. The host and browser builds have no I2S peripheral to read from, so
    // this is how they get audio in. The samples must be NUM_SAMPLES long, which
    // is what the FFT is sized for; a shorter block leaves the tail silent.
    void submitSamples(const float* samples, size_t count);
};

// Note: method implementations moved to AudioProcessor.cpp