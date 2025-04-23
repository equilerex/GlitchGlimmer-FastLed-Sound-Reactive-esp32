#pragma once
#include "../config/Config.h"
#include <Arduino.h>

struct AudioFeatures {
    float volume = 0.0f;            // Root mean square volume
    float loudness = 0.0f;          // Smoothed loudness (0–100)
    float peak = 0.0f;              // Peak amplitude
    float average = 0.0f;           // Mean signal level
    float agcLevel = 1.0f;          // Auto gain correction multiplier

    float bass = 0.0f;              // Low frequency energy
    float mid = 0.0f;               // Mid frequency energy
    float treble = 0.0f;            // High frequency energy

    float spectrumCentroid = 0.0f;  // Centroid of frequency content
    int dominantBand = 0;           // Index of loudest spectrum bin
    float dynamics = 0.0f;          // Difference between peak and average
    float energy = 0.0f;            // Sum of spectral magnitudes

    bool beatDetected = false;      // Beat detection flag
    float bpm = 0.0f;               // Estimated BPM
    int bassHits = 0;               // Count of strong bass impulses

    float noiseFloor = 0.0f;        // Tracked silence baseline

    bool signalPresence = false;  // True if volume exceeds noise floor (e.g., > 0.05)

    int16_t* waveform = nullptr;    // Pointer to time-domain samples
    size_t waveformSize = 0;        // Size of waveform buffer
    double spectrum[NUM_SAMPLES / 2] = {};  // FFT magnitudes

    float centroid = 0.0f;          //  ??
    float frequency = 0.0f;         //  frequency
};
