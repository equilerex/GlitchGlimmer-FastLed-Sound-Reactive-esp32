# TODO
<!-- Live working set. `jookoi-paper-trail flush` archives it and resets it. See AGENTS.md. -->

## Context

ESP32 sound-reactive LED firmware (PlatformIO, board `ttgo-t1`, FastLED for LEDs on pins 25/33, TFT_eSPI on 18/19/5/16/23, INMP441 I2S mic on 26/27/32). The codebase was largely AI-generated before current models, and inherited the defects of that era.

On 2026-09-16 a three-way audit (animations/lifecycle, audio, display) ran, driven by a graphify knowledge graph in `graphify-out/`. It confirmed one CRITICAL defect — a per-frame `new` of `VisualLayer` objects that are never freed, leaking ~120 objects/sec — plus a per-frame full-screen TFT repaint and a double free at shutdown. Full findings: `plans/2026-09-16-ai-era-defect-audit.md`.

Nothing is fixed yet. The next step is a fix plan, then the fixes.

## Checklist

- [x] Build graphify knowledge graph (`graphify-out/`)
- [x] Audit animations and layer lifecycle
- [x] Audit audio and mic pipeline
- [x] Audit display and blocking paths
- [x] Set up paper trail
- [ ] Write the fix plan from the audit findings
- [ ] Fix: `applySceneLayers` re-adds layers every frame and `expired()` never fires (`LayerManager.cpp:22-24,146-154,179-184`, called from `LEDStripController.h:182`) — the crash
- [ ] Fix: drop the per-frame `fillScreen` and the 4-widgets-per-frame cap (`DisplayManager.cpp:156-157`, `GridLayout.h:36-40,82-92`)
- [ ] Fix: restore CPU to 240 MHz (`src/main.ino:19` calls `setCpuFrequencyMhz(160)`)
- [ ] Fix: double ownership of widgets in `setupLayout` (`DisplayManager.cpp:27-38` + `GridLayout.h:26`)
- [ ] Fix: `ArduinoFFT<double>` to `float`, and decouple `i2s_read` from the 30 FPS frame gate (`AudioProcessor.cpp:7,49`)
- [ ] Re-check the heap-gate ordering in `MainController.cpp:297-320` once the leak is gone
- [ ] Write `ARCHITECTURE.md` — deferred until the fix reshapes the loop
