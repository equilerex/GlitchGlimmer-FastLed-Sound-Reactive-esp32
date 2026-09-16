#include "CommunicationService.h"
#include "TFT_eSPI.h"
#include "../audio/AudioProcessor.h"
#include "../audio/AudioHistoryTracker.h"
#include "../scenes/SceneDirector.h"
#include "../core/LEDStripController.h"
#include "../input/EncoderInput.h"
#include "../input/ButtonInput.h"
#include "../display/DisplayManager.h"
#include "../core/SettingsManager.h" // <-- Add this include

// Forward declarations (already present, kept for clarity)
class AudioProcessor;
class AudioHistoryTracker;
class MoodHistory;
class SceneRegistry;
class SceneDirector;
class LEDStripController;
class EncoderInput;
class ButtonInput;
class DisplayManager;

class MainController {
public:
    MainController(CommunicationService& comm);
    ~MainController(); // <-- Add destructor declaration
    void begin();
    void update();

private:
    AudioFeatures      audioFeatures;
    // Change these members to pointers
    AudioHistoryTracker* audioHistory;
    MoodHistory*         moodHistory;
    SceneRegistry*       sceneRegistry;
    // Pointers below were already correct
    SceneDirector*      sceneDirector;
    AudioProcessor*     audioProcessor;
    LEDStripController* ledController;
    TFT_eSPI            tft; // Keep as object
    EncoderInput*       encoderInput;
    ButtonInput*        buttonInput;
    DisplayManager*     displayManager;
    CommunicationService& commService;
};