#pragma once

#include <Arduino.h>
#include <arduinoFFT.h>
#include "AudioFeatures.h"
#include "../config/Config.h"
#include "../core/Debug.h"

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

public:
    AudioProcessor();            // Construct and initialize FFT resources
    ~AudioProcessor();           // Clean up allocated resources

    void begin();                // Initialize the I2S hardware for audio capture
    void captureAudio();         // Read raw audio samples into internal buffers
    AudioFeatures analyzeAudio(); // Analyze buffered audio data and return computed features
};

// Note: method implementations moved to AudioProcessor.cpp