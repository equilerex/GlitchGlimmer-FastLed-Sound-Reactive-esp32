// Browser entry point for the web visualiser.
//
// Compiled to WebAssembly by tools/build-wasm.sh. It is the same
// LEDStripController, SceneDirector, LayerManager and animation catalog the
// device firmware runs, stepped once per animation frame from JavaScript, so the
// page shows the firmware's own output rather than a JavaScript reimplementation
// of it. Nothing in here is linked into the firmware or the native harness.
//
// Audio arrives from the page: the microphone's time-domain samples are written
// into gg_sample_buffer() and AudioProcessor::analyzeAudio() does the FFT and the
// feature extraction on this side, because reproducing its magnitude scale in
// JavaScript would be guesswork. The page decides how often to step, but not how
// much time passed: FastLED's wasm platform supplies millis() from Emscripten's
// clock, so timing stays honest across a stalled or backgrounded tab.

#include <Arduino.h>
#include <FastLED.h>
#include <emscripten/emscripten.h>

#include <string>

#include "config/Config.h"
#include "audio/AudioFeatures.h"
#include "audio/AudioSnapshot.h"
#include "audio/AudioHistoryTracker.h"
#include "audio/AudioProcessor.h"
#include "scenes/MoodHistory.h"
#include "scenes/SceneRegistry.h"
#include "scenes/SceneState.h"
#include "scenes/LayerTypes.h"
#include "scenes/LayerManager.h"
#include "animations/AnimationCatalog.h"
#include "core/LEDStripController.h"

// -----------------------------------------------------------------------------
//  Globals the firmware expects to find at link time
// -----------------------------------------------------------------------------
CRGB ledStrip_0[LED_0_NUM];
CRGB ledStrip_1[LED_1_NUM];

// -----------------------------------------------------------------------------
//  Heap counters
//
//  SimEsp is declared in sim/stubs/wasm/prelude.h, which this build force-includes
//  into every translation unit, because FastLED's Arduino emulation has no ESP
//  object. LEDStripController's memory check compares these against MIN_FREE_HEAP,
//  so they report a plausible fixed figure rather than zero, which would trip the
//  warning on every frame.
// -----------------------------------------------------------------------------
uint32_t SimEsp::getFreeHeap() const { return 200u * 1024u; }
uint32_t SimEsp::getMinFreeHeap() const { return 200u * 1024u; }

SimEsp ESP;

// -----------------------------------------------------------------------------
//  The live pipeline
// -----------------------------------------------------------------------------
namespace {

AudioFeatures       g_audio;
MoodHistory         g_mood;
AudioHistoryTracker g_history;
LEDStripController  g_ctrl(g_audio, g_mood, g_history);
AudioProcessor      g_proc;
AudioFeatures       g_last;          // what the controller was last stepped with

// Samples the page writes into. NUM_SAMPLES is what the FFT is sized for, so a
// step is exactly one analysis window.
float g_samples[NUM_SAMPLES];

// getCurrentSceneName() and getCurrentMoodName() both return by value, so the
// pointer would dangle if it were taken from the temporary. These statics hold
// the copy that the returned c_str() points at.
std::string g_sceneName = "—";
std::string g_moodName  = "—";

} // namespace

// -----------------------------------------------------------------------------
//  Exported surface. Everything is called from web/live.js.
//
//  extern "C" so the names reach the page unmangled. Without it these are C++
//  symbols, the glue carries them as __Z7gg_initv and everything the page calls
//  is undefined, which shows up in the browser as "wasm._gg_init is not a
//  function" with no build error to point at.
// -----------------------------------------------------------------------------
extern "C" {

// Build the controller and register the strips. Call once, before gg_step().
EMSCRIPTEN_KEEPALIVE
void gg_init(void) {
    g_ctrl.begin();
    g_sceneName = g_ctrl.getCurrentSceneName();
    g_moodName  = g_mood.getCurrentMoodName();
}

// Length of the window gg_sample_buffer() expects, so the page does not hardcode
// a number that only the firmware knows.
EMSCRIPTEN_KEEPALIVE
int gg_sample_count(void) { return NUM_SAMPLES; }

// Where to write the microphone's time-domain samples, each normalised to -1..1.
EMSCRIPTEN_KEEPALIVE
float* gg_sample_buffer(void) { return g_samples; }

// Advance one frame: analyse the samples the page just wrote and step the
// controller. The clock is FastLED's wasm millis(), which counts real elapsed time
// from module load, so the beat detector and every time-based fade see the same
// variable-length frames the device's loop produces. The page passes no delta and
// so cannot lie about one; it only decides how often to call this.
EMSCRIPTEN_KEEPALIVE
void gg_step(void) {
    g_proc.submitSamples(g_samples, NUM_SAMPLES);
    g_last = g_proc.analyzeAudio();
    g_audio = g_last;

    g_history.addSnapshot(g_audio);
    g_ctrl.update();

    g_sceneName = g_ctrl.getCurrentSceneName();
    g_moodName  = g_mood.getCurrentMoodName();
}

EMSCRIPTEN_KEEPALIVE
uint8_t* gg_leds0(void) { return reinterpret_cast<uint8_t*>(ledStrip_0); }

EMSCRIPTEN_KEEPALIVE
uint8_t* gg_leds1(void) { return reinterpret_cast<uint8_t*>(ledStrip_1); }

EMSCRIPTEN_KEEPALIVE
int gg_leds0_count(void) { return LED_0_NUM; }

EMSCRIPTEN_KEEPALIVE
int gg_leds1_count(void) { return LED_1_NUM; }

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_name(void) { return g_sceneName.c_str(); }

EMSCRIPTEN_KEEPALIVE
const char* gg_mood_name(void) { return g_moodName.c_str(); }

// The feature values the HUD shows. Exposed as one call rather than a field per
// accessor because the page reads them together and this keeps the boundary from
// growing an accessor for every field someone later wants to display.
EMSCRIPTEN_KEEPALIVE
float gg_feature(int index) {
    switch (index) {
        case 0:  return g_last.volume;
        case 1:  return g_last.loudness;
        case 2:  return g_last.peak;
        case 3:  return g_last.bass;
        case 4:  return g_last.mid;
        case 5:  return g_last.treble;
        case 6:  return g_last.energy;
        case 7:  return g_last.dynamics;
        case 8:  return g_last.bpm;
        case 9:  return g_last.beatDetected ? 1.0f : 0.0f;
        case 10: return g_last.level;
        case 11: return g_last.noiseFloor;
        case 12: return g_last.signalPresence ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

// Pointers to the feature struct and the spectrum array, so the page can render a
// spectrum meter without an accessor per bin.
EMSCRIPTEN_KEEPALIVE
float* gg_spectrum(void) { return g_last.spectrum; }

EMSCRIPTEN_KEEPALIVE
int gg_spectrum_count(void) { return NUM_SAMPLES / 2; }

EMSCRIPTEN_KEEPALIVE
int gg_beat_count(void) { return g_audio.bassHits; }

// How many layers are stacked on a strip, and how many times a scene has been
// begun. Both are what the page's ?debug=1 trace reads: the first shows a strip
// accumulating layers until the cap, the second shows the scene churning.
EMSCRIPTEN_KEEPALIVE
int gg_layer_count(int strip) { return g_ctrl.layerCount(strip); }

EMSCRIPTEN_KEEPALIVE
int gg_scene_changes(void) { return g_ctrl.getSceneChangeCount(); }

// -----------------------------------------------------------------------------
//  Derivations the state panel shows
//
//  The mood system's decisions are not visible in its inputs alone. What makes a
//  mood flicker is the pair (instantaneous classification, classification of the
//  averaged window), and what makes a scene change is the pair (elapsed time,
//  the duration thresholds it is being compared against). Exposing only energy
//  and bpm leaves the page unable to show why anything changed.
// -----------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE
const char* gg_mood_predicted_name(void) {
    static std::string name;
    name = g_mood.getPredictedMoodName();
    return name.c_str();
}

EMSCRIPTEN_KEEPALIVE
double gg_scene_elapsed_ms(void) { return double(g_ctrl.sceneElapsedMs()); }

EMSCRIPTEN_KEEPALIVE
double gg_scene_min_ms(void) { return double(g_ctrl.sceneMinMs()); }

EMSCRIPTEN_KEEPALIVE
double gg_scene_ideal_ms(void) { return double(g_ctrl.sceneIdealMs()); }

// One more gg_feature-style block rather than more cases on gg_feature, so the
// existing indices the page already reads keep their meaning.
EMSCRIPTEN_KEEPALIVE
float gg_spectrum_centroid(void) { return g_last.spectrumCentroid; }

EMSCRIPTEN_KEEPALIVE
int gg_dominant_band(void) { return g_last.dominantBand; }

EMSCRIPTEN_KEEPALIVE
int gg_history_size(void) { return int(g_mood.size()); }

// Counted in the firmware, not derived from the page's trace. The trace only
// records with ?debug=1 on, so a snapshot taken without it reported zero mood
// changes however much the mood had moved.
EMSCRIPTEN_KEEPALIVE
int gg_mood_changes(void) { return g_mood.getMoodChangeCount(); }

// The classifier's dynamics cut points, which move with the observed range. The
// panel used to mark them at a fixed 0.2 and 0.5, which is exactly the claim the
// moving thresholds were introduced to stop making. `active` is 0 when the input
// has too little dynamic variation for the cut points to mean anything, and the
// panel hides them rather than drawing a line through noise.
EMSCRIPTEN_KEEPALIVE
float gg_mood_dynamics_low(void) { return g_mood.getDynamicsLow(); }

EMSCRIPTEN_KEEPALIVE
float gg_mood_dynamics_high(void) { return g_mood.getDynamicsHigh(); }

EMSCRIPTEN_KEEPALIVE
float gg_mood_dynamics_active(void) {
    return g_mood.dynamicsThresholdsActive() ? 1.0f : 0.0f;
}

EMSCRIPTEN_KEEPALIVE
float gg_mood_min_hold_ms(void) { return float(g_mood.getMinHoldMs()); }

// -----------------------------------------------------------------------------
//  Live tuning
//
//  One indexed pair rather than a named export per dial, because the page builds
//  its controls from a table and the two have to agree on ordering. Adding a dial
//  means adding a case here and a row there.
//
//  The dials exist because every one of them is a judgement about how the
//  installation should feel, not a number that can be derived: how long a mood
//  should be held, how twitchy the classifier should be, how long a scene should
//  run. Tuning those by editing a constant and rebuilding was the slow path.
// -----------------------------------------------------------------------------
namespace {
constexpr int kTuneSceneMinBase   = 0;
constexpr int kTuneSceneIdealBase = 1;
constexpr int kTuneSceneIdealSpan = 2;
constexpr int kTuneMoodHold       = 3;
constexpr int kTuneMoodConfirm    = 4;
constexpr int kTuneMoodSmoothing  = 5;
constexpr int kTuneDynUp          = 6;
constexpr int kTuneDynDown        = 7;
constexpr int kTuneCount          = 8;
}  // namespace

EMSCRIPTEN_KEEPALIVE
float gg_tuning(int which) {
    switch (which) {
        case kTuneSceneMinBase:   return g_ctrl.sceneStateForTuning().getMinBaseMs();
        case kTuneSceneIdealBase: return g_ctrl.sceneStateForTuning().getIdealBaseMs();
        case kTuneSceneIdealSpan: return g_ctrl.sceneStateForTuning().getIdealSpanMs();
        case kTuneMoodHold:       return g_mood.getMinHoldMs();
        case kTuneMoodConfirm:    return g_mood.getConfirmMs();
        case kTuneMoodSmoothing:  return g_mood.getSmoothingRate();
        case kTuneDynUp:          return g_mood.getDynUpPerSec();
        case kTuneDynDown:        return g_mood.getDynDownPerSec();
        default:                  return 0.0f;
    }
}

EMSCRIPTEN_KEEPALIVE
void gg_set_tuning(int which, float value) {
    switch (which) {
        case kTuneSceneMinBase:   g_ctrl.sceneStateForTuning().setMinBaseMs(value);   break;
        case kTuneSceneIdealBase: g_ctrl.sceneStateForTuning().setIdealBaseMs(value); break;
        case kTuneSceneIdealSpan: g_ctrl.sceneStateForTuning().setIdealSpanMs(value); break;
        case kTuneMoodHold:       g_mood.setMinHoldMs(value);       break;
        case kTuneMoodConfirm:    g_mood.setConfirmMs(value);       break;
        case kTuneMoodSmoothing:  g_mood.setSmoothingRate(value);   break;
        case kTuneDynUp:          g_mood.setDynUpPerSec(value);     break;
        case kTuneDynDown:        g_mood.setDynDownPerSec(value);   break;
        default: break;
    }
}

EMSCRIPTEN_KEEPALIVE
int gg_tuning_count(void) { return kTuneCount; }

EMSCRIPTEN_KEEPALIVE
float gg_average(void) { return g_last.average; }

// How much of a strip is actually lit, and how much light it is emitting. The
// strips going black was the reported symptom and there is no way to see it from
// the feature values: a strip can sit at full energy and emit nothing. Lit count
// is the direct measure, the channel sum is the dimming that falls short of black.
EMSCRIPTEN_KEEPALIVE
int gg_lit_count(int strip) {
    const CRGB* buf = (strip == 0) ? ledStrip_0 : ledStrip_1;
    const int n = (strip == 0) ? LED_0_NUM : LED_1_NUM;
    int lit = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].r || buf[i].g || buf[i].b) ++lit;
    }
    return lit;
}

EMSCRIPTEN_KEEPALIVE
double gg_lit_sum(int strip) {
    const CRGB* buf = (strip == 0) ? ledStrip_0 : ledStrip_1;
    const int n = (strip == 0) ? LED_0_NUM : LED_1_NUM;
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += buf[i].r + buf[i].g + buf[i].b;
    }
    return sum;
}

EMSCRIPTEN_KEEPALIVE
int gg_lit_capacity(int strip) { return (strip == 0) ? LED_0_NUM : LED_1_NUM; }

} // extern "C"
