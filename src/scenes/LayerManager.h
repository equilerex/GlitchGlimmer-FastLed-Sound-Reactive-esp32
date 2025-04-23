#pragma once

#include <vector>
#include <FastLED.h>

#include "LayerTypes.h"
#include "../audio/AudioFeatures.h"
#include "../audio/AudioSnapshot.h"
#include "../animations/VisualLayer.h"

class LayerManager {
public:
    struct LayerInstance {
        VisualLayer* layer = nullptr;
        unsigned long startTime = 0;
        unsigned long duration = 0;
        LayerType type;
        bool active = true;

        bool isExpired(unsigned long now) const {
            return duration > 0 && (now - startTime > duration);
        }
    };

private:
    std::vector<LayerInstance> layers;
    CRGB* leds = nullptr;
    int ledCount = 0;

public:
    void setLEDs(CRGB* buffer, int count) {
        leds = buffer;
        ledCount = count;
    }

    void clearLayers() {
        for (auto& l : layers) {
            delete l.layer;
        }
        layers.clear();
    }

    void updateLayers(const AudioFeatures& audio, const std::deque<AudioSnapshot>& history) {
        unsigned long now = millis();
        for (auto& l : layers) {
            if (l.active && l.layer) {
                l.layer->update(audio, history);
            }
        }
        // Clean up expired layers
        layers.erase(std::remove_if(layers.begin(), layers.end(),
            [now](const LayerInstance& l) {
                if (l.isExpired(now)) {
                    delete l.layer;
                    return true;
                }
                return false;
            }), layers.end());
    }

    void renderLayers() {
        if (!leds) return;
        for (auto& l : layers) {
            if (l.active && l.layer) {
                l.layer->render(leds, ledCount);
            }
        }
    }

    void addLayer(VisualLayer* layer, LayerType type = LayerType::OVERLAY, unsigned long durationMs = 0) {
        LayerInstance inst;
        inst.layer = layer;
        inst.startTime = millis();
        inst.duration = durationMs;
        inst.type = type;
        inst.active = true;
        layers.push_back(inst);
    }

    int countLayersOfType(LayerType t) const {
        return std::count_if(layers.begin(), layers.end(), [t](const LayerInstance& l) {
            return l.type == t;
        });
    }

    bool hasActiveLayerOfType(LayerType t) const {
        return std::any_of(layers.begin(), layers.end(), [t](const LayerInstance& l) {
            return l.type == t;
        });
    }
};
