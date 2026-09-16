# verification harness and what it found

Session: 2026-09-16. Status: superseded in part. The harness grew and every defect this document reports as open was fixed the same day, in `inherited-booping-cerf`. The last section records the current numbers. Read the body below for why the harness is built the way it is and what it proved when it was first run. Hardware is still unverified, which is unchanged.

## Context

The codebase was largely AI-generated before current models and inherited the defects of that era. The fixes recorded in `2026-09-16-ai-era-defect-audit.md` were made by reading, and reading is exactly how that audit's headline claim went wrong. It named a per-frame layer leak as the crash, when `SceneDirector::attachState` had no call sites, so the director early-returned every call, `getActiveScene()` returned null, and the per-strip block that built those layers never ran. The leak was unreachable, and so was the entire scene path. Nothing was drawn at all.

The lesson is that this code had to be executed rather than read. This is that execution.

## What was built

Two things, and only the first is new code.

**A device build that works.** `platformio.ini` carried a `build_src_filter` that excluded `main.ino`, the only file holding `setup()` and `loop()`, and separately named `-<MainController.cpp>`, a path that does not exist because the file sits at `core/MainController.cpp`. Both are gone. The device environment now builds from `+<*>` minus `sim_main.cpp`, and `pio run -e ttgo-t1` completes for the first time:

```
RAM:   [=         ]   8.3% (used 27060 bytes from 327680 bytes)
Flash: [===       ]  32.7% (used 429009 bytes from 1310720 bytes)
```

The only warning is pre-existing, TFT_eSPI reporting `TOUCH_CS pin not defined`. Until this passed, no claim about the firmware's behaviour was anchored to anything.

**A host harness for the scene and layer lifecycle.** `pio run -e native` compiles the real `LEDStripController`, `LayerManager`, `SceneRegistry`, `SceneState`, `SceneDirector` and `MoodHistory` against a stub Arduino core (`sim/stubs/Arduino.h`), links them with `src/sim_main.cpp`, scripts an audio timeline across four moods, prints both strips as an ANSI colour bar in the terminal, and asserts the lifecycle invariants. It needs no hardware and it has no clock of its own, which is the point.

### The non-obvious part of the harness

FastLED already ships a stub platform, selected automatically for `__x86_64__` by `led_sysdefs.h:66`. Its `.cpp` files look like they supply `millis()`, `delay()`, `yield()` and `pinMode()`, and they do not. `stub/Arduino.cpp` is guarded by `#if defined(FASTLED_USE_STUB_ARDUINO)`, `stub/time_stub.cpp` by `#ifdef FASTLED_STUB_IMPL`, and the only thing that defines `FASTLED_STUB_IMPL` is `led_sysdefs_stub_generic.h`, which neither file includes. Both therefore compile to empty translation units.

That accident is what makes the design work. The harness supplies `millis()`, `micros()`, `delay()`, `yield()` and `pinMode()` itself, so it owns a clock it can step in fixed 33 ms increments and gets a bit-identical run every time. The alternative, FastLED's own `time_stub.cpp`, reads the wall clock and would have made every timing assertion flaky.

The signatures are not free choices either. `led_sysdefs_stub_generic.h` declares them inside `extern "C"`, so `unsigned long millis()` would be a conflicting declaration and fail to compile. Same for `INPUT`, `OUTPUT` and `INPUT_PULLUP`, which that header `#error`s on unless they are 0, 1 and 2.

Run it with `pio run -e native -t exec`, or `.pio/build/native/program.exe` directly. `--plain` swaps the colour bar for an ASCII ramp, `--quiet` drops the per-second strip view and prints only the checks.

## What it verified

14 checks, all passing. The first seven are unit-level and isolate one property each, so a failure names its own cause.

| Check | What it pins down |
|---|---|
| catalog index matches `AnimationType` | Every slot is index-aligned, so no lookup reads one entry early |
| every catalog entry builds an animation | No empty `std::function` survives to be called |
| a layer that draws nothing leaves the base intact | The compositor no longer lets an unlit layer erase what is under it |
| a half-opacity layer adds to the base | Opacity is a share of light, and light only goes up |
| a zero-opacity layer contributes nothing | Zero short-circuits rather than blending black |
| layer count is capped at insertion | The cap holds where layers are added, not only where they render |
| applying a scene's layers is idempotent | Scene layers cannot stack duplicates on repeat application |

The remaining seven run the real controller for 3000 frames, about 100 seconds of simulated time.

| Check | What it pins down |
|---|---|
| both configured strips are registered | Strip 0 and strip 1 both reach `begin()` |
| layer count never exceeds the cap | Across the whole run, on both strips |
| live allocations stop growing once the history buffers are full | The leak test. A layer rebuilt each frame would add 600 live allocations over the measured window |
| allocation rate is flat in steady state | The same test from the rate side |
| the scene clock advances and transitions fire | Scenes actually change, and more than one is ever reached |
| every registered strip receives light | Strip 1 is not dark, which the double-fade defect would have caused |
| steady-state live allocations are bounded | The live set stays small rather than creeping |

## What it found

**The leak is fixed.** This is the headline result and it is now measured rather than argued. Across the 600-frame steady-state window the live allocation count does not move, and both strips hold exactly 2 layers for the entire run. If the per-frame rebuild were still present, the window would show +600 live allocations and the layer count would climb to the cap and stay there. Neither happens. `setScene`'s pointer guard is doing exactly what it was written to do, rebuilding only on a real scene change.

**The heap never goes quiet, and that is new information.** The run performs 1541 allocations and 1516 frees over 3000 frames, and the sizes name the source without a profiler:

```
1541 allocations, 1516 frees, 25 live at end
255 from AudioHistoryTracker::addSnapshot, 1128 from ctrl.update()
   456 bytes  x1001
   480 bytes  x251
```

Both sizes are `std::deque` node blocks. `MoodHistory::history` stores 76-byte `MoodSnapshot`s, six to a 456-byte node, capped at 150 snapshots. `AudioHistoryTracker::history` stores 48-byte `AudioSnapshot`s, ten to a 480-byte node, capped at 1500 snapshots.

So the steady state is a 456-byte block taken and returned every 2.7 frames, about twelve times a second, plus another every 8 frames. The loop is otherwise allocation-free.

The cost is not fragmentation, and an earlier draft of this section said it was. Reallocating the same size tends to reuse the same freed block, so 456 out and 456 back in is close to self-healing rather than a heap that degrades. The live count holding flat at 25 across 600 frames is the direct evidence for that.

The real cost is the one crash path the loop has. Nothing catches an exception: `main.ino` calls `controller.update()` bare inside `loop()`, so a `std::deque::push_back` that fails throws `std::bad_alloc` out of the loop, reaches `std::terminate`, and reboots the board. `isMemoryHealthy()` does not prevent it, because `ESP.getFreeHeap() > 20KB` is a total and the allocation needs one contiguous 456-byte block. Low free heap and no room for a 456-byte block are different conditions, and only the first is checked. The churn is what makes a failure possible at all, and it is the only code in the frame that can throw.

The fix is small and obvious in hindsight: both are fixed-capacity ring buffers, and neither needs a block-allocating container. A `std::array` plus a head index, or a `std::vector` reserved once in the constructor, does the same job with zero steady-state allocations.

**A false lead worth recording.** The first version of the leak check asserted that no frame with an unchanged scene may allocate at all. That failed 504 times out of 1500, which looked like a leak and was not. Both history deques were still filling, and a growing deque allocates without freeing. The check was later rebuilt to measure the live count after both buffers saturate, which is the question that was actually being asked.

## What is still unverified

Everything electrical. The harness proves the lifecycle logic and the compositor, and it proves nothing about whether the strips light, whether the I2S mic delivers frames, whether the TFT flickers, or whether the frame budget holds on hardware. Frame time is the biggest open question, because the per-pixel `exp`, `pow` and `sin` calls in `VisualLayers.h` executed for the first time in this project's history when the scene path was wired up, and they were previously costed against code that did not run.

`pio run -e native` also compiles FastLED's stub platform in place of the ESP32 one, so nothing here exercises the RMT or I2S drivers at all.

## The fix pass that followed

Every defect above was fixed in the same session, and the harness was extended to cover the fixes rather than assuming them.

**The throw path is gone at the source.** Both `std::deque` histories became `SnapshotRing`, a fixed-capacity ring in `src/audio/SnapshotRing.h` that indexes oldest-first so it matches what `std::deque` gave the one consumer that reads it. `AudioHistory` (1500 snapshots) and `MoodHistory` (150) are now single blocks taken once. Measured result:

```
101 checks passed
1699 allocations, 1501 frees, 198 live at end
0 from AudioHistoryTracker::addSnapshot, 126 from ctrl.update()
```

The 456-byte and 480-byte rows are gone and no steady-state size dominates any more. `AudioHistoryTracker::addSnapshot` allocates zero times across a whole run. The 126 remaining are scene-transition layer rebuilds in the first 1500 frames, and the 198 live at end are the harness's own 101 checks retaining two strings each, not the firmware. Peak history usage is measured rather than reconciled: `AudioHistoryTracker` peaks at 1500 elements / 60000 bytes, `MoodHistory` at 150 / 11400 bytes.

Note that the 1541 figure above is a floor, not the device number. `sim/stubs/Arduino.h:51` aliases `String` to `std::string`, which has small-string optimisation, so any harness count of per-frame `String` churn is invisible. The device has no SSO. The ring conversion is what actually matters, and it is unaffected by that; the raw allocation total is not a device prediction.

**Nine defects fixed.** Self-transitioning scenes, a dead `SceneDefinition::activeScene`, C++20 designated initializers in a `gnu++11` build, a function-local static scratch buffer shared across strips, a brightness underflow in `neonFlow`, a negative argument to a `uint8_t` in both `AlienPulse` call sites, six unbounded float phase accumulators, missing zero-length guards in every catalog animation, and a heap monitor with no call sites. Two more surfaced while fixing those and are recorded rather than fixed: `WormholeVortexLayer` and `CentroidColorFlowLayer` are never instantiated anywhere in `src/`, and the mood classifier's `energy` thresholds are written against a 0..1 range while the device field is a raw FFT magnitude sum in the thousands.

**The soak, and what it can honestly claim.** The accumulator wraps exist because an unbounded float loses precision after hours. That failure is not reachable in a harness: `wavePhase` gains at most 0.11 a frame, so reaching float's exact-integer limit takes on the order of ten million frames. A long pass therefore cannot test the reason the wraps exist. It tests that the bound holds, which is the thing a wrong modulus breaks, so each of the four accumulator sites a build actually reaches carries a test seam and the soak asserts the value stays inside one period across 20000 frames:

```
Alien Pulse wavePhase      max 6.28317 of 6.28319
Neon Flow hueOffset        max 254.99850 of 255.00000
Squirt offset              max 6.28251 of 6.28319
EnergyPulseRiver position  max 99.99683 of 100.00000
```

Float-precision exhaustion still rests on inspection, not on this run.

**What the harness proves and still does not.** It drives all eight catalog animations at n=100 and n=8 for 1200 frames each, all eight reachable layer factories for 900, and the real controller for 3000. Every sweep reports zero second-half allocations and a flat live count. It still proves nothing electrical, and the animations' visual output cannot be confirmed without hardware.
