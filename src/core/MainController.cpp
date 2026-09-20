#include "MainController.h"
#include "../audio/AudioProcessor.h"
#include "../audio/AudioHistoryTracker.h"
#include "../scenes/SceneDirector.h"
#include "../core/LEDStripController.h"
#include "../input/EncoderInput.h"
#include "../input/ButtonInput.h"
#include "../display/DisplayManager.h"
#include "../core/SettingsManager.h" // Included in previous fix
#include "../config/Config.h" // Make sure this defines the PINs and NUMs
// Include other necessary headers for the class implementation

// --- Actual Definitions of the Global LED Buffers ---
// Use the same #ifdef guards as in the header to ensure consistency
#ifdef LED_0_PIN
    CRGB ledStrip_0[LED_0_NUM]; // Definition for Strip 0
#endif
#ifdef LED_1_PIN
CRGB ledStrip_1[LED_1_NUM]; // Definition for Strip 1
#endif
#ifdef LED_2_PIN
CRGB ledStrip_2[LED_2_NUM]; // Definition for Strip 2
#endif
#ifdef LED_3_PIN
CRGB ledStrip_3[LED_3_NUM]; // Definition for Strip 3
#endif
#ifdef LED_4_PIN
CRGB ledStrip_4[LED_4_NUM]; // Definition for Strip 4
#endif
// Add definitions for any other strips declared in the header


// Define the global settings manager instance (assuming it's defined in SettingsManager.cpp)
extern SettingsManager settingsManager;

MainController::MainController(CommunicationService& comm)
    : commService(comm)
{
    // Initialize all pointers to nullptr first
    audioHistory    = nullptr;
    moodHistory     = nullptr;
    sceneRegistry   = nullptr;
    sceneDirector   = nullptr;
    audioProcessor  = nullptr;
    ledController   = nullptr;
    encoderInput    = nullptr;
    buttonInput     = nullptr;
    displayManager  = nullptr;

    // Hardware initialization belongs in begin(), after Arduino setup() starts.
    // In particular, TFT_eSPI touches SPI state and cannot be initialized from
    // this global controller's constructor.
}

MainController::~MainController() {
    // Clean up all dynamically allocated objects in reverse order of creation
    delete displayManager;
    delete buttonInput;
    delete encoderInput;
    delete ledController;
    delete audioProcessor;
    delete sceneDirector;
    delete sceneRegistry;
    delete moodHistory;
    delete audioHistory;
}

void MainController::begin() {
    // Now initialize all components when begin() is called
    Serial.println("Initializing MainController components...");
    bool allComponentsInitialized = true;

    // --- Create minimal essential components first ---
    // Always create AudioHistoryTracker first as other components depend on it
    Serial.println("Creating AudioHistoryTracker..."); Serial.flush();
    audioHistory = new AudioHistoryTracker();
    if (!audioHistory) { 
        Serial.println("Failed to create AudioHistoryTracker"); 
        allComponentsInitialized = false;
    }

    Serial.println("Creating MoodHistory..."); Serial.flush();
    moodHistory = new MoodHistory();
    if (!moodHistory) { 
        Serial.println("Failed to create MoodHistory"); 
        allComponentsInitialized = false;
    }

    #if GG_HAS_DISPLAY
    Serial.println("Creating DisplayManager..."); Serial.flush();
    displayManager = new DisplayManager(tft);
    if (!displayManager) {
        Serial.println("DisplayManager allocation returned nullptr"); Serial.flush();
        allComponentsInitialized = false;
    } else {
        Serial.println("DisplayManager object created."); Serial.flush();
    }
    #else
    Serial.println("Display disabled; skipping DisplayManager"); Serial.flush();
    #endif

    // Create essential processors
    Serial.println("Creating AudioProcessor..."); Serial.flush();
    audioProcessor = new AudioProcessor();
    if (!audioProcessor) { 
        Serial.println("Failed to create AudioProcessor"); 
        allComponentsInitialized = false;
    }

    // Create scene management components
    Serial.println("Creating SceneRegistry..."); Serial.flush();
    sceneRegistry = new SceneRegistry();
    if (!sceneRegistry) { 
        Serial.println("Failed to create SceneRegistry"); 
        allComponentsInitialized = false;
    }

    // Only create SceneDirector if dependencies exist
    Serial.println("Creating SceneDirector..."); Serial.flush();
    if (moodHistory && sceneRegistry) {
        sceneDirector = new SceneDirector(*moodHistory, *sceneRegistry);
        if (!sceneDirector) { 
            Serial.println("Failed to create SceneDirector"); 
            allComponentsInitialized = false;
        }
    } else {
        Serial.println("Cannot create SceneDirector due to missing dependencies");
        allComponentsInitialized = false;
    }

    // Create LED controller with safer construction
    Serial.println("Creating LEDStripController..."); Serial.flush();
    if (moodHistory && audioHistory) {
        ledController = new LEDStripController(audioFeatures, *moodHistory, *audioHistory);
        if (!ledController) { 
            Serial.println("Failed to create LEDStripController"); 
            allComponentsInitialized = false;
        }
    } else {
        Serial.println("Cannot create LEDController due to missing dependencies");
        allComponentsInitialized = false;
    }

    // Create UI input components last
    Serial.println("Creating EncoderInput..."); Serial.flush();
    if (displayManager) {
        encoderInput = new EncoderInput(&settingsManager, *displayManager);
        if (!encoderInput) { 
            Serial.println("Failed to create EncoderInput"); 
            allComponentsInitialized = false;
        }
    } else {
        Serial.println("Cannot create EncoderInput without DisplayManager");
        allComponentsInitialized = false;
    }

    Serial.println("Creating ButtonInput..."); Serial.flush();
    buttonInput = new ButtonInput();
    if (!buttonInput) { 
        Serial.println("Failed to create ButtonInput"); 
        allComponentsInitialized = false;
    }

    // Report component initialization status
    if (allComponentsInitialized) {
        Serial.println("All components initialized successfully");
    } else {
        Serial.println("Some components failed to initialize, continuing with available ones");
    }

    // --- Begin essential components first ---
    FastLED.clear();
    
    // Initialize critical components first with error checking
    if (audioProcessor) {
        Serial.println("Initializing AudioProcessor..."); Serial.flush();
        audioProcessor->begin();
        Serial.println("AudioProcessor initialized"); Serial.flush();
    }
    
    if (sceneDirector) {
        Serial.println("Initializing SceneDirector..."); Serial.flush();
        sceneDirector->begin();
        Serial.println("SceneDirector initialized"); Serial.flush();
    }
    
    // Initialize display if available
    if (displayManager) {
        Serial.println("Initializing DisplayManager..."); Serial.flush();
        displayManager->begin();
        Serial.println("DisplayManager initialized"); Serial.flush();
    } else {
        Serial.println("DisplayManager is NULL, skipping initialization"); Serial.flush();
    }

    // Add safety delay before LED controller initialization
    Serial.println("Preparing for LED controller initialization..."); Serial.flush();
    delay(100); // Allow system to stabilize
    
    // Initialize LED controller with extra safety checks
    if (ledController) {
        Serial.println("Initializing LEDStripController..."); Serial.flush();
        
        // Create a wrapper to catch any potential issues during initialization
        bool ledInitSuccess = false;
        
        // First verify that our LED buffers are valid
        #ifdef LED_0_PIN
        if (ledStrip_0 != nullptr) {
            FastLED.clear(true); // Clear all LED data first
            Serial.println("LED buffer 0 is valid"); Serial.flush();
            ledInitSuccess = true;
        } else {
            Serial.println("WARNING: LED buffer 0 is NULL"); Serial.flush();
        }
        #endif
        
        if (ledInitSuccess) {
            // Initialize with extra caution
            ledController->begin();
            Serial.println("LEDStripController initialized successfully"); Serial.flush();
        } else {
            Serial.println("Skipping LEDStripController begin() due to invalid buffers"); Serial.flush();
        }
    } else {
        Serial.println("LEDStripController is NULL, skipping initialization"); Serial.flush();
    }
    
    // Add another safety delay after LED initialization
    delay(100);

    // Initialize input components
    if (encoderInput) {
        Serial.println("Initializing EncoderInput..."); Serial.flush();
        encoderInput->begin();
        Serial.println("EncoderInput initialized"); Serial.flush();
    } else {
        Serial.println("EncoderInput is NULL, skipping initialization"); Serial.flush();
    }

    if (buttonInput) {
        Serial.println("Initializing ButtonInput..."); Serial.flush();
        buttonInput->begin();
        Serial.println("ButtonInput initialized"); Serial.flush();
    } else {
        Serial.println("ButtonInput is NULL, skipping initialization"); Serial.flush();
    }

    // Finalize LED setup with more robust error checking
    Serial.println("Setting FastLED Brightness..."); Serial.flush();
    
    // Check if we have valid LED strips configured before calling FastLED
    bool hasValidLEDStrips = false;
    
    #ifdef LED_0_PIN
    if (ledStrip_0 != nullptr) {
        hasValidLEDStrips = true;
    }
    #endif
    
    #ifdef LED_1_PIN
    if (ledStrip_1 != nullptr) {
        hasValidLEDStrips = true;
    }
    #endif
    
    if (hasValidLEDStrips) {
        // Only call FastLED functions if we have valid LED strips
        FastLED.setBrightness(DEFAULT_BRIGHTNESS);
        Serial.println("FastLED Brightness Set to " + String(DEFAULT_BRIGHTNESS)); Serial.flush();
        delay(10); // Small stabilization delay
        
        // Clear strips first
        FastLED.clear(true);
        Serial.println("LEDs cleared"); Serial.flush();
        delay(10); // Small stabilization delay
        
        // Now show (apply) the zero values
        FastLED.show();
        Serial.println("FastLED.show() called successfully"); Serial.flush();
    } else {
        Serial.println("No valid LED strips detected, skipping FastLED initialization"); Serial.flush();
    }
    
    delay(100); // Add a final stabilization delay
    Serial.println("MainController::begin() fully finished."); Serial.flush();
}

void MainController::update() {
    // Drain the I2S DMA ring on every pass, before the frame gate below.
    // The ring holds 8 x 64 samples, about 11.6ms of audio, so reading it only
    // once per 33ms frame throws away most of the signal and leaves the FFT
    // analysing a stale slice.
    if (audioProcessor && ESP.getFreeHeap() > 20 * 1024) {
        audioProcessor->captureAudio();
    }

    static unsigned long lastFrame = 0;
    const unsigned long frameInterval = 33; // Target ~30 FPS for display updates
    unsigned long now = millis();
    if (now - lastFrame < frameInterval) return;
    lastFrame = now;

    // Use local error tracking to isolate component failures
    bool hasErrors = false;

    // Check heap health first
    if (ESP.getFreeHeap() < 10 * 1024) {
        // Memory is getting low, skip non-essential updates
        Serial.println("Low memory detected, skipping non-essential updates");
        
        // Still update LEDs but nothing else
        if (ledController) {
            ledController->update();
        }
        
        // Give time for memory to recover
        yield();
        return;
    }

    // Update Input Components
    if (encoderInput) {
        encoderInput->update();
    }
    
    if (buttonInput) {
        buttonInput->update();
    }

    // Process Audio - Critical component.
    // Capture already happened above; this is the frame-rate analysis pass.
    if (audioProcessor) {
        if (ESP.getFreeHeap() > 20 * 1024) { // Only process audio if we have enough memory
            audioFeatures = audioProcessor->analyzeAudio();

            #if !GG_HAS_MICROPHONE
            // Keep an LED-only board visibly alive without pretending that a
            // microphone supplied audio. The analyzer remains silent; this
            // fallback only gives the renderer a gentle idle level.
            audioFeatures.level = GG_IDLE_VISUAL_LEVEL;
            audioFeatures.bassLevel = GG_IDLE_VISUAL_LEVEL;
            audioFeatures.midLevel = GG_IDLE_VISUAL_LEVEL;
            audioFeatures.trebleLevel = GG_IDLE_VISUAL_LEVEL;
            audioFeatures.gateGain = 1.0f;
            audioFeatures.signalPresence = true;
            #endif

            if (audioHistory) {
                audioHistory->addSnapshot(audioFeatures);
            }
        }
    } else {
        // Only log error periodically to avoid overwhelming Serial
        static unsigned long lastAudioError = 0;
        if (now - lastAudioError > 5000) {
            Serial.println("Audio processor not available");
            lastAudioError = now;
            hasErrors = true;
        }
    }

    // Update LED Controller - Critical for visual output
    // Add extra safety check with return value validation
    if (ledController) {
        // Check LED buffers validity before updating
        bool ledBuffersValid = true;
        
        #ifdef LED_0_PIN
        if (ledStrip_0 == nullptr) {
            ledBuffersValid = false;
        }
        #endif
        
        if (ledBuffersValid) {
            ledController->update();
        } else {
            static unsigned long lastLEDBufferError = 0;
            if (now - lastLEDBufferError > 10000) {
                Serial.println("LED buffers invalid, skipping update");
                lastLEDBufferError = now;
            }
        }
    } else {
        static unsigned long lastLEDError = 0;
        if (now - lastLEDError > 5000) {
            Serial.println("LED controller not available");
            lastLEDError = now;
            hasErrors = true;
        }
    }

    // Update Display - Non-critical
    if (displayManager) {
        // Re-query the name only when the scene actually changes.
        // getCurrentSceneName() returns a String by value and this ran every
        // frame, so it built and discarded a heap-backed string per frame for a
        // name that changes every twenty seconds.
        static String lastAnimName = "Unknown";
        static int    lastSceneChangeCount = -1;

        if (ledController) {
            const int changes = ledController->getSceneChangeCount();
            if (changes != lastSceneChangeCount) {
                lastAnimName = ledController->getCurrentSceneName();
                lastSceneChangeCount = changes;
            }
        }

        displayManager->update(audioFeatures, lastAnimName);
    }
    
    // Log error summary only once per interval to avoid flooding Serial
    if (hasErrors) {
        static unsigned long lastSummaryError = 0;
        if (now - lastSummaryError > 10000) {
            Serial.println("Some components are missing or failed");
            lastSummaryError = now;
        }
    }
    
    // Always yield to prevent watchdog timeouts
    yield();
}
