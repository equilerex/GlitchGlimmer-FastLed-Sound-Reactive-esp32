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

// FastLED's WASM platform is also usable as an Arduino-style application and
// keeps these lifecycle hooks in its platform object. The browser entry point
// drives the library through gg_step(), so the hooks are intentionally no-ops.
void setup() {}
void loop() {}

extern "C" void js_post_ui_elements(const char*) {}

// -----------------------------------------------------------------------------
//  Globals the firmware expects to find at link time
// -----------------------------------------------------------------------------
CRGB ledStrip_0[LED_0_CAPACITY];
CRGB ledStrip_1[LED_1_CAPACITY];

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
String g_sceneName = "—";
String g_moodName  = "—";

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

// Forget the analysis state left behind by whatever was feeding the module.
//
// The page has two inputs at very different amplitudes and switches between them,
// and the features are normalised against references learned from the input. A
// switch is therefore a discontinuity the analysis cannot see: it has no way to
// know the samples stopped coming from the same place, and it goes on measuring
// the new signal against the old signal's peak. Called on every change of source,
// including the one at load, so the first reading is of the source actually in
// use rather than of whatever the default filled the references with.
//
// The mood classifier is reset with it and is the same defect. Its smoothed values,
// its adaptive dynamics window and the 150-snapshot window predictNextMood averages
// are all built from the input that was playing, so a switch left the mood on
// display as a verdict on audio that had stopped, for as long as the ring held it.
EMSCRIPTEN_KEEPALIVE
void gg_reset_analysis(void) {
    g_proc.resetTracking();
    g_mood.reset();
    g_last = AudioFeatures{};
}

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
int gg_leds0_count(void) { return g_ctrl.getSoftwareLength(0); }

EMSCRIPTEN_KEEPALIVE
int gg_leds1_count(void) { return g_ctrl.getSoftwareLength(1); }

EMSCRIPTEN_KEEPALIVE
int gg_strip_capacity(int strip) { return g_ctrl.getStripCapacity(strip); }

EMSCRIPTEN_KEEPALIVE
int gg_strip_length(int strip) { return g_ctrl.getSoftwareLength(strip); }

EMSCRIPTEN_KEEPALIVE
int gg_set_software_length(int strip, int length) {
    return g_ctrl.setSoftwareLength(strip, length);
}

// Compatibility name for pages built against the first software-length API.
EMSCRIPTEN_KEEPALIVE
int gg_set_strip_length(int strip, int length) {
    return gg_set_software_length(strip, length);
}

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_name(void) { return g_sceneName.c_str(); }

EMSCRIPTEN_KEEPALIVE
int gg_scene_count(void) { return g_ctrl.getSceneCount(); }

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_name_by_index(int index) {
    return g_ctrl.getSceneNameByIndex(index);
}

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_mood_by_index(int index) {
    return g_ctrl.getSceneMoodByIndex(index);
}

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_role_by_index(int index) {
    return g_ctrl.getSceneRoleByIndex(index);
}

EMSCRIPTEN_KEEPALIVE
float gg_scene_intensity_by_index(int index) {
    return g_ctrl.getSceneIntensityByIndex(index);
}

EMSCRIPTEN_KEEPALIVE
void gg_lock_scene(int index) {
    g_ctrl.lockScene(index);
    g_sceneName = g_ctrl.getCurrentSceneName();
}

EMSCRIPTEN_KEEPALIVE
void gg_unlock_scene(void) {
    g_ctrl.unlockScene();
    g_sceneName = g_ctrl.getCurrentSceneName();
}

EMSCRIPTEN_KEEPALIVE
int gg_locked_scene(void) {
    return g_ctrl.getLockedSceneIndex();
}

EMSCRIPTEN_KEEPALIVE
int gg_current_scene_index(void) {
    return g_ctrl.getCurrentSceneIndex();
}

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
        // The render drive the animations actually read, beside the shares above.
        // Shown because the two differ by a factor of twenty on real audio and the
        // shares alone do not explain what a pixel is doing.
        case 13: return g_last.bassLevel;
        case 14: return g_last.midLevel;
        case 15: return g_last.trebleLevel;
        case 16: return g_last.buildup;
        case 17: return g_last.descent;
        case 18: return g_last.dropDetected ? 1.0f : 0.0f;
        case 19: return g_last.teaseDetected ? 1.0f : 0.0f;
        case 20: return g_last.anomaly;
        case 21: return g_last.gateGain;
        case 22: return g_last.spectralFlatness;
        // MusicState coordinates: value, confidence, trend
        case 23: return g_last.music.intensity.value;
        case 24: return g_last.music.intensity.confidence;
        case 25: return g_last.music.intensity.trend;
        case 26: return g_last.music.activity.value;
        case 27: return g_last.music.activity.confidence;
        case 28: return g_last.music.activity.trend;
        case 29: return g_last.music.brightness.value;
        case 30: return g_last.music.brightness.confidence;
        case 31: return g_last.music.brightness.trend;
        case 32: return g_last.music.weight.value;
        case 33: return g_last.music.weight.confidence;
        case 34: return g_last.music.weight.trend;
        case 35: return g_last.music.pulse.value;
        case 36: return g_last.music.pulse.confidence;
        case 37: return g_last.music.pulse.trend;
        case 38: return g_last.music.tempo.value;
        case 39: return g_last.music.tempo.confidence;
        case 40: return g_last.music.tempo.trend;
        case 41: return g_last.music.texture.value;
        case 42: return g_last.music.texture.confidence;
        case 43: return g_last.music.texture.trend;
        case 44: return g_last.music.presence.value;
        case 45: return g_last.music.presence.confidence;
        case 46: return g_last.music.presence.trend;
        // Sample clock telemetry
        case 47: return g_last.dtSeconds;
        case 48: return static_cast<float>(g_last.sampleTimeMs);
        case 49: return static_cast<float>(g_last.sampleFrame);
        // Autocorrelation rhythm and phase tracking
        case 50: return g_last.beatPhase;
        case 51: return g_last.beatConfidence;
        default: return 0.0f;
    }
}

EMSCRIPTEN_KEEPALIVE
double gg_sample_frame(void) { return static_cast<double>(g_last.sampleFrame); }

EMSCRIPTEN_KEEPALIVE
double gg_sample_time_ms(void) { return static_cast<double>(g_last.sampleTimeMs); }

EMSCRIPTEN_KEEPALIVE
float gg_dt_seconds(void) { return g_last.dtSeconds; }

EMSCRIPTEN_KEEPALIVE
float gg_beat_phase(void) { return g_last.beatPhase; }

EMSCRIPTEN_KEEPALIVE
float gg_beat_confidence(void) { return g_last.beatConfidence; }

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
const char* gg_layer_name(int strip, int index) {
    return g_ctrl.getLayerName(strip, index);
}

EMSCRIPTEN_KEEPALIVE
double gg_layer_elapsed_ms(int strip, int index) {
    return double(g_ctrl.getLayerElapsedMs(strip, index));
}

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
    static String name;
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
constexpr int kTuneAudioDynDecay  = 8;
constexpr int kTuneGainSmoothing  = 9;
constexpr int kTuneDynGrowth      = 10;
constexpr int kTuneCount           = 11;
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
        case kTuneAudioDynDecay:  return g_proc.getDynamicsDecayPerBlock();
        case kTuneGainSmoothing:  return g_proc.getGainSmoothing();
        case kTuneDynGrowth:      return g_proc.getDynamicsGrowthPerBlock();
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
        case kTuneAudioDynDecay:  g_proc.setDynamicsDecayPerBlock(value); break;
        case kTuneGainSmoothing:  g_proc.setGainSmoothing(value); break;
        case kTuneDynGrowth:      g_proc.setDynamicsGrowthPerBlock(value); break;
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
    const int n = g_ctrl.getSoftwareLength(strip);
    int lit = 0;
    for (int i = 0; i < n; ++i) {
        if (buf[i].r || buf[i].g || buf[i].b) ++lit;
    }
    return lit;
}

EMSCRIPTEN_KEEPALIVE
double gg_lit_sum(int strip) {
    const CRGB* buf = (strip == 0) ? ledStrip_0 : ledStrip_1;
    const int n = g_ctrl.getSoftwareLength(strip);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        sum += buf[i].r + buf[i].g + buf[i].b;
    }
    return sum;
}

EMSCRIPTEN_KEEPALIVE
int gg_lit_capacity(int strip) { return g_ctrl.getStripCapacity(strip); }

} // extern "C"
