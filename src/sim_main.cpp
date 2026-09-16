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
#include <string>
#include <thread>
#include <vector>

#include "config/Config.h"
#include "audio/AudioFeatures.h"
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
    f.bass             = constrain((bSum / bassLimit) / 100.0f, 0.0f, 1.0f);
    f.mid              = constrain((mSum / (midLimit - bassLimit)) / 80.0f, 0.0f, 1.0f);
    f.treble           = constrain((tSum / (half - midLimit)) / 50.0f, 0.0f, 1.0f);
    f.energy           = eTotal;
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

    // energy is the raw FFT magnitude sum on device, so it is in the thousands,
    // not 0..1. The classifier's thresholds are what they are; this only needs a
    // snapshot that lands on INTENSE, and 2000 with dynamics 0.8 does.
    MoodSnapshot mood;
    mood.energy   = 2000.0f;
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

int main(int argc, char** argv) {
    bool verbose = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--plain") == 0) colour  = false;
        if (std::strcmp(argv[i], "--quiet") == 0) verbose = false;
    }

    enableVt();
    randomSeed(20260916);

    std::thread watchdog(watchdogMain);

    std::printf("GlitchGlimmer scene and layer harness\n");
    std::printf("-------------------------------------\n");

    setPhase("checkCatalog");        checkCatalog();
    setPhase("checkCompositor");     checkCompositor();
    setPhase("checkLayerCap");       checkLayerCap();
    setPhase("checkHistorySizing");  checkHistorySizing();
    setPhase("checkLifecycle");      checkLifecycle(verbose);
    setPhase("checkAnimationSweep"); checkAnimationSweep(verbose);
    setPhase("checkLayerSweep");     checkLayerSweep(verbose);
    setPhase("checkSoak");           checkSoak(verbose);
    setPhase("checkSceneTransitions"); checkSceneTransitions();

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
