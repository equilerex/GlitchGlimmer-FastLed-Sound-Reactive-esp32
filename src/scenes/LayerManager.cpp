#include "../animations/VisualLayer.h"  // Make sure this is first for full definition visibility
#include "LayerManager.h"
#include <FastLED.h>
#include <algorithm>
#include <cstring>
#include <Arduino.h>
#include "SceneRegistry.h"
#include "../animations/VisualLayers.h"

// Verify that VisualLayer is properly defined with these essential members
// static_assert(std::is_member_function_pointer<decltype(&VisualLayer::update)>::value, "VisualLayer::update is not defined properly");
// static_assert(std::is_member_function_pointer<decltype(&VisualLayer::render)>::value, "VisualLayer::render is not defined properly");

// Constructor: initialize pointers and counters
LayerManager::LayerManager()
  : leds(nullptr), ledCnt(0)
{
    // No dynamic allocation here; scratch will resize when setLEDs is called
}

// Check if a layer instance has exceeded its duration
bool LayerManager::LayerInstance::expired(unsigned long now) const {
    return durMs > 0 && (now - startMs > durMs);
}

// Assign the LED buffer and number of pixels; prepare scratch buffer
void LayerManager::setLEDs(CRGB* buf, size_t count) {
    leds = buf;
    ledCnt = count;
    scratch.resize(count);
}

// Remove all active layers; unique_ptr will auto-delete each VisualLayer
void LayerManager::clearLayers() {
    layers.clear();
}

// Update each active layer with current audio features and historical snapshots
// Then prune any layers whose duration has expired
void LayerManager::updateLayers(const AudioFeatures& now,
                                const std::deque<AudioSnapshot>& hist) {
    unsigned long ts = millis();
    // Call update() on each active VisualLayer
    for (auto& inst : layers) {
        if (inst.active) {
            inst.layer->update(now, hist);
        }
    }
    // Remove expired layers in-place
    layers.erase(
        std::remove_if(
            layers.begin(), layers.end(),
            [ts](const LayerInstance& inst) { return inst.expired(ts); }
        ),
        layers.end()
    );
}

// Render all layers onto the LED buffer using alpha blending
void LayerManager::renderLayers(uint8_t globalFade) {
    // Enhanced safety checks
    if (!leds || ledCnt == 0 || ledCnt > 1000) {
        Serial.println("Invalid LED buffer in renderLayers");
        return; // nothing to draw if buffer not set or invalid
    }

    // Check if scratch buffer is the right size
    if (scratch.size() != ledCnt) {
        Serial.println("Resizing scratch buffer");
        scratch.resize(ledCnt, CRGB::Black);
    }

    // Apply global fade to avoid pixel burn-in - safely
    try {
        fadeToBlackBy(leds, ledCnt, globalFade);
    } catch (...) {
        Serial.println("Exception in fadeToBlackBy");
        return;
    }

    // Copy current LED state to scratch as base for blending - safely
    try {
        memcpy(scratch.data(), leds, sizeof(CRGB) * ledCnt);
    } catch (...) {
        Serial.println("Exception in memcpy to scratch");
        return;
    }

    // Make sure layerBuf exists and is the right size
    static std::vector<CRGB> layerBuf;
    if (layerBuf.size() != ledCnt) {
        layerBuf.resize(ledCnt, CRGB::Black);
    } else {
        std::fill(layerBuf.begin(), layerBuf.end(), CRGB::Black);
    }

    // Render each layer into layerBuf, then blend over scratch - with extra safety
    int layerCount = 0;
    for (auto& inst : layers) {
        if (!inst.active || !inst.layer) continue;

        // Limit to 3 layers max for performance
        if (layerCount >= 3) break;
        layerCount++;

        // Reset layer buffer to black
        std::fill(layerBuf.begin(), layerBuf.end(), CRGB::Black);

        // Get the layer opacity once to avoid repeated access
        float opacity = inst.layer->opacity;
        if (opacity <= 0.0f) continue; // Skip fully transparent layers

        // Safely call layer render method
        try {
            inst.layer->render(layerBuf.data(), ledCnt);
        } catch (...) {
            Serial.println("Exception in layer render");
            continue;
        }

        // Blend with alpha from layer opacity
        uint8_t alpha = uint8_t(opacity * 255);
        if (alpha == 0) continue; // Skip if fully transparent

        // Safely blend pixels
        try {
            for (size_t i = 0; i < ledCnt; ++i) {
                nblend(scratch[i], layerBuf[i], alpha);
            }
        } catch (...) {
            Serial.println("Exception in nblend");
            continue;
        }
    }

    // Commit blended result back to LEDs - safely
    try {
        memcpy(leds, scratch.data(), sizeof(CRGB) * ledCnt);
    } catch (...) {
        Serial.println("Exception in memcpy to leds");
        return;
    }
}

// Instantiate a new layer and add to the active list
void LayerManager::addLayer(VisualLayer* raw, LayerType type, unsigned long duration) {
    LayerInstance inst;
    inst.layer.reset(raw);     // take ownership
    inst.startMs = millis();
    inst.durMs = duration;
    inst.type = type;
    inst.active = true;
    layers.emplace_back(std::move(inst));
}

// Return count of currently active layers
int LayerManager::activeCount() const {
    return static_cast<int>(layers.size());
}

// Check if there is any active layer of a given type
bool LayerManager::hasActiveLayerOfType(LayerType t) const {
    return std::any_of(
        layers.begin(), layers.end(),
        [t](const LayerInstance& inst) { return inst.active && inst.type == t; }
    );
}

// Count how many layers of a specific type are present
int LayerManager::countLayersOfType(LayerType t) const {
    return static_cast<int>(
        std::count_if(
            layers.begin(), layers.end(),
            [t](const LayerInstance& inst) { return inst.type == t; }
        )
    );
}

// Apply a scene's layer types by instantiating each via factory template
void LayerManager::applySceneLayers(const SceneDefinition& sd) {
    for (LayerType t : sd.layerTypes) {
        addLayerByType(t); // duration=0 => live until expired
    }
}

// Template implementation for addLayerByType
// This function needs to be included in the .cpp file to be available to calling code

// Must include implementations of the template function for each LayerType
template<typename... Args>
void LayerManager::addLayerByType(LayerType t, Args&&... args) {
    VisualLayer* layer = nullptr;
    
    // Factory function to create the appropriate layer based on type
    switch(t) {
        case LayerType::BASE:
            layer = new EnergyPulseRiverLayer();
            break;
        case LayerType::BACKGROUND:
            layer = new NoiseFloorMistLayer();
            break;
        case LayerType::OVERLAY:
            layer = new CentroidRadianceLayer();
            break;
        case LayerType::REACTIVE:
            layer = new BassShockwaveLayer();
            break;
        case LayerType::HIGHLIGHT:
            layer = new BeatFlashSparkLayer();
            break;
        case LayerType::ENERGY:
            layer = new EnergyFogLayer();
            break;
        case LayerType::MOOD_ARC:
            layer = new MoodMemoryArcLayer();
            break;
        case LayerType::TRANSITION:
            layer = new TrebleSparkleLayer();
            break;
        default:
            layer = new SpectralRibbonLayer(); // Fallback
            break;
    }
    
    if (layer) {
        addLayer(layer, t);
    }
}

// Explicit template instantiations for the types used in the codebase
template void LayerManager::addLayerByType<>(LayerType t);
