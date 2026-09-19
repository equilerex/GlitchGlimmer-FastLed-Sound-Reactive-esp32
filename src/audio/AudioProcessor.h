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
    float gainSmoothing   = GAIN_SMOOTHING;
    float dynamicsRisePerBlock = DYN_EDGE_RISE;
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

    // Silence tracking. noiseFloor is a slow follower of the quietest recent block,
    // and it may rise only onto a block whose spectrum is noise-like, which is what
    // stops a track raising the floor it is being measured against. signalPresence is
    // the hysteresis gate that reads the floor, and gateGain is that gate ramped,
    // because a hard 0/1 switch strobes a signal sitting on the threshold.
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
    float dynamicsDecayPerBlock = DYN_EDGE_FALL;

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

    // ==== Structural detection ====
    //
    // Everything below answers "what shape is this passage" rather than "how loud
    // is it", and every window is in milliseconds because the device analyses a
    // block every 33 ms and the page steps once per frame.

    // The signal's own slow mean, and the one follower the two displacements are
    // both read from. A signed displacement has two halves and the two moods are
    // those halves named, so a single mean serves BUILDUP and DESCENT rather than
    // two of them that could disagree. See BUILDUP_TAU_SEC for the time constant,
    // which is set by the ramp speed it has to be able to see.
    float         slowLevel       = 0.0f;
    bool          slowSeeded      = false;
    unsigned long structuralLastMs = 0;

    // BUILDUP. The displacement has to hold for BUILDUP_HOLD_MS before it counts,
    // and the climb is measured from the level at the moment the displacement
    // began, so a plateau wobbling across the threshold is not a climb. No
    // cooldown, see BUILDUP_HOLD_MS.
    bool          buildupActive    = false;
    unsigned long buildupHoldSince = 0;
    float         buildupFromLevel = 0.0f;

    // DESCENT. The mirror, with DESCENT_FALL in place of BUILDUP_CLIMB.
    bool          descentActive    = false;
    unsigned long descentHoldSince = 0;
    float         descentFromLevel = 0.0f;

    // DROP's preceding quiet. A drop follows a breakdown, so the passage has to
    // have been quiet for DROP_ARM_MS before a slam counts, and at most one drop
    // is reported per DROP_COOLDOWN_MS so the mood cannot park there.
    unsigned long quietSince = 0;
    bool          quietHeld  = false;
    unsigned long lastDropMs = 0;

    // TEASE's fake-out test. A level whose mean stays mid while its variance is
    // high is a pulse that does not sustain, which is what teasing is. The ring is
    // fixed-capacity and written in place, like every other history here.
    float levelRing[TEASE_WINDOW] = {0};
    int   levelRingCount = 0;
    int   levelRingNext  = 0;

    // WEIRD's centroid test. The distance from the middle of the window the
    // centroid has covered, against the width of that window, so a steady passage
    // scores zero whatever its spectrum is and a drifting one does not.
    float centHi      = 0.0f;
    float centLo      = 0.0f;
    bool  centSeeded  = false;
    unsigned long weirdSince = 0;

    // The spread of the remembered beat intervals over their median, which is the
    // strongest eclectic signal available and costs one pass over twelve numbers.
    // Zero when there are too few intervals to say.
    float tempoSpread() const;

    // Return every structural detector to its unseeded state, without touching the
    // audio tracking around it. Two callers: the reset that follows a change of
    // input, and the guard at the top of updateStructure, which has to discard the
    // same set of windows because they were filled while the gate was shut and
    // measure the gate's ramp rather than the music.
    void clearStructure();

    // Everything above, run over the finished feature block. Separate from
    // analyzeAudio rather than inline because it is the half of that function
    // concerned with the shape of a passage, and the arithmetic in it is easier to
    // check against the constants it reads when it is not interleaved with the
    // spectrum.
    void updateStructure(AudioFeatures& features, unsigned long now);

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

    // Tune how quickly a remembered dynamics span closes while the envelope is
    // no longer moving. This is intentionally a per-analysis-block coefficient:
    // it is the same unit as the original device-side tuning constant.
    void setDynamicsDecayPerBlock(float rate) {
        dynamicsDecayPerBlock = constrain(rate, 0.0f, 1.0f);
    }
    float getDynamicsDecayPerBlock() const { return dynamicsDecayPerBlock; }

    // Tune the one-pole smoothing applied to the loudness estimate. Higher values
    // hold the previous estimate longer; lower values follow the input faster.
    void setGainSmoothing(float rate) {
        gainSmoothing = constrain(rate, 0.0f, 1.0f);
    }
    float getGainSmoothing() const { return gainSmoothing; }

    void setDynamicsGrowthPerBlock(float rate) {
        dynamicsRisePerBlock = constrain(rate, 0.0f, 1.0f);
    }
    float getDynamicsGrowthPerBlock() const { return dynamicsRisePerBlock; }

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
