#include "AudioProcessor.h"
#include <driver/i2s.h>      // ESP32 I2S driver
#include <Arduino.h>         // for millis()

// Constructor: allocate FFT engine
AudioProcessor::AudioProcessor() {
    FFT = new ArduinoFFT<double>(vReal, vImag, NUM_SAMPLES, SAMPLE_RATE);
}

// Destructor: clean up FFT engine
AudioProcessor::~AudioProcessor() {
    delete FFT;
}

// Initialize I2S peripheral for microphone input
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
    size_t bytesRead = 0;
    esp_err_t err = i2s_read(I2S_PORT,
                             (void*)i2sBuffer,
                             sizeof(i2sBuffer),
                             &bytesRead,
                             portMAX_DELAY);
    if (err != ESP_OK) return;

    int count = bytesRead / sizeof(int32_t);
    for (int i = 0; i < count && i < NUM_SAMPLES; i++) {
        // Convert 32-bit signed i2s sample to normalized double
        int32_t s = i2sBuffer[i] >> 8;
        if (s & 0x800000) s |= ~0xFFFFFF;
        double norm = s / 8388608.0;
        vReal[i] = norm;
        vImag[i] = 0.0;
        buffer[i] = static_cast<int16_t>(norm * 32767);
    }
}

// Perform FFT and compute audio features
AudioFeatures AudioProcessor::analyzeAudio() {
    AudioFeatures features;
    features.waveform = buffer;
    features.waveformSize = NUM_SAMPLES;

    // Time-domain metrics
    double sum = 0, sumSq = 0, maxV = 0;
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        double v = constrain(vReal[i], -1.0, 1.0);
        double av = fabs(v);
        sum += av;
        sumSq += v * v;
        maxV = max(maxV, av);
    }
    double avg = sum / NUM_SAMPLES;
    volume = sqrt(sumSq / NUM_SAMPLES);
    peak = maxV;
    loudness = gainSmoothing * loudness + (1 - gainSmoothing) * (volume * 100.0);

    // Frequency-domain analysis
    FFT->windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT->compute(FFT_FORWARD);
    FFT->complexToMagnitude();
    memcpy(features.spectrum, vReal, sizeof(double) * (NUM_SAMPLES/2));

    int bassLimit = 200 * NUM_SAMPLES / SAMPLE_RATE;
    int midLimit  = 2000 * NUM_SAMPLES / SAMPLE_RATE;
    int half      = NUM_SAMPLES / 2;
    double eTotal=0, cSum=0, bSum=0, mSum=0, tSum=0;
    for (int i=1;i<half;i++){
        double mag = vReal[i];
        if(i<=bassLimit) bSum+=mag;
        else if(i<=midLimit) mSum+=mag;
        else tSum+=mag;
        eTotal += mag;
        cSum    += mag * i;
    }
    features.bass  = constrain((bSum/bassLimit)/100.0, 0,1);
    features.mid   = constrain((mSum/(midLimit-bassLimit))/80.0,  0,1);
    features.treble= constrain((tSum/(half-midLimit))/50.0,       0,1);
    features.energy= eTotal;
    features.spectrumCentroid = eTotal>0? (cSum/eTotal): 0;
    // Find dominant bin
    int dom=1; double mx=vReal[1];
    for(int i=2;i<half;i++) if(vReal[i]>mx){ mx=vReal[i]; dom=i; }
    features.dominantBand = dom;
    features.frequency    = dom * SAMPLE_RATE / NUM_SAMPLES;

    // Beat detection
    unsigned long now = millis();
    bool beat = false;
    if (volume - previousVolume > 0.04 && now - lastBeatTime > 250) {
        beat = true;
        unsigned long interval = now - lastBeatTime;
        if (interval>=250 && interval<=2000) currentBPM = 60000.0/interval;
        lastBeatTime = now;
    }
    features.beatDetected = beat;
    features.bpm          = currentBPM;

    previousVolume = volume;
    return features;
}
