# CONTEXT — scenes
updated: 28-09-2026 21:33

<!-- All four sections stay even when briefly empty — an absent section is
     indistinguishable from an omission. -->

## What this is

Scene selection and the layer compositor. `SceneRegistry` holds one scene per catalog animation and scores them against the moods, `SceneDirector` decides when to switch and what to add on top, `SceneState` is the running scene's clock, `LayerManager` composites the layers.

## Why it's built this way

The base scene follows the moods the firmware measures (`audio/Moods.h`). Each frame `SceneRegistry::score` scores every scene from its fit list (`animations/AnimationFit.h`), the best few are the bucket, and the director draws from it: rotation at the ideal duration, an early change when the scene has left the bucket, and a drop base on a confirmed drop. The whole design is in `_architecture/ARCHITECTURE.md`, "Moods and scene selection", and the calls made building it are in `_architecture/plans/2026-09-28-base-layer-mood.md`.

`SceneState::recent` holds the last three animations left, so the score can lean away from them. The draw uses a xorshift the director owns (`seed`), so the harness can fix it. The bucket, the scores and the reason the running scene is up are readable from the director, and the emulator shows them.

Scene layers (`SceneDefinition::layerTypes`) are the fixed list from the animation's fit row, `durMs = 0`, persisting until the scene changes. Everything else is added to `strips[i].layers()` by `SceneDirector::feedLayers`.

What those layers answer is defined in `docs/music-research/MODEL.md` and summarised in `_architecture/ARCHITECTURE.md`, "Musical events and musical states". A drop has an onset event, whose layers are one-shots with their own envelope, and a drop window that follows it. A buildup, descent, drop window, tease or anomaly is an episode decided by the firmware (`src/audio/StructuralEpisodes.h`), and its layer follows that episode.

`SceneDirector::attachEpisodeLayers` attaches the layer once while the episode is open and `LayerManager::updateLayers` releases it, with a 400 ms fade, the frame the firmware's episode is idle or has a new `episodeId`. There is no timer and no cooldown on them. A layer lost to a scene change is attached again next frame while its episode is open. The onset fires its `HIGHLIGHT` and `ENERGY` one-shots once per episode per strip (`impactFiredFor` on the strip's own manager) and they expire on their own 2.5 s and 3.5 s. The sustained drop layer (`ENERGY_SPIRAL`) attaches only once the window is confirmed.

Layer classes decide eviction at the four layer cap: accent, scene, overlay, impact, section, lowest first. A full manager drops a releasing layer first, then the oldest of the lowest class, and only for a strictly higher newcomer. So a buildup can evict a scene layer, and an accent never evicts a section. `AUTO` gives a layer with no duration the scene class and one with a duration the accent class. Every layer records why it is up (`LayerWhy`: scene, episode, edge, mood, beat, shift), and the emulator shows it.

Which layer each episode gets is a visual choice and may change: buildup `BUILDUP_SWELL`, descent `DESCENT_COOL`, drop window `ENERGY_SPIRAL`, tease `MOOD_ARC`, anomaly `DYNAMICS_FLICKER_STORM`. `TensionRamp` in `BeatClock.h` follows the buildup episode, so Rising Tension, Strobe Pulse and Pop Fade develop over its length; Cosmic Chaos reads the drop window and the buildup state. A hand-built block with a positive `f.buildup` and no episode still works, because the harness builds features that way.

## Gotchas

The base scene follows the moods. A confirmed drop replaces it for the hold, and nothing else does. Buildup, descent, tease and anomaly add layers and do not move the base except through the episode weight of the scenes that have an entry for them.

`ENERGY_SPIRAL` is the confirmed drop window, not an accent. It stays for the whole episode, up to `STRUCT_DROP_MAX_MS` (60 s), at opacity 1, and `render` adds a saturated value onto every LED. The accents are 450 ms to 1200 ms, and the four-layer cap evicts the lowest class first, so a section layer keeps its slot and accents often never appear. Owner noted this on 28-09-2026. No change yet. The open item is cyu5 Energy spiral reads as the whole show.

`SceneDirector::attachState` has to be called or `update` returns immediately and nothing draws.

`LEDStripController::update` must call `sceneDirector.feedLayers(strips[i].layers(), audio, now)` or the episode layers and accents will never trigger.

A mood row nobody has a fit for (Syncopated today) is left out of the score's average, so it moves nothing. Adding a fit entry for it is what makes it count.

Fixtures built by hand need mood strengths, or every scene scores the same. The harness builds them with `attachFixtureMusic` through the firmware's own `MoodModel`.

## Don't

Do not assign high-energy or full-screen reactive layers (like `TriwaveBeatLayer` or unattenuated `NoiseFloorMistLayer`) as permanent `durMs=0` scene layers. Keep scene beds minimal so base animations can breathe. Do not give an animation 1.0 on two moods. Do not put a layer with a reserved meaning (`MOOD_ARC`, `DYNAMICS_FLICKER_STORM`, `HIGHLIGHT` with `ENERGY`, the buildup, descent and edge layers) in the live table.
