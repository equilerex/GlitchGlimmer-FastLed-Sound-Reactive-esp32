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
    #if GG_HAS_MICROPHONE
    i2s_config_t i2s_config = {
        .mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 32,
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
    #else
    Serial.println("Microphone disabled; skipping I2S initialization");
    // Keep analysis deterministic when no capture source is present. The
    // analyzer still runs so the LED scene pipeline remains alive, but it must
    // see silence rather than uninitialized sample storage.
    submitSamples(nullptr, 0);
    #endif
}

// Capture raw audio samples into internal buffers
void AudioProcessor::captureAudio() {
    #if GG_HAS_MICROPHONE
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
    #endif
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
        const float s = constrain(samples[i] - mean, -1.0f, 1.0f);
        vReal[i]  = s;
        vImag[i]  = 0.0f;
        buffer[i] = static_cast<int16_t>(constrain(s * 32767.0f, -32768.0f, 32767.0f));
    }
    for (size_t i = n; i < size_t(NUM_SAMPLES); ++i) {
        vReal[i]  = 0.0f;
        vImag[i]  = 0.0f;
        buffer[i] = 0;
    }
    if (n > 0) sampleClock += n;
}

// Drop the remembered statistics of the input. Not the buffers: the FFT's vReal
// and vImag are overwritten by the next submitSamples() and hold nothing but the
// block just analysed. Not the seeding of the first block either, which every
// filter here reads as "start on this value rather than climbing off zero", so
// clearing the flags lets the next block seed again.
void AudioProcessor::resetTracking() {
    // The gate, and the floor it is measured against. A floor carried over from a
    // louder source sits above the new signal's every block, which closes the gate
    // and sends the level to zero by construction rather than by masking.
    noiseFloor      = 0.0f;
    signalPresence  = false;
    gateGain        = 0.0f;

    // The reference level is a fraction of, the envelope it is measured from, and
    // the band equivalents. The bandLevels go with the band references they are
    // measured against, since a reference from a louder source reads the next one
    // as quiet for as long as it takes to decay.
    levelRef       = 0.0f;
    levelEnv       = 0.0f;
    levelEnvSeeded = false;

    bandRefs.reset();

    sampleClock = 0;
    previousSampleClock = 0;
    sampleClockSeeded = false;
    intensityTracker.reset();
    activityTracker.reset();
    brightnessTracker.reset();
    weightTracker.reset();
    pulseTracker.reset();
    textureTracker.reset();
    memset(previousSpectrum, 0, sizeof(previousSpectrum));
    fluxReference = 0.0f;
    fluxSeeded = false;

    smoothBass    = 0.0f;
    smoothMid     = 0.0f;
    smoothTreble  = 0.0f;
    bandsSeeded   = false;

    dynHi            = 0.0f;
    dynLo            = 0.0f;
    dynamicsSeeded   = false;

    // The beat clock is stamped with now rather than zeroed, so the minimum
    // interval applies from the switch instead of being already expired, and the
    // remembered intervals are dropped because they describe the tempo of audio
    // that has stopped.
    previousVolume     = 0.0f;
    previousBassEnergy = 0.0f;
    previousMidEnergy  = 0.0f;
    lastSignalTime     = 0;
    loudness           = 0.0f;
    // Beat timestamps are now in the sample-clock domain.  Starting these
    // with wall-clock millis() would make the first sample-time subtraction
    // underflow after a reset.
    lastBeatTime   = 0;
    currentBPM     = 0.0f;
    bpmAtLastBeat  = 0.0f;
    beatIntervalCount = 0;
    beatIntervalNext  = 0;
    for (int i = 0; i < ONSET_HISTORY_LEN; ++i) onsetHistory[i] = 0.0f;
    onsetIndex = 0;
    onsetCount = 0;
    trackedPeriodFrames = 43.0f;
    beatPhase = 0.0f;
    beatConfidence = 0.0f;
    autocorrCadence = 0;

    // The structural state. Every member here is a statistic of the source that
    // has just been switched away from, and the switch is the one moment that is
    // knowable rather than inferred. The slow mean is the clearest case: it is a
    // mean of the old source's level, and the demo signal is roughly forty times
    // louder than the microphone, so a displacement measured against it would
    // report DESCENT from the first block of the quieter source for as long as the
    // follower took to arrive, which is the same ten seconds BUILDUP_TAU_SEC names.
    clearStructure();

    // The drop clock is stamped with now, like the beat clock above, so the
    // cooldown applies from the switch rather than being already expired. A drop
    // reported immediately on a new source would be a claim about audio the
    // detector never heard. Not part of clearStructure, which runs every frame the
    // gate is shut: re-stamping it there would keep pushing the cooldown out and
    // leave a passage's first eight seconds unable to report a drop.
    lastDropMs = 0;
}

void AudioProcessor::clearStructure() {
    // Ends every open episode with reason gate and restarts the tracker's clock, which
    // the sample clock has just done on a source change. The event ring is kept, so
    // a reader never loses an end that was decided here.
    episodes.clearEpisodes();
    buildupLatched = false;

    slowLevel        = 0.0f;
    slowSeeded       = false;
    structuralLastMs = 0;

    buildupActive         = false;
    buildupSince          = 0;
    buildupHoldSince      = 0;
    buildupLastExceededMs = 0;
    buildupFromLevel      = 0.0f;

    descentActive         = false;
    descentHoldSince      = 0;
    descentLastExceededMs = 0;
    descentFromLevel      = 0.0f;

    quietSince = 0;
    quietHeld  = false;

    for (int i = 0; i < TEASE_WINDOW; ++i) levelRing[i] = 0.0f;
    levelRingCount = 0;
    levelRingNext  = 0;

    centHi     = 0.0f;
    centLo     = 0.0f;
    centSeeded = false;
    weirdSince = 0;
}

// Insertion sort on a copy of at most BEAT_BPM_WINDOW elements. The ring keeps its
// insertion order and the caller keeps the ring.
unsigned long AudioProcessor::medianBeatInterval() const {
    unsigned long sorted[BEAT_BPM_WINDOW];
    for (int i = 0; i < beatIntervalCount; ++i) sorted[i] = beatIntervals[i];
    for (int i = 1; i < beatIntervalCount; ++i) {
        const unsigned long v = sorted[i];
        int j = i - 1;
        while (j >= 0 && sorted[j] > v) {
            sorted[j + 1] = sorted[j];
            --j;
        }
        sorted[j + 1] = v;
    }
    return sorted[beatIntervalCount / 2];
}

void AudioProcessor::updateAutocorrBeat(float novelty) {
    onsetHistory[onsetIndex] = novelty;
    onsetIndex = (onsetIndex + 1) % ONSET_HISTORY_LEN;
    if (onsetCount < ONSET_HISTORY_LEN) ++onsetCount;

    if (trackedPeriodFrames > 1.0f) {
        beatPhase += 1.0f / trackedPeriodFrames;
        if (beatPhase >= 1.0f) {
            beatPhase -= 1.0f;
        }
    }

    if (++autocorrCadence % 3 != 0 || onsetCount < 96) {
        return;
    }

    const int kMinLag = 26; // ~199 BPM (26 * 11.6ms = 302ms)
    const int kMaxLag = 86; // ~60 BPM (86 * 11.6ms = 998ms)
    const int kCompareLen = 96;

    if (onsetCount < kMaxLag + kCompareLen) {
        return;
    }

    float scores[61] = {};
    float bestScore = -1.0f;
    int bestLag = 0;
    float sumScore = 0.0f;
    int scoreCount = 0;

    for (int lag = kMinLag; lag <= kMaxLag; ++lag) {
        float r = 0.0f;
        for (int i = 0; i < kCompareLen; ++i) {
            int idx0 = (onsetIndex - 1 - i + ONSET_HISTORY_LEN * 2) % ONSET_HISTORY_LEN;
            int idxLag = (onsetIndex - 1 - i - lag + ONSET_HISTORY_LEN * 2) % ONSET_HISTORY_LEN;
            r += onsetHistory[idx0] * onsetHistory[idxLag];
        }

        const float diff = float(lag - 43);
        const float weight = 1.0f - 0.35f * (diff * diff) / (diff * diff + 250.0f);
        const float score = r * weight;
        scores[lag - kMinLag] = score;

        sumScore += score;
        ++scoreCount;

        if (score > bestScore) {
            bestScore = score;
            bestLag = lag;
        }
    }

    // Octave disambiguation: check if half-lag (double tempo) has a substantial peak.
    // In 4/4 music, a 2-beat period (60-80 BPM, lag 60-86) will always show high autocorrelation.
    // If the 1-beat period (120-160 BPM, lag 30-43) also has a strong peak, the 1-beat period is the true tempo.
    if (bestLag >= 52) {
        int halfLag = (bestLag + 1) / 2;
        if (halfLag >= kMinLag && halfLag <= kMaxLag) {
            float halfScore = scores[halfLag - kMinLag];
            if (halfLag - 1 >= kMinLag) halfScore = fmaxf(halfScore, scores[halfLag - 1 - kMinLag]);
            if (halfLag + 1 <= kMaxLag) halfScore = fmaxf(halfScore, scores[halfLag + 1 - kMinLag]);
            if (halfScore >= 0.50f * bestScore) {
                bestLag = halfLag;
                bestScore = halfScore;
            }
        }
    }

    if (scoreCount > 0 && bestScore > 1e-6f) {
        const float meanScore = sumScore / float(scoreCount);
        const float rawConf = constrain((bestScore - meanScore) / bestScore, 0.0f, 1.0f);
        beatConfidence += (rawConf - beatConfidence) * 0.15f;
    } else {
        beatConfidence *= 0.90f;
    }

    if (beatConfidence > 0.25f && bestLag >= kMinLag && bestLag <= kMaxLag) {
        trackedPeriodFrames += (float(bestLag) - trackedPeriodFrames) * 0.10f;
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
        maxV = fmaxf(maxV, av);
    }
    float avg = sum / NUM_SAMPLES;
    volume = sqrtf(sumSq / NUM_SAMPLES);
    peak = maxV;
    loudness = gainSmoothing * loudness + (1 - gainSmoothing) * (volume * 100.0f);

    // These live in members because they are stateful between frames, but they are
    // also what every consumer reads, so they have to reach the returned struct.
    loudness = gainSmoothing * loudness + (1 - gainSmoothing) * (volume * 100.0f);

    // These live in members because they are stateful between frames, but they are
    // also what every consumer reads, so they have to reach the returned struct.
    // Uncopied, features.volume, features.peak and features.loudness stay at their
    // defaults and the panel reads zero for all three. No animation drives from
    // these any more: they are absolute sample amplitudes, so a constant compared
    // against one is a guess about the microphone's gain, and the render path took
    // level and the band drives instead. They are kept as the honest measurement.
    features.volume   = volume;
    features.peak     = peak;
    features.loudness = loudness;

    // Mean level. dynamics is set below, once the envelope it is measured from
    // exists.
    features.average  = avg;

    const unsigned long now = static_cast<unsigned long>(
        (sampleClock * 1000ULL) / SAMPLE_RATE);

    // Hysteresis with hangover: open well above the floor, and once open, hold for at
    // least GATE_HANGOVER_MS so pauses between beats, vocal phrases, and stop consonants
    // do not strobe/chatter the gate.
    if (signalPresence) {
        if (volume > noiseFloor * 1.5f + 0.0005f) {
            lastSignalTime = now;
        } else if (now - lastSignalTime >= GATE_HANGOVER_MS) {
            signalPresence = false;
        }
    } else {
        if (volume > noiseFloor * 2.5f + 0.001f) {
            signalPresence = true;
            lastSignalTime = now;
        }
    }
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
    float spectralFlux = 0.0f;
    // Spectral flatness, the geometric mean of the magnitudes over their arithmetic
    // mean, accumulated here because this loop already walks the same bins. It is
    // what tells a room from a track, and level cannot: a loud room and a loud
    // compressed track are the same number, which is how the noise floor came to
    // climb onto the music in the first place. A room's noise spreads over every
    // bin, which puts the two means close together, and anything carrying a pitch
    // concentrates its energy into a few, which pulls the geometric mean down.
    float logSum  = 0.0f;
    int   magBins = 0;
    for (int i=1;i<half;i++){
        float mag = vReal[i];
        spectralFlux += fmaxf(0.0f, mag - previousSpectrum[i]);
        previousSpectrum[i] = mag;
        if(i<=bassLimit) bSum+=mag;
        else if(i<=midLimit) mSum+=mag;
        else tSum+=mag;
        eTotal += mag;
        cSum    += mag * i;
        if (mag > 1e-9f) { logSum += logf(mag); ++magBins; }
    }
    // The bands are each band's share of the total magnitude rather than an
    // absolute level divided by a constant. The old divisors were small enough
    // that broadband audio pinned all three at 1.0, which is white, and a quiet
    // microphone drove all three to 0.000, which is black. A share is 0..1 by
    // construction at any input gain and keeps the relationship between the bands
    // that the animations colour with. energy stays the raw sum, so nothing that
    // divides it by a device-scale constant changes.
    //
    // The three sum to 1, and they still do by the time they reach a consumer: the
    // gate scales all three by the same amount and the smoothing is the only thing
    // that separates them, since it runs at a different rate on the way up than on
    // the way down.
    if (eTotal > 1e-6f) {
        features.bass   = bSum / eTotal;
        features.mid    = mSum / eTotal;
        features.treble = tSum / eTotal;
    } else {
        features.bass = features.mid = features.treble = 0.0f;
    }
    features.energy= eTotal;
    features.spectrumCentroid = eTotal>0? (cSum/eTotal): 0.0f;

    const float spectralFlatness =
        (magBins > 0 && eTotal > 1e-6f)
            ? expf(logSum / float(magBins)) / (eTotal / float(magBins))
            : 0.0f;
    features.spectralFlatness = spectralFlatness;

    if (!fluxSeeded) {
        fluxReference = spectralFlux;
        fluxSeeded = true;
    } else {
        fluxReference = fmaxf(fluxReference * 0.995f, spectralFlux);
    }

    // Tracked silence baseline. It falls onto a quieter block at a fixed rate, and
    // rises only onto a block whose spectrum is noise-like. See NOISE_FLAT_MIN in
    // Config.h for why the spectrum decides it rather than the level, and
    // NOISE_FLOOR_MAX for why the rise is bounded.
    //
    // The rise used to be unconditional, and during music nearly every block clears
    // the floor, so it rose on nearly every block: at the end of a forty second
    // track it had climbed to 89 percent of the signal it was supposed to be a floor
    // under. Two things broke at once then, and both are multiples of it. The gate
    // wanted 2.5 times it, which was above the signal, so the gate shut and the level
    // went to zero with it. The beat detector wanted 4 times it, also above the
    // signal, so the detector went deaf and the tempo faded out. That is one defect
    // with both symptoms, and it is this comment's subject.
    if (volume < noiseFloor) {
        noiseFloor -= 0.0002f;
        if (noiseFloor < volume) noiseFloor = volume;
    } else if (spectralFlatness > NOISE_FLAT_MIN && noiseFloor < NOISE_FLOOR_MAX) {
        noiseFloor += 0.00002f;
    }
    features.noiseFloor = noiseFloor;

    // The bass band's raw magnitude, before the gate touches it, which is what the
    // beat detector compares against the block before. Energy and not the share:
    // the share of a block that is mostly bass is already at the top of its range,
    // so a share cannot rise no matter how hard the kick lands, and the detector
    // would never fire on the bass-heavy material it exists for. The raw sum has no
    // ceiling. Ungated as well, because the gate opening is itself a rise.
    const float bassEnergy = bSum;

    // Find dominant bin
    int dom=1; float mx=vReal[1];
    for(int i=2;i<half;i++) if(vReal[i]>mx){ mx=vReal[i]; dom=i; }
    features.dominantBand = dom;
    features.frequency    = dom * SAMPLE_RATE / NUM_SAMPLES;

    // With no signal present the FFT sees microphone self-noise and room rumble,
    // which is broadband and lights every band and every bar. The gate is what
    // makes silence read as silence. Applied after the centroid and the dominant
    // band, since both are ratios over the same magnitudes and cancel out. Ramped
    // rather than switched, because a hard 0/1 strobes a signal sitting on the
    // threshold; see GATE_RAMP in Config.h for the rate and why it slowed.
    gateGain += ((signalPresence ? 1.0f : 0.0f) - gateGain) * GATE_RAMP;
    features.gateGain = gateGain;
    features.energy *= gateGain;
    for (int i = 0; i < half; ++i) features.spectrum[i] *= gateGain;
    // The three bands are gated below instead, after their smoothing and their
    // bandLevels. Gating them here as well would drag the per-band references down
    // by the gate on every silence, and a reference measured through a gate is not
    // a reference for the band.

    // The envelope of the input, which is what level is measured from. A single
    // block's RMS is not the loudness of the room: it is one 11.6 ms window, and on
    // music it swings by a factor of two or more between blocks, so a ratio taken
    // straight from it moved with the beat and level read as a second beat
    // detector. The envelope is the surroundings rather than the hit, which is what
    // the layers modulating their overall range from it are asking for.
    //
    // Seeded from the first block rather than from zero, like the other filters
    // here, so the first frame is a reading of real audio rather than a value
    // climbing off the floor.
    if (!levelEnvSeeded) {
        levelEnv       = volume;
        levelEnvSeeded = true;
    } else {
        const float toEnv = (volume > levelEnv) ? LEVEL_ENV_ATTACK : LEVEL_ENV_RELEASE;
        levelEnv += (volume - levelEnv) * toEnv;
    }

    // The loudest recent envelope value, which is what the level below is a
    // fraction of. Rises onto a louder passage over about a fifth of a second and
    // forgets over twenty, because the loud part of a track is the reference for
    // its quiet part: a reference that fell as fast as the signal would turn every
    // quiet passage back up to full. The rise is no longer instantaneous for the
    // same reason on the other side of it, so that one loud envelope value cannot
    // become the denominator for everything in the next twenty seconds.
    if (levelEnv > levelRef) {
        levelRef += (levelEnv - levelRef) * LEVEL_REF_RISE;
    } else {
        levelRef *= LEVEL_REF_DECAY;
    }
    if (levelRef < 1e-4f) levelRef = 1e-4f;

    // dynamics, which is how far the loudness travels rather than what shape it
    // has. It used to be (peak - average) / peak over one block, a crest factor,
    // and a crest factor is a property of a waveform's shape that every sound a
    // room produces shares: speech, hum, noise and music through a small speaker
    // all sit near 0.65, and turning the gain down does not change a shape, so the
    // reading held at 0.65 whether anyone was talking or not. A number that cannot
    // move cannot separate a calm room from a busy one, which is what the mood
    // classifier and several layer opacities ask it to do.
    //
    // The span the envelope covers is what does move, so the two edges follow it,
    // quickly outwards and slowly back in, which makes the window they remember a
    // few seconds wide. As a fraction of the top of the span, so it stays a ratio
    // and not a level.
    if (!dynamicsSeeded) {
        dynHi          = levelEnv;
        dynLo          = levelEnv;
        dynamicsSeeded = true;
    } else {
        dynHi += (levelEnv - dynHi) * (levelEnv > dynHi ? dynamicsRisePerBlock : dynamicsDecayPerBlock);
        dynLo += (levelEnv - dynLo) * (levelEnv < dynLo ? dynamicsRisePerBlock : dynamicsDecayPerBlock);
    }
    features.dynamics = (dynHi > 1e-6f)
        ? constrain((dynHi - dynLo) / dynHi, 0.0f, 1.0f)
        : 0.0f;

    // Normalised against the floor as well as the peak. Against the peak alone the
    // ratio climbs as the room goes quiet, because both terms fall but levelRef
    // falls geometrically while the envelope drops onto the room tone at once, so
    // the level rose during silence and only the gate hid it. Subtracting the floor
    // sends the numerator to zero there, so silence now reads as zero by
    // construction rather than by masking.
    //
    // There is no second filter on the ratio. The envelope and the reference are
    // both slow, so the ratio is already a level rather than a spike train, and a
    // further follower would only add lag to a value whose whole job is to be
    // stable.
    const float levelRefEff = fmaxf(levelRef, noiseFloor * LEVEL_REF_MIN_OVER_NOISE);
    const float levelSpan = levelRefEff - noiseFloor;
    const float levelRaw  = (levelSpan > 1e-5f) ? (levelEnv - noiseFloor) / levelSpan : 0.0f;

    // Still gated, for the moment the gate opens and closes: the envelope cannot
    // move faster than its release, so the gate is what takes the level to zero on
    // the frame the room goes quiet rather than a second later.
    features.level = constrain(levelRaw, 0.0f, 1.0f) * gateGain;

    // The shares are left as shares. They were rescaled against the largest of the
    // three, which made the dominant band read exactly 1.0 by construction, and on
    // this microphone the dominant band is mid on anything with a voice or a
    // melody in it. That is why mid sat pinned at the top of its bar while bass and
    // treble moved, which is a claim about which band is present rather than about
    // how much of it there is. A share already says how much of it there is, at any
    // gain, and the three now read in the proportions the audio actually has.
    //
    // The cost is that every band reads lower than it did, by roughly the factor
    // the shared reference was lifting them, so the animations that drive
    // brightness from a band are dimmer than they were. Inflating a measurement to
    // suit a consumer is what produced the pinned band, so the scale belongs in the
    // animations.

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

    // The render drive, from the smoothed ungated shares and the gate, which is
    // applied here rather than above so the reference is a reference for the band
    // rather than for the band times the gate. See updateBandLevels and the
    // bandLevels member block in AudioFeatures.h for why a share is not a drive.
    //
    // The reference coefficients are level's, which hold the recent peak for about
    // twenty seconds and take a sustained passage to move. One reference per band
    // rather than one for the three, because the bands do not peak together: a kick
    // and a cymbal are seconds apart and a shared reference would read whichever
    // came last.
    features.updateBandLevels(bandRefs, LEVEL_REF_RISE, LEVEL_REF_DECAY, gateGain);

    features.bass   *= gateGain;
    features.mid    *= gateGain;
    features.treble *= gateGain;

    // Beat detection
    //
    // The rise is measured against the block before it, which is a single 11.6 ms
    // window and therefore the right reference for an onset: a kick is an edge at
    // that timescale, and a reference averaging over more than a few blocks already
    // carries the beat it is supposed to be detecting.
    //
    // Both terms of the threshold are relative. The fraction is a fraction rather
    // than an amount because an amount is a claim about one microphone's gain, and
    // the floor is a multiple of the measured noise floor for the same reason
    // rather than the fixed 0.008 it used to be. That constant was a third of the
    // entire range on the microphone in use, so every block carrying room noise
    // cleared it and the tempo followed the room. Gated on signal presence as well,
    // so silence cannot manufacture a beat out of nothing.
    //
    // A level rise alone is not a beat, and that is what made the tempo follow
    // speech: every syllable is an onset, the block before it is quieter, and the
    // detector fired on each one, which is where 140 to 200 BPM with nothing
    // playing came from. The second condition is that the bass band has to rise
    // too. A kick is low end and a consonant is not, so it is the condition that
    // tells a rhythm from a voice. See BEAT_BASS_RISE in Config.h.
    features.sampleFrame = sampleClock / NUM_SAMPLES;
    features.sampleTimeMs = now;
    features.dtSeconds = sampleClockSeeded
        ? float(sampleClock - previousSampleClock) / float(SAMPLE_RATE)
        : 0.0f;
    previousSampleClock = sampleClock;
    sampleClockSeeded = true;

    const float normSpectralFlux = (eTotal > 1e-5f) ? spectralFlux / eTotal : 0.0f;
    const float normBassFlux = (bSum > 1e-5f) ? fmaxf(0.0f, bassEnergy - previousBassEnergy) / bSum : 0.0f;
    const float normMidFlux = (mSum > 1e-5f) ? fmaxf(0.0f, mSum - previousMidEnergy) / mSum : 0.0f;

    const float blockNovelty = (gateGain > 0.15f)
        ? (normBassFlux * 1.0f + normMidFlux * 0.8f + normSpectralFlux * 0.5f)
        : 0.0f;
    updateAutocorrBeat(blockNovelty);

    const unsigned long sinceBeat = now - lastBeatTime;
    const float beatRise = fmaxf(noiseFloor * BEAT_RISE_NOISE,
                               previousVolume * BEAT_RISE_FRACTION);
    const bool volRising = (volume - previousVolume > beatRise) || (normSpectralFlux > 0.12f);

    // Multi-band beat trigger conditions:
    // 1. Bass downbeat (kick/808):
    const bool isBassHit = (bassEnergy > previousBassEnergy * BEAT_BASS_RISE) &&
                           (features.bassLevel >= BEAT_MIN_BASS_LEVEL || normBassFlux > 0.20f);
    // 2. Mid/Treble backbeat (snare/clap/bright acoustic transient):
    const bool isMidHit = (normMidFlux > 0.25f) && (features.midLevel >= 0.12f) && (features.level >= 0.06f);
    // 3. Phase-locked expectation: when autocorrelation confidence is high (>0.35),
    // onsets near the predicted downbeat phase trigger cleanly even at low volume
    const bool isPhaseHit = (beatConfidence >= 0.35f) &&
                            (beatPhase < 0.15f || beatPhase > 0.85f) &&
                            (normBassFlux > 0.10f || normMidFlux > 0.15f || normSpectralFlux > 0.10f);

    bool beat = false;
    if (signalPresence && volRising && (isBassHit || isMidHit || isPhaseHit) &&
        (lastBeatTime == 0 || sinceBeat > MIN_BEAT_INTERVAL)) {
        beat = true;
        // The tempo is the median of the recent intervals, not the newest one. One
        // interval moves the readout by tens of BPM when a beat lands a block early
        // or late, and a beat the detector misses doubles the interval and halves
        // the number, which is what an erratic readout is made of.
        if (lastBeatTime > 0 && sinceBeat <= TEMPO_HOLD_MS) {
            beatIntervals[beatIntervalNext] = sinceBeat;
            beatIntervalNext = (beatIntervalNext + 1) % BEAT_BPM_WINDOW;
            if (beatIntervalCount < BEAT_BPM_WINDOW) ++beatIntervalCount;
            currentBPM    = 60000.0f / float(medianBeatInterval());
            bpmAtLastBeat = currentBPM;
        }
        lastBeatTime = now;

        // Phase-locked loop nudging: if a strong kick lands, nudge beatPhase towards 0.0 (downbeat)
        if (beatPhase < 0.35f) {
            beatPhase *= 0.5f;
        } else if (beatPhase > 0.65f) {
            beatPhase += (1.0f - beatPhase) * 0.5f;
            if (beatPhase >= 1.0f) beatPhase -= 1.0f;
        }
    }

    // Autocorrelation tempo lock:
    // When autocorrelation has locked onto a periodic rhythm with good confidence,
    // drive BPM smoothly and reliably, avoiding octave-jumps and missing-kick stalls.
    const float autocorrBPM = (trackedPeriodFrames > 1.0f)
        ? (float(SAMPLE_RATE) * 60.0f / (float(NUM_SAMPLES) * trackedPeriodFrames))
        : 0.0f;
    if (beatConfidence >= 0.30f && autocorrBPM >= 55.0f && autocorrBPM <= 200.0f) {
        currentBPM = autocorrBPM;
        bpmAtLastBeat = currentBPM;
    }

    // No beats for a while means the tempo is no longer known. Without this the
    // last measured value stays on the readout forever, which reads as stuck
    // rather than as stale. The intervals go with it, because the first beat after
    // a break would otherwise be averaged against a tempo that stopped a minute
    // ago and report a number belonging to neither.
    //
    // The fade is a function of the elapsed time since the last beat rather than a
    // per-block multiplier on the running value. The old form held for two seconds
    // and then multiplied by 0.94 a block, so the number was gone inside another
    // second and the speed of that depended on the loop rate, which is 86 a second
    // on the device and up to 60 on the page and neither of them a clock. It also
    // faded whatever the value had already decayed to, so two blocks arriving a
    // little apart reported different tempos for the same silence.
    if (sinceBeat > TEMPO_HOLD_MS) {
        currentBPM = bpmAtLastBeat *
                     expf(-float(sinceBeat - TEMPO_HOLD_MS) / TEMPO_FADE_MS);
        if (currentBPM < 1.0f) {
            currentBPM    = 0.0f;
            bpmAtLastBeat = 0.0f;
            beatIntervalCount = 0;
            beatIntervalNext  = 0;
            lastBeatTime      = 0;
        }
    }
    features.beatDetected   = beat;
    features.bpm            = currentBPM;
    features.beatPhase      = beatPhase;
    features.beatConfidence = beatConfidence;
    features.bassHits       = beat ? 1 : 0;

    float pulse = 0.0f;
    float pulseConfidence = float(beatIntervalCount) / float(BEAT_BPM_WINDOW);
    if (beatIntervalCount > 0) {
        const float median = float(medianBeatInterval());
        float deviation = 0.0f;
        for (int i = 0; i < beatIntervalCount; ++i) {
            deviation += fabsf(float(beatIntervals[i]) - median);
        }
        deviation /= float(beatIntervalCount);
        pulse = median > 0.0f ? constrain(1.0f - deviation / median, 0.0f, 1.0f) : 0.0f;
    }
    pulse = fmaxf(pulse, beatConfidence);
    pulseConfidence = fmaxf(pulseConfidence, beatConfidence);
    updateStructure(features, now);
    updateMusicState(features, spectralFlux, pulse, pulseConfidence);

    previousVolume     = volume;
    previousBassEnergy = bassEnergy;
    previousMidEnergy  = mSum;
    return features;
}

void AudioProcessor::updateMusicState(AudioFeatures& features, float spectralFlux,
                                      float pulse, float pulseConfidence) {
    const float dt = features.dtSeconds > 0.0f ? features.dtSeconds : 1.0f / float(SAMPLE_RATE);
    const float activity = fluxReference > 1e-6f
        ? constrain(spectralFlux / fluxReference, 0.0f, 1.0f)
        : 0.0f;
    const float gateConfidence = constrain(features.gateGain, 0.0f, 1.0f);
    const float rawIntensity = constrain(
        0.40f * features.level +
        0.30f * activity +
        0.20f * features.bassLevel +
        0.10f * features.dynamics,
        0.0f, 1.0f);
    intensityTracker.update(rawIntensity, gateConfidence, dt);
    activityTracker.update(activity, gateConfidence, dt);
    brightnessTracker.update(constrain(features.spectrumCentroid / float(NUM_SAMPLES / 2), 0.0f, 1.0f), gateConfidence, dt);
    weightTracker.update(features.bassLevel, gateConfidence, dt);
    pulseTracker.update(pulse, pulseConfidence, dt);
    textureTracker.update(features.spectralFlatness, gateConfidence, dt);

    features.music.intensity = intensityTracker.output;
    features.music.activity = activityTracker.output;
    features.music.brightness = brightnessTracker.output;
    features.music.weight = weightTracker.output;
    features.music.pulse = pulseTracker.output;
    features.music.tempo.value = constrain(features.bpm / 240.0f, 0.0f, 1.0f);
    features.music.tempo.confidence = pulseConfidence;
    features.music.tempo.trend = 0.0f;
    features.music.texture = textureTracker.output;
    features.music.presence.value = features.gateGain;
    features.music.presence.confidence = gateConfidence;
    features.music.presence.trend = 0.0f;
    features.music.buildup = features.buildup > 0.0f;
    features.music.descent = features.descent > 0.0f;
    features.music.dropDetected = features.dropDetected;
    features.music.teaseDetected = features.teaseDetected;
    features.music.anomaly = features.anomaly > 0.0f;
    features.music.initialized = true;
}

// The structural detectors: BUILDUP, DESCENT, DROP, TEASE and WEIRD. SILENT needs
// nothing here, since the gate is already ramped and features.gateGain carries it.
//
// Placed after the beat detector rather than before it, because DROP's breadth
// condition reads the three bandLevels and TEASE's post-drop trigger reads the
// beat clock. It is last because it consumes and does not feed: nothing below it
// in the frame depends on what it concludes, so a detector that is wrong can only
// mislabel a passage and cannot move the numbers the animations draw from.
void AudioProcessor::updateStructure(AudioFeatures& features, unsigned long now) {
    // The gate's own ramp is not music. See GATE_SETTLED for what it would
    // otherwise be read as and why the threshold sits where it does. Nothing is
    // reported while it is climbing, and every window is discarded rather than
    // merely ignored, because the ring and the centroid span would otherwise carry
    // the ramp into the first seconds after the gate opened and the fake-out test
    // would read the ramp's own variance as a pulse that does not sustain.
    if (gateGain < GATE_SETTLED) {
        clearStructure();
        episodes.fill(features, now);
        return;
    }

    const float level = features.level;

    // One follower, two moods. Seeded from the first block it is allowed to see, so
    // the first frame is a displacement against real audio rather than against a
    // zero the follower then climbs away from.
    if (!slowSeeded) {
        slowLevel  = level;
        slowSeeded = true;
    }
    if (structuralLastMs == 0) structuralLastMs = now;
    float dt = float(now - structuralLastMs) * 0.001f;
    // Clamped, so a first frame from a zero timestamp cannot move the follower the
    // whole way and a stall between frames cannot either. Same guard the mood's
    // dynamics window uses, for the same reason.
    if (dt > 0.1f) dt = 0.1f;
    if (dt > 0.0f) slowLevel += (level - slowLevel) * (1.0f - expf(-dt / BUILDUP_TAU_SEC));
    structuralLastMs = now;

    const float displacement = level - slowLevel;

    // ==== BUILDUP and DESCENT ====
    //
    // The same measurement with the sign used, so the two cannot both be true. The
    // hold starts when the displacement crosses its threshold and is dropped the
    // moment it falls back, so the reported state is a sustained movement rather
    // than a sample that happened to be high. `fromLevel` is the level at the
    // moment the hold began, which is what makes the climb condition a measurement
    // of the movement rather than of the level: a plateau sitting high crosses the
    // displacement threshold on its own noise and then climbs nothing.
    //
    // No cooldown on either. See BUILDUP_HOLD_MS for why, and note that the
    // follower supplies the property a cooldown would have been protecting: a
    // movement that stops ends its own displacement as the mean catches up.
    const unsigned long kStructureHangoverMs = 350;

    if (displacement < BUILDUP_LEVEL) buildupLatched = false;

    if (displacement >= BUILDUP_LEVEL && !buildupLatched) {
        buildupLastExceededMs = now;
        if (buildupHoldSince == 0) {
            buildupHoldSince = now;
            buildupFromLevel = level;
        } else if (level < buildupFromLevel) {
            buildupFromLevel = level;
        }
        if (now - buildupHoldSince >= BUILDUP_HOLD_MS &&
            (level - buildupFromLevel >= BUILDUP_CLIMB || displacement >= BUILDUP_LEVEL * 1.5f)) {
            if (!buildupActive) buildupSince = now;
            buildupActive = true;
        }
    } else {
        if (now - buildupLastExceededMs >= kStructureHangoverMs) {
            buildupHoldSince = 0;
            // A confirmed buildup is not ended by the displacement decaying. It
            // ends on a descent, a drop or the gate (handled below and in
            // clearStructure), or after BUILDUP_SUSTAIN_MAX_MS.
            if (buildupActive && now - buildupSince >= BUILDUP_SUSTAIN_MAX_MS) buildupActive = false;
        }
    }

    if (displacement <= -DESCENT_LEVEL) {
        descentLastExceededMs = now;
        if (descentHoldSince == 0) {
            descentHoldSince = now;
            descentFromLevel = level;
        } else if (level > descentFromLevel) {
            descentFromLevel = level;
        }
        if (now - descentHoldSince >= DESCENT_HOLD_MS &&
            (descentFromLevel - level >= DESCENT_FALL || -displacement >= DESCENT_LEVEL * 1.5f)) {
            descentActive = true;
        }
    } else {
        if (now - descentLastExceededMs >= kStructureHangoverMs) {
            descentHoldSince = 0;
            descentActive    = false;
        }
    }

    // A descent begins: the buildup it follows is over.
    if (descentActive && buildupActive) {
        buildupActive    = false;
        buildupHoldSince = 0;
    }

    // Reported as a displacement so a reader can see how hard the movement is, and
    // zero when there is no movement, which is the single test the classifier
    // makes. See the field's comment in AudioFeatures.
    // A sustained buildup on a plateau has a displacement near zero or slightly
    // negative, and a reader tests only whether this is above zero, so it is floored.
    features.buildup = buildupActive ? fmaxf(displacement, 0.01f) : 0.0f;
    features.descent = descentActive ? -displacement : 0.0f;

    // ==== DROP ====
    //
    // Four conditions and none of them is one a beat can satisfy. The breadth test
    // is the load-bearing one: a beat is low end and nothing else, which is exactly
    // what BEAT_BASS_RISE exploits to tell a rhythm from a voice, and a kick cannot
    // light all three bands against their own recent peaks at once. A drop does it
    // by construction.
    //
    // The quiet condition is what stops a drop firing mid-chorus, and it encodes
    // the musical fact that a drop follows a breakdown or a buildup rather than
    // arriving in the middle of a loud passage. It reads the slow mean rather than
    // the instantaneous level, so a single quiet block inside a loud passage is not
    // a breakdown.
    if (slowLevel < DROP_QUIET_LEVEL) {
        if (!quietHeld) {
            quietHeld  = true;
            quietSince = now;
        }
    } else {
        quietHeld = false;
    }

    const bool armed = quietHeld && (now - quietSince >= DROP_ARM_MS);
    if (armed &&
        displacement >= DROP_SCALE &&
        features.bassLevel   >= DROP_BAND_LEVEL &&
        features.midLevel    >= DROP_BAND_LEVEL &&
        features.trebleLevel >= DROP_BAND_LEVEL &&
        now - lastDropMs >= DROP_COOLDOWN_MS) {
        features.dropDetected = true;
        lastDropMs = now;

        // The arrival releases the buildup or descent it ended, so both stop here
        // and the animations reading features.buildup stop with them. Before this a
        // drop left them running until the displacement decayed. The buildup is then
        // held off until the displacement has fallen back under its threshold: the
        // payoff sits above the slow mean for seconds, and without the latch the
        // hold would restart on the next block and open a buildup inside the drop.
        buildupActive    = false;
        buildupHoldSince = 0;
        descentActive    = false;
        descentHoldSince = 0;
        buildupLatched   = true; 
        features.buildup = 0.0f;
        features.descent = 0.0f;
    }

    // ==== TEASE ====
    //
    // Three triggers, any one of which is enough, because to a listener they are
    // the same thing: tension without full energy. A breakdown after a drop, a hush
    // before one, and a pulse that will not sustain.
    //
    // No hold and no cooldown of its own, deliberately. The mood system already has
    // both at confirmMs and minHoldMs, and a second pair here would open a dead zone
    // where a genuinely teasing passage is not reported at all, which is the failure
    // the total partition exists to remove.
    levelRing[levelRingNext] = level;
    levelRingNext = (levelRingNext + 1) % TEASE_WINDOW;
    if (levelRingCount < TEASE_WINDOW) ++levelRingCount;

    bool fakeOut = false;
    if (levelRingCount >= TEASE_WINDOW) {
        float mean = 0.0f;
        for (int i = 0; i < TEASE_WINDOW; ++i) mean += levelRing[i];
        mean /= float(TEASE_WINDOW);

        float variance = 0.0f;
        for (int i = 0; i < TEASE_WINDOW; ++i) {
            const float d = levelRing[i] - mean;
            variance += d * d;
        }
        variance /= float(TEASE_WINDOW);

        fakeOut = variance > TEASE_VARIANCE &&
                  mean > TEASE_MEAN_LOW && mean < TEASE_MEAN_HIGH;
    }

    const bool postDrop = lastDropMs != 0 && now - lastDropMs < TEASE_POST_DROP_MS;
    const bool hush     = level < TEASE_LOW_LEVEL &&
                          (features.buildup > 0.0f ||
                           features.dynamics > TEASE_HUSH_DYNAMICS);

    features.teaseDetected = features.gateGain >= SILENT_GATE &&
                             (postDrop || hush || fakeOut);

    // ==== WEIRD ====
    //
    // Two of three, and each is a rate of change or a band membership rather than a
    // level, so a stable passage scores zero whatever its spectrum is. That is the
    // property that keeps a quiet drifting ambient passage at CALM instead of
    // turning it into this, along with the ladder gate in the classifier.
    //
    // Flatness is the value the noise floor already computes and used to discard.
    const float span = centHi - centLo;
    const float mid  = (centHi + centLo) * 0.5f;
    const float centroidShare =
        (span > WEIRD_CENTROID_MIN_SPAN) ? fabsf(features.spectrumCentroid - mid) / span : 0.0f;

    const float spread = tempoSpread();

    int score = 0;
    if (centroidShare > WEIRD_CENTROID_SHARE)                       ++score;
    if (spread > WEIRD_TEMPO_SPREAD)                                ++score;
    if (features.spectralFlatness > WEIRD_FLAT_MIN &&
        features.spectralFlatness < WEIRD_FLAT_MAX)                 ++score;

    // The window is a two-sided follower rather than a rise/fall pair, because the
    // centroid move in both directions and a window that only ever widened would
    // make the share a shrinking number over the course of a track.
    if (!centSeeded) {
        centLo = centHi = features.spectrumCentroid;
        centSeeded = true;
    } else {
        const float up   = 1.0f - expf(-dt / 4.0f);
        const float down = 1.0f - expf(-dt / 12.0f);
        if (features.spectrumCentroid > centHi) centHi += (features.spectrumCentroid - centHi) * up;
        else                                    centHi -= (centHi - features.spectrumCentroid) * down;
        if (features.spectrumCentroid < centLo) centLo += (features.spectrumCentroid - centLo) * up;
        else                                    centLo -= (centLo - features.spectrumCentroid) * down;
    }

    // Held, and no cooldown after it. The hold is what rejects a passage that
    // flickers across two of three for a moment, and clearing it the moment the
    // score drops means re-entry costs another full WEIRD_HOLD_MS of sustained
    // evidence. A cooldown on top of that would only open a dead zone where a
    // genuinely eclectic passage stops being reported, which is the failure the
    // total partition exists to remove.
    if (score >= int(WEIRD_ANOMALY_MIN)) {
        if (weirdSince == 0) weirdSince = now;
    } else {
        weirdSince = 0;
    }
    const bool held = weirdSince != 0 && now - weirdSince >= WEIRD_HOLD_MS;

    features.anomaly = held ? float(score) : 0.0f;

    // ==== Episodes ====
    //
    // Last, for the same reason the whole function is: it consumes what the
    // detectors concluded and feeds nothing back into them.
    StructuralEpisodes::Inputs in;
    in.now              = now;
    in.level            = level;
    in.displacement     = displacement;
    in.buildupActive    = buildupActive;
    in.buildupHoldSince = buildupHoldSince;
    in.descentActive    = descentActive;
    in.descentHoldSince = descentHoldSince;
    in.dropOnset        = features.dropDetected;
    in.dropArrival      = displacement;
    in.tease            = features.teaseDetected;
    in.anomaly          = features.anomaly > 0.0f;
    episodes.update(in);
    episodes.fill(features, now);
}

// The spread of the remembered inter-beat intervals over their median. Zero when
// there are too few intervals to say anything, which reads as a steady tempo
// rather than as an unstable one: a detector that had not yet seen six beats has
// no evidence of eclecticism and should not be inventing it.
float AudioProcessor::tempoSpread() const {
    if (beatIntervalCount < WEIRD_TEMPO_MIN_HITS) return 0.0f;

    const float median = float(medianBeatInterval());
    if (median <= 0.0f) return 0.0f;

    unsigned long lo = beatIntervals[0];
    unsigned long hi = beatIntervals[0];
    for (int i = 1; i < beatIntervalCount; ++i) {
        if (beatIntervals[i] < lo) lo = beatIntervals[i];
        if (beatIntervals[i] > hi) hi = beatIntervals[i];
    }
    return float(hi - lo) / median;
}
