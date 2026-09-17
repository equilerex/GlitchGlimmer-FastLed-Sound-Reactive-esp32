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
    loudness           = 0.0f;
    lastBeatTime   = millis();
    currentBPM     = 0.0f;
    bpmAtLastBeat  = 0.0f;
    beatIntervalCount = 0;
    beatIntervalNext  = 0;
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

    // Hysteresis: open well above the floor, close closer to it, so a signal
    // sitting on the threshold does not chatter the whole spectral feature set.
    // Both thresholds are multiples of the floor rather than fixed amounts above
    // it. A fixed amount is a claim about the input's absolute scale, and this
    // microphone runs about 40 dB below it, so the absolute form left the gate
    // shut through music. Multiples hold at any gain. The small additive terms
    // only stop the gate opening on the first block, when the floor is still 0.
    //
    // Reads the floor this block left behind, since the floor is now measured
    // after the FFT and the flatness that decides it comes from there. One block
    // is 11.6 ms, which is under the gate's own ramp.
    if (signalPresence) {
        if (volume < noiseFloor * 1.5f + 0.0005f) signalPresence = false;
    } else {
        if (volume > noiseFloor * 2.5f + 0.001f) signalPresence = true;
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
        dynHi += (levelEnv - dynHi) * (levelEnv > dynHi ? DYN_EDGE_RISE : DYN_EDGE_FALL);
        dynLo += (levelEnv - dynLo) * (levelEnv < dynLo ? DYN_EDGE_RISE : DYN_EDGE_FALL);
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
    const float levelSpan = levelRef - noiseFloor;
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
    unsigned long now = millis();
    const unsigned long sinceBeat = now - lastBeatTime;
    const float beatRise = max(noiseFloor * BEAT_RISE_NOISE,
                               previousVolume * BEAT_RISE_FRACTION);
    bool beat = false;
    if (signalPresence && volume - previousVolume > beatRise &&
        bassEnergy > previousBassEnergy * BEAT_BASS_RISE &&
        sinceBeat > MIN_BEAT_INTERVAL) {
        beat = true;
        // The tempo is the median of the recent intervals, not the newest one. One
        // interval moves the readout by tens of BPM when a beat lands a block early
        // or late, and a beat the detector misses doubles the interval and halves
        // the number, which is what an erratic readout is made of.
        if (sinceBeat <= TEMPO_HOLD_MS) {
            beatIntervals[beatIntervalNext] = sinceBeat;
            beatIntervalNext = (beatIntervalNext + 1) % BEAT_BPM_WINDOW;
            if (beatIntervalCount < BEAT_BPM_WINDOW) ++beatIntervalCount;
            currentBPM    = 60000.0f / float(medianBeatInterval());
            bpmAtLastBeat = currentBPM;
        }
        lastBeatTime = now;
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
        }
    }
    features.beatDetected = beat;
    features.bpm          = currentBPM;
    // 1 on a block carrying a strong bass impulse, 0 otherwise. BassPulseStorm
    // reads this, and nothing had ever assigned it, so its branch that tests it
    // was unreachable.
    features.bassHits     = beat ? 1 : 0;

    previousVolume     = volume;
    previousBassEnergy = bassEnergy;
    return features;
}
