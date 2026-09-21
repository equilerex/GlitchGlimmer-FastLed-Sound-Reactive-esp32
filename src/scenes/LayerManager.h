#pragma once

#include <vector>
#include <memory>
#include <cstdint>
#include "LayerTypes.h"
#include "../animations/visual-layers/VisualLayer.h"  // Include the full definition of VisualLayer

// Forward declarations to minimize header dependencies. CRGB is not among them:
// VisualLayer.h above already pulls in FastLED.h, and on the browser build that
// header brings fl::CRGB into the global namespace, which a second declaration of
// the name here would make ambiguous.
struct AudioFeatures;
struct SceneDefinition;

// How much a layer matters when the cap is reached. A full manager makes room for a
// layer by dropping one of a strictly lower class, a releasing one first, then the
// oldest of the lowest class, and refuses the newcomer when nothing ranks below it.
// A musically important layer is therefore never evicted because it is the oldest.
// AUTO is the default and resolves to SCENE for a layer with no duration and ACCENT
// for one with a duration, which is what the two callers that predate the classes
// meant.
enum class LayerClass : uint8_t {
    ACCENT  = 0,    // beat pops and other passing flares
    SCENE   = 1,    // the layers a scene asks for, which live until it changes
    OVERLAY = 2,    // tease, anomaly and the surge flare
    IMPACT  = 3,    // the drop onset's one-shots
    SECTION = 4,    // buildup, descent and the drop window
    AUTO    = 255
};

// LayerManager: manages and composites multiple visual layers onto the LED buffer
class LayerManager {
public:
    struct LayerInstance {
        std::unique_ptr<VisualLayer> layer;
        unsigned long startMs;
        unsigned long durMs;
        LayerType type;
        bool active;
        LayerClass cls = LayerClass::ACCENT;
        // Set for a layer that lives exactly as long as one structural episode. The
        // signal is a StructSignal and the id is the firmware's episodeId, so a layer
        // can tell its own episode from the next one of the same signal.
        int      ownerSignal = -1;
        uint32_t ownerId = 0;
        float    gain = 1.0f;          // scales opacity, e.g. by dropConfidence
        bool     releasing = false;
        unsigned long releaseStartMs = 0;
        bool expired(unsigned long now) const; // moved to .cpp
    };

    LayerManager();                                    // ctor initializes internal state
    void setLEDs(CRGB* buf, size_t count);             // assign LED buffer and size
    void setLength(size_t count);                      // change active software length
    void clearLayers();                                // remove all layers

    void updateLayers(const AudioFeatures& now,
                      const AudioHistory& hist); // update and prune

    void renderLayers(uint8_t globalFade = 10);        // fade & blend each layer

    // Takes ownership. Returns whether the layer was kept, which is false when the
    // manager is full and nothing in it ranks below the newcomer.
    bool addLayer(VisualLayer* raw,
                  LayerType type = LayerType::OVERLAY,
                  unsigned long duration = 0,
                  LayerClass cls = LayerClass::AUTO);

    int activeCount() const;                           // currently live layers
    bool hasActiveLayerOfType(LayerType t) const;      // check for type
    int countLayersOfType(LayerType t) const;          // count by type
    LayerType getLayerType(int index) const;
    const char* getLayerName(int index) const;
    unsigned long getLayerElapsedMs(int index) const;

    bool addLayerByType(LayerType t, unsigned long duration = 0,
                        LayerClass cls = LayerClass::AUTO);  // instantiates layer by enum with optional duration

    // A layer bound to one episode. It has no duration of its own: updateLayers()
    // starts its release the moment the firmware's episode for `signal` is no longer
    // the one with `episodeId`, and the release is a short fade and not a cut.
    bool addOwnedLayerByType(LayerType t, LayerClass cls, int signal,
                             uint32_t episodeId, float gain = 1.0f,
                             unsigned long duration = 0);
    bool hasOwned(int signal, uint32_t episodeId) const;  // present and not releasing
    void releaseOwned(int signal);                         // release everything a signal owns
    LayerClass getLayerClass(int index) const;
    int  getLayerOwnerSignal(int index) const;
    bool isLayerReleasing(int index) const;

    // The drop onset fires its one-shots once per episode per strip. Kept here, on
    // the strip's own manager, so a director shared by several strips does not spend
    // the one edge on the first of them.
    uint32_t impactFiredFor() const { return impactId; }
    void     markImpactFired(uint32_t episodeId) { impactId = episodeId; }

    // How long a released layer takes to fade out.
    static const unsigned long kReleaseMs = 400;

    void applySceneLayers(const SceneDefinition& sd);  // add layers for a scene

    // The visualiser's layer picker. Clears everything and adds one layer with no
    // duration, then refuses every other addLayer() and ignores clearLayers() (scene
    // changes) until releaseManual(), so the layer is the only thing on the strip.
    bool triggerManual(LayerType t);
    void releaseManual();

private:
    void syncOwned(const AudioFeatures& f);
    std::vector<LayerInstance> layers;
    uint32_t impactId = 0;
    bool manualLock = false;
    CRGB* leds;
    size_t ledCnt;
    std::vector<CRGB> scratch;
    // Per-instance, not a function-local static. There is one LayerManager per
    // strip, so a static here was one buffer shared by every strip, resized
    // twice per frame when the strips differ in length.
    std::vector<CRGB> layerBuf;
};

// Note: all method implementations have been moved to LayerManager.cpp
