# Backlog — logged, not yet scoped
<!-- Unordered. Promote to next-steps.md when an item gets a real slot. See AGENTS.md. -->

## <Item title>

Status: OPEN

<!-- Status is one of: OPEN | DESIGNED | BLOCKED | MOVED | DROPPED
     BLOCKED names what it waits on. MOVED says where. DROPPED says why.
     Status never appears in the heading. Headings are undated topic titles. -->

<Body.>

## Inert audio tuning macros in Config.h

Status: OPEN

BEAT_THRESHOLD, MIN_BEAT_INTERVAL, GAIN_SMOOTHING, LOUDNESS_SMOOTHING, NOISE_THRESHOLD, FFT_SMOOTHING and MAX_AUDIO_LEVEL have zero uses outside Config.h. The live values are hardcoded instead (0.85f in AudioProcessor.h:23, 250 inline in the beat path). MIN_BEAT_INTERVAL declares 300 while the code uses 250, so the documented knob and the real one disagree. Deferred until the loop structure is fixed, because tuning sensitivity against a broken frame budget measures the wrong thing.

## Mood classifier runs on level alone

Status: OPEN

AudioProcessor.cpp:79 computes 'double avg = sum / NUM_SAMPLES' and never uses it. Neither features.average nor features.dynamics is ever assigned (AudioFeatures.h:9,18), so both stay 0 and the classifier sees level and nothing else. Level cannot separate a punchy track from a steady loud one. This is the gap the audio reference calls out: route level into brightness and dynamics into motion, not one axis into everything.

## Per-pixel transcendental math in layers

Status: OPEN

VisualLayers.h:256 runs 'exp(-pow(...))' inside a per-LED loop. VisualLayers.h:231 and :127 call sin per LED. float arguments promote to software double, and ESP32 has no hardware double FPU. The cost scales with LED count, so it is invisible on a 10-LED strip and dominant on a 300-LED one.

## Dead code and uninstantiated classes

Status: OPEN

LayerPool is never instantiated anywhere, so its getByType() returning std::vector<Entry> by value is latent rather than live. AudioHistoryTracker::getRecent() returns std::deque<AudioSnapshot> by value, roughly 60KB per call, and has zero callers. MoodReactiveAnimation.h does not compile (mood.centroid and moodHistory.latest() do not exist) and is excluded from AnimationCatalog.h, so it silently rots. AlienSquirtTrailLayer is unused. SettingIconWidget is declared in DisplayManager.h:67 but never constructed. ScrollingTextWidget is never instantiated.

## Verbose ESP-IDF logging left on

Status: OPEN

platformio.ini sets -DCORE_DEBUG_LEVEL=5, which is verbose. It costs cycles and floods the serial console during timing work.
