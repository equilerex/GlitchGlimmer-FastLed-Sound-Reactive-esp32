#include "AudioProcessor.h"
#ifndef GG_HOST_BUILD
#include <driver/i2s.h>      // ESP32 I2S driver
#endif
#include <Arduino.h>         // for millis()

// Constructor: allocate FFT engine
AudioProcessor::AudioProcessor() {
    FFT = new ArduinoFFT<float>(vReal, vImag, NUM_SAMPLES, SAMPLE_RATE);
}

// Destructor: clean up FFT engine
AudioProcessor::~AudioProcessor() {
    delete FFT;
}

// Initialize I2S peripheral for microphone input
#ifndef GG_HOST_BUILD
void AudioProcessor::begin() {
    i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK,
        .ws_io_num = I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD
    };
    i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr);
    i2s_set_pin(I2S_PORT, &pin_config);
    i2s_zero_dma_buffer(I2S_PORT);
}

// Capture raw audio samples into internal buffers
void AudioProcessor::captureAudio() {
    static int32_t i2sBuffer[NUM_SAMPLES];
    static float   normalised[NUM_SAMPLES];
    size_t bytesRead = 0;
    // Bounded wait, not portMAX_DELAY: one DMA ring holds ~11.6ms of audio, so
    // 20ms is generous. An unbounded wait here stalls the whole loop if the
    // microphone stops producing.
    esp_err_t err = i2s_read(I2S_PORT,
                             (void*)i2sBuffer,
                             sizeof(i2sBuffer),
                             &bytesRead,
                             pdMS_TO_TICKS(20));
    if (err != ESP_OK || bytesRead == 0) return;

    int count = bytesRead / sizeof(int32_t);
    // A partial read would leave a seam of stale samples mid-buffer. Skip the
    // whole frame instead; the next pass is only a few milliseconds away.
    if (count < NUM_SAMPLES) return;

    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Convert 32-bit signed i2s sample to normalized float
        int32_t s = i2sBuffer[i] >> 8;
        if (s & 0x800000) s |= ~0xFFFFFF;
        normalised[i] = s / 8388608.0f;
    }
    submitSamples(normalised, NUM_SAMPLES);
}
#endif  // GG_HOST_BUILD

void AudioProcessor::submitSamples(const float* samples, size_t count) {
    const size_t n = count < NUM_SAMPLES ? count : size_t(NUM_SAMPLES);

    // Remove the block's DC offset before anything else reads it. The FFT applies
    // a Hamming window and a windowed constant is not zero: the offset lands in
    // the DC bin and its main lobe leaks across the lowest few bins, which is the
    // whole of the display's lowest bar. Both the band loop and the display skip
    // bin 0, so nothing downstream removes this. The INMP441 carries a real
    // offset, which is why the lowest bar read full in silence as well as music.
    float mean = 0.0f;
    for (size_t i = 0; i < n; ++i) mean += samples[i];
    if (n > 0) mean /= static_cast<float>(n);

    for (size_t i = 0; i < n; ++i) {
        // Constrained rather than cast bare: removing the mean can push a sample
        // just past 1.0, and the cast would wrap instead of clipping.
        const float s = samples[i] - mean;
        vReal[i]  = s;
        vImag[i]  = 0.0f;
        buffer[i] = static_cast<int16_t>(constrain(s * 32767.0f, -32768.0f, 32767.0f));
    }
    for (size_t i = n; i < size_t(NUM_SAMPLES); ++i) {
        vReal[i]  = 0.0f;
        vImag[i]  = 0.0f;
        buffer[i] = 0;
    }
}

// Perform FFT and compute audio features
AudioFeatures AudioProcessor::analyzeAudio() {
    AudioFeatures features;
    features.waveform = buffer;
    features.waveformSize = NUM_SAMPLES;

    // Time-domain metrics
    float sum = 0, sumSq = 0, maxV = 0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        float v = constrain(vReal[i], -1.0f, 1.0f);
        float av = fabsf(v);
        sum += av;
        sumSq += v * v;
        maxV = max(maxV, av);
    }
    float avg = sum / NUM_SAMPLES;
    volume = sqrtf(sumSq / NUM_SAMPLES);
    peak = maxV;
    loudness = gainSmoothing * loudness + (1 - gainSmoothing) * (volume * 100.0f);

    // These live in members because they are stateful between frames, but they are
    // also what every consumer reads, so they have to reach the returned struct.
    // Uncopied, features.volume, features.peak and features.loudness stay at their
    // defaults and the animations reading them are silently dead: NeonBeatTunnel
    // scales by volume and renders black, neonFlow clamps to its brightness floor,
    // and the display's loudness bar reads zero.
    features.volume   = volume;
    features.peak     = peak;
    features.loudness = loudness;

    // Mean level and how much the signal moves relative to its own peak.
    // dynamics is what separates a steady loud track from a punchy one, and the
    // mood classifier and several layer opacities read it, so it has to be set.
    features.average  = avg;

    // Smoothed, because peak is one sample of one block: on a steady tone the raw
    // ratio still moves several tenths per frame, which is the flicker the browser
    // reported as dynamics reading unreliably between 50 and 70. Smoothed at the
    // producer rather than at each consumer, since the classifier and the layer
    // opacities both read it as a level.
    const float rawDynamics = (peak - avg) / (peak + 1e-6f);
    if (!dynamicsSeeded) {
        smoothedDynamics = rawDynamics;
        dynamicsSeeded   = true;
    } else {
        smoothedDynamics += (rawDynamics - smoothedDynamics) * 0.08f;
    }
    features.dynamics = smoothedDynamics;

    // Tracked silence baseline. It falls onto a quieter block slowly and climbs
    // back more slowly still, so it follows a change of room without drifting up
    // through the signal during a track's quiet passages.
    if (volume < noiseFloor) {
        noiseFloor -= 0.0002f;
        if (noiseFloor < volume) noiseFloor = volume;
    } else {
        noiseFloor += 0.00002f;
        if (noiseFloor > 0.5f) noiseFloor = 0.5f;
    }

    // Hysteresis: open well above the floor, close closer to it, so a signal
    // sitting on the threshold does not chatter the whole spectral feature set.
    // Both thresholds are multiples of the floor rather than fixed amounts above
    // it. A fixed amount is a claim about the input's absolute scale, and this
    // microphone runs about 40 dB below it, so the absolute form left the gate
    // shut through music. Multiples hold at any gain. The small additive terms
    // only stop the gate opening on the first block, when the floor is still 0.
    if (signalPresence) {
        if (volume < noiseFloor * 1.5f + 0.0005f) signalPresence = false;
    } else {
        if (volume > noiseFloor * 2.5f + 0.001f) signalPresence = true;
    }
    features.noiseFloor     = noiseFloor;
    features.signalPresence = signalPresence;

    // Frequency-domain analysis
    FFT->windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT->compute(FFT_FORWARD);
    FFT->complexToMagnitude();
    memcpy(features.spectrum, vReal, sizeof(float) * (NUM_SAMPLES/2));

    int bassLimit = 200 * NUM_SAMPLES / SAMPLE_RATE;
    int midLimit  = 2000 * NUM_SAMPLES / SAMPLE_RATE;
    int half      = NUM_SAMPLES / 2;
    float eTotal=0, cSum=0, bSum=0, mSum=0, tSum=0;
    for (int i=1;i<half;i++){
        float mag = vReal[i];
        if(i<=bassLimit) bSum+=mag;
        else if(i<=midLimit) mSum+=mag;
        else tSum+=mag;
        eTotal += mag;
        cSum    += mag * i;
    }
    // The bands are each band's share of the total magnitude rather than an
    // absolute level divided by a constant. The old divisors were small enough
    // that broadband audio pinned all three at 1.0, which is white, and a quiet
    // microphone drove all three to 0.000, which is black. A share is 0..1 by
    // construction at any input gain and keeps the relationship between the bands
    // that the animations colour with. energy stays the raw sum, so nothing that
    // divides it by a device-scale constant changes.
    //
    // The shares sum to 1 here and do not after the normalisation below, which
    // rescales them against a shared peak so the dominant band can reach its bar.
    // Anything reading these reads the normalised set.
    if (eTotal > 1e-6f) {
        features.bass   = bSum / eTotal;
        features.mid    = mSum / eTotal;
        features.treble = tSum / eTotal;
    } else {
        features.bass = features.mid = features.treble = 0.0f;
    }
    features.energy= eTotal;
    features.spectrumCentroid = eTotal>0? (cSum/eTotal): 0.0f;
    // Find dominant bin
    int dom=1; float mx=vReal[1];
    for(int i=2;i<half;i++) if(vReal[i]>mx){ mx=vReal[i]; dom=i; }
    features.dominantBand = dom;
    features.frequency    = dom * SAMPLE_RATE / NUM_SAMPLES;

    // With no signal present the FFT sees microphone self-noise and room rumble,
    // which is broadband and lights every band and every bar. The gate is what
    // makes silence read as silence. Applied after the centroid and the dominant
    // band, since both are ratios over the same magnitudes and cancel out.
    // Ramped slowly rather than at a quarter per frame, because a fast ramp reads
    // as the spectrum being yanked away rather than as the room going quiet.
    gateGain += ((signalPresence ? 1.0f : 0.0f) - gateGain) * 0.12f;
    features.bass   *= gateGain;
    features.mid    *= gateGain;
    features.treble *= gateGain;
    features.energy *= gateGain;
    for (int i = 0; i < half; ++i) features.spectrum[i] *= gateGain;

    // The loudest recent block, which is what the level below is a fraction of.
    // Rises onto a louder block at once and forgets slowly, because the loud
    // part of a track is the reference for its quiet part: a reference that fell
    // as fast as the signal would turn every quiet passage back up to full.
    if (volume > levelRef) {
        levelRef = volume;
    } else {
        levelRef *= 0.995f;
    }
    if (levelRef < 1e-4f) levelRef = 1e-4f;

    // Normalised against the floor as well as the peak. Against the peak alone the
    // ratio climbs as the room goes quiet, because both terms fall but levelRef
    // falls geometrically while volume drops onto the room tone at once, so the
    // level rose during silence and only the gate hid it. Subtracting the floor
    // sends the numerator to zero there, so silence now reads as zero by
    // construction rather than by masking.
    const float levelSpan = levelRef - noiseFloor;
    const float levelRaw  = (levelSpan > 1e-5f) ? (volume - noiseFloor) / levelSpan : 0.0f;

    // Then the asymmetric follower. Attack and release are separate coefficients
    // because the raw ratio is a spike train: every block louder than the one
    // before sets it to 1 for that frame, and the value fell away again by the
    // next, which read as flicker in the readout and in everything driven from it.
    // Fast attack keeps a hit on the frame it happens, slow release gives the tail
    // a visible shape, which is the asymmetry the eye reads as motion.
    const float levelTarget = constrain(levelRaw, 0.0f, 1.0f);
    if (!levelSeeded) {
        levelSmoothed = levelTarget;
        levelSeeded   = true;
    } else {
        const float k = (levelTarget > levelSmoothed) ? LEVEL_ATTACK : LEVEL_RELEASE;
        levelSmoothed += (levelTarget - levelSmoothed) * k;
    }

    // Still gated, for the moment the gate opens and closes: the follower cannot
    // move faster than its release, so the gate is what takes the level to zero on
    // the frame the room goes quiet rather than a tenth of a second later.
    features.level = constrain(levelSmoothed, 0.0f, 1.0f) * gateGain;

    // The bands against one shared recent peak, on the same rise-fast, forget-slow
    // rule as levelRef above and for the same reason: the loudest recent block is
    // the reference for the quiet ones, so the set does not fall back to full
    // during a quiet passage.
    //
    // One reference, not one per band. A share is gain-independent but it is a
    // fraction of a total that treble's 232 bins dominate, so on real music the
    // largest band sat around 0.5 and none of them filled its bar. Against a
    // shared reference the dominant band uses the whole bar and the others stay
    // proportionally below it, which is the relationship the animations colour
    // with. A per-band reference would destroy exactly that: a band carrying
    // nothing but noise floor would normalise to 1 against its own noise, and a
    // pure 100 Hz tone would report bass, mid and treble all at full.
    const float bandPeak = max(features.bass, max(features.mid, features.treble));
    if (bandPeak > bandsRef) bandsRef = bandPeak; else bandsRef *= 0.995f;
    if (bandsRef < 1e-4f) bandsRef = 1e-4f;
    features.bass   = constrain(features.bass   / bandsRef, 0.0f, 1.0f);
    features.mid    = constrain(features.mid    / bandsRef, 0.0f, 1.0f);
    features.treble = constrain(features.treble / bandsRef, 0.0f, 1.0f);

    // Seeded from the first block rather than from zero, so the first frame is a
    // reading of real audio instead of a value climbing off the floor.
    const float kBandsAttack  = 0.6f;
    const float kBandsRelease = 0.12f;
    if (!bandsSeeded) {
        smoothBass   = features.bass;
        smoothMid    = features.mid;
        smoothTreble = features.treble;
        bandsSeeded  = true;
    } else {
        const float toBass   = features.bass   > smoothBass   ? kBandsAttack : kBandsRelease;
        const float toMid    = features.mid    > smoothMid    ? kBandsAttack : kBandsRelease;
        const float toTreble = features.treble > smoothTreble ? kBandsAttack : kBandsRelease;
        smoothBass   += (features.bass   - smoothBass)   * toBass;
        smoothMid    += (features.mid    - smoothMid)    * toMid;
        smoothTreble += (features.treble - smoothTreble) * toTreble;
    }
    features.bass   = smoothBass;
    features.mid    = smoothMid;
    features.treble = smoothTreble;

    // Beat detection
    unsigned long now = millis();
    const unsigned long sinceBeat = now - lastBeatTime;
    // The rise is measured against the recent level rather than against a fixed
    // 0.04. An absolute rise is unreachable when the microphone runs at low gain:
    // at a peak of 0.02 no block can rise by 0.04, so the detector never fired and
    // the BPM readout sat at zero however much music was playing. Gated on signal
    // presence as well, so silence cannot manufacture a beat out of nothing.
    const float beatRise = max(0.008f, previousVolume * 0.35f);
    bool beat = false;
    if (signalPresence && volume - previousVolume > beatRise && sinceBeat > 250) {
        beat = true;
        if (sinceBeat <= 2000) currentBPM = 60000.0 / sinceBeat;
        lastBeatTime = now;
    }
    // No beats for a while means the tempo is no longer known. Without this the
    // last measured value stays on the readout forever, which reads as stuck
    // rather than as stale.
    if (sinceBeat > 2000) {
        currentBPM *= 0.94f;
        if (currentBPM < 1.0f) currentBPM = 0.0f;
    }
    features.beatDetected = beat;
    features.bpm          = currentBPM;
    // 1 on a block carrying a strong bass impulse, 0 otherwise. BassPulseStorm
    // reads this, and nothing had ever assigned it, so its branch that tests it
    // was unreachable.
    features.bassHits     = beat ? 1 : 0;

    previousVolume = volume;
    return features;
}
