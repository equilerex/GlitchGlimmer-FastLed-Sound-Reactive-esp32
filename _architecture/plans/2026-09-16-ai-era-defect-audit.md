# AI-era defect audit

Session: 2026-09-16. Status: findings recorded and verified against source, no fixes applied.

## Context

Three reported symptoms prompted this: animations crash after a while, the microphone performs badly on less powerful controllers, and the display was suspected of locking or synchronous problems. The repo's history says the bulk of it was AI-generated when models were still weak at FastLED and embedded C++, so the working assumption was that the defects are characteristic of that era rather than random.

A graphify knowledge graph was built over the repo first (`graphify-out/`, 729 nodes, 1195 edges, 55 communities). Its suggested questions pointed at `AudioFeatures` bridging 35 communities, `DisplayManager` bridging six widget communities, and 239 weakly-connected nodes around `mood` / `preferredTempo` / `intensity`. Those turned out to be structural symptoms of the same underlying faults rather than independent leads.

Three parallel audits followed, one per symptom area. Every CRITICAL finding below was then re-verified by reading the source directly. Where a claim was reported but not independently confirmed, it is marked as such.

Intended outcome: an ordered defect list good enough to plan the fix from, and a record of what the old code got wrong so the same shapes are not reintroduced.

## Context correction

The display-locking hypothesis was half wrong and worth stating plainly, because it changes the fix.

There is no locking anywhere. `grep` finds zero `beginTransaction` / `endTransaction` and zero mutexes in `src/`. The TFT is on pins 18/19/5/16/23, the LEDs on 25/33, the mic I2S on 26/27/32. No pin or bus overlap, and FastLED drives the strip through RMT, not SPI. Nothing contends for a shared resource.

What exists instead is a single `loop()` doing everything in series with no FreeRTOS tasks in `src/` at all. The problem is wall-clock monopoly, not arbitration. Every symptom below follows from that plus one hard bug.

## Correction after the fixes

Added once the fixes landed, because finding 1 was wrong about causality and the error was load-bearing.

Finding 1 calls the per-frame layer leak the crash. It is a real defect in the code, but it never ran. `SceneDirector::attachState` had no call sites anywhere in `src/`, so `state` stayed null in every director instance. `begin()` and `update()` both early-return on `!state`, and `getActiveScene()` returns `state ? state->activeScene : nullptr`, so it returned null on every call. `LEDStripController.h:190` guards the whole per-strip block on `scenePtr != nullptr`, which means `setAnimation`, `setScene`, `applySceneLayers` and `strips[i].update()` never executed at all. The leak's allocation site was unreachable.

The consequence is larger than the retraction. With that block dead, the only per-frame LED work was a `fadeToBlackBy` over `FastLED.leds()`, which addresses the first registered controller only, plus `FastLED.show()`. Strip 0 faded black to black and strip 1 was never written. The installation as committed draws nothing. Whatever crashed on hardware was therefore not this revision, and that crash still has no cause. Finding 2 describes the heap-gate ordering accurately but inherits the same problem, since the leak it blames never depleted the heap.

The layer defects are real, they were just dormant rather than firing. Every scene carried the identical `{OVERLAY, REACTIVE}` preset, `BassShockwaveLayer` had no constructor and so inherited `opacity = 1.0f`, and `renderLayers` capped rendering at 3 layers while `updateLayers` updated all of them. The reported symptom, the animation vanishing as layers stack, is explained by the compositor rather than by accumulation. Layers render additively into a black buffer, and `nblend(scratch, layerBuf, alpha)` at alpha 255 replaces the destination, so each layer painted the base black wherever it drew nothing. That is a compositing bug producing the same visible result as the leak the audit blamed, which is likely why the two were conflated.

Findings 3, 4 and 5 were all verified against code that does execute, and stand unchanged.

## Findings

> Finding 1's causal claim is retracted, see the correction above. The defect described is real but was unreachable.

### 1. Layers are re-added every frame and never freed (CRITICAL, verified)

The crash. `LEDStripController.h:182` calls `strips[i].layers().applySceneLayers(scene)` inside the per-strip loop on every frame. `applySceneLayers` (`LayerManager.cpp:179-184`) does a bare `new` per layer type via `addLayerByType` (`:191-227`). `addLayer` (`:146-154`) sets `inst.durMs = duration`, and `duration` defaults to `0` (`LayerManager.h:37-39`). `addLayer` has no dedupe guard, and `hasActiveLayerOfType()` exists at `:162` but is never called from it or from `applySceneLayers`.

`LayerInstance::expired()` is `durMs > 0 && (now - startMs > durMs)` (`:22-24`). With `durMs == 0` it returns false permanently, so the `remove_if` at `:52-58` erases nothing and the layer vector only grows.

Volume: `SceneRegistry.cpp:9` sets `layerTypes = {OVERLAY, REACTIVE}`. `stripCount == 2` (`Config.h:36-37` define `LED_0_PIN` and `LED_1_PIN`, with `LED_2` through `LED_9` commented out). The frame gate is 33 ms (`MainController.cpp:291`). That is 2 types times 2 strips times 30 fps, so roughly 120 `VisualLayer` heap objects per second, forever. Each carries a `String name` deep copy and each `emplace_back` risks a vector regrowth. Heap exhaustion in tens of seconds matches "crashes after a while" exactly.

Compounding effect: `LayerManager.cpp:44-48` calls `update()` on every accumulated layer, so the virtual-call count climbs monotonically. A watchdog reset or frame collapse is likely to arrive before the allocation failure does.

`SceneDirector.h:88-90` holds a `MAX_LAYERS = 4` cap, but `maybeInjectReactiveLayer()` is never called from anywhere, so the only rate limit in the design is dead code.

### 2. Microphone failure is downstream of finding 1 (verified)

This is the non-obvious one, and it is the actual answer to "why is the mic bad on weaker controllers".

`MainController.cpp:320` gates audio behind `ESP.getFreeHeap() > 20 * 1024`. `MainController.cpp:297` drops to an LED-only branch below `10 * 1024`, but that branch still calls `ledController->update()`, which is the leaking path from finding 1.

So as the leak drains the heap, audio is switched off first, display second, and the leak runs to completion unimpeded. The low-memory guard cannot stop the thing causing the low memory. A board with less free-heap headroom crosses the 20 KB line sooner, which is why the symptom is reported against weaker controllers specifically. The microphone is not slow here. It is being disabled by an unrelated leak.

### 3. Display repaints the whole screen every frame (CRITICAL cost, verified)

`DisplayManager.cpp:156-157` runs `_tft.fillScreen(TFT_BLACK)` followed by a full `layout.draw(_tft)` unconditionally, and `MainController.cpp:382` calls it every gated frame. The panel is 240x135 at 16bpp, so 64,800 bytes over SPI at 40 MHz is in the region of 13 to 16 ms of blocking transfer, repeated every 33 ms frame, to redraw a screen that is mostly unchanged.

`GridLayout.h:36-40` adds a second `fillScreen` on its own 500 ms timer, so those frames pay roughly 32 ms of clears in one frame.

`GridLayout.h:82-92` caps drawing at 4 widgets per frame while 7 widgets exist, then resets `lastDrawnWidget` each pass. Combined with the per-frame clear, the half not painted this frame is black, so the widgets visibly blink at roughly 11 to 15 Hz while the code pays for two full screen clears.

`GridLayout.h:51` allocates `std::vector<WidgetPosition>` per frame and recomputes a layout that is derived entirely from compile-time-constant `getMinWidth()` and `getMinHeight()`.

### 4. Double free at shutdown (verified)

`DisplayManager.cpp:27-38` does `bassBar.reset(new VerticalBarWidget(...))` and then `layout.addWidget(bassBar.get())`. `addWidget` (`GridLayout.h:26`) constructs a second `unique_ptr` from that same raw pointer, so one object has two owners.

`GridLayout layout` is declared at `DisplayManager.h:55`, before the widget `unique_ptr`s at `:60-66`. Members destruct in reverse declaration order, so the `unique_ptr`s free first and `GridLayout`'s vector then deletes already-freed memory.

### 5. Audio pipeline (verified unless noted)

- `src/main.ino:19` calls `setCpuFrequencyMhz(160)`, commented "Lower CPU frequency for stability". The board is a 240 MHz part, so the firmware runs at two thirds clock for no stated benefit. This is the cheapest single win in the whole list.
- `AudioProcessor.cpp:7` constructs `ArduinoFFT<double>`. ESP32 has no hardware double FPU, so every `double` multiply and add compiles to a software call sequence. `vReal`, `vImag` and `spectrum` are all `double`.
- `AudioProcessor.cpp:49` calls `i2s_read(..., portMAX_DELAY)` for 2048 bytes inside the main loop. The DMA ring is `dma_buf_count = 8` by `dma_buf_len = 64`, which is 512 samples, or 11.6 ms of audio. The read happens once per 33 ms frame, so the ring fills and stalls, each call waits about 11.6 ms for a fresh block, and the roughly 21 ms in between is discarded. The result is sample-and-hold at 30 Hz with 11.6 ms windows, so most transients fall in the gap. `portMAX_DELAY` also means a miswired or dead microphone blocks forever rather than failing visibly.
- `AudioProcessor.cpp:53-61` loops `i < count && i < NUM_SAMPLES`. On a short read the tail of `vReal` still holds the previous frame's data, which splices two signals into one FFT frame.
- `AudioProcessor.cpp:79` computes `double avg = sum / NUM_SAMPLES` and never uses it. Neither `features.average` nor `features.dynamics` is ever assigned, so both stay at their defaults of 0 (`AudioFeatures.h:9,18`). The mood classifier therefore has level and nothing else to work with.
  - Fixed since, 2026-09-18: `features.average` is assigned at `AudioProcessor.cpp:198` and `features.dynamics` at `:375`. The finding as written is a record of that date, not of the current build.
- Every audio tuning macro is inert. `BEAT_THRESHOLD`, `MIN_BEAT_INTERVAL`, `GAIN_SMOOTHING`, `LOUDNESS_SMOOTHING`, `NOISE_THRESHOLD`, `FFT_SMOOTHING` and `MAX_AUDIO_LEVEL` have zero references outside `Config.h`. The live values are hardcoded, `0.85f` at `AudioProcessor.h:23` and `250` inline. `MIN_BEAT_INTERVAL` declares 300 while the code uses 250.
  - Corrected in place, 2026-09-18: two of those facts were wrong when they were written. `BEAT_THRESHOLD` does not exist in `Config.h` at all, and `MIN_BEAT_INTERVAL` declares 250 and is used, at `AudioProcessor.cpp:477`. The macros with no call site are `NOISE_THRESHOLD`, `MAX_AUDIO_LEVEL`, `GAIN_SMOOTHING`, `LOUDNESS_SMOOTHING` and `FFT_SMOOTHING`, and the disagreement is `GAIN_SMOOTHING`'s `0.92f` against the live `0.85f` at `AudioProcessor.h:22`.
- Reported but not independently confirmed: the `arduinoFFT` constructor's `windowingFactors` argument defaults to false in the installed 2.0.4, which would force the window to be recomputed every frame rather than cached. The library lives in the user's Arduino libraries directory, outside this repo, so the claim could not be checked here.

## What this says about the era

The defects share one shape, and it is a shape worth naming because it is what to watch for when reading the rest of this codebase.

Every fault above is locally reasonable and globally wrong. Adding a layer when a scene asks for one is reasonable. Reading audio once per frame is reasonable. Clearing the screen before drawing it is reasonable. Caching FFT constants is reasonable. What the old code never did was ask what happens on the second call, or the ten-thousandth. There is no counter anywhere in this codebase. No cap on layers, no dirty flag on a widget, no bound on a read, no check that a knob is connected to anything. The `MAX_LAYERS = 4` cap and the entire `Config.h` audio block show the intent was there, and that the wiring between intent and execution was the part that got skipped.

The second pattern is the silent failure. `features.average` is never assigned. `MIN_BEAT_INTERVAL` declares 300 and the code uses 250. `MoodReactiveAnimation.h` does not compile, which is invisible because `AnimationCatalog.h` does not include it. `LayerPool` is never instantiated. `getRecent()` returns a 60 KB deque by value and has zero callers. `settingIconWidget` is declared and never constructed. None of this crashes, so none of it ever got noticed.

The third pattern is the double ownership in `setupLayout`, which is the one place where the old code reached for a modern C++ idiom (`unique_ptr`) and got the ownership model exactly backwards. Mixing `unique_ptr` members with a container that also adopts raw pointers is worse than raw pointers alone, because it looks safe.

The current audit found all of this in one pass, from reading, with no hardware. That is the honest measure of the distance. The old code is not stupid, it is unverified, and verification was the step that was unavailable then and is cheap now.

## Build order

Sequenced so each step is verifiable on its own. The first two are the crash and should land before any tuning work, because tuning against a leaking heap measures the wrong thing.

1. Stop the layer leak. Rebuild the layer list on scene change rather than per frame, and make `addLayerByType` set a real duration or guard it with `hasActiveLayerOfType`. This is the crash and everything else is secondary.
2. Trim the display. Drop the per-frame `fillScreen`, make widget draws value-dirty, remove the 4-per-frame cap in `GridLayout.h:82-92`.
3. Restore 240 MHz in `src/main.ino:19`.
4. Fix the double ownership in `setupLayout`.
5. Switch `ArduinoFFT` to `float` and decouple `i2s_read` from the frame gate.
6. Re-examine the heap-gate ordering in `MainController.cpp:297-320`. It exists to cope with a leak that will no longer be there, and its current ordering silently trades the microphone away first.
7. Revisit the backlog items. The inert macros and the dead `average` / `dynamics` fields become worth fixing only once the frame budget is real.
