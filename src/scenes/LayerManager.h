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

// LayerManager: manages and composites multiple visual layers onto the LED buffer
class LayerManager {
public:
    struct LayerInstance {
        std::unique_ptr<VisualLayer> layer;
        unsigned long startMs;
        unsigned long durMs;
        LayerType type;
        bool active;
        bool expired(unsigned long now) const; // moved to .cpp
    };

    LayerManager();                                    // ctor initializes internal state
    void setLEDs(CRGB* buf, size_t count);             // assign LED buffer and size
    void setLength(size_t count);                      // change active software length
    void clearLayers();                                // remove all layers

    void updateLayers(const AudioFeatures& now,
                      const AudioHistory& hist); // update and prune

    void renderLayers(uint8_t globalFade = 10);        // fade & blend each layer

    void addLayer(VisualLayer* raw,
                  LayerType type = LayerType::OVERLAY,
                  unsigned long duration = 0);        // takes ownership of layer

    int activeCount() const;                           // currently live layers
    bool hasActiveLayerOfType(LayerType t) const;      // check for type
    int countLayersOfType(LayerType t) const;          // count by type
    LayerType getLayerType(int index) const;
    const char* getLayerName(int index) const;
    unsigned long getLayerElapsedMs(int index) const;

    template<typename... Args>
    void addLayerByType(LayerType t, Args&&... args);  // instantiates layer by enum

    void applySceneLayers(const SceneDefinition& sd);  // add layers for a scene

private:
    std::vector<LayerInstance> layers;
    CRGB* leds;
    size_t ledCnt;
    std::vector<CRGB> scratch;
    // Per-instance, not a function-local static. There is one LayerManager per
    // strip, so a static here was one buffer shared by every strip, resized
    // twice per frame when the strips differ in length.
    std::vector<CRGB> layerBuf;
};

// Note: all method implementations have been moved to LayerManager.cpp
