#pragma once
#include "../config/Config.h"
#include <Arduino.h>
#include <cmath>

struct AudioFeatures {
    float volume = 0.0f;            // Root mean square volume
    float loudness = 0.0f;          // Smoothed loudness (0–100)
    float peak = 0.0f;              // Peak amplitude
    float average = 0.0f;           // Mean signal level
    float agcLevel = 1.0f;          // Auto gain correction multiplier

    // Current level as a fraction of the loudest recent block, so it is 0..1 at
    // any microphone gain. This is the field an effect should drive loudness
    // from. energy and the raw magnitudes are absolute sums whose scale depends
    // entirely on the input, so a constant compared against them is really a
    // guess about one microphone, and every such constant in the animations was
    // wrong for the one in use. level needs no constant.
    float level = 0.0f;

    // level, curved for driving pixel brightness. A method and not another field,
    // so there is nothing to stamp: every producer that sets level gets this
    // correct for free, including the harness, which builds AudioFeatures by hand
    // and would otherwise leave a field at zero and render every animation black.
    //
    // Derived from level rather than replacing it, because the mood classifier's
    // thresholds are tuned against level as it stands and the curve would move
    // every one of them. See BRIGHTNESS_GAMMA.
    float pixelLevel() const { return powf(level, BRIGHTNESS_GAMMA); }

    // The same curve, for brightness written through CHSV's val.
    //
    // FastLED squares that field on its way out: hsv2rgb_rainbow does
    // val = scale8_video(val, val) before scaling the channels, so CHSV(h, s, v)
    // emits duty proportional to v * v. It is not a rounding detail, it is a full
    // gamma, and it means the two colour sinks in this project are not
    // interchangeable. Handing pixelLevel to a CHSV val cancels exactly, sqrt
    // against the library's square, and the animation goes on rendering black with
    // nothing to show anything went wrong. That is what the catalog animations were
    // doing when their brightness was first curved: every one of them measured at
    // a ratio of exactly level against a full-level run.
    //
    // So this supplies the other half. Composed with the library's square it lands
    // on the same emitted duty as pixelLevel does through nscale8, which is what
    // lets both sinks share one curve and one set of constants.
    float hsvLevel() const { return sqrtf(pixelLevel()); }

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
    float spectrum[NUM_SAMPLES / 2] = {};   // FFT magnitudes

    float centroid = 0.0f;          //  ??
    float frequency = 0.0f;         //  frequency
};
