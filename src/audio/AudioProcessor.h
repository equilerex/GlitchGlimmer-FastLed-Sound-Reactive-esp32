#pragma once

#include <Arduino.h>
#include <driver/i2s.h>
#include <arduinoFFT.h>
#include "../config/Config.h"
#include "../core/Debug.h"
#include "AudioFeatures.h"

class AudioProcessor {
private:
    double vReal[NUM_SAMPLES];
    double vImag[NUM_SAMPLES];
    int16_t buffer[NUM_SAMPLES];

    ArduinoFFT<double>* FFT;

    float gainSmoothing = 0.85f;
    float volume = 0;
    float previousVolume = 0;

    float agcLevel = 1.0;
    float noiseFloor = 0.0f;
    float loudness = 0;
    float peak = 0;
    float average = 0;
    float energy = 0;
    float dynamics = 0;
    float spectrumCentroid = 0;
    int dominantBand = 0;

    unsigned long lastBeatTime = 0;
    float currentBPM = 0.0;
    int bassHitCount = 0;

public:
    AudioProcessor() {
        FFT = new ArduinoFFT<double>(vReal, vImag, NUM_SAMPLES, SAMPLE_RATE);
    }

    ~AudioProcessor() {
        delete FFT;
    }

    void begin() {
        i2s_config_t i2s_config = {
            .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
            .sample_rate = SAMPLE_RATE,
            .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
            .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
            .communication_format = I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
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

        i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
        i2s_set_pin(I2S_PORT, &pin_config);
        i2s_zero_dma_buffer(I2S_PORT);
    }

    void captureAudio() {
        static int32_t i2sBuffer[NUM_SAMPLES];
        memset(i2sBuffer, 0, sizeof(i2sBuffer));
        size_t bytesRead = 0;

        esp_err_t result = i2s_read(I2S_PORT, (void*)i2sBuffer, sizeof(i2sBuffer), &bytesRead, 100 / portTICK_PERIOD_MS);
        if (result != ESP_OK) return;

        int samplesRead = bytesRead / sizeof(int32_t);
        for (int i = 0; i < samplesRead && i < NUM_SAMPLES; i++) {
            int32_t sample = i2sBuffer[i] >> 8;
            if (sample & 0x800000) sample |= ~0xFFFFFF;
            float normalized = sample / 8388608.0f;
            vReal[i] = normalized;
            vImag[i] = 0.0;
            buffer[i] = static_cast<int16_t>(normalized * 32767);
        }
    }

    AudioFeatures analyzeAudio() {
        AudioFeatures features = {};
        
        // Make sure buffer is properly initialized before assigning
        if (buffer != nullptr) {
            features.waveform = buffer;
            features.waveformSize = NUM_SAMPLES;
        } else {
            features.waveform = nullptr;
            features.waveformSize = 0;
            Serial.println("WARNING: Audio buffer is null in analyzeAudio");
        }

        float sum = 0;
        float maxVal = 0;
        float sumSquares = 0;

        for (int i = 0; i < NUM_SAMPLES; ++i) {
            vReal[i] = constrain(vReal[i], -1.0, 1.0);
            float absVal = fabs(vReal[i]);
            sum += absVal;
            sumSquares += vReal[i] * vReal[i];
            if (absVal > maxVal) maxVal = absVal;
        }

        average = sum / NUM_SAMPLES;
        volume = sqrt(sumSquares / NUM_SAMPLES);
        peak = maxVal;
        loudness = gainSmoothing * loudness + (1 - gainSmoothing) * (volume * 100.0);

        FFT->windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
        FFT->compute(FFT_FORWARD);
        FFT->complexToMagnitude();

        memcpy(features.spectrum, vReal, sizeof(double) * (NUM_SAMPLES / 2));

        int bassLimit = 200 * NUM_SAMPLES / SAMPLE_RATE;
        int midLimit = 2000 * NUM_SAMPLES / SAMPLE_RATE;
        int trebLimit = NUM_SAMPLES / 2;

        double bassSum = 0, midSum = 0, trebSum = 0, centroidSum = 0, totalEnergy = 0;

        // Calculate spectral features and energy
        for (int i = 1; i < NUM_SAMPLES / 2; ++i) {
            double magnitude = vReal[i];

            // Accumulate energy by frequency bands
            if (i <= bassLimit) {
                bassSum += magnitude;
            } else if (i <= midLimit) {
                midSum += magnitude;
            } else {
                trebSum += magnitude;
            }

            // Calculate total energy and spectral centroid
            totalEnergy += magnitude;
            centroidSum += magnitude * i;
        }

        // Dominant frequency bin (excluding bin 0)
        int dominantBand = 1;
        double maxMagnitude = vReal[1];
        for (int i = 2; i < NUM_SAMPLES / 2; ++i) {
            if (vReal[i] > maxMagnitude) {
                maxMagnitude = vReal[i];
                dominantBand = i;
            }
        }
        features.dominantBand = dominantBand;

        // Optional: compute frequency in Hz if desired
        float freqHz = dominantBand * SAMPLE_RATE / NUM_SAMPLES;
        features.frequency = freqHz;
        // Serial.printf("Dominant Frequency: %.2f Hz\n", freqHz); // Uncomment to log

        // Presence detection (optional boolean)
        features.signalPresence = features.volume > 0.05f; // tweak threshold if needed

        // Occasionally dump a snapshot of FFT bins (optional visual debugging)
        static unsigned long lastFftLog = 0;
        if (millis() - lastFftLog > 5000) { // every 5 seconds
      //      Serial.println(F("FFT Bin Snapshot:"));
            for (int i = 0; i < 32; ++i) {
              //  Serial.printf("%2d: %.2f\t", i, vReal[i]);
             //   if ((i + 1) % 4 == 0) Serial.println();
            }
            lastFftLog = millis();
        }

        features.bass = constrain((bassSum / bassLimit) / 100.0, 0.0, 1.0);
        features.mid = constrain((midSum / (midLimit - bassLimit)) / 80.0, 0.0, 1.0);
        features.treble = constrain((trebSum / (trebLimit - midLimit)) / 50.0, 0.0, 1.0);

        features.volume = volume;
        features.loudness = constrain(loudness, 0, 100);
        features.peak = peak;
        features.average = average;
        features.agcLevel = agcLevel;
        features.energy = totalEnergy;
        // Protect against division by zero
        features.spectrumCentroid = (totalEnergy > 0) ? (centroidSum / totalEnergy) : 0;
        features.dominantBand = dominantBand;
        features.dynamics = peak - average;
        features.noiseFloor = noiseFloor;

        // Beat detection
        float delta = volume - previousVolume;
        unsigned long currentTime = millis();
        features.beatDetected = false; // Initialize to false

        if (delta > 0.04 && (currentTime - lastBeatTime) > 250) {
            features.beatDetected = true;
            unsigned long beatInterval = currentTime - lastBeatTime;
            if (beatInterval >= 250 && beatInterval <= 2000) {
                currentBPM = 60000.0f / beatInterval;
                bassHitCount++;
            }
            lastBeatTime = currentTime;
        }

        features.bpm = currentBPM;
        features.bassHits = bassHitCount;

        previousVolume = volume;

        // Print audio analysis data at a reduced rate (every 3 seconds)
        static unsigned long lastPrintTime = 0;
        if (currentTime - lastPrintTime > 3000) { // Print once every 3 seconds
            Serial.printf("VOL: %.2f, LOUD: %.2f, PEAK: %.2f, AVG: %.2f, BASS: %.2f, MID: %.2f, TREBLE: %.2f, DYN: %.2f, BPM: %.1f, CENTROID: %.2f, BAND: %d, BEAT: %d, FREQ: %.1f Hz\n",
                        features.volume, features.loudness, features.peak, features.average,
                        features.bass, features.mid, features.treble, features.dynamics,
                        features.bpm, features.spectrumCentroid, features.dominantBand,
                        features.beatDetected, features.frequency);
            lastPrintTime = currentTime;
        }

        return features;
    }
};