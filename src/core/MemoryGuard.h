#pragma once

#include <stdint.h>

#include "../config/Config.h"

// Heap pressure as a pure state machine. Readings are passed in, so the host
// harness drives it with fake numbers and this header never calls the ESP heap
// APIs. loop() is the only place that samples the device.

enum class MemoryPressure {
    OK,
    DEGRADED,
    CRITICAL
};

struct MemoryGuardSample {
    bool changed;
    MemoryPressure pressure;
    bool restart;
};

inline const char* memoryPressureName(MemoryPressure pressure) {
    if (pressure == MemoryPressure::DEGRADED) return "DEGRADED";
    if (pressure == MemoryPressure::CRITICAL) return "CRITICAL";
    return "OK";
}

class MemoryGuard {
public:
    MemoryGuard() : pressure_(MemoryPressure::OK), criticalSinceMs_(0) {}

    MemoryGuardSample update(uint32_t freeHeap, uint32_t largestBlock, uint32_t nowMs) {
        const uint32_t worst = freeHeap < largestBlock ? freeHeap : largestBlock;
        const MemoryPressure next = nextPressure(pressure_, worst);

        MemoryGuardSample out;
        out.changed = next != pressure_;
        out.pressure = next;
        if (next == MemoryPressure::CRITICAL) {
            if (pressure_ != MemoryPressure::CRITICAL) criticalSinceMs_ = nowMs;
            out.restart = static_cast<uint32_t>(nowMs - criticalSinceMs_) >= HEAP_CRITICAL_RESTART_MS;
        } else {
            out.restart = false;
        }
        pressure_ = next;
        return out;
    }

    MemoryPressure pressure() const { return pressure_; }

private:
    static MemoryPressure nextPressure(MemoryPressure current, uint32_t worst) {
        if (current == MemoryPressure::CRITICAL) {
            if (worst < HEAP_CRITICAL_EXIT_BYTES) return MemoryPressure::CRITICAL;
            if (worst < HEAP_DEGRADED_EXIT_BYTES) return MemoryPressure::DEGRADED;
            return MemoryPressure::OK;
        }
        if (worst < HEAP_CRITICAL_ENTER_BYTES) return MemoryPressure::CRITICAL;
        if (current == MemoryPressure::DEGRADED) {
            if (worst < HEAP_DEGRADED_EXIT_BYTES) return MemoryPressure::DEGRADED;
            return MemoryPressure::OK;
        }
        if (worst < HEAP_DEGRADED_ENTER_BYTES) return MemoryPressure::DEGRADED;
        return MemoryPressure::OK;
    }

    MemoryPressure pressure_;
    uint32_t criticalSinceMs_;
};

inline MemoryGuard& memoryGuard() {
    static MemoryGuard guard;
    return guard;
}
