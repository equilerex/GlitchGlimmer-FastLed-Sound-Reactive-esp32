#include "../animations/visual-layers/VisualLayer.h"  // Make sure this is first for full definition visibility
#include "LayerManager.h"
#include <FastLED.h>
#include <algorithm>
#include <cstring>
#include <Arduino.h>
#include "SceneRegistry.h"
#include "../animations/visual-layers/VisualLayers.h"

namespace {
// Ceiling on active layers. Scene layers live until the scene changes, so this is
// the real bound on per-frame compositing cost.
constexpr size_t kMaxLayers = 4;
}

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

void LayerManager::setLength(size_t count) {
    ledCnt = count;
    scratch.resize(count);
    layerBuf.resize(count);
}

// Remove all active layers; unique_ptr will auto-delete each VisualLayer
void LayerManager::clearLayers() {
    layers.clear();
}

// Update each active layer with current audio features and historical snapshots
// Then prune any layers whose duration has expired
void LayerManager::updateLayers(const AudioFeatures& now,
                                const AudioHistory& hist) {
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

// Fade the strip for trails, then add each layer's light on top of it
void LayerManager::renderLayers(uint8_t globalFade) {
    // Enhanced safety checks
    if (!leds || ledCnt == 0 || ledCnt > 10000) {
        Serial.println("Invalid LED buffer in renderLayers");
        return; // nothing to draw if buffer not set or invalid
    }

    // Check if scratch buffer is the right size
    if (scratch.size() != ledCnt) {
        Serial.println("Resizing scratch buffer");
        scratch.resize(ledCnt, CRGB::Black);
    }

    // Apply global fade to avoid pixel burn-in
    fadeToBlackBy(leds, ledCnt, globalFade);

    // Copy current LED state to scratch as base for blending
    memcpy(scratch.data(), leds, sizeof(CRGB) * ledCnt);

    // Make sure layerBuf exists and is the right size
    if (layerBuf.size() != ledCnt) {
        layerBuf.resize(ledCnt, CRGB::Black);
    } else {
        std::fill(layerBuf.begin(), layerBuf.end(), CRGB::Black);
    }

    // Composite the layers additively. Every layer renders into a black buffer, so
    // its unlit pixels are black -- blending that buffer over the base with
    // nblend(scratch, layerBuf, 255) would repaint the base black wherever the
    // layer drew nothing, which is why the animation kept disappearing. Stacking
    // emitters adds light; opacity scales how much light the layer contributes.
    for (auto& inst : layers) {
        if (!inst.active || !inst.layer) continue;

        // Opacity is this layer's share of the light. Zero means it contributes
        // nothing, so skip the render entirely rather than blending black.
        uint8_t alpha = uint8_t(inst.layer->opacity * 255.0f);
        if (alpha == 0) continue;

        std::fill(layerBuf.begin(), layerBuf.end(), CRGB::Black);
        inst.layer->render(layerBuf.data(), ledCnt);

        // nscale8_video, not nscale8: it rounds dim values up, so an accent fades
        // out smoothly instead of lingering then dumping to black.
        for (size_t i = 0; i < ledCnt; ++i) {
            scratch[i] += layerBuf[i].nscale8_video(alpha);
        }
    }

    // Commit blended result back to LEDs
    memcpy(leds, scratch.data(), sizeof(CRGB) * ledCnt);
}

// Instantiate a new layer and add to the active list
void LayerManager::addLayer(VisualLayer* raw, LayerType type, unsigned long duration) {
    // Cap here rather than at render time. A layer past the cap still gets an
    // update() every frame, so refusing it at insertion is the honest limit.
    if (raw == nullptr || layers.size() >= kMaxLayers) {
        delete raw;
        return;
    }

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

LayerType LayerManager::getLayerType(int index) const {
    if (index >= 0 && index < static_cast<int>(layers.size())) {
        return layers[index].type;
    }
    return LayerType::BASE;
}

const char* LayerManager::getLayerName(int index) const {
    if (index >= 0 && index < static_cast<int>(layers.size())) {
        if (layers[index].layer) {
            return layers[index].layer->getName();
        }
        return layerTypeToString(layers[index].type);
    }
    return "—";
}

unsigned long LayerManager::getLayerElapsedMs(int index) const {
    if (index >= 0 && index < static_cast<int>(layers.size())) {
        return millis() - layers[index].startMs;
    }
    return 0;
}

// Apply a scene's layer types by instantiating each via factory template
void LayerManager::applySceneLayers(const SceneDefinition& sd) {
    for (LayerType t : sd.layerTypes) {
        // Scene layers live until the scene changes, so they never expire on their
        // own. Guard the add or repeated calls stack duplicates onto the heap.
        if (hasActiveLayerOfType(t)) continue;
        addLayerByType(t); // durMs=0 => lives until clearLayers() on scene change
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
        // The concrete layers. Same shape as the roles above: one value names one
        // class, so a scene that wants a specific alternative does not have to name a
        // role and hope. See LayerTypes.h for why both kinds exist.
        case LayerType::DOMINANT_BAND_FIRE_TRAIL:
            layer = new DominantBandFireTrailLayer();
            break;
        case LayerType::DYNAMICS_FLICKER_STORM:
            layer = new DynamicsFlickerStormLayer();
            break;
        case LayerType::TRIWAVE_BEAT:
            layer = new TriwaveBeatLayer();
            break;
        case LayerType::ENERGY_SPIRAL:
            layer = new EnergySpiralLayer();
            break;
        case LayerType::DOMINANT_BAND_TRAIL:
            layer = new DominantBandTrailLayer();
            break;
        case LayerType::WAVEFORM_SCRIBBLE:
            layer = new WaveformScribbleLayer();
            break;
        case LayerType::WORMHOLE_VORTEX:
            layer = new WormholeVortexLayer();
            break;
        case LayerType::LOUDNESS_LIGHTNING:
            layer = new LoudnessLightningLayer();
            break;
        case LayerType::CENTROID_GLOW_WIPE:
            layer = new CentroidGlowWipeLayer();
            break;
        case LayerType::BPM_WAVE_PULSE:
            layer = new BPMWavePulseLayer();
            break;
        case LayerType::BPM_BEAT_FLASH:
            layer = new BPMBeatFlashLayer();
            break;
        case LayerType::CENTROID_COLOR_FLOW:
            layer = new CentroidColorFlowLayer();
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
