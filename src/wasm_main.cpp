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
//  object. The figures are a healthy stand-in. The device gate lives in
//  MemoryGuard and is fed from loop(), which this entry point does not run.
// -----------------------------------------------------------------------------
uint32_t SimEsp::getFreeHeap() const { return SIM_HEAP_BYTES; }
uint32_t SimEsp::getMinFreeHeap() const { return SIM_HEAP_BYTES; }

SimEsp ESP;

// -----------------------------------------------------------------------------
//  The live pipeline
// -----------------------------------------------------------------------------
namespace {

AudioFeatures       g_audio;
AudioHistoryTracker g_history;
LEDStripController  g_ctrl(g_audio, g_history);
AudioProcessor      g_proc;
AudioFeatures       g_last;          // what the controller was last stepped with

// Samples the page writes into. NUM_SAMPLES is what the FFT is sized for, so a
// step is exactly one analysis window.
float g_samples[NUM_SAMPLES];

// getCurrentSceneName() returns by value, so the pointer would dangle if it were
// taken from the temporary. This static holds the copy that the returned c_str()
// points at.
String g_sceneName = "—";

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
// The mood strengths are reset with it, inside resetTracking, and so is the
// director's memory of the drop and the mood jump, which were measured against
// audio that has stopped.
EMSCRIPTEN_KEEPALIVE
void gg_reset_analysis(void) {
    g_proc.resetTracking();
    g_ctrl.sceneDirectorForTuning().reset();
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
int gg_layer_type_count(void) { return static_cast<int>(LayerType::COUNT); }

EMSCRIPTEN_KEEPALIVE
const char* gg_layer_type_name(int type) {
    if (type < 0 || type >= static_cast<int>(LayerType::COUNT)) return "";
    return layerTypeToString(static_cast<LayerType>(type));
}

// Clear every layer and show only the one of this type until called with -1.
EMSCRIPTEN_KEEPALIVE
int gg_trigger_layer(int type) { return g_ctrl.triggerLayer(type) ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE
int gg_locked_scene(void) {
    return g_ctrl.getLockedSceneIndex();
}

EMSCRIPTEN_KEEPALIVE
int gg_current_scene_index(void) {
    return g_ctrl.getCurrentSceneIndex();
}

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
        // Structural episodes, decided in the firmware. 52 to 81 is six fields per
        // signal in the order buildup, descent, drop, tease, anomaly: state (0 idle,
        // 1 arming, 2 active, 3 fading), episodeId, elapsedMs, lastDurationMs,
        // sinceEndMs, lastEndReason. The page reads them and derives nothing.
        case 82: return g_last.displacement;
        case 83: return g_last.arming;
        case 84: return g_last.dropConfidence;
        case 85: return g_last.dropConfirmed ? 1.0f : 0.0f;
        // Mood readings appended after 85 so no index moves: tilt, evenness, punch,
        // body, each value then confidence then trend.
        case 86: return g_last.music.tilt.value;
        case 87: return g_last.music.tilt.confidence;
        case 88: return g_last.music.tilt.trend;
        case 89: return g_last.music.evenness.value;
        case 90: return g_last.music.evenness.confidence;
        case 91: return g_last.music.evenness.trend;
        case 92: return g_last.music.punch.value;
        case 93: return g_last.music.punch.confidence;
        case 94: return g_last.music.punch.trend;
        case 95: return g_last.music.body.value;
        case 96: return g_last.music.body.confidence;
        case 97: return g_last.music.body.trend;
        default:
            if (index >= 52 && index < 52 + 6 * SIG_COUNT) {
                const EpisodeStatus& e = g_last.episode[(index - 52) / 6];
                switch ((index - 52) % 6) {
                    case 0: return float(e.state);
                    case 1: return float(e.episodeId);
                    case 2: return float(e.elapsedMs);
                    case 3: return float(e.lastDurationMs);
                    case 4: return float(e.sinceEndMs);
                    default: return float(e.lastEndReason);
                }
            }
            return 0.0f;
    }
}

// The episode event ring. Records are numbered from 1 and the numbers never go back,
// not even across gg_reset_analysis, so the page keeps the newest seq it has drained
// and asks for the ones after it. gg_event_load fills a one-record buffer and says
// whether the record is still in the ring, and gg_event_field reads it.
EMSCRIPTEN_KEEPALIVE
double gg_event_newest_seq(void) { return double(g_proc.structuralEpisodes().newestSeq()); }

EMSCRIPTEN_KEEPALIVE
double gg_event_oldest_seq(void) { return double(g_proc.structuralEpisodes().oldestSeq()); }

namespace { EpisodeEvent g_event; }

EMSCRIPTEN_KEEPALIVE
int gg_event_load(double seq) {
    return g_proc.structuralEpisodes().eventBySeq(uint32_t(seq), g_event) ? 1 : 0;
}

// 0 signal, 1 kind (0 started, 1 ended, 2 triggered, 3 confirmed), 2 end reason,
// 3 confirmed, 4 atMs (sample time), 5 durationMs, 6 value (preparation on a drop
// onset).
EMSCRIPTEN_KEEPALIVE
double gg_event_field(int field) {
    switch (field) {
        case 0: return g_event.signal;
        case 1: return g_event.kind;
        case 2: return g_event.reason;
        case 3: return g_event.confirmed;
        case 4: return double(g_event.atMs);
        case 5: return double(g_event.durationMs);
        case 6: return double(g_event.value);
        default: return 0.0;
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
//  What makes a scene change is the pair (elapsed time, the duration thresholds
//  it is being compared against), and the score list and reason line below say
//  which scene ranks where. Exposing only the features leaves the page unable to
//  show why anything changed.
// -----------------------------------------------------------------------------
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
constexpr int kTuneAudioDynDecay  = 3;
constexpr int kTuneGainSmoothing  = 4;
constexpr int kTuneDynGrowth      = 5;
constexpr int kTuneSectionWindow  = 6;
constexpr int kTuneTeaseWindow    = 7;
constexpr int kTuneDropMax        = 8;
constexpr int kTuneDropHold       = 9;
// The selector: the bucket around the best score, the weight of an open buildup or
// descent, and the drop base's hold.
constexpr int kTuneBucketMargin   = 10;
constexpr int kTuneBucketMin      = 11;
constexpr int kTuneBucketMax      = 12;
constexpr int kTuneEpisodeWeight  = 13;
constexpr int kTuneDropHoldMin    = 14;
constexpr int kTuneDropHoldCap    = 15;
constexpr int kTuneDropSpacing    = 16;
constexpr int kTuneToneWeight     = 17;
constexpr int kTuneMoodCredit     = 18;
constexpr int kTuneCount          = 19;
}  // namespace

EMSCRIPTEN_KEEPALIVE
float gg_tuning(int which) {
    const SceneDirector& dir = g_ctrl.sceneDirectorView();
    switch (which) {
        case kTuneSceneMinBase:   return g_ctrl.sceneStateForTuning().getMinBaseMs();
        case kTuneSceneIdealBase: return g_ctrl.sceneStateForTuning().getIdealBaseMs();
        case kTuneSceneIdealSpan: return g_ctrl.sceneStateForTuning().getIdealSpanMs();
        case kTuneAudioDynDecay:  return g_proc.getDynamicsDecayPerBlock();
        case kTuneGainSmoothing:  return g_proc.getGainSmoothing();
        case kTuneDynGrowth:      return g_proc.getDynamicsGrowthPerBlock();
        case kTuneSectionWindow:  return float(g_proc.structuralEpisodes().sectionWindowMs);
        case kTuneTeaseWindow:    return float(g_proc.structuralEpisodes().teaseWindowMs);
        case kTuneDropMax:        return float(g_proc.structuralEpisodes().dropMaxMs);
        case kTuneDropHold:       return g_proc.structuralEpisodes().dropHoldFraction;
        case kTuneBucketMargin:   return dir.selectionParams().bucketMargin;
        case kTuneBucketMin:      return float(dir.selectionParams().bucketMin);
        case kTuneBucketMax:      return float(dir.selectionParams().bucketMax);
        case kTuneEpisodeWeight:  return dir.selectionParams().episodeWeight;
        case kTuneDropHoldMin:    return float(dir.dropParamsRef().holdMinMs);
        case kTuneDropHoldCap:    return float(dir.dropParamsRef().holdCapMs);
        case kTuneDropSpacing:    return float(dir.dropParamsRef().spacingMs);
        case kTuneToneWeight:     return dir.selectionParams().toneWeight;
        case kTuneMoodCredit:     return g_proc.moods().credit();
        default:                  return 0.0f;
    }
}

EMSCRIPTEN_KEEPALIVE
void gg_set_tuning(int which, float value) {
    SceneDirector& dir = g_ctrl.sceneDirectorForTuning();
    const unsigned long ms = value < 0.0f ? 0 : (unsigned long)value;
    switch (which) {
        case kTuneSceneMinBase:   g_ctrl.sceneStateForTuning().setMinBaseMs(value);   break;
        case kTuneSceneIdealBase: g_ctrl.sceneStateForTuning().setIdealBaseMs(value); break;
        case kTuneSceneIdealSpan: g_ctrl.sceneStateForTuning().setIdealSpanMs(value); break;
        case kTuneAudioDynDecay:  g_proc.setDynamicsDecayPerBlock(value); break;
        case kTuneGainSmoothing:  g_proc.setGainSmoothing(value); break;
        case kTuneDynGrowth:      g_proc.setDynamicsGrowthPerBlock(value); break;
        // Clamped here so a slider cannot make a window negative or a fraction
        // above one. What the number means is decided in StructuralEpisodes.
        case kTuneSectionWindow:  g_proc.structuralEpisodesForTuning().sectionWindowMs = ms; break;
        case kTuneTeaseWindow:    g_proc.structuralEpisodesForTuning().teaseWindowMs   = ms; break;
        case kTuneDropMax:        g_proc.structuralEpisodesForTuning().dropMaxMs       = ms; break;
        case kTuneDropHold:       g_proc.structuralEpisodesForTuning().dropHoldFraction = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); break;
        case kTuneBucketMargin:   dir.selectionForTuning().bucketMargin = value < 0.0f ? 0.0f : value; break;
        case kTuneBucketMin:      dir.selectionForTuning().bucketMin = value < 1.0f ? 1 : int(value + 0.5f); break;
        case kTuneBucketMax:      dir.selectionForTuning().bucketMax = value < 1.0f ? 1 : int(value + 0.5f); break;
        case kTuneEpisodeWeight:  dir.selectionForTuning().episodeWeight = value < 0.0f ? 0.0f : value; break;
        case kTuneDropHoldMin:    dir.dropForTuning().holdMinMs = ms; break;
        case kTuneDropHoldCap:    dir.dropForTuning().holdCapMs = ms; break;
        case kTuneDropSpacing:    dir.dropForTuning().spacingMs = ms; break;
        case kTuneToneWeight:     dir.selectionForTuning().toneWeight = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value); break;
        case kTuneMoodCredit:     g_proc.moodsForTuning().setCredit(value); break;
        default: break;
    }
}

EMSCRIPTEN_KEEPALIVE

// The selector, read back. Nothing here is computed on the page: the score of
// every scene, the bucket, why the running scene is up and the drop base's hold
// all come from the director.

// field 0 mood term, 1 episode bonus, 2 recency penalty, 3 score, 4 in the bucket
EMSCRIPTEN_KEEPALIVE
float gg_scene_score(int index, int field) {
    const std::vector<SceneScore>& all = g_ctrl.sceneDirectorView().sceneScores();
    if (index < 0 || index >= int(all.size())) return 0.0f;
    const SceneScore& s = all[size_t(index)];
    switch (field) {
        case 0: return s.mood;
        case 1: return s.episode;
        case 2: return s.recency;
        case 3: return s.score;
        case 4: return s.inBucket ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

// The bucket, best first: how many, and the scene index at a rank.
EMSCRIPTEN_KEEPALIVE
int gg_bucket_count(void) { return int(g_ctrl.sceneDirectorView().bucketIndices().size()); }

EMSCRIPTEN_KEEPALIVE
int gg_bucket_index(int rank) {
    const std::vector<int>& b = g_ctrl.sceneDirectorView().bucketIndices();
    return (rank >= 0 && rank < int(b.size())) ? b[size_t(rank)] : -1;
}

EMSCRIPTEN_KEEPALIVE
const char* gg_scene_reason(void) { return g_ctrl.sceneDirectorView().reasonText(); }

// 0 holding, 1 a drastic shift seen, 2 ms left of the minimum, 3 ms left to the cap
EMSCRIPTEN_KEEPALIVE
double gg_drop_hold(int field) {
    const SceneDirector::DropHoldStatus s = g_ctrl.sceneDirectorView().dropHold();
    switch (field) {
        case 0: return s.holding ? 1.0 : 0.0;
        case 1: return s.shiftSeen ? 1.0 : 0.0;
        case 2: return double(s.minLeftMs);
        case 3: return double(s.capLeftMs);
        default: return 0.0;
    }
}

// The draw's seed, so a recording replays the same scenes.
EMSCRIPTEN_KEEPALIVE
void gg_scene_seed(unsigned int value) { g_ctrl.sceneDirectorForTuning().seed(value); }

// Fit lists for the scene catalog. A key below the mood count is a mood, the rest
// name an episode or the drop marker.
EMSCRIPTEN_KEEPALIVE
int gg_scene_fit_count(int index) { return g_ctrl.getSceneFitCount(index); }

EMSCRIPTEN_KEEPALIVE
int gg_scene_fit_key(int index, int entry) { return g_ctrl.getSceneFitKey(index, entry); }

EMSCRIPTEN_KEEPALIVE
float gg_scene_fit_value(int index, int entry) { return g_ctrl.getSceneFitValue(index, entry); }

EMSCRIPTEN_KEEPALIVE
const char* gg_fit_key_name(int key) {
    if (key >= 0 && key < MN_COUNT) return g_proc.moods().name(key);
    if (key == FK_BUILDUP) return "buildup";
    if (key == FK_DESCENT) return "descent";
    if (key == FK_DROP) return "drop";
    return "";
}

// Why a layer on a strip is up: scene, episode, edge, mood, beat or shift.
EMSCRIPTEN_KEEPALIVE
const char* gg_layer_why(int strip, int index) { return g_ctrl.getLayerWhy(strip, index); }
int gg_tuning_count(void) { return kTuneCount; }

// Moods. The page builds its strength panel and its tuning rows from these
// calls and holds no mood name or breakpoint of its own.
EMSCRIPTEN_KEEPALIVE
int gg_mood_count(void) { return MN_COUNT; }

EMSCRIPTEN_KEEPALIVE
const char* gg_mood_name_by_index(int i) { return g_proc.moods().name(i); }

EMSCRIPTEN_KEEPALIVE
float gg_mood_strength(int i) { return g_last.music.mood[(i >= 0 && i < MN_COUNT) ? i : 0]; }

// 1 for a tone mood (warm, heavy, bright, full, sparse), 0 for a character mood.
EMSCRIPTEN_KEEPALIVE
int gg_mood_is_tone(int i) { return g_proc.moods().isTone(i) ? 1 : 0; }

// 0 for a target the selector cannot read yet, which the page greys.
EMSCRIPTEN_KEEPALIVE
int gg_mood_in_selector(int i) { return g_proc.moods().inSelector(i) ? 1 : 0; }

EMSCRIPTEN_KEEPALIVE
int gg_mood_param_count(void) { return g_proc.moods().paramCount(); }

EMSCRIPTEN_KEEPALIVE
const char* gg_mood_param_name(int i) { return g_proc.moodsForTuning().paramName(i); }

EMSCRIPTEN_KEEPALIVE
float gg_mood_param(int i) { return g_proc.moods().param(i); }

EMSCRIPTEN_KEEPALIVE
void gg_set_mood_param(int i, float v) { g_proc.moodsForTuning().setParam(i, v); }

// Back to the table in Moods.h, for the page's reset.
EMSCRIPTEN_KEEPALIVE
void gg_mood_params_reset(void) { g_proc.moodsForTuning().loadDefaults(); }

// The room calibration: a centre and a spread for each room-coloured reading,
// measured from the music playing and read as 0.5 = what this device usually hears.
EMSCRIPTEN_KEEPALIVE
int gg_room_param_count(void) { return g_proc.moods().roomParamCount(); }

EMSCRIPTEN_KEEPALIVE
const char* gg_room_param_name(int i) { return g_proc.moodsForTuning().roomParamName(i); }

EMSCRIPTEN_KEEPALIVE
float gg_room_param(int i) { return g_proc.moods().roomParam(i); }

EMSCRIPTEN_KEEPALIVE
void gg_set_room_param(int i, float v) { g_proc.moodsForTuning().setRoomParam(i, v); }

EMSCRIPTEN_KEEPALIVE
void gg_room_reset(void) { g_proc.moodsForTuning().loadRoomDefaults(); }

// 1 starts measuring, 0 ends it and returns the frames used (0 if it was not on).
EMSCRIPTEN_KEEPALIVE
int gg_room_measure(int on) {
    MoodModel& model = g_proc.moodsForTuning();
    if (on) { model.measureBegin(); return 0; }
    return model.measuring() ? model.measureEnd() : 0;
}

EMSCRIPTEN_KEEPALIVE
int gg_room_measured_frames(void) { return g_proc.moods().measuredFrames(); }

// Where the calibration came from: 0 defaults and waiting for music, 1 the
// firmware is measuring the first seconds, 2 the firmware measured it, 3 the
// owner set it. And how many of the auto seconds have passed.
EMSCRIPTEN_KEEPALIVE
int gg_room_status(void) { return int(g_proc.moods().roomMode()); }

EMSCRIPTEN_KEEPALIVE
float gg_room_auto_seconds(void) { return g_proc.moods().roomAutoSeconds(); }

// The current values as the rows of kMoodTable, for `Copy mood table`.
EMSCRIPTEN_KEEPALIVE
const char* gg_mood_table_text(void) {
    static char text[4096];
    g_proc.moods().formatTable(text, sizeof(text));
    return text;
}

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
