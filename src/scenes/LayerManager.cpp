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
    if (manualLock) return;   // a scene change must not remove the picked layer
    layers.clear();
}

// Update each active layer with current audio features and historical snapshots
// Then prune any layers whose duration has expired
void LayerManager::updateLayers(const AudioFeatures& now,
                                const AudioHistory& hist) {
    unsigned long ts = millis();
    // Before anything is drawn, so a layer whose episode has ended starts fading on
    // the same frame the firmware ended it.
    syncOwned(now);
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
            [ts](const LayerInstance& inst) {
                return inst.expired(ts) ||
                       (inst.releasing && ts - inst.releaseStartMs >= kReleaseMs);
            }
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
    ::memcpy(scratch.data(), leds, sizeof(CRGB) * ledCnt);

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
        float share = inst.layer->opacity * inst.gain;
        if (inst.releasing) {
            const unsigned long gone = millis() - inst.releaseStartMs;
            share *= gone >= kReleaseMs ? 0.0f : 1.0f - float(gone) / float(kReleaseMs);
        }
        if (share > 1.0f) share = 1.0f;
        uint8_t alpha = uint8_t(share * 255.0f);
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
    ::memcpy(leds, scratch.data(), sizeof(CRGB) * ledCnt);
}

// Instantiate a new layer and add to the active list
bool LayerManager::addLayer(VisualLayer* raw, LayerType type, unsigned long duration,
                            LayerClass cls) {
    if (cls == LayerClass::AUTO) {
        cls = duration == 0 ? LayerClass::SCENE : LayerClass::ACCENT;
    }

    if (raw == nullptr) return false;
    if (manualLock) { delete raw; return false; }

    // Cap here rather than at render time. A layer past the cap still gets an
    // update() every frame, so refusing it at insertion is the honest limit. A full
    // manager makes room for a layer that outranks something in it, and only then.
    if (layers.size() >= kMaxLayers) {
        int victim = -1;
        for (size_t i = 0; i < layers.size(); ++i) {
            if (layers[i].releasing) { victim = int(i); break; }   // leaving anyway
        }
        if (victim < 0) {
            for (size_t i = 0; i < layers.size(); ++i) {
                // Strictly lower, and the first found is the oldest of its class.
                if (victim < 0 || layers[i].cls < layers[size_t(victim)].cls) victim = int(i);
            }
        }
        if (victim < 0 ||
            (!layers[size_t(victim)].releasing && layers[size_t(victim)].cls >= cls)) {
            delete raw;
            return false;
        }
        layers.erase(layers.begin() + victim);
    }

    LayerInstance inst;
    inst.layer.reset(raw);     // take ownership
    inst.startMs = millis();
    inst.durMs = duration;
    inst.type = type;
    inst.active = true;
    inst.cls = cls;
    layers.emplace_back(std::move(inst));
    return true;
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

bool LayerManager::triggerManual(LayerType t) {
    if (t < LayerType::BASE || t >= LayerType::COUNT) return false;
    manualLock = false;
    layers.clear();
    const bool ok = addLayerByType(t, 0, LayerClass::SECTION);
    manualLock = ok;
    return ok;
}

void LayerManager::releaseManual() {
    manualLock = false;
    layers.clear();
}

// Implementation for addLayerByType with optional duration
bool LayerManager::addLayerByType(LayerType t, unsigned long duration, LayerClass cls) {
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
        case LayerType::BUILDUP_SWELL:
            layer = new BuildupSwellLayer();
            break;
        case LayerType::DESCENT_COOL:
            layer = new DescentCoolLayer();
            break;
        default:
            layer = new SpectralRibbonLayer(); // Fallback
            break;
    }
    
    return layer ? addLayer(layer, t, duration, cls) : false;
}

bool LayerManager::addOwnedLayerByType(LayerType t, LayerClass cls, int signal,
                                       uint32_t episodeId, float gain,
                                       unsigned long duration) {
    if (!addLayerByType(t, duration, cls)) return false;
    // addLayer() appends, and an eviction only ever shortens the list first, so the
    // new instance is the last one whichever way the count moved.
    LayerInstance& inst = layers.back();
    inst.ownerSignal = signal;
    inst.ownerId = episodeId;
    inst.gain = gain < 0.0f ? 0.0f : (gain > 1.0f ? 1.0f : gain);
    return true;
}

bool LayerManager::hasOwned(int signal, uint32_t episodeId) const {
    for (const LayerInstance& inst : layers) {
        if (inst.ownerSignal == signal && inst.ownerId == episodeId && !inst.releasing) return true;
    }
    return false;
}

void LayerManager::releaseOwned(int signal) {
    const unsigned long ts = millis();
    for (LayerInstance& inst : layers) {
        if (inst.ownerSignal == signal && !inst.releasing) {
            inst.releasing = true;
            inst.releaseStartMs = ts;
        }
    }
}

// A layer belongs to the episode it was made for. Once the firmware's episode for
// that signal is idle, or is a different one, the layer starts fading, even if a
// layer of its own kind would have had time left.
void LayerManager::syncOwned(const AudioFeatures& f) {
    const unsigned long ts = millis();
    for (LayerInstance& inst : layers) {
        if (inst.ownerSignal < 0 || inst.releasing) continue;
        const EpisodeStatus& e = f.episode[inst.ownerSignal];
        if (e.state == EP_IDLE || e.episodeId != inst.ownerId) {
            inst.releasing = true;
            inst.releaseStartMs = ts;
        }
    }
}

LayerClass LayerManager::getLayerClass(int index) const {
    return index >= 0 && index < static_cast<int>(layers.size())
        ? layers[size_t(index)].cls : LayerClass::ACCENT;
}

int LayerManager::getLayerOwnerSignal(int index) const {
    return index >= 0 && index < static_cast<int>(layers.size())
        ? layers[size_t(index)].ownerSignal : -1;
}

bool LayerManager::isLayerReleasing(int index) const {
    return index >= 0 && index < static_cast<int>(layers.size()) &&
           layers[size_t(index)].releasing;
}
