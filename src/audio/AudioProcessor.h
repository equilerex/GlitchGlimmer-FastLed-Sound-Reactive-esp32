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

    // The last few intervals between beats, which the tempo is the median of
    // rather than the newest one. See BEAT_BPM_WINDOW in Config.h for why the
    // median and not the interval.
    unsigned long beatIntervals[BEAT_BPM_WINDOW] = {0};
    int  beatIntervalCount = 0;
    int  beatIntervalNext  = 0;

    // The middle of the remembered intervals. A copy is sorted rather than the ring
    // itself, because the ring's order is its insertion order.
    unsigned long medianBeatInterval() const;

    // Silence tracking. noiseFloor is a slow follower of the quietest recent
    // block, and it rises only while signalPresence is false, which is what stops a
    // track raising the floor it is being measured against. signalPresence is the
    // hysteresis gate over it, and gateGain is that gate ramped, because a hard 0/1
    // switch strobes a signal sitting on the threshold.
    float noiseFloor     = 0.0f;
    bool  signalPresence = false;
    float gateGain       = 0.0f;

    // The tempo as of the last beat, which the readout fades from once the beats
    // stop. Fading the running value by a per-block factor made the speed of the
    // fade depend on the loop rate and put a two second plateau in front of a
    // plunge from the full tempo to zero inside a second.
    float bpmAtLastBeat  = 0.0f;

    // dynamics is the span the envelope covers while its window is open, so these
    // are the top and the bottom of that window rather than a smoothed ratio. See
    // DYN_EDGE_RISE in Config.h.
    float dynHi            = 0.0f;
    float dynLo            = 0.0f;
    bool  dynamicsSeeded   = false;

    // The bass band's raw magnitude on the block before this one, which a beat has
    // to rise above. See BEAT_BASS_RISE in Config.h for why the level rise alone
    // was not enough and why this is the energy rather than the share.
    float previousBassEnergy = 0.0f;

    // The loudest block seen recently, which is what features.level is a fraction
    // of. This is what makes the level independent of the microphone's gain.
    float levelRef = 0.0f;

    // features.level is measured from this rather than from a single block, so
    // that it reports how loud the surroundings are instead of how hard the last
    // kick hit. See LEVEL_ENV_ATTACK in Config.h for why the two are not the same
    // number and why the envelope is asymmetric.
    float levelEnv       = 0.0f;
    bool  levelEnvSeeded = false;

    // The band equivalents of levelRef, for the three bandLevels. Beside the level
    // reference rather than in the feature block, which is allocated fresh every
    // call.
    AudioFeatures::BandRefs bandRefs;

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

    // Forget every rolling reference and return to the unseeded state, so the next
    // block is measured on its own terms.
    //
    // Two kinds of state live in this class. The first is a measurement of the
    // block just submitted: volume, peak, the band shares, energy. The second is a
    // memory of blocks already gone: the noise floor, the level and band peaks,
    // the smoothed values, the gate, the beat clock. The second kind is what makes
    // the features gain-independent, and it is also what makes them meaningless
    // across a change of input, because every reference is a statistic of a signal
    // that is no longer playing.
    //
    // The browser switches between a synthetic demo signal and the microphone, and
    // the demo is roughly forty times louder than this microphone. A level measured
    // against the peak the demo left behind reads about a fortieth of the truth,
    // and levelRef forgets at 0.995 per block, so it took some thirteen seconds to
    // decay far enough for the values to climb back. Nothing was wrong with the
    // microphone and nothing was wrong with the normalisation. The reference
    // belonged to audio that had stopped.
    void resetTracking();
};

// Note: method implementations moved to AudioProcessor.cpp