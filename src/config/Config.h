#define THEME        "CYBERPUNK"       // Sampling rate in Hz


// ==== I2S Audio Configuration ====
#define I2S_PORT        I2S_NUM_0
#define I2S_WS          26  // Word Select (LRCL)
#define I2S_SD          32  // Serial Data in (DOUT from mic)
#define I2S_SCK         27  // Bit Clock (BCLK)

// ==== Sampling ====
#define SAMPLE_RATE     44100        // Standard audio sampling rate
#define NUM_SAMPLES     512          // Must be a power of 2 (used by FFT)
#define CHANNEL_COUNT   1            // Mono input
#define BITS_PER_SAMPLE I2S_BITS_PER_SAMPLE_32BIT

// ==== Gain / Normalization ====
#define NOISE_THRESHOLD     0.02f    // Minimum input level before considered real signal
#define MAX_AUDIO_LEVEL     1.0f     // Normalized max range after scaling
#define GAIN_SMOOTHING      0.92f    // Smoothing for gain level
#define LOUDNESS_SMOOTHING  0.9f     // Smoothing for loudness calculation

// ==== FFT Configuration ====
#define FFT_SMOOTHING       0.8f     // Spectral smoothing for more stable bars
#define FFT_BANDS           16       // Number of bands for visualization/spectrum

// ==== Beat Detection ====
#define BEAT_THRESHOLD      0.05f    // Minimum change in volume to consider beat
#define MIN_BEAT_INTERVAL   300      // ms between beats (to avoid rapid re-triggers)

// ==== Display ====
#define DEFAULT_BRIGHTNESS  150



// ==== LED ====
#define LED_0_PIN            25
#define LED_1_PIN            33
// #define LED_2_PIN            25
// #define LED_3_PIN            25
// #define LED_4_PIN            25
// #define LED_5_PIN            25
// #define LED_6_PIN            25
// #define LED_7_PIN            25
// #define LED_8_PIN            25
// #define LED_9_PIN            25


#define LED_0_NUM 100
#define LED_1_NUM 10
#define LED_2_NUM 0
#define LED_3_NUM 0
#define LED_4_NUM 0
#define LED_5_NUM 0
#define LED_6_NUM 0
#define LED_7_NUM 0
#define LED_8_NUM 0
#define LED_9_NUM 0





//cant put this in the array, needs to be defined on compile
#define MIN_SWITCH_INTERVAL 10000





// ==== MEMORY MANAGEMENT ====
#define ENABLE_HEAP_MONITORING true
#define MIN_FREE_HEAP         32768    // 32KB minimum free heap

// ==== ENCODER ====
#define ENCODER_PIN_A      39
#define ENCODER_PIN_B      38
#define ENCODER_BTN_PIN    17

// ==== BUTTON ====
#define BUTTON_PIN_1         0
#define BUTTON_PIN_2         35

// ==== OTHER ====
#define ENABLE_WEB_UI      false        // Enable/disable WebUI
#define DEBUG_ENABLED      true        // Toggle debug logging

// ==== DISPLAY ====
#define DISPLAY_WIDTH      240
#define DISPLAY_HEIGHT     135
#define DISPLAY_PIN         4

// ==== VISUALIZATION ====
#define FFT_MAX_SCALE      50.0        // Scale factor for normalizing FFT bars
#define BAR_HEIGHT_MAX     30          // Max height for bass/mid/treble bars

// ==== DEBUG CONFIG ====
#define DEBUG_ENABLED      true        // Master debug switch
#define DEBUG_LEVEL       2           // 0=ERROR, 1=INFO, 2=DEBUG
#define DEBUG_BAUDRATE    115200      // Debug serial baudrate

// Fallback for std::make_unique if not available
#if __cplusplus < 201402L
#include <memory>
#endif
