#pragma once
#include "../config/Config.h"
#include <Arduino.h>
#include <cmath>

struct AudioFeatures {
    float volume = 0.0f;            // Root mean square volume
    float loudness = 0.0f;          // Smoothed loudness (0–100)
    float peak = 0.0f;              // Peak amplitude
    float average = 0.0f;           // Mean signal level

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

    // The references the three bandLevels are measured against. Held by whoever
    // owns the analysis rather than by the block, which is allocated fresh every
    // call and would carry the reference for exactly one frame. AudioProcessor
    // keeps one beside its level reference and the harness keeps its own, and both
    // run the one implementation below rather than their own.
    struct BandRefs {
        float bass   = 0.0f;
        float mid    = 0.0f;
        float treble = 0.0f;
        bool  seeded = false;

        void reset() { bass = mid = treble = 0.0f; seeded = false; }
    };

    // Moves the references onto the bands and derives the three bandLevels. `rise`
    // and `decay` are per-block one-pole coefficients, level's, which hold a recent
    // peak for about twenty seconds and take a sustained passage to move. `gate` is
    // the silence gate, applied to the output here rather than to the bands, so the
    // reference is a reference for the band and is not dragged down every time the
    // room goes quiet. Called with the smoothed ungated shares, which is what lets
    // the reference compare like with like across a silence.
    void updateBandLevels(BandRefs& r, float rise, float decay, float gate) {
        if (!r.seeded) {
            r.bass   = bass;
            r.mid    = mid;
            r.treble = treble;
            r.seeded = true;
        } else {
            r.bass   = bass   > r.bass   ? r.bass   + (bass   - r.bass)   * rise : r.bass   * decay;
            r.mid    = mid    > r.mid    ? r.mid    + (mid    - r.mid)    * rise : r.mid    * decay;
            r.treble = treble > r.treble ? r.treble + (treble - r.treble) * rise : r.treble * decay;
        }

        bassLevel   = r.bass   > 1e-5f ? constrain(bass   / r.bass,   0.0f, 1.0f) * gate : 0.0f;
        midLevel    = r.mid    > 1e-5f ? constrain(mid    / r.mid,    0.0f, 1.0f) * gate : 0.0f;
        trebleLevel = r.treble > 1e-5f ? constrain(treble / r.treble, 0.0f, 1.0f) * gate : 0.0f;
    }

    float bass = 0.0f;              // Low frequency energy
    float mid = 0.0f;               // Mid frequency energy
    float treble = 0.0f;            // High frequency energy

    // The same three bands against their own recent peak, 0..1 and gated by the
    // same silence gate level is. These are what an effect drives pixels from, and
    // the three above are the measurement.
    //
    // A share is the honest way to report which part of the audio carries the
    // energy, which is why the three above are left as shares. It is also small
    // whenever the audio is broadband, which most music is: on the microphone in
    // use bass reads 0.001 to 0.049 and treble 0.006 to 0.40. So every constant an
    // animation compared a band against was a claim about one input's spectrum, and
    // the bass-driven animations rendered near black on real audio while passing
    // every check, because the harness fed them bass at 0.95.
    //
    // A per-band rolling reference removes the constant. bass reads near 1 when
    // bass is near the most bass the room has had recently, at any gain, which is
    // the question level answers for loudness. Not the shared peak the bands were
    // once rescaled against, which held the largest band at exactly 1.0 every frame
    // and pinned mid there, because mid is the largest band on a voice or a melody.
    float bassLevel   = 0.0f;
    float midLevel    = 0.0f;
    float trebleLevel = 0.0f;

    float spectrumCentroid = 0.0f;  // Centroid of frequency content
    int dominantBand = 0;           // Index of loudest spectrum bin
    float dynamics = 0.0f;          // Span the loudness covers over a few seconds
    float energy = 0.0f;            // Sum of spectral magnitudes

    bool beatDetected = false;      // Beat detection flag
    float bpm = 0.0f;               // Estimated BPM
    int bassHits = 0;               // Count of strong bass impulses

    float noiseFloor = 0.0f;        // Tracked silence baseline

    bool signalPresence = false;  // True if volume exceeds noise floor (e.g., > 0.05)

    // ==== Structural detection ====
    //
    // Five moods name the shape of a passage rather than how loud it is: SILENT,
    // TEASE, BUILDUP, DROP and WEIRD. The ladder cannot report any of them,
    // because a quiet drift and a hush before a drop are the same level and the
    // difference is entirely in what came before. These fields carry that.

    // How far the gate has opened, 0..1. This is the one structural input that
    // already existed, since the gate has to ramp anyway to stop a signal on the
    // presence threshold from chattering. Exposed rather than re-derived.
    //
    // Defaults to 1.0f, not 0.0f, and that default is load-bearing. The harness
    // builds AudioFeatures by hand in scriptedAudio and in every fixture, and none
    // of those would learn about a new field. A 0.0f default would make every
    // scripted frame read SILENT and break the dwell checks at once. Same rule as
    // pixelLevel being a method: a producer that forgets has to get normal
    // behaviour rather than a dead strip.
    float gateGain = 1.0f;

    // Displacement above the signal's own slow mean, and only while it is still
    // climbing. Not dynamics, which is the width of the window the envelope has
    // covered and so is equally large on a fall as on a rise.
    //
    // Zero means "not a buildup". Nonzero means one is being reported and the
    // magnitude is how far above its own mean the signal sits, so the hold and
    // the cooldown live in the detector and the classifier reads a single > 0.
    // See BUILDUP_TAU_SEC.
    float buildup = 0.0f;

    // The same measurement on the other side of the slow mean, and the mirror of
    // BUILDUP in every respect: a steady fall rather than a steady climb, so a
    // passage that has simply settled at a lower level is not one. Both come out
    // of the one follower, since a displacement is signed and the two fields are
    // just its two halves named.
    //
    // Barely reachable after a drop, because a drop pins the mood for three
    // seconds and TEASE claims the twelve after it. What it catches instead is
    // the long wind-down at the end of a track and the retreat out of a chorus,
    // which the ladder can only report as a sequence of rungs going down, and
    // which reads as a run of changes rather than as one movement.
    float descent = 0.0f;

    // One block's pulse, not a state. A slam is an edge, so anything that
    // required it to persist would miss the thing it exists to catch. Moods
    // display it by pinning for DROP_PIN_MS rather than by holding this true.
    bool dropDetected = false;

    // Spectral flatness: the geometric mean of the magnitudes over their
    // arithmetic mean, near 1 for noise and near 0 for a tone. Computed for the
    // noise floor and discarded until now, which is why the floor can tell a room
    // from a track and nothing else could.
    float spectralFlatness = 0.0f;

    // How many of WEIRD's three conditions hold, 0..3, counted only while its own
    // hold and cooldown are satisfied. A count rather than a bool so a check can
    // assert on how close a passage came, and so the threshold is one number to
    // move rather than a nest of comparisons.
    float anomaly = 0.0f;

    // Tension without full energy: a breakdown after a drop, a hush before one, or
    // a pulse that will not sustain. Any one of the three is enough, because to a
    // listener they are the same thing.
    bool teaseDetected = false;

    int16_t* waveform = nullptr;    // Pointer to time-domain samples
    size_t waveformSize = 0;        // Size of waveform buffer
    float spectrum[NUM_SAMPLES / 2] = {};   // FFT magnitudes

    float centroid = 0.0f;          //  ??
    float frequency = 0.0f;         //  frequency
};
