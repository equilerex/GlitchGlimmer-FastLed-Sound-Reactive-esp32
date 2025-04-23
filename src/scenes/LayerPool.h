#pragma once

#include <functional>
#include <vector>
#include <memory>

#include "../scenes/LayerTypes.h"
#include "../animations/VisualLayer.h"

// This defines a reusable pool of known layer templates
// SceneDirector can instantiate layers by type or by name/tag/etc.
class LayerPool {
public:
    // A single layer template with metadata
    struct Entry {
        LayerType type;
        std::function<VisualLayer*()> factory;
        String name;
    };

    std::vector<Entry> entries;

    void registerLayer(LayerType type, std::function<VisualLayer*()> factory, const String& name) {
        entries.push_back({type, factory, name});
    }

    std::vector<Entry> getByType(LayerType type) const {
        std::vector<Entry> result;
        for (const auto& e : entries) {
            if (e.type == type) result.push_back(e);
        }
        return result;
    }

    Entry getRandomByType(LayerType type) const {
        auto list = getByType(type);
        if (list.empty()) return {LayerType::BASE, nullptr, "None"};
        return list[random(list.size())];
    }

    void clear() { entries.clear(); }
};

