// Host harness for the LED scene and layer lifecycle.
//
// Built only by the native environment in platformio.ini. It compiles the real
// LEDStripController, LayerManager, SceneRegistry, SceneState, SceneDirector and
// MoodHistory, drives them with a scripted audio timeline over a clock the harness
// owns, prints the strips as an ANSI colour bar so the effect is inspectable, and
// asserts the lifecycle invariants that the audit found broken.
//
// Nothing here is linked into the device firmware.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include <Arduino.h>
#include <FastLED.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "audio/AudioFeatures.h"
#include "audio/AudioProcessor.h"
#include "audio/AudioSnapshot.h"
#include "audio/AudioHistoryTracker.h"
#include "scenes/MoodHistory.h"
#include "scenes/SceneRegistry.h"
#include "scenes/SceneState.h"
#include "scenes/LayerTypes.h"
#include "scenes/LayerManager.h"
#include "animations/VisualLayer.h"
#include "animations/VisualLayers.h"
#include "animations/AlienPulse.h"
#include "animations/neonFlow.h"
#include "animations/PsychedelicInkSquirtAnimation.h"
#include "animations/AnimationCatalog.h"
#include "core/LEDStripController.h"

// -----------------------------------------------------------------------------
//  Globals the firmware expects to find at link time
// -----------------------------------------------------------------------------
CRGB ledStrip_0[LED_0_NUM];
CRGB ledStrip_1[LED_1_NUM];

// -----------------------------------------------------------------------------
//  Allocation accounting. The headline defect was a layer object allocated every
//  frame and never freed, so counting allocations per frame is the direct test.
//  Sizes are recorded as well, because they name the source without a profiler:
//  a deque block is one fixed size, a leaked object another.
// -----------------------------------------------------------------------------
namespace {
size_t g_allocCount = 0;
size_t g_freeCount  = 0;

// Whole-run split of the allocation total between the two things a frame does:
// feeding the audio history, and everything the controller then does with it.
size_t g_historyAllocs    = 0;
size_t g_controllerAllocs = 0;

constexpr int kSizeSlots = 64;
struct SizeSlot { size_t bytes; size_t count; };
SizeSlot g_sizes[kSizeSlots] = {};
int      g_sizeUsed     = 0;
size_t   g_sizeOverflow = 0;

// Linear scan over a fixed table. Recording an allocation must not itself
// allocate, or the instrument becomes part of what it measures. Sizes past the
// table are counted, not silently dropped, so a full table cannot look complete.
void noteSize(size_t n) {
    for (int i = 0; i < g_sizeUsed; ++i) {
        if (g_sizes[i].bytes == n) { ++g_sizes[i].count; return; }
    }
    if (g_sizeUsed < kSizeSlots) {
        g_sizes[g_sizeUsed].bytes = n;
        g_sizes[g_sizeUsed].count = 1;
        ++g_sizeUsed;
    } else {
        ++g_sizeOverflow;
    }
}
} // namespace

void* operator new(size_t n) {
    ++g_allocCount;
    noteSize(n);
    void* p = std::malloc(n ? n : 1);
    if (!p) throw std::bad_alloc();
    return p;
}
void* operator new[](size_t n) { return ::operator new(n); }

void operator delete(void* p) noexcept { if (p) { ++g_freeCount; std::free(p); } }
void operator delete[](void* p) noexcept { ::operator delete(p); }
void operator delete(void* p, size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, size_t) noexcept { ::operator delete(p); }

// -----------------------------------------------------------------------------
//  Arduino core definitions (declared in sim/stubs/Arduino.h)
// -----------------------------------------------------------------------------
namespace {
unsigned long g_now = 0;
}

unsigned long& simNow() { return g_now; }
void simAdvance(unsigned long ms) { g_now += ms; }

extern "C" {
uint32_t millis(void) { return static_cast<uint32_t>(g_now); }
uint32_t micros(void) { return static_cast<uint32_t>(g_now * 1000UL); }
void     delay(int) {}
void     yield(void) {}
void     pinMode(uint8_t, uint8_t) {}
}

long random(long howbig) { return howbig > 0 ? std::rand() % howbig : 0; }
long random(long howsmall, long howbig) {
    return howbig > howsmall ? howsmall + std::rand() % (howbig - howsmall) : howsmall;
}
void randomSeed(unsigned long seed) { std::srand(static_cast<unsigned>(seed)); }

SimSerial Serial;
SimEsp    ESP;

uint32_t SimEsp::getFreeHeap() const { return 200u * 1024u; }
uint32_t SimEsp::getMinFreeHeap() const { return 200u * 1024u; }

// -----------------------------------------------------------------------------
//  Check bookkeeping
// -----------------------------------------------------------------------------
namespace {

struct Check {
    std::string name;
    bool        passed;
    std::string detail;
};

std::vector<Check> g_checks;

void record(const std::string& name, bool passed, const std::string& detail = "") {
    g_checks.push_back({name, passed, detail});
}

bool colour = true;

// -----------------------------------------------------------------------------
//  Scripted audio. Four moods in rotation so the director has something to pick
//  between and the scene clock actually fires.
// -----------------------------------------------------------------------------
AudioFeatures scriptedAudio(int frame) {
    const int phase = (frame / 150) % 4;   // 150 frames at 33 ms is ~5 s

    float energy, dynamics, bpm;
    switch (phase) {
        case 0:  energy = 0.05f; dynamics = 0.05f; bpm = 90.0f;  break;  // silence
        case 1:  energy = 0.22f; dynamics = 0.10f; bpm = 70.0f;  break;  // calm
        case 2:  energy = 0.92f; dynamics = 0.62f; bpm = 128.0f; break;  // intense
        default: energy = 0.55f; dynamics = 0.30f; bpm = 72.0f;  break;  // floaty
    }

    AudioFeatures f;
    f.volume         = energy;
    f.loudness       = energy * 100.0f;
    f.peak           = energy;
    f.average        = energy * 0.7f;
    f.bass           = energy;
    f.mid            = energy * 0.6f;
    f.treble         = energy * 0.3f;
    f.energy         = energy;
    f.level          = energy;
    f.dynamics       = dynamics;
    f.bpm            = bpm;
    f.spectrumCentroid = 60.0f + energy * 120.0f;
    f.dominantBand   = int(energy * 8.0f);
    f.beatDetected   = (frame % 15 == 0);
    f.bassHits       = (frame % 15 == 0) ? 1 : 0;
    f.noiseFloor     = 0.02f;
    f.signalPresence = energy > 0.10f;
    return f;
}

// -----------------------------------------------------------------------------
//  Rendering the strips to the terminal
// -----------------------------------------------------------------------------
CRGB averageBlock(const CRGB* px, int count) {
    if (count <= 0) return CRGB::Black;
    uint32_t r = 0, g = 0, b = 0;
    for (int i = 0; i < count; ++i) { r += px[i].r; g += px[i].g; b += px[i].b; }
    return CRGB(uint8_t(r / count), uint8_t(g / count), uint8_t(b / count));
}

const char* asciiRamp(uint8_t level) {
    static const char* ramp = " .:-=+*#%@";
    return &ramp[(level * 9) / 255];
}

void drawStrip(const char* label, const CRGB* px, int n, int cells) {
    if (n <= 0 || cells <= 0) return;
    std::printf("%-7s |", label);
    for (int c = 0; c < cells; ++c) {
        const int lo = (c * n) / cells;
        const int hi = ((c + 1) * n) / cells;
        const CRGB avg = averageBlock(px + lo, hi - lo > 0 ? hi - lo : 1);
        if (colour) {
            std::printf("\033[48;2;%u;%u;%um  \033[0m",
                        unsigned(avg.r), unsigned(avg.g), unsigned(avg.b));
        } else {
            const uint8_t level = uint8_t((avg.r * 77 + avg.g * 150 + avg.b * 29) >> 8);
            std::printf("%c%c", *asciiRamp(level), *asciiRamp(level));
        }
    }
    std::printf("|\n");
}

} // namespace

// -----------------------------------------------------------------------------
//  Local test layers. Neither exists in the firmware: they isolate one property of
//  the compositor each, so a failure names the cause.
// -----------------------------------------------------------------------------
namespace {

// Renders nothing at all. Every pixel it leaves black used to erase the base
// animation, because the compositor blended this buffer over the base with an
// alpha of 255, where 255 means replace rather than blend.
class DrawNothingLayer : public VisualLayer {
public:
    explicit DrawNothingLayer(float op) { name = "DrawNothing"; opacity = op; }
    void update(const AudioFeatures&, const AudioHistory&) override {}
    void render(CRGB*, int) override {}
};

// Fills every pixel with one colour, so the additive contribution is predictable.
class SolidFillLayer : public VisualLayer {
    CRGB fill;
public:
    SolidFillLayer(CRGB c, float op) : fill(c) { name = "SolidFill"; opacity = op; }
    void update(const AudioFeatures&, const AudioHistory&) override {}
    void render(CRGB* leds, int count) override { fill_solid(leds, count, fill); }
};

// -----------------------------------------------------------------------------
//  Check 1 - every catalog slot is index-aligned and constructible
// -----------------------------------------------------------------------------
void checkCatalog() {
    bool aligned = true;
    bool nonNull = true;
    std::string bad;

    for (size_t i = 0; i < static_cast<size_t>(AnimationType::COUNT); ++i) {
        const AnimationMeta& meta = animationCatalog[i];
        if (static_cast<size_t>(meta.type) != i) {
            aligned = false;
            bad += std::string(meta.name) + " at index " + std::to_string(i) + "; ";
        }
        if (meta.type == AnimationType::NONE) continue;
        if (!meta.create) {
            nonNull = false;
            bad += std::string(meta.name) + " has no factory; ";
            continue;
        }
        Animation* a = meta.create();
        if (a == nullptr) {
            nonNull = false;
            bad += std::string(meta.name) + " built null; ";
        }
        delete a;
    }

    record("catalog index matches AnimationType", aligned, bad);
    record("every catalog entry builds an animation", nonNull, bad);
}

// -----------------------------------------------------------------------------
//  Check 2 - the compositor adds light, and never subtracts it
// -----------------------------------------------------------------------------
void checkCompositor() {
    const CRGB base(80, 0, 120);

    {
        CRGB buf[16];
        fill_solid(buf, 16, base);
        LayerManager lm;
        lm.setLEDs(buf, 16);
        lm.addLayer(new DrawNothingLayer(1.0f), LayerType::OVERLAY, 0);
        lm.renderLayers(0);

        const bool intact = buf[0] == base && buf[15] == base;
        record("a layer that draws nothing leaves the base intact", intact,
               intact ? "" : "base was erased to (" + std::to_string(buf[0].r) + "," +
                             std::to_string(buf[0].g) + "," + std::to_string(buf[0].b) + ")");
    }

    {
        CRGB buf[16];
        fill_solid(buf, 16, base);
        LayerManager lm;
        lm.setLEDs(buf, 16);
        lm.addLayer(new SolidFillLayer(CRGB(40, 0, 0), 0.5f), LayerType::OVERLAY, 0);
        lm.renderLayers(0);

        const bool brighter = buf[0].r > base.r && buf[0].b == base.b && buf[0].g == base.g;
        record("a half-opacity layer adds to the base", brighter,
               "result (" + std::to_string(buf[0].r) + "," + std::to_string(buf[0].g) +
               "," + std::to_string(buf[0].b) + ")");
    }

    {
        CRGB buf[16];
        fill_solid(buf, 16, base);
        LayerManager lm;
        lm.setLEDs(buf, 16);
        lm.addLayer(new SolidFillLayer(CRGB(40, 0, 0), 0.0f), LayerType::OVERLAY, 0);
        lm.renderLayers(0);

        const bool untouched = buf[0] == base;
        record("a zero-opacity layer contributes nothing", untouched,
               "result (" + std::to_string(buf[0].r) + "," + std::to_string(buf[0].g) +
               "," + std::to_string(buf[0].b) + ")");
    }
}

// -----------------------------------------------------------------------------
//  Check 3 - the layer cap, and that scene layers are not stacked twice
// -----------------------------------------------------------------------------
void checkLayerCap() {
    {
        CRGB buf[8];
        fill_solid(buf, 8, CRGB::Black);
        LayerManager lm;
        lm.setLEDs(buf, 8);
        for (int i = 0; i < 10; ++i) {
            lm.addLayer(new DrawNothingLayer(1.0f), LayerType::OVERLAY, 0);
        }
        record("layer count is capped at insertion", lm.activeCount() == 4,
               "count " + std::to_string(lm.activeCount()) + ", expected 4");
    }

    {
        CRGB buf[8];
        fill_solid(buf, 8, CRGB::Black);
        LayerManager lm;
        lm.setLEDs(buf, 8);

        SceneDefinition sd;
        sd.baseAnimation = AnimationType::ALIEN_BREATH;
        sd.layerTypes = { LayerType::BACKGROUND, LayerType::REACTIVE };

        lm.applySceneLayers(sd);
        lm.applySceneLayers(sd);
        lm.applySceneLayers(sd);

        record("applying a scene's layers is idempotent", lm.activeCount() == 2,
               "count " + std::to_string(lm.activeCount()) + ", expected 2");
    }
}

} // namespace

// -----------------------------------------------------------------------------
//  Device-scale audio
//
//  The four-mood timeline in scriptedAudio() feeds energy in 0.05..0.92, but on
//  device energy is the raw sum of 255 FFT magnitudes (AudioProcessor.cpp:118),
//  which is three orders of magnitude larger. Animations that divide energy down
//  therefore sit pinned at one end of their range here and never exercise the
//  other. This builds the same features from a synthetic spectrum, using the
//  processor's own band limits and normalisations, so the numbers are on the
//  scale the firmware actually sees.
// -----------------------------------------------------------------------------
namespace {

// -----------------------------------------------------------------------------
//  Runaway protection
//
//  An animation whose update() never returns hangs the harness forever, and
//  nothing in-process can interrupt a spinning function call. Windows has no
//  SIGALRM, and the frame loops cannot inject a budget without changing the
//  firmware's signatures. A watchdog thread can: it only reads two atomics, so
//  it cannot allocate and cannot perturb the counts it is guarding.
//
//  Each frame loop bumps g_tick; each phase names itself in g_phase. If the tick
//  does not move for a second, the harness names the phase that stopped it and
//  aborts, which makes the non-zero exit code visible to `pio run -t exec`.
// -----------------------------------------------------------------------------
std::atomic<unsigned long> g_tick{0};
std::atomic<const char*>   g_phase{"startup"};
std::atomic<bool>          g_watchdogRun{true};

void watchdogMain() {
    unsigned long last    = 0;
    int           stalled = 0;

    while (g_watchdogRun.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));

        const unsigned long now = g_tick.load(std::memory_order_relaxed);
        if (now == last) {
            if (++stalled >= 4) {
                // Only write() and fflush() are safe here. No std::string, no printf.
                std::fprintf(stderr, "\nRUNAWAY: %s stalled at tick %lu\n",
                             g_phase.load(std::memory_order_relaxed), now);
                std::fflush(stderr);
                std::abort();
            }
        } else {
            stalled = 0;
            last    = now;
        }
    }
}

void setPhase(const char* name) {
    g_phase.store(name, std::memory_order_relaxed);
    g_tick.fetch_add(1, std::memory_order_relaxed);
}

void fillDeviceAudio(int frame, AudioFeatures& f, int16_t* wave, float* spectrum) {
    const int phase = (frame / 200) % 5;

    // Target band levels, in the 0..1 range AudioProcessor's own normalisation
    // produces. The spectrum below is built to hit these exactly, so a layer
    // gated on `bass > 0.8` is reachable here for the same reason it is on
    // device. An earlier version shaped the spectrum with one decaying curve and
    // could not exceed bass 0.35, which made BassShockwaveLayer and
    // TrebleSparkleLayer look dead when they were only unfed.
    float level, bassT, midT, trebleT;
    switch (phase) {
        case 0:  level = 0.00f; bassT = 0.00f; midT = 0.00f; trebleT = 0.00f; break;  // silence
        case 1:  level = 0.10f; bassT = 0.15f; midT = 0.10f; trebleT = 0.05f; break;  // quiet
        case 2:  level = 0.90f; bassT = 0.95f; midT = 0.55f; trebleT = 0.25f; break;  // bass-heavy
        case 3:  level = 0.45f; bassT = 0.30f; midT = 0.45f; trebleT = 0.30f; break;  // mid-forward
        default: level = 1.00f; bassT = 0.50f; midT = 0.70f; trebleT = 0.85f; break;  // bright
    }

    const int half      = NUM_SAMPLES / 2;
    const int bassLimit = 200  * NUM_SAMPLES / SAMPLE_RATE;
    const int midLimit  = 2000 * NUM_SAMPLES / SAMPLE_RATE;

    // One decaying weight profile per band, summed so each band can then be
    // scaled to its target independently.
    float wBass = 0, wMid = 0, wTreble = 0;
    for (int i = 1; i < half; ++i) {
        if      (i <= bassLimit) wBass   += expf(-float(i) / 1.5f);
        else if (i <= midLimit)  wMid    += expf(-float(i - bassLimit) / 12.0f);
        else                     wTreble += expf(-float(i - midLimit) / 60.0f);
    }

    // The divisor AudioProcessor applies to each band sum, inverted, so the
    // magnitudes it sums back add up to the target level.
    const float bassSum   = bassT   * 100.0f * float(bassLimit);
    const float midSum    = midT    *  80.0f * float(midLimit - bassLimit);
    const float trebleSum = trebleT *  50.0f * float(half - midLimit);

    const float kBass   = wBass   > 0 ? bassSum   / wBass   : 0.0f;
    const float kMid    = wMid    > 0 ? midSum    / wMid    : 0.0f;
    const float kTreble = wTreble > 0 ? trebleSum / wTreble : 0.0f;

    float eTotal = 0, cSum = 0, bSum = 0, mSum = 0, tSum = 0;
    int   dom = 1;
    float domMag = -1.0f;

    spectrum[0] = 0.0f;
    for (int i = 1; i < half; ++i) {
        float mag;
        if (i <= bassLimit) {
            mag = kBass * expf(-float(i) / 1.5f);
            bSum += mag;
        } else if (i <= midLimit) {
            mag = kMid * expf(-float(i - bassLimit) / 12.0f);
            mSum += mag;
        } else {
            mag = kTreble * expf(-float(i - midLimit) / 60.0f);
            tSum += mag;
        }

        spectrum[i] = mag;
        eTotal += mag;
        cSum   += mag * i;
        if (mag > domMag) { domMag = mag; dom = i; }
    }

    for (int i = 0; i < NUM_SAMPLES; ++i) {
        const float t = float(i) / float(NUM_SAMPLES);
        wave[i] = int16_t(32767.0f * level * sinf(t * 6.2831853f * 2.0f));
    }

    f = AudioFeatures();
    memcpy(f.spectrum, spectrum, sizeof(float) * (NUM_SAMPLES / 2));
    f.volume           = level;
    f.loudness         = level * 100.0f;
    f.peak             = level;
    f.average          = level * 0.6f;
    // Shares of the total magnitude, matching AudioProcessor. The per-bin
    // averages divided by 100, 80 and 50 that used to be here were the old
    // firmware formula, and the recorded frames would no longer be what the
    // device draws now that the firmware divides by the sum instead.
    f.bass             = eTotal > 1e-6f ? bSum / eTotal : 0.0f;
    f.mid              = eTotal > 1e-6f ? mSum / eTotal : 0.0f;
    f.treble           = eTotal > 1e-6f ? tSum / eTotal : 0.0f;
    f.energy           = eTotal;
    f.level            = level;
    f.spectrumCentroid = eTotal > 0 ? (cSum / eTotal) : 0.0f;
    f.dominantBand     = dom;
    f.frequency        = dom * SAMPLE_RATE / NUM_SAMPLES;
    f.dynamics         = (f.peak - f.average) / (f.peak + 1e-6f);
    f.beatDetected     = (frame % 12 == 0) && level > 0.2f;
    f.bassHits         = f.beatDetected ? 1 : 0;
    f.bpm              = 60.0f + 90.0f * level;
    f.noiseFloor       = 0.02f;
    f.signalPresence   = level > 0.05f;
    f.waveform         = wave;
    f.waveformSize     = NUM_SAMPLES;
}

// -----------------------------------------------------------------------------
//  Check 5 - how much history each tracker actually holds
//
//  Sizing a replacement buffer needs the real peak element count, not the
//  declared cap. This is measured because the whole-run live-block count does
//  not reconcile with those caps: 25 live blocks at frame 2400 is exactly
//  MoodHistory's node count, which would leave the audio deque holding nothing,
//  and a deque fed one snapshot per frame cannot hold nothing.
// -----------------------------------------------------------------------------
void checkHistorySizing() {
    const int kFrames = 3000;
    const int kDtMs   = 33;

    AudioFeatures       audio;
    MoodHistory         mood;
    AudioHistoryTracker history;

    size_t peakMood  = 0;
    size_t peakAudio = 0;

    for (int frame = 0; frame < kFrames; ++frame) {
        g_tick.fetch_add(1, std::memory_order_relaxed);

        audio = scriptedAudio(frame);
        simAdvance(kDtMs);
        history.addSnapshot(audio);
        mood.update(audio);

        if (history.getHistory().size() > peakAudio) peakAudio = history.getHistory().size();
        if (mood.size()                 > peakMood)  peakMood  = mood.size();
    }

    std::printf("\nhistory sizing\n");
    std::printf("  AudioSnapshot %zu bytes, MoodSnapshot %zu bytes\n",
                sizeof(AudioSnapshot), sizeof(MoodSnapshot));
    std::printf("  AudioHistoryTracker peak %zu elements, %zu bytes of payload, declared cap 1500\n",
                peakAudio, peakAudio * sizeof(AudioSnapshot));
    std::printf("  MoodHistory peak %zu elements, %zu bytes of payload, declared cap 150\n",
                peakMood, peakMood * sizeof(MoodSnapshot));

    record("both history buffers fill to their declared cap",
           peakAudio == 1500 && peakMood == 150,
           "audio peak " + std::to_string(peakAudio) + " of 1500, mood peak " +
           std::to_string(peakMood) + " of 150");
}

// -----------------------------------------------------------------------------
//  Check 6 - drive every catalog animation
//
//  checkCatalog() above constructs each class and throws it away, so nothing has
//  ever called update() on any of them. This runs each one for a long pass at
//  two strip lengths and reports what it does: whether it allocates in steady
//  state, how much it allocates in total, and whether it ever lights a pixel.
//  Black output is a signal to look at, not an automatic failure, since some
//  animations are legitimately dark under a given audio phase.
//
//  A hang inside update() cannot be caught from in-process, so the animation's
//  name is printed before it runs. If the harness stops, the last line names
//  the animation that stopped it.
// -----------------------------------------------------------------------------
void checkAnimationSweep(bool verbose) {
    const int kFrames = 1200;
    const int kDtMs   = 33;
    const int kHalf   = kFrames / 2;

    int16_t* wave     = new int16_t[NUM_SAMPLES];
    float*   spectrum = new float[NUM_SAMPLES / 2];

    const int lengths[2] = { LED_0_NUM, 8 };

    for (size_t idx = 1; idx < static_cast<size_t>(AnimationType::COUNT); ++idx) {
        const AnimationMeta& meta = animationCatalog[idx];
        if (meta.type == AnimationType::NONE || !meta.create) continue;

        for (int li = 0; li < 2; ++li) {
            const int n = lengths[li];

            std::printf("  sweeping %-24s n=%3d ... ", meta.name, n);
            std::fflush(stdout);
            setPhase(meta.name);

            std::vector<CRGB> buf(static_cast<size_t>(n), CRGB::Black);

            const size_t allocStart = g_allocCount;
            const size_t liveStart  = g_allocCount - g_freeCount;

            Animation* anim = meta.create();
            anim->begin();

            size_t allocFirstHalf = 0;
            size_t allocSecondHalf = 0;
            uint8_t peak = 0;
            bool   everLit = false;

            for (int frame = 0; frame < kFrames; ++frame) {
                g_tick.fetch_add(1, std::memory_order_relaxed);

                AudioFeatures f;
                fillDeviceAudio(frame, f, wave, spectrum);
                simAdvance(kDtMs);

                fill_solid(buf.data(), n, CRGB::Black);

                const size_t before = g_allocCount;
                anim->update(buf.data(), n, f);
                const size_t used = g_allocCount - before;

                if (frame < kHalf) allocFirstHalf += used;
                else               allocSecondHalf += used;

                for (int i = 0; i < n; ++i) {
                    const uint8_t m = std::max(buf[i].r, std::max(buf[i].g, buf[i].b));
                    if (m > peak) peak = m;
                    if (m > 0) everLit = true;
                }
            }

            const size_t allocTotal = g_allocCount - allocStart;

            // The animation object itself is heap-allocated, so the live count is
            // only meaningful once it is gone. Sampling before the delete reports
            // the object rather than a leak, which is what the first version of
            // this check did on all eighteen runs.
            delete anim;

            const long liveGrowth = static_cast<long>(g_allocCount - g_freeCount) -
                                    static_cast<long>(liveStart);

            std::printf("alloc %zu (2nd half %zu), peak ch %u, live %+ld%s\n",
                        allocTotal, allocSecondHalf, unsigned(peak), liveGrowth,
                        everLit ? "" : ", NEVER LIT");

            if (verbose && !everLit) {
                std::printf("      %s n=%d produced no light at all\n", meta.name, n);
            }

            const std::string label = std::string(meta.name) + " n=" + std::to_string(n);

            record(label + " does not allocate in steady state",
                   allocSecondHalf == 0,
                   std::to_string(allocFirstHalf) + " allocations in the first half, " +
                   std::to_string(allocSecondHalf) + " in the second");

            record(label + " keeps a flat live allocation count",
                   liveGrowth == 0,
                   "live count moved by " + std::to_string(liveGrowth) +
                   " across " + std::to_string(kFrames) + " frames, " +
                   std::to_string(allocTotal) + " allocations total");

            record(label + " lights at least one pixel",
                   everLit,
                   "peak channel value " + std::to_string(unsigned(peak)) +
                   " across " + std::to_string(kFrames) + " frames of device-scale audio");
        }
    }

    delete[] wave;
    delete[] spectrum;
}

// -----------------------------------------------------------------------------
//  Check 6b - the strip is lit at the level this microphone actually reports
//
//  Every check above runs on fillDeviceAudio, whose five-phase table averages a
//  level of 0.49. A live session on the microphone in use reported 0.159, three
//  times lower, with litSum 586 of a possible 76500: under one percent of the
//  strip's capacity, which reads as off. The sweep was green throughout, because
//  no check in this file had ever run at the level the page sees.
//
//  Scored against the same animation at full level rather than against the
//  strip's raw capacity. Capacity assumes all three channels at 255, which no
//  saturated colour reaches: the tunnel's own sine fill peaks at about a fifth of
//  capacity even at level 1.0, so a capacity bar would be a bar about saturation
//  rather than about brightness. The ratio is scale-free and holds for every
//  animation without a per-animation constant.
// -----------------------------------------------------------------------------
namespace {

struct LitPeak {
    double  sum        = 0.0;
    uint8_t maxChannel = 0;
};

// Peak summed channels over a run at a fixed level. Peak rather than mean,
// because an animation that only draws on a beat would otherwise be scored on the
// frames between its beats.
LitPeak peakLitSumAt(const AnimationMeta& meta, int n, int frames, int dtMs,
                     int16_t* wave, float* spectrum, float level) {
    std::vector<CRGB> buf(static_cast<size_t>(n), CRGB::Black);
    Animation* anim = meta.create();
    anim->begin();

    LitPeak peak;
    for (int frame = 0; frame < frames; ++frame) {
        g_tick.fetch_add(1, std::memory_order_relaxed);

        AudioFeatures f;
        fillDeviceAudio(frame, f, wave, spectrum);

        // level carries the run. The other amplitude fields are held at the ratios
        // the live session reported against it, 0.041 for volume, 0.113 for peak
        // and 0.033 for average, so an animation reading any of them still sees a
        // self-consistent block rather than a level with a stale block around it.
        f.level          = level;
        f.volume         = level * 0.041f;
        f.peak           = level * 0.113f;
        f.average        = level * 0.033f;
        f.loudness       = f.volume * 100.0f;
        f.dynamics       = 0.68f;
        f.signalPresence = level > 0.01f;
        f.beatDetected   = (frame % 27 == 0) && level > 0.01f;
        f.bassHits       = f.beatDetected ? 1 : 0;
        simAdvance(dtMs);

        fill_solid(buf.data(), n, CRGB::Black);
        anim->update(buf.data(), n, f);

        double  sum = 0.0;
        uint8_t top = 0;
        for (int i = 0; i < n; ++i) {
            sum += buf[i].r + buf[i].g + buf[i].b;
            const uint8_t m = std::max(buf[i].r, std::max(buf[i].g, buf[i].b));
            if (m > top) top = m;
        }
        if (sum > peak.sum) {
            peak.sum        = sum;
            peak.maxChannel = top;
        }
    }

    delete anim;
    return peak;
}

}  // namespace

void checkMeasuredLevelBrightness(bool verbose) {
    const int kFrames = 900;
    const int kDtMs   = 33;
    const int n       = LED_0_NUM;

    // The live session's level, and the ratio of the two runs below that the curve
    // has to clear. Linear mapping gives 0.159, the square root gives 0.399, and
    // the bar sits between them with room on both sides.
    const float  kMicLevel = 0.159f;
    const double kMinRatio = 0.30;

    int16_t* wave     = new int16_t[NUM_SAMPLES];
    float*   spectrum = new float[NUM_SAMPLES / 2];

    // The curve's own contract, before any animation reads it. Passes through both
    // ends and lifts what is between them, which is what makes it a brightness
    // curve rather than a floor: a floor would lift silence too.
    record("the brightness curve leaves silence at zero",
           AudioFeatures{}.pixelLevel() == 0.0f,
           "a default feature block reads level 0 and brightness " +
           std::to_string(AudioFeatures{}.pixelLevel()) +
           ", so the curve cannot light a strip the gate has closed");

    {
        AudioFeatures full;
        full.level = 1.0f;
        AudioFeatures quiet;
        quiet.level = kMicLevel;
        record("the brightness curve lifts a quiet moment without lifting silence",
               full.pixelLevel() == 1.0f &&
               quiet.pixelLevel() > kMicLevel * 2.0f &&
               quiet.pixelLevel() < 1.0f,
               "level " + std::to_string(kMicLevel) + " maps to brightness " +
               std::to_string(quiet.pixelLevel()) + " and level 1 maps to " +
               std::to_string(full.pixelLevel()));
    }

    // The composition the two curves exist to make true, pinned because getting it
    // wrong is silent. hsv2rgb_rainbow squares CHSV's val on the way out, so the
    // emitted duty of CHSV(h, s, hsvLevel) is the square of hsvLevel, which is
    // pixelLevel. Written as a measurement of the conversion rather than of the
    // arithmetic, so a FastLED upgrade that changes the squaring shows up here
    // instead of as a strip that has quietly gone dark.
    {
        AudioFeatures q;
        q.level = kMicLevel;
        const uint8_t v = uint8_t(q.hsvLevel() * 255.0f);
        const CRGB    emitted = CHSV(0, 255, v);   // hue 0 and full saturation put 255 in red
        const double  duty = emitted.r / 255.0;

        record("the HSV brightness curve composes with the library's own curve",
               duty > 0.30 && duty < 0.50,
               "level " + std::to_string(kMicLevel) + " emits red " +
               std::to_string(emitted.r) + " through CHSV, duty " + std::to_string(duty) +
               ", against " + std::to_string(q.pixelLevel()) + " through a linear scale");
    }

    bool allClear = true;
    std::string detail;

    for (size_t idx = 1; idx < static_cast<size_t>(AnimationType::COUNT); ++idx) {
        const AnimationMeta& meta = animationCatalog[idx];
        if (meta.type == AnimationType::NONE || !meta.create) continue;

        setPhase(meta.name);

        const LitPeak quiet = peakLitSumAt(meta, n, kFrames, kDtMs, wave, spectrum, kMicLevel);
        const LitPeak full  = peakLitSumAt(meta, n, kFrames, kDtMs, wave, spectrum, 1.0f);
        const double ratio = full.sum > 0.0 ? quiet.sum / full.sum : 1.0;

        if (verbose) {
            std::printf("  brightness %-24s quiet %8.0f ch %3u, full %8.0f ch %3u, ratio %.3f\n",
                        meta.name, quiet.sum, unsigned(quiet.maxChannel),
                        full.sum, unsigned(full.maxChannel), ratio);
        }

        if (ratio < kMinRatio) {
            allClear = false;
            detail += std::string(meta.name) + " holds " +
                      std::to_string(int(ratio * 100.0)) + " percent; ";
        }
    }

    record("every catalog animation stays lit at the microphone's level",
           allClear,
           allClear
             ? "all animations hold at least " + std::to_string(int(kMinRatio * 100.0)) +
               " percent of their full-level brightness at level " +
               std::to_string(kMicLevel) + ", where a linear mapping holds 16"
             : detail + "against a floor of " + std::to_string(int(kMinRatio * 100.0)) +
               " percent at level " + std::to_string(kMicLevel));

    delete[] wave;
    delete[] spectrum;
}

// -----------------------------------------------------------------------------
//  Check 7 - drive every layer type
//
//  The lifecycle check only ever activates BACKGROUND and REACTIVE, because
//  SceneRegistry's scene definitions only name those two. MoodMemoryArcLayer is
//  the sole consumer of the audio history in the whole codebase -- the line that
//  decides what a replacement container has to support -- and it has never run.
//  This drives all eight factories.
//
//  The history is filled before the measured window so MoodMemoryArcLayer's
//  `history.size() < 10` guard is actually passed rather than silently returning.
// -----------------------------------------------------------------------------
void checkLayerSweep(bool verbose) {
    const int kFrames = 900;
    const int kDtMs   = 33;
    const int kHalf   = kFrames / 2;

    int16_t* wave     = new int16_t[NUM_SAMPLES];
    float*   spectrum = new float[NUM_SAMPLES / 2];

    AudioHistoryTracker history;
    {
        AudioFeatures f;
        for (int i = 0; i < 1500; ++i) {
            fillDeviceAudio(i * 7, f, wave, spectrum);
            simAdvance(kDtMs);
            history.addSnapshot(f);
        }
    }

    std::vector<CRGB> buf(static_cast<size_t>(LED_0_NUM), CRGB::Black);

    for (int t = 0; t < static_cast<int>(LayerType::COUNT); ++t) {
        const LayerType type = static_cast<LayerType>(t);

        std::printf("  sweeping layer %-12s ... ", layerTypeToString(type));
        std::fflush(stdout);
        setPhase(layerTypeToString(type));

        const size_t liveStart = g_allocCount - g_freeCount;

        size_t  allocFirstHalf  = 0;
        size_t  allocSecondHalf = 0;
        size_t  allocTotal      = 0;
        uint8_t peak            = 0;
        bool    everLit         = false;
        int     activeAtEnd     = 0;
        long    liveGrowth      = 0;

        {
            LayerManager lm;
            lm.setLEDs(buf.data(), LED_0_NUM);
            lm.addLayerByType(type);

            const size_t allocStart = g_allocCount;

            for (int frame = 0; frame < kFrames; ++frame) {
                g_tick.fetch_add(1, std::memory_order_relaxed);

                AudioFeatures f;
                fillDeviceAudio(frame, f, wave, spectrum);
                simAdvance(kDtMs);

                fill_solid(buf.data(), LED_0_NUM, CRGB::Black);

                const size_t before = g_allocCount;
                lm.updateLayers(f, history.getHistory());
                lm.renderLayers();
                const size_t used = g_allocCount - before;

                if (frame < kHalf) allocFirstHalf += used;
                else               allocSecondHalf += used;

                for (int i = 0; i < LED_0_NUM; ++i) {
                    const uint8_t m = std::max(buf[i].r, std::max(buf[i].g, buf[i].b));
                    if (m > peak) peak = m;
                    if (m > 0) everLit = true;
                }
            }

            allocTotal  = g_allocCount - allocStart;
            activeAtEnd = lm.activeCount();
        }

        liveGrowth = static_cast<long>(g_allocCount - g_freeCount) -
                     static_cast<long>(liveStart);

        std::printf("alloc %zu (2nd half %zu), peak ch %u, live %+ld%s\n",
                    allocTotal, allocSecondHalf, unsigned(peak), liveGrowth,
                    everLit ? "" : ", NEVER LIT");

        if (verbose && !everLit) {
            std::printf("      %s produced no light at all\n", layerTypeToString(type));
        }

        const std::string label = std::string("layer ") + layerTypeToString(type);

        record(label + " does not allocate in steady state",
               allocSecondHalf == 0,
               std::to_string(allocFirstHalf) + " allocations in the first half, " +
               std::to_string(allocSecondHalf) + " in the second");

        record(label + " keeps a flat live allocation count",
               liveGrowth == 0,
               "live count moved by " + std::to_string(liveGrowth) +
               " across " + std::to_string(kFrames) + " frames, " +
               std::to_string(allocTotal) + " allocations total");

        record(label + " stays active for the whole run",
               activeAtEnd == 1,
               std::to_string(activeAtEnd) + " layers active at frame " +
               std::to_string(kFrames) + ", expected 1");
    }

    delete[] wave;
    delete[] spectrum;
}

} // namespace

// -----------------------------------------------------------------------------
//  Check 4 - the lifecycle across a run, driven through the real controller
// -----------------------------------------------------------------------------
void checkLifecycle(bool verbose) {
    const int kFrames = 3000;   // 3000 frames at 33 ms is ~100 s of simulated time
    const int kDtMs   = 33;

    // Steady-state windows for the leak test. Both history deques grow until they
    // are full, and a deque that is still growing allocates without freeing -- so
    // counting allocations early measures the fill-up, not a leak. MoodHistory
    // caps at 150 snapshots (frame 150) and AudioHistoryTracker at 1500 (frame
    // 1500); 1800 clears both, and each window is then 600 frames.
    const int kWindowA = 1800;
    const int kWindowB = 2400;

    AudioFeatures       audio;
    MoodHistory         mood;
    AudioHistoryTracker history;
    LEDStripController  ctrl(audio, mood, history);
    ctrl.begin();

    record("both configured strips are registered", ctrl.getStripCount() == 2,
           "strip count " + std::to_string(ctrl.getStripCount()));

    // Scene name only on a transition. getCurrentSceneName() returns a String by
    // value, so calling it every frame allocates a std::string every frame and the
    // harness's own churn would bury the firmware's in the size histogram.
    std::string lastSceneName;
    int         lastChangeCount = -1;

    int    overCapViolations = 0;
    int    maxLayersSeen     = 0;
    size_t allocA            = 0;
    size_t allocB            = 0;
    size_t liveAtA           = 0;
    size_t liveAtB           = 0;
    uint8_t strip1Peak       = 0;

    std::vector<std::string> scenesSeen;

    for (int frame = 0; frame < kFrames; ++frame) {
        g_tick.fetch_add(1, std::memory_order_relaxed);

        audio = scriptedAudio(frame);
        simAdvance(kDtMs);

        const size_t beforeSnapshot = g_allocCount;
        history.addSnapshot(audio);
        g_historyAllocs += g_allocCount - beforeSnapshot;

        const size_t allocBefore    = g_allocCount;
        ctrl.update();
        const size_t allocThisFrame = g_allocCount - allocBefore;
        g_controllerAllocs += allocThisFrame;

        const int changeCount = ctrl.getSceneChangeCount();
        const bool sceneChanged = (changeCount != lastChangeCount);
        if (sceneChanged) {
            lastSceneName   = ctrl.getCurrentSceneName();
            lastChangeCount = changeCount;
            if (std::find(scenesSeen.begin(), scenesSeen.end(), lastSceneName)
                    == scenesSeen.end()) {
                scenesSeen.push_back(lastSceneName);
            }
        }

        const int counts[2] = { ctrl.layerCount(0), ctrl.layerCount(1) };

        for (int i = 0; i < 2; ++i) {
            if (counts[i] < 0 || counts[i] > 4) ++overCapViolations;
            if (counts[i] > maxLayersSeen) maxLayersSeen = counts[i];
        }

        for (int i = 0; i < LED_1_NUM; ++i) {
            const uint8_t m = std::max(ledStrip_1[i].r,
                              std::max(ledStrip_1[i].g, ledStrip_1[i].b));
            if (m > strip1Peak) strip1Peak = m;
        }

        if (frame == kWindowA) liveAtA = g_allocCount - g_freeCount;
        if (frame == kWindowB) liveAtB = g_allocCount - g_freeCount;
        if (frame >= kWindowA && frame < kWindowB) allocA += allocThisFrame;
        if (frame >= kWindowB)                     allocB += allocThisFrame;

        if (verbose && (frame % 60 == 0 || sceneChanged)) {
            std::printf("\nt=%5lu ms  scene=%-20s mood=%-9s L0=%d L1=%d  new-alloc=%lu\n",
                        static_cast<unsigned long>(millis()), lastSceneName.c_str(),
                        mood.getCurrentMoodName().c_str(), counts[0], counts[1],
                        static_cast<unsigned long>(allocThisFrame));
            drawStrip("strip0", ledStrip_0, LED_0_NUM, 50);
            drawStrip("strip1", ledStrip_1, LED_1_NUM, 10);
        }
    }

    record("layer count never exceeds the cap", overCapViolations == 0,
           "max seen " + std::to_string(maxLayersSeen));

    // The leak test. A layer rebuilt every frame -- the defect the audit found --
    // would add one live allocation per frame, so 600 frames of a flat live count
    // is what rules it out. Anything still growing by the end is a leak.
    const long liveGrowth = static_cast<long>(liveAtB) - static_cast<long>(liveAtA);
    record("live allocations stop growing once the history buffers are full",
           liveGrowth <= 8,
           "live " + std::to_string(liveAtA) + " at frame " + std::to_string(kWindowA) +
           ", " + std::to_string(liveAtB) + " at frame " + std::to_string(kWindowB) +
           " (" + (liveGrowth >= 0 ? "+" : "") + std::to_string(liveGrowth) +
           "; a per-frame layer leak would show +600)");

    record("allocation rate is flat in steady state",
           allocB <= allocA + allocA / 4 + 16,
           std::to_string(allocA) + " allocations in frames " + std::to_string(kWindowA) +
           "-" + std::to_string(kWindowB) + ", " + std::to_string(allocB) + " in " +
           std::to_string(kWindowB) + "-" + std::to_string(kFrames));

    record("the scene clock advances and transitions fire",
           ctrl.getSceneChangeCount() >= 2 && scenesSeen.size() >= 2,
           std::to_string(ctrl.getSceneChangeCount()) + " scene changes across " +
           std::to_string(scenesSeen.size()) + " distinct scenes");

    record("every registered strip receives light", strip1Peak > 0,
           "strip1 peak channel " + std::to_string(strip1Peak));

    record("steady-state live allocations are bounded", liveAtB < 400,
           std::to_string(liveAtB) + " live blocks held at frame " +
           std::to_string(kWindowB) + "; " + std::to_string(g_allocCount) +
           " new / " + std::to_string(g_freeCount) + " delete over the run");
}

// -----------------------------------------------------------------------------
//  Check 8 - the soak
//
//  Part 2 wrapped six unbounded float accumulators. What a harness run can and
//  cannot show about them: precision exhaustion is not reachable here at all.
//  wavePhase gains at most 0.11 a frame, so reaching float's exact-integer limit
//  takes on the order of ten million frames, and inf takes twenty orders of
//  magnitude more. A long pass therefore does not test the reason the wraps
//  exist. It tests a different, checkable claim: that the bound actually holds,
//  so no update leaves a value outside one period. That is what a wrong or
//  missing modulus breaks, and it is what these checks assert.
//
//  Four sites are reachable from a build. The other two the plan named,
//  WormholeVortexLayer and CentroidColorFlowLayer, are never instantiated
//  anywhere in src/, so their wraps are hygiene rather than a live fix.
// -----------------------------------------------------------------------------
namespace {

struct SoakResult {
    float  maxValue    = 0.0f;
    uint8_t firstPeak  = 0;
    uint8_t lastPeak   = 0;
    size_t allocSecond = 0;
    size_t allocTotal  = 0;
    long   liveGrowth  = 0;
};

// `drive` performs one frame and returns the accumulator's value after it. The
// value is sampled after the update, which is the only point the bound is
// claimed to hold.
template <typename Drive>
SoakResult runSoak(const char* label, float period, int frames, int n,
                   CRGB* buf, int16_t* wave, float* spectrum, Drive drive) {
    const int kLastFrom = frames - frames / 5;

    std::printf("  soaking %-26s %d frames ... ", label, frames);
    std::fflush(stdout);
    setPhase(label);

    SoakResult  r;
    const size_t liveStart  = g_allocCount - g_freeCount;
    const size_t allocStart = g_allocCount;

    for (int frame = 0; frame < frames; ++frame) {
        g_tick.fetch_add(1, std::memory_order_relaxed);

        AudioFeatures f;
        fillDeviceAudio(frame, f, wave, spectrum);
        simAdvance(33);

        fill_solid(buf, n, CRGB::Black);

        const size_t before = g_allocCount;
        const float  value  = drive(f, buf, n);
        if (frame >= frames / 2) r.allocSecond += g_allocCount - before;

        if (value > r.maxValue) r.maxValue = value;

        uint8_t peak = 0;
        for (int i = 0; i < n; ++i) {
            const uint8_t m = std::max(buf[i].r, std::max(buf[i].g, buf[i].b));
            if (m > peak) peak = m;
        }
        uint8_t& window = (frame < kLastFrom) ? r.firstPeak : r.lastPeak;
        if (peak > window) window = peak;
    }

    r.allocTotal = g_allocCount - allocStart;
    r.liveGrowth = static_cast<long>(g_allocCount - g_freeCount) -
                   static_cast<long>(liveStart);

    std::printf("max %.5f of %.5f, ch %u -> %u, 2nd half alloc %zu, live %+ld\n",
                r.maxValue, period, unsigned(r.firstPeak), unsigned(r.lastPeak),
                r.allocSecond, r.liveGrowth);
    return r;
}

void reportSoak(const char* label, int frames, float period, const SoakResult& r) {
    const std::string l(label);

    record(l + " stays inside one period", r.maxValue < period,
           "max " + std::to_string(r.maxValue) + " after " + std::to_string(frames) +
           " frames, against a period of " + std::to_string(period));

    record(l + " allocates nothing in the second half of the soak", r.allocSecond == 0,
           std::to_string(r.allocSecond) + " allocations in the second half, " +
           std::to_string(r.allocTotal) + " over the whole run, live " +
           std::to_string(r.liveGrowth));

    record(l + " is still lighting pixels at the end of the soak", r.lastPeak > 0,
           "peak channel " + std::to_string(unsigned(r.firstPeak)) + " early, " +
           std::to_string(unsigned(r.lastPeak)) + " in the final fifth");
}

} // namespace

void checkSoak(bool verbose) {
    const int kFrames = 20000;   // 20000 frames at 33 ms is ~11 min of simulated time
    const int n       = LED_0_NUM;

    int16_t* wave     = new int16_t[NUM_SAMPLES];
    float*   spectrum = new float[NUM_SAMPLES / 2];
    std::vector<CRGB> buf(static_cast<size_t>(n), CRGB::Black);

    // Filled before the measured window, so the one layer under test that reads
    // history has something to read.
    AudioHistoryTracker history;
    {
        AudioFeatures f;
        for (int i = 0; i < 1500; ++i) {
            fillDeviceAudio(i * 7, f, wave, spectrum);
            simAdvance(33);
            history.addSnapshot(f);
        }
    }

    {
        AlienPulseAnimation a;
        a.begin();
        const SoakResult r = runSoak("Alien Pulse wavePhase", 6.2831853f, kFrames, n,
            buf.data(), wave, spectrum,
            [&](const AudioFeatures& f, CRGB* b, int cnt) {
                a.update(b, cnt, f);
                return a.debugWavePhase();
            });
        reportSoak("Alien Pulse wavePhase", kFrames, 6.2831853f, r);
    }

    {
        NeonFlowAnimation a;
        a.begin();
        const SoakResult r = runSoak("Neon Flow hueOffset", 255.0f, kFrames, n,
            buf.data(), wave, spectrum,
            [&](const AudioFeatures& f, CRGB* b, int cnt) {
                a.update(b, cnt, f);
                return a.debugHueOffset();
            });
        reportSoak("Neon Flow hueOffset", kFrames, 255.0f, r);
    }

    {
        PsychedelicInkSquirtAnimation a;
        a.begin();
        const SoakResult r = runSoak("Squirt offset", 6.2831853f, kFrames, n,
            buf.data(), wave, spectrum,
            [&](const AudioFeatures& f, CRGB* b, int cnt) {
                a.update(b, cnt, f);
                return a.debugOffset();
            });
        reportSoak("Squirt offset", kFrames, 6.2831853f, r);
    }

    {
        // Read after render(), because that is where this one's wrap lives.
        EnergyPulseRiverLayer layer;
        const SoakResult r = runSoak("EnergyPulseRiver position", float(n), kFrames, n,
            buf.data(), wave, spectrum,
            [&](const AudioFeatures& f, CRGB* b, int cnt) {
                layer.update(f, history.getHistory());
                layer.render(b, cnt);
                return layer.debugPosition();
            });
        reportSoak("EnergyPulseRiver position", kFrames, float(n), r);
    }

    if (verbose) {
        std::printf("      soak drove %d frames per site, 33 ms apart\n", kFrames);
    }

    delete[] wave;
    delete[] spectrum;
}

// -----------------------------------------------------------------------------
//  Check 9 - the scene picker does not re-pick the running scene
//
//  MoodType::INTENSE has two catalog scenes, so a mood match is genuinely
//  ambiguous and the picker used to choose uniformly between them, the running
//  one included. beginScene then reset the clock and nothing changed on screen.
// -----------------------------------------------------------------------------
void checkSceneTransitions() {
    SceneRegistry reg;
    reg.registerDefaultScenes();

    std::vector<const SceneDefinition*> intense;
    for (const SceneDefinition& s : reg.getAll()) {
        if (s.supportsMood(MoodType::INTENSE)) intense.push_back(&s);
    }

    record("the catalog has more than one scene for a mood",
           intense.size() > 1,
           std::to_string(intense.size()) + " scenes prefer INTENSE; the check needs two");

    if (intense.size() < 2) return;

    // level, not energy. The classifier tests its thresholds against level, which
    // is 0..1 by construction. This check used to set energy to 2000 to land on
    // INTENSE, which only worked while the classifier was comparing a raw
    // magnitude sum against 0.8.
    MoodSnapshot mood;
    mood.level    = 1.0f;
    mood.dynamics = 0.8f;
    mood.bpm      = 120.0f;

    SceneState state;
    state.activeScene = intense[0];

    int selfPicks = 0;
    int others    = 0;
    for (int i = 0; i < 400; ++i) {
        const SceneDefinition& picked = reg.pickSceneByMood(state, mood);
        if (&picked == intense[0]) ++selfPicks;
        else                       ++others;
    }

    record("the running scene is not re-picked while another matches",
           selfPicks == 0,
           std::to_string(selfPicks) + " self-picks and " + std::to_string(others) +
           " alternatives in 400 draws of an otherwise even two-way choice");
}

// -----------------------------------------------------------------------------
//  The real FFT, which nothing else here touches
//
//  Every other check builds an AudioFeatures by hand with scriptedAudio(), which
//  is how features.volume, features.peak and features.loudness sat at zero while
//  101 checks passed: analyzeAudio() computed them into AudioProcessor's members
//  and never copied them into the struct it returns. On hardware that left
//  NeonBeatTunnel (which scales by volume) rendering black, neonFlow clamped to
//  its brightness floor, and the display's loudness bar reading zero.
//
//  This is also the only place the classifier meets a signal that came out of the
//  FFT rather than out of a script, so it is where the energy scale can be seen.
// -----------------------------------------------------------------------------
void checkAudioProcessor() {
    const float amplitude = 0.4f;
    // Not named twoPi: arduinoFFT ships a twoPi macro, and it outranks a local.
    constexpr float kTwoPi = 6.28318530718f;

    // Bin 2, not a round 100 Hz. A whole number of cycles across the window makes
    // the block's mean exactly zero, so the DC removal in submitSamples is a no-op
    // and peak and volume are what the sine actually has. 100 Hz is 1.16 cycles
    // over 512 samples, which leaves a real offset behind and moves the measured
    // peak off the amplitude. Still under the 200 Hz bass limit, so the band split
    // keeps a direction to check instead of one undifferentiated blob.
    const float kToneHz = 2.0f * float(SAMPLE_RATE) / float(NUM_SAMPLES);
    std::vector<float> samples(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        samples[i] = amplitude * std::sin(kTwoPi * kToneHz * float(i) / float(SAMPLE_RATE));
    }

    AudioProcessor proc;
    proc.submitSamples(samples.data(), samples.size());
    const AudioFeatures f = proc.analyzeAudio();

    const float rms = amplitude / std::sqrt(2.0f);
    record("analyzeAudio reports the volume it measured",
           std::fabs(f.volume - rms) < 0.02f,
           "volume " + std::to_string(f.volume) + ", expected about " + std::to_string(rms));

    record("analyzeAudio reports the peak it measured",
           std::fabs(f.peak - amplitude) < 0.02f,
           "peak " + std::to_string(f.peak) + ", expected " + std::to_string(amplitude));

    // loudness is smoothed from zero, so one frame is one smoothing step toward
    // volume * 100 rather than the converged value.
    record("analyzeAudio reports a loudness tracking the volume",
           f.loudness > 0.0f && f.loudness < f.volume * 100.0f,
           "loudness " + std::to_string(f.loudness) + " after one frame at volume " +
           std::to_string(f.volume));

    float spectrumPeak = 0.0f;
    for (int i = 1; i < NUM_SAMPLES / 2; ++i) spectrumPeak = std::max(spectrumPeak, f.spectrum[i]);
    record("analyzeAudio fills the spectrum", spectrumPeak > 0.0f,
           "largest bin magnitude " + std::to_string(spectrumPeak));

    record("a 100 Hz tone lands in the bass band",
           f.bass > f.mid && f.bass > f.treble,
           "bass " + std::to_string(f.bass) + " mid " + std::to_string(f.mid) +
           " treble " + std::to_string(f.treble));

    // A signal that does not change must not change its classification. This is
    // the invariant behind the mood flicker seen once the browser build started
    // feeding the classifier live audio: a steady tone should be one mood, and
    // any mood it reports should be a mood the rules can reach.
    //
    // The first frames are not counted. The gate ramps open over about ten frames
    // and level is below every threshold until it has, so the classifier reports
    // nothing for that fraction of a second. That is the gate doing its job on the
    // silence before the tone, not an unstable classification, and what has to
    // hold is the state it settles into.
    constexpr int kSettleFrames = 20;
    MoodHistory mood;
    int changes = 0;
    MoodType previous = MoodType::UNKNOWN;
    bool sawUnknown = false;
    for (int frame = 0; frame < 100; ++frame) {
        proc.submitSamples(samples.data(), samples.size());
        const AudioFeatures next = proc.analyzeAudio();
        mood.update(next);
        if (frame >= kSettleFrames) {
            if (frame > kSettleFrames && mood.getCurrentMood() != previous) ++changes;
            if (mood.getCurrentMood() == MoodType::UNKNOWN) sawUnknown = true;
            previous = mood.getCurrentMood();
        }
    }

    record("a steady tone keeps one mood once the gate has opened", changes == 0,
           std::to_string(changes) + " mood changes in the " +
           std::to_string(100 - kSettleFrames) + " frames after the gate ramped open");

    // The signal came out of the FFT rather than out of a script, which makes this
    // the only place the classifier meets a real measurement. It has to reach a
    // mood, not sit on UNKNOWN.
    record("the classifier reaches a mood for an FFT-scale signal",
           !sawUnknown,
           std::string("the settled frames of a 0.4 tone stayed on ") +
           moodToString(mood.getCurrentMood()) + " at energy " +
           std::to_string(f.energy));

    // --- DC offset -----------------------------------------------------------
    // The browser's lowest spectrum bar read full in silence as well as in music.
    // The INMP441 carries a DC offset, the FFT applies its window before it
    // transforms, and a windowed constant is not zero: the offset lands in the DC
    // bin and its main lobe leaks across the lowest few bins, which is the whole
    // of that first bar. Both the band loop and the display start at bin 1, so
    // nothing downstream removed it.
    std::vector<float> offsetSamples(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) offsetSamples[i] = 0.3f + samples[i];

    AudioProcessor procOffset;
    procOffset.submitSamples(offsetSamples.data(), offsetSamples.size());
    const AudioFeatures fo = procOffset.analyzeAudio();

    float cleanLow = 0.0f, offsetLow = 0.0f, offsetPeakBin = 0.0f;
    for (int i = 1; i <= 4; ++i) {
        cleanLow  = std::max(cleanLow,  f.spectrum[i]);
        offsetLow = std::max(offsetLow, fo.spectrum[i]);
    }
    for (int i = 0; i < NUM_SAMPLES / 2; ++i) {
        offsetPeakBin = std::max(offsetPeakBin, fo.spectrum[i]);
    }

    record("a DC offset does not leak into the lowest spectrum bins",
           offsetLow < cleanLow * 1.5f + 1.0f,
           "lowest four bins peak at " + std::to_string(offsetLow) +
           " with a 0.3 offset against " + std::to_string(cleanLow) + " without");

    record("the DC bin is not the largest bin",
           fo.spectrum[0] < offsetPeakBin,
           "DC bin " + std::to_string(fo.spectrum[0]) +
           " against a largest bin of " + std::to_string(offsetPeakBin));

    // --- Silence gate --------------------------------------------------------
    // Silence still showed a half-filled, active spectrum, because microphone
    // self-noise and room rumble are broadband and lit every band. The floor and
    // the gate are what make silence read as silence.
    std::vector<float> silent(NUM_SAMPLES, 0.0005f);
    AudioProcessor procSilent;
    procSilent.submitSamples(silent.data(), silent.size());
    const AudioFeatures fsil = procSilent.analyzeAudio();

    record("a block below the noise floor is gated out",
           !fsil.signalPresence && fsil.energy == 0.0f,
           "signalPresence " + std::string(fsil.signalPresence ? "true" : "false") +
           ", energy " + std::to_string(fsil.energy) + ", floor " +
           std::to_string(fsil.noiseFloor));

    record("a block above the noise floor opens the gate",
           f.signalPresence,
           "signalPresence " + std::string(f.signalPresence ? "true" : "false") +
           ", floor " + std::to_string(f.noiseFloor));

    // A block that is quiet but audible still has to read as level 0, or silence
    // is the loudest thing on the recording once the reference has decayed.
    record("a gated block reads as zero level",
           fsil.level == 0.0f,
           "level " + std::to_string(fsil.level) + " on a block below the floor");

    // --- Normalised level ----------------------------------------------------
    // AlienPulse and BassPulseStorm drove themselves from energy divided by a
    // device-magnitude constant and rendered black, because the microphone in use
    // runs about 40 dB below the scale those constants assumed. level is the
    // replacement: the current block against the loudest recent one, so 0..1 at
    // any gain. These two checks are what establish that it actually is.
    // submitSamples only fills the buffers. analyzeAudio is what advances the
    // gate and the reference, so the two have to alternate or thirty submits are
    // one frame.
    AudioProcessor procLevel;
    AudioFeatures loudLevel;
    for (int warm = 0; warm < 30; ++warm) {
        procLevel.submitSamples(samples.data(), samples.size());
        loudLevel = procLevel.analyzeAudio();
    }

    record("the loudest block seen reads as full level",
           loudLevel.level > 0.95f,
           "level " + std::to_string(loudLevel.level) + " on a repeated 0.4 tone");

    // Held at a quarter for a stretch rather than submitted once. level is an
    // enveloped ratio now, so a single quieter block is exactly what it is built to
    // reject, and reading it after one block measures the release coefficient
    // rather than the normalisation. Forty blocks is about half a second, which is
    // long enough for the follower to arrive and short enough that the reference
    // has not decayed far enough to matter. The reference does decay while these
    // play, which is why the window is above a literal quarter rather than on it.
    std::vector<float> quieter(samples.size());
    for (size_t i = 0; i < quieter.size(); ++i) quieter[i] = samples[i] * 0.25f;

    AudioFeatures quarterLevel;
    for (int held = 0; held < 40; ++held) {
        procLevel.submitSamples(quieter.data(), quieter.size());
        quarterLevel = procLevel.analyzeAudio();
    }

    record("a quarter-amplitude passage settles a quarter of the level",
           quarterLevel.level > 0.2f && quarterLevel.level < 0.4f,
           "level " + std::to_string(quarterLevel.level) +
           " held for forty blocks at a quarter of the amplitude that set the reference");

    // --- BPM decay -----------------------------------------------------------
    // The readout froze at its last measured value when the music stopped.
    // currentBPM was assigned only inside the beat branch and never reset.
    std::vector<float> quiet(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        quiet[i] = 0.01f * std::sin(kTwoPi * kToneHz * float(i) / float(SAMPLE_RATE));
    }

    AudioProcessor procBpm;
    for (int beatFrame = 0; beatFrame < 12; ++beatFrame) {
        procBpm.submitSamples(samples.data(), samples.size());   // loud
        procBpm.analyzeAudio();
        simAdvance(400);
        procBpm.submitSamples(quiet.data(), quiet.size());       // the gap
        procBpm.analyzeAudio();
        simAdvance(400);
    }
    const float bpmBefore = procBpm.analyzeAudio().bpm;

    record("a beat train produces a BPM", bpmBefore > 0.0f,
           "bpm " + std::to_string(bpmBefore) + " after 12 beats at 800 ms apart");

    float bpmAfter = bpmBefore;
    int quietFrames = 0;
    for (; quietFrames < 2000 && bpmAfter > 0.0f; ++quietFrames) {
        procBpm.submitSamples(quiet.data(), quiet.size());
        bpmAfter = procBpm.analyzeAudio().bpm;
        simAdvance(33);
    }

    record("the BPM falls back to zero once the beats stop",
           bpmBefore > 0.0f && bpmAfter == 0.0f,
           "bpm went from " + std::to_string(bpmBefore) + " to " + std::to_string(bpmAfter) +
           " over " + std::to_string(quietFrames) + " quiet frames");

    // --- Mood flicker --------------------------------------------------------
    // The browser reported the mood value jumping several times a second, with or
    // without music. The classifier reads instantaneous values, so the input has to
    // cross one of its thresholds for this to be a real test rather than a signal
    // that was never going to move: a sine and a single-sample spike have the same
    // energy scale but opposite crest factors, and dynamics is the threshold they
    // straddle.
    std::vector<float> spiky(NUM_SAMPLES, 0.0f);
    spiky[0] = 0.9f;

    // Driven repeatedly for the same reason as procLevel above: level is held
    // down by the gate until it has ramped, so a single frame of this block reads
    // 0.12 rather than the 0.9 it settles at.
    AudioProcessor procSpike;
    AudioFeatures spike;
    for (int warm = 0; warm < 20; ++warm) {
        procSpike.submitSamples(spiky.data(), spiky.size());
        spike = procSpike.analyzeAudio();
    }

    // The INTENSE gate is level > 0.8 and dynamics > 0.5, so the pair has to
    // straddle both. The spike's processor has seen only that one block, so its
    // level is 1.0 by construction: level is a fraction of the loudest block in
    // the same processor's history.
    record("the flicker test straddles a classifier threshold",
           f.dynamics < 0.5f && spike.dynamics > 0.6f && spike.level > 0.8f,
           "sine dynamics " + std::to_string(f.dynamics) + " against spike dynamics " +
           std::to_string(spike.dynamics) + " at level " + std::to_string(spike.level) +
           ", the INTENSE gate being level > 0.8 and dynamics > 0.5");

    // The block alternates on every frame, so an undamped classifier changes on all
    // 99 transitions while a damped one settles and holds.
    AudioProcessor procFlicker;
    MoodHistory flicker;
    int flickerChanges = 0;
    MoodType flickerPrevious = MoodType::UNKNOWN;
    for (int frame = 0; frame < 100; ++frame) {
        const std::vector<float>& block = (frame % 2 == 0) ? samples : spiky;
        procFlicker.submitSamples(block.data(), block.size());
        flicker.update(procFlicker.analyzeAudio());
        if (frame > 0 && flicker.getCurrentMood() != flickerPrevious) ++flickerChanges;
        flickerPrevious = flicker.getCurrentMood();
    }

    // The settled mood is checked as well as the count, because a mood frozen at
    // UNKNOWN would change zero times and pass the count on its own. moodToString
    // maps UNKNOWN to "Calm", so the detail line cannot be read as proof on its own.
    record("an alternating signal does not flicker the mood",
           flickerChanges <= 3 && flicker.getCurrentMood() != MoodType::UNKNOWN,
           std::to_string(flickerChanges) + " mood changes across 100 frames of a signal "
           "that alternates every frame, settling on " +
           moodToString(flicker.getCurrentMood()));

    // The counter the page reports as moodChanges. It lives in the firmware
    // because the page derived it from a trace that only records with ?debug=1
    // on, so a session opened without it reported zero changes however much the
    // mood had moved, which reads as a classifier that is stuck. Counted outside
    // the class here, so the check is that it observes the same transitions the
    // harness does rather than that it agrees with itself.
    record("the firmware's mood-change count matches the transitions observed",
           flicker.getMoodChangeCount() == flickerChanges,
           "firmware counted " + std::to_string(flicker.getMoodChangeCount()) +
           ", the harness observed " + std::to_string(flickerChanges) +
           " across the same 100 frames");

    // --- Mood dwell and the moving dynamics thresholds ------------------------
    // Both are new behaviour with nothing else guarding them. They are driven from
    // scripted AudioFeatures rather than from the FFT, because what is under test
    // is the rule the classifier applies to its inputs, not what the FFT makes of
    // a waveform. MoodHistory stamps each snapshot from millis(), so the harness
    // clock is what advances the dwell.
    {
        const auto feed = [](MoodHistory& m, const AudioFeatures& src, int frames) {
            for (int i = 0; i < frames; ++i) {
                simAdvance(33);
                m.update(src);
            }
        };

        AudioFeatures loud{};
        loud.level = 0.95f;
        loud.dynamics = 0.9f;
        loud.bpm = 140.0f;

        AudioFeatures quiet{};
        quiet.level = 0.05f;
        quiet.dynamics = 0.02f;
        quiet.bpm = 60.0f;

        MoodHistory dwell;
        feed(dwell, loud, 30);
        const MoodType loudMood = dwell.getCurrentMood();

        record("a scripted loud block reaches a mood without waiting on UNKNOWN",
               loudMood != MoodType::UNKNOWN,
               "settled on " + std::string(moodToString(loudMood)));

        // Arriving is not a change. Without this the counter would report a
        // change on the first frame of every session, which is the one transition
        // that is certainly not the mood moving.
        record("arriving at a first mood is not counted as a change",
               dwell.getMoodChangeCount() == 0,
               "counter reads " + std::to_string(dwell.getMoodChangeCount()) +
               " after settling from UNKNOWN on " + std::string(moodToString(loudMood)));

        // 495 ms of the opposite condition. Under the 500 ms confirmation and well
        // under the 2000 ms hold, so neither rule permits a change yet.
        feed(dwell, quiet, 15);
        record("a mood survives half a second of the opposite condition",
               dwell.getCurrentMood() == loudMood,
               "moved to " + std::string(moodToString(dwell.getCurrentMood())) +
               " after 495 ms of the opposite condition, from " +
               moodToString(loudMood));

        // A further 1980 ms, so both the confirmation and the hold have passed.
        feed(dwell, quiet, 60);
        record("a mood yields once its minimum hold has passed",
               dwell.getCurrentMood() != loudMood,
               "still on " + std::string(moodToString(dwell.getCurrentMood())) +
               " after 2475 ms of the opposite condition");

        // The one place a real change is confirmed to have happened, so it is
        // where the counter can be shown to move rather than only to hold still.
        record("a confirmed change increments the mood-change count",
               dwell.getMoodChangeCount() == 1,
               "counter reads " + std::to_string(dwell.getMoodChangeCount()) +
               " after one change, from " + std::string(moodToString(loudMood)) +
               " to " + std::string(moodToString(dwell.getCurrentMood())));

        // Each phase runs three seconds, which is inside the window's own memory:
        // the point of the check is that the cut points follow the signal off the
        // fixed pair, not that they converge on an exact pair of numbers.
        AudioFeatures narrow{};
        narrow.level = 0.5f;
        narrow.bpm = 90.0f;
        narrow.dynamics = 0.2f;
        MoodHistory range;
        feed(range, narrow, 90);
        narrow.dynamics = 0.8f;
        feed(range, narrow, 90);

        const float low = range.getDynamicsLow();
        const float high = range.getDynamicsHigh();
        record("the dynamics thresholds move with the observed range",
               low > 0.25f && high > 0.55f && high > low,
               "cut points at " + std::to_string(low) + " and " + std::to_string(high) +
               " after a signal running 0.2 to 0.8, where the fixed pair was 0.2 and 0.5");
    }
}

// -----------------------------------------------------------------------------
//  Entry point
// -----------------------------------------------------------------------------
namespace {

#ifdef _WIN32
void enableVt() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}
#else
void enableVt() {}
#endif

} // namespace

// -----------------------------------------------------------------------------
//  Frame dump for the web player
//
//  web/ plays back what this build actually produced rather than a JavaScript
//  reimplementation of the animations, so the recording and the firmware cannot
//  drift apart. What it is not: a live simulation. Each scenario is recorded once
//  here at the same 33 ms step the rest of the harness uses, and the page replays
//  the bytes. Re-run this after any change to the animations or the compositor.
//
//  Layout, per frame, oldest byte first: LED_0_NUM then LED_1_NUM pixels, three
//  bytes each in CRGB order. The static_assert below is what keeps the reader in
//  web/app.js honest.
// -----------------------------------------------------------------------------
namespace {

static_assert(sizeof(CRGB) == 3, "the frame format assumes three bytes per pixel");

struct ScenarioSpec {
    const char* id;
    const char* label;
    const char* note;
    int         frames;
    bool        deviceScale;   // fillDeviceAudio rather than scriptedAudio
};

const ScenarioSpec kScenarios[] = {
    { "device", "Device-scale audio",
      "Five phases of silence, quiet, bass-heavy, mid-forward and bright material "
      "at the magnitudes AudioProcessor produces on hardware, with a populated "
      "spectrum and waveform.",
      1200, true },
    { "moods",  "Mood rotation",
      "Four moods in rotation on the 0 to 1 scale the classifier's thresholds were "
      "written against, so the director has more than one scene to pick between.",
      1200, false },
};

struct SceneEvent {
    int         frame;
    std::string scene;
    std::string mood;
};

struct ScenarioResult {
    std::vector<SceneEvent> events;
    // A recording can open on silence, and the device scenario does: its first
    // phase is quiet by design and the strips stay near black for thirteen
    // seconds. Opening the page there reads as broken, so the recorder picks the
    // first frame that is meaningfully lit and the player starts there. Frame 0
    // wins whenever it is already bright.
    int startFrame = 0;
};

// Mean channel value of one frame, in 0..255.
float frameBrightness(const CRGB* a, int na, const CRGB* b, int nb) {
    unsigned long sum = 0;
    for (int i = 0; i < na; ++i) sum += a[i].r + a[i].g + a[i].b;
    for (int i = 0; i < nb; ++i) sum += b[i].r + b[i].g + b[i].b;
    const int pixels = na + nb;
    return pixels == 0 ? 0.0f : static_cast<float>(sum) / (pixels * 3);
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    for (char c : in) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}

bool writeScenario(const std::string& dir, const ScenarioSpec& spec,
                   ScenarioResult& out, bool verbose) {
    AudioFeatures       audio;
    MoodHistory         mood;
    AudioHistoryTracker history;
    LEDStripController  ctrl(audio, mood, history);
    ctrl.begin();

    std::vector<int16_t> wave(NUM_SAMPLES, 0);
    std::vector<float>   spectrum(NUM_SAMPLES / 2, 0.0f);

    const std::string path = dir + "/" + spec.id + ".bin";
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) {
        std::printf("  cannot open %s for writing\n", path.c_str());
        return false;
    }

    std::vector<float> brightness(spec.frames, 0.0f);
    int lastChangeCount = -1;
    for (int frame = 0; frame < spec.frames; ++frame) {
        if (spec.deviceScale) fillDeviceAudio(frame, audio, wave.data(), spectrum.data());
        else                  audio = scriptedAudio(frame);

        simAdvance(33);
        history.addSnapshot(audio);
        ctrl.update();

        std::fwrite(ledStrip_0, 1, sizeof(CRGB) * LED_0_NUM, f);
        std::fwrite(ledStrip_1, 1, sizeof(CRGB) * LED_1_NUM, f);

        brightness[frame] = frameBrightness(ledStrip_0, LED_0_NUM,
                                            ledStrip_1, LED_1_NUM);

        const int changeCount = ctrl.getSceneChangeCount();
        if (changeCount != lastChangeCount) {
            lastChangeCount = changeCount;
            out.events.push_back({ frame, ctrl.getCurrentSceneName(),
                                          mood.getCurrentMoodName() });
        }

        if (verbose && frame % 120 == 0) {
            std::printf("  %-7s frame %4d/%d  scene=%-22s mood=%s\n",
                        spec.id, frame, spec.frames,
                        ctrl.getCurrentSceneName().c_str(),
                        mood.getCurrentMoodName().c_str());
        }
    }

    std::fclose(f);

    float peak = 0.0f;
    for (float v : brightness) peak = v > peak ? v : peak;
    if (peak > 0.0f) {
        const float want = peak * 0.25f;
        for (int frame = 0; frame < spec.frames; ++frame) {
            if (brightness[frame] >= want) { out.startFrame = frame; break; }
        }
    }

    if (verbose) {
        std::printf("  %-7s wrote %s, %d frames, %zu scene events, "
                    "starts at %d (peak mean %.1f)\n",
                    spec.id, path.c_str(), spec.frames, out.events.size(),
                    out.startFrame, peak);
    }
    return true;
}

bool writeManifest(const std::string& dir,
                   const std::vector<std::pair<const ScenarioSpec*,
                                               ScenarioResult>>& results) {
    const std::string path = dir + "/manifest.json";
    FILE* m = std::fopen(path.c_str(), "wb");
    if (m == nullptr) {
        std::printf("  cannot open %s for writing\n", path.c_str());
        return false;
    }

    std::fprintf(m, "{\n  \"fps\": 30,\n  \"leds0\": %d,\n  \"leds1\": %d,\n",
                 LED_0_NUM, LED_1_NUM);
    std::fprintf(m, "  \"bytesPerFrame\": %zu,\n", sizeof(CRGB) * (LED_0_NUM + LED_1_NUM));
    std::fprintf(m, "  \"scenarios\": [\n");

    for (size_t s = 0; s < results.size(); ++s) {
        const ScenarioSpec&    spec   = *results[s].first;
        const std::vector<SceneEvent>& events = results[s].second.events;

        std::fprintf(m, "    {\n");
        std::fprintf(m, "      \"id\": \"%s\",\n", jsonEscape(spec.id).c_str());
        std::fprintf(m, "      \"label\": \"%s\",\n", jsonEscape(spec.label).c_str());
        std::fprintf(m, "      \"note\": \"%s\",\n", jsonEscape(spec.note).c_str());
        std::fprintf(m, "      \"frames\": %d,\n", spec.frames);
        std::fprintf(m, "      \"startFrame\": %d,\n", results[s].second.startFrame);
        std::fprintf(m, "      \"file\": \"%s.bin\",\n", spec.id);
        std::fprintf(m, "      \"events\": [");
        for (size_t e = 0; e < events.size(); ++e) {
            std::fprintf(m, "%s\n        {\"frame\": %d, \"scene\": \"%s\", \"mood\": \"%s\"}",
                         e == 0 ? "" : ",",
                         events[e].frame,
                         jsonEscape(events[e].scene).c_str(),
                         jsonEscape(events[e].mood).c_str());
        }
        std::fprintf(m, "\n      ]\n    }%s\n", s + 1 == results.size() ? "" : ",");
    }

    std::fprintf(m, "  ]\n}\n");
    std::fclose(m);
    std::printf("  wrote %s\n", path.c_str());
    return true;
}

int dumpFrames(const char* outDir, bool verbose) {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        std::printf("cannot create %s: %s\n", outDir, ec.message().c_str());
        return 1;
    }

    std::printf("Recording frames into %s\n", outDir);
    std::vector<std::pair<const ScenarioSpec*, ScenarioResult>> results;
    for (const ScenarioSpec& spec : kScenarios) {
        ScenarioResult result;
        if (!writeScenario(outDir, spec, result, verbose)) return 1;
        results.emplace_back(&spec, std::move(result));
    }

    if (!writeManifest(outDir, results)) return 1;
    std::printf("Done. Serve web/ with any static file server and open index.html\n");
    return 0;
}

} // namespace

// -----------------------------------------------------------------------------
//  Capture replay
//
//  The Record button in web/live.js writes what the browser actually fed the
//  analyser into a .f32 file. This reads one back through the real
//  AudioProcessor and reports what every feature did over it.
//
//  It exists because the microphone cannot be reached from here. A threshold
//  tuned against a synthetic tone, or against Chrome's fake capture device,
//  proves nothing about the input in use, and a report that the bands look wrong
//  cannot be checked against anything. A capture turns the real input into a
//  fixture that can be replayed after every edit, so a tuning change is measured
//  rather than guessed at, and the whole loop costs one rebuild instead of a
//  browser session per attempt.
//
//  Format, little-endian: 8 byte magic "GGCAP001", uint32 frame count, then that
//  many blocks of NUM_SAMPLES float32.
// -----------------------------------------------------------------------------
namespace {

struct Range {
    float lo = 1e30f;
    float hi = -1e30f;
    float sum = 0.0f;
    float last = 0.0f;
    float previous = 0.0f;
    float churn = 0.0f;          // summed |v - previous|
    bool  seeded = false;
    int   frames = 0;

    void add(float v) {
        if (v < lo) lo = v;
        if (v > hi) hi = v;
        sum += v;
        last = v;
        if (seeded) churn += std::fabs(v - previous);
        previous = v;
        seeded = true;
        ++frames;
    }
    float mean() const { return frames ? sum / float(frames) : 0.0f; }
    float churnMean() const { return frames > 1 ? churn / float(frames - 1) : 0.0f; }

    // How much of its own range the value covers per frame. This is the number
    // that separates a value which is inside a sane range from one which is
    // inside a sane range and unusable: at 0.5 the value crosses half its spread
    // every frame whatever the audio is doing.
    float churnShare() const {
        const float span = hi - lo;
        return span > 1e-6f ? churnMean() / span : 0.0f;
    }
};

struct Tracked {
    const char* name;
    float (*get)(const AudioFeatures&);
};

const Tracked kTracked[] = {
    {"level",           [](const AudioFeatures& f) { return f.level; }},
    {"volume",          [](const AudioFeatures& f) { return f.volume; }},
    {"loudness",        [](const AudioFeatures& f) { return f.loudness; }},
    {"peak",            [](const AudioFeatures& f) { return f.peak; }},
    {"average",         [](const AudioFeatures& f) { return f.average; }},
    {"bass",            [](const AudioFeatures& f) { return f.bass; }},
    {"mid",             [](const AudioFeatures& f) { return f.mid; }},
    {"treble",          [](const AudioFeatures& f) { return f.treble; }},
    {"energy",          [](const AudioFeatures& f) { return f.energy; }},
    {"dynamics",        [](const AudioFeatures& f) { return f.dynamics; }},
    {"bpm",             [](const AudioFeatures& f) { return f.bpm; }},
    {"centroid",        [](const AudioFeatures& f) { return f.spectrumCentroid; }},
    {"noiseFloor",      [](const AudioFeatures& f) { return f.noiseFloor; }},
};

constexpr size_t kTrackedCount = sizeof(kTracked) / sizeof(kTracked[0]);

struct ReplayReport {
    int                frames = 0;
    std::vector<Range> stats;          // parallel to kTracked
    Range              presence;
    int                beats = 0;
    int                moodChanges = 0;
    int                moodFrames[5] = {0, 0, 0, 0, 0};
    std::vector<unsigned long> dwells;

    const Range* find(const char* name) const {
        for (size_t i = 0; i < kTrackedCount; ++i) {
            if (std::strcmp(kTracked[i].name, name) == 0) return &stats[i];
        }
        return nullptr;
    }
};

// Reads a capture into one block of NUM_SAMPLES per frame. `why` is filled in on
// failure so the caller can say whether the file was missing, truncated, or not a
// capture at all, which are three different mistakes.
bool loadCapture(const char* path, std::vector<std::vector<float>>& blocks, std::string& why) {
    std::FILE* fp = std::fopen(path, "rb");
    if (fp == nullptr) {
        why = "cannot open";
        return false;
    }

    char         magic[8] = {};
    unsigned int frames   = 0;
    if (std::fread(magic, 1, 8, fp) != 8 || std::memcmp(magic, "GGCAP001", 8) != 0) {
        std::fclose(fp);
        why = "the magic is not GGCAP001";
        return false;
    }
    if (std::fread(&frames, 4, 1, fp) != 1) {
        std::fclose(fp);
        why = "no frame count";
        return false;
    }

    blocks.clear();
    blocks.reserve(frames);
    std::vector<float> block(NUM_SAMPLES);
    for (unsigned int i = 0; i < frames; ++i) {
        const size_t got = std::fread(block.data(), sizeof(float), NUM_SAMPLES, fp);
        if (got != NUM_SAMPLES) {
            std::fclose(fp);
            why = "truncated after " + std::to_string(i) + " of " +
                  std::to_string(frames) + " frames";
            return false;
        }
        blocks.push_back(block);
    }
    std::fclose(fp);
    return true;
}

void writeCapture(const char* path, const std::vector<std::vector<float>>& blocks) {
    std::FILE* fp = std::fopen(path, "wb");
    if (fp == nullptr) return;
    const unsigned int frames = static_cast<unsigned int>(blocks.size());
    std::fwrite("GGCAP001", 1, 8, fp);
    std::fwrite(&frames, 4, 1, fp);
    for (const std::vector<float>& block : blocks) {
        std::fwrite(block.data(), sizeof(float), NUM_SAMPLES, fp);
    }
    std::fclose(fp);
}

// Runs the blocks through the real pipeline. A frame is 33 ms, the harness's usual
// step, so the beat detector's 250 ms refractory and the BPM's decay behave over a
// capture the way they do live.
ReplayReport analyzeCapture(const std::vector<std::vector<float>>& blocks) {
    ReplayReport report;
    report.stats.resize(kTrackedCount);

    AudioProcessor proc;
    MoodHistory    mood;

    MoodType      previousMood = MoodType::UNKNOWN;
    unsigned long moodSince    = 0;

    for (const std::vector<float>& block : blocks) {
        proc.submitSamples(block.data(), block.size());
        const AudioFeatures f = proc.analyzeAudio();
        simAdvance(33);
        ++report.frames;

        for (size_t i = 0; i < kTrackedCount; ++i) report.stats[i].add(kTracked[i].get(f));
        report.presence.add(f.signalPresence ? 1.0f : 0.0f);
        if (f.beatDetected) ++report.beats;

        mood.update(f);
        const MoodType m = mood.getCurrentMood();
        report.moodFrames[int(m)] += 1;
        if (m != previousMood) {
            if (previousMood != MoodType::UNKNOWN || report.moodChanges > 0) {
                report.dwells.push_back(simNow() - moodSince);
            }
            ++report.moodChanges;
            moodSince    = simNow();
            previousMood = m;
        }
    }
    return report;
}

void printCapture(const char* path, const ReplayReport& r) {
    std::printf("capture replay: %s\n", path);
    std::printf("%d frames, %.1f s at 33 ms\n\n", r.frames, double(r.frames) * 0.033);

    std::printf("  %-12s %10s %10s %10s %10s %8s\n",
                "value", "min", "max", "mean", "churn/frm", "churn%");
    for (size_t i = 0; i < kTrackedCount; ++i) {
        const Range& v = r.stats[i];
        std::printf("  %-12s %10.4f %10.4f %10.4f %10.4f %7.1f%%\n",
                    kTracked[i].name, v.lo, v.hi, v.mean(), v.churnMean(),
                    double(v.churnShare()) * 100.0);
    }

    std::printf("\n  signal present  %.1f%% of frames\n", double(r.presence.mean()) * 100.0);
    std::printf("  beats           %d (%.1f per second)\n",
                r.beats, r.frames ? double(r.beats) / (double(r.frames) * 0.033) : 0.0);

    std::printf("\n  mood changes    %d over %.1f s\n", r.moodChanges, double(r.frames) * 0.033);
    for (int i = 0; i < 5; ++i) {
        if (r.moodFrames[i] == 0) continue;
        std::printf("    %-10s %5.1f%% of frames\n", moodToString(MoodType(i)),
                    r.frames ? double(r.moodFrames[i]) * 100.0 / double(r.frames) : 0.0);
    }
    if (!r.dwells.empty()) {
        std::vector<unsigned long> sorted = r.dwells;
        std::sort(sorted.begin(), sorted.end());
        unsigned long sum = 0;
        for (unsigned long d : sorted) sum += d;
        std::printf("    dwell ms   min %lu  median %lu  mean %lu  max %lu\n",
                    sorted.front(), sorted[sorted.size() / 2], sum / sorted.size(),
                    sorted.back());
    }

    std::printf("\n  churn%% is the mean frame-to-frame change as a fraction of the\n");
    std::printf("  value's own range. Above about 10%% the value is crossing a tenth of\n");
    std::printf("  its spread every frame, which is what a flickering reading is.\n");
}

int replayCapture(const char* path) {
    std::vector<std::vector<float>> blocks;
    std::string why;
    if (!loadCapture(path, blocks, why)) {
        std::printf("%s is not usable: %s\n", path, why.c_str());
        return 1;
    }
    if (blocks.empty()) {
        std::printf("%s holds no frames\n", path);
        return 1;
    }
    printCapture(path, analyzeCapture(blocks));
    return 0;
}

// The capture format is the only channel between the browser and here, so it gets
// its own check. Without one, a change to the header layout or the block order
// would show up as every tuning number being wrong rather than as a failure.
void checkReplay() {
    constexpr float kTwoPi  = 6.28318530718f;
    constexpr float kToneHz = 2.0f * float(SAMPLE_RATE) / float(NUM_SAMPLES);

    std::vector<float> tone(NUM_SAMPLES);
    for (int i = 0; i < NUM_SAMPLES; ++i) {
        tone[i] = 0.4f * std::sin(kTwoPi * kToneHz * float(i) / float(SAMPLE_RATE));
    }
    std::vector<float> silence(NUM_SAMPLES, 0.0f);

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "gg-replay-check";
    std::filesystem::create_directories(dir);
    const std::filesystem::path tonePath    = dir / "tone.f32";
    const std::filesystem::path silencePath = dir / "silence.f32";
    const std::filesystem::path junkPath    = dir / "junk.bin";

    const int toneFrames    = 200;
    const int silenceFrames = 100;
    writeCapture(tonePath.string().c_str(),    std::vector<std::vector<float>>(size_t(toneFrames), tone));
    writeCapture(silencePath.string().c_str(), std::vector<std::vector<float>>(size_t(silenceFrames), silence));

    {
        std::FILE* fp = std::fopen(junkPath.string().c_str(), "wb");
        if (fp != nullptr) {
            std::fwrite("not a capture at all", 1, 20, fp);
            std::fclose(fp);
        }
    }

    std::vector<std::vector<float>> blocks;
    std::string why;
    const bool loaded = loadCapture(tonePath.string().c_str(), blocks, why);
    record("a capture round-trips through the file", loaded && blocks.size() == size_t(toneFrames),
           loaded ? std::to_string(blocks.size()) + " frames read back"
                  : "load refused it: " + why);

    std::vector<std::vector<float>> unused;
    std::string junkWhy;
    record("a file that is not a capture is refused",
           !loadCapture(junkPath.string().c_str(), unused, junkWhy) && unused.empty(),
           "refused with: " + junkWhy);

    if (loaded && blocks.size() == size_t(toneFrames)) {
        const ReplayReport r = analyzeCapture(blocks);
        record("a replayed capture reports every frame", r.frames == toneFrames,
               std::to_string(r.frames) + " frames reported");

        int moodTotal = 0;
        for (int i = 0; i < 5; ++i) moodTotal += r.moodFrames[i];
        record("a replayed capture classifies every frame", moodTotal == toneFrames,
               std::to_string(moodTotal) + " of " + std::to_string(toneFrames) +
               " frames carried a mood");

        const Range* level = r.find("level");
        record("a steady tone replays as a steady level",
               level != nullptr && level->hi > 0.9f && level->churnShare() < 0.1f,
               level ? "level " + std::to_string(level->lo) + " to " + std::to_string(level->hi) +
                       ", churn " + std::to_string(level->churnShare() * 100.0f) + "%"
                     : "level was not tracked");

        record("a replayed tone reads as signal present", r.presence.mean() > 0.9f,
               std::to_string(r.presence.mean() * 100.0f) + "% of frames");
    }

    std::vector<std::vector<float>> quiet;
    std::string quietWhy;
    if (loadCapture(silencePath.string().c_str(), quiet, quietWhy)) {
        const ReplayReport r = analyzeCapture(quiet);
        const Range* bpm = r.find("bpm");
        record("a replayed silence reads as no signal", r.presence.mean() < 0.2f,
               std::to_string(r.presence.mean() * 100.0f) + "% of frames");
        record("a replayed silence ends with no tempo", bpm != nullptr && bpm->last < 1.0f,
               bpm ? "final bpm " + std::to_string(bpm->last) : "bpm was not tracked");
    } else {
        record("a replayed silence reads as no signal", false, "silence capture would not load: " + quietWhy);
        record("a replayed silence ends with no tempo", false, "silence capture would not load: " + quietWhy);
    }

    std::error_code ignored;
    std::filesystem::remove_all(dir, ignored);
}

}  // namespace

int main(int argc, char** argv) {
    bool        verbose = true;
    const char* dumpDir = nullptr;
    const char* replayPath = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--plain") == 0) colour  = false;
        if (std::strcmp(argv[i], "--quiet") == 0) verbose = false;
        if (std::strcmp(argv[i], "--dump-frames") == 0 && i + 1 < argc) {
            dumpDir = argv[++i];
        }
        if (std::strcmp(argv[i], "--replay") == 0 && i + 1 < argc) {
            replayPath = argv[++i];
        }
    }

    // A replay is a measurement of the real microphone, not a check, so it runs
    // on its own and skips the watchdog and the assertions entirely.
    if (replayPath != nullptr) return replayCapture(replayPath);

    enableVt();
    randomSeed(20260916);

    // Recording shares the stub layer and the real controller with the checks, but
    // not their result: there is nothing to assert about a frame dump, so it runs
    // on its own and skips the watchdog and the checks entirely.
    if (dumpDir != nullptr) return dumpFrames(dumpDir, verbose);

    std::thread watchdog(watchdogMain);

    std::printf("GlitchGlimmer scene and layer harness\n");
    std::printf("-------------------------------------\n");

    setPhase("checkCatalog");        checkCatalog();
    setPhase("checkCompositor");     checkCompositor();
    setPhase("checkLayerCap");       checkLayerCap();
    setPhase("checkHistorySizing");  checkHistorySizing();
    setPhase("checkLifecycle");      checkLifecycle(verbose);
    setPhase("checkAnimationSweep"); checkAnimationSweep(verbose);
    setPhase("checkMeasuredLevel");  checkMeasuredLevelBrightness(verbose);
    setPhase("checkLayerSweep");     checkLayerSweep(verbose);
    setPhase("checkSoak");           checkSoak(verbose);
    setPhase("checkSceneTransitions"); checkSceneTransitions();
    setPhase("checkAudioProcessor"); checkAudioProcessor();
    setPhase("checkReplay");         checkReplay();

    g_watchdogRun.store(false, std::memory_order_relaxed);
    watchdog.join();

    std::printf("\n-------------------------------------\n");
    int failed = 0;
    for (const Check& c : g_checks) {
        std::printf("  %-4s %s\n", c.passed ? "ok" : "FAIL", c.name.c_str());
        if (!c.passed) {
            ++failed;
            if (!c.detail.empty()) std::printf("       %s\n", c.detail.c_str());
        }
    }
    std::printf("-------------------------------------\n");
    std::printf("%d of %d checks passed\n",
                static_cast<int>(g_checks.size()) - failed,
                static_cast<int>(g_checks.size()));

    // Sizes name the allocator. With both history buffers now fixed-capacity
    // rings, no steady-state size dominates any more: what is left is the layer
    // vectors growing to their operating size and the harness's own scaffolding.
    // A new large constant size appearing here is new information.
    if (!g_checks.empty()) {
        std::printf("\nallocation sizes (whole run)\n");
        std::printf("  %zu allocations, %zu frees, %zu live at end\n",
                    g_allocCount, g_freeCount, g_allocCount - g_freeCount);
        std::printf("  %zu from AudioHistoryTracker::addSnapshot, %zu from ctrl.update()\n",
                    g_historyAllocs, g_controllerAllocs);
        if (g_sizeOverflow) {
            std::printf("  %zu allocations of sizes past the %d-entry table\n",
                        g_sizeOverflow, kSizeSlots);
        }
        for (int rank = 0; rank < g_sizeUsed; ++rank) {
            int best = -1;
            for (int i = 0; i < g_sizeUsed; ++i) {
                if (g_sizes[i].count == 0) continue;
                if (best < 0 || g_sizes[i].count > g_sizes[best].count) best = i;
            }
            if (best < 0) break;
            std::printf("  %6zu bytes  x%-6zu\n", g_sizes[best].bytes, g_sizes[best].count);
            g_sizes[best].count = 0;
        }
    }

    return failed == 0 ? 0 : 1;
}
