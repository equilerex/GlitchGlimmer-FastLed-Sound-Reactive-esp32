# CONTEXT — scenes
updated: 2026-09-21

<!-- All four sections stay even when briefly empty — an absent section is
     indistinguishable from an omission. -->

## What this is

Scene selection and the layer compositor. `SceneRegistry` holds one scene per catalog animation, `SceneDirector` decides when to switch and what to add on top, `SceneState` is the per-strip running state, `LayerManager` and `LayerPool` composite the accents.

## Why it's built this way

Live audio takes the music-space path: `pickSceneByMusic` ranks by distance to each animation's profile (`animations/AnimationProfile.h`), and the director switches only when a rival wins by a margin for a sustained time, or on a structural event. The mood ladder and `pickSceneByMood` remain for snapshots built by hand, which carry no coordinates. See `_architecture/plans/decisions/001-select-scenes-by-distance-in-music-space.md`.

`SceneState::recent` holds the last three animations left, so the selector can lean away from them. Cooldown stamps for layer injection are members of the director, not function statics, so two directors do not share one.

Scene layers (`SceneDefinition::layerTypes`) are static background/ambient beds with `durMs = 0` (persisting until scene transition). Reactive transient layers (drops, beat accents, energy rises) are injected dynamically via `SceneDirector::maybeInjectReactiveLayer` into `strips[i].layers()` with explicit durations.

What those layers answer is defined in `docs/music-research/MODEL.md` and summarised in `_architecture/ARCHITECTURE.md`, "Musical events and musical states". A drop has an onset event, whose layers are one-shots with their own envelope, and a drop window that follows it. A buildup, descent, drop window, tease or anomaly is an episode decided by the firmware (`src/audio/StructuralEpisodes.h`), and its layer follows that episode.

`SceneDirector::attachEpisodeLayers` attaches the layer once while the episode is open and `LayerManager::updateLayers` releases it, with a 400 ms fade, the frame the firmware's episode is idle or has a new `episodeId`. There is no timer and no cooldown on them. A layer lost to a scene change is attached again next frame while its episode is open. The onset fires its `HIGHLIGHT` and `ENERGY` one-shots once per episode per strip (`impactFiredFor` on the strip's own manager, so a shared director does not spend the edge on the first strip) and they expire on their own 2.5 s and 3.5 s. The sustained drop layer (`ENERGY_SPIRAL`) attaches only once the window is confirmed.

Layer classes decide eviction at the four layer cap: accent, scene, overlay, impact, section, lowest first. A full manager drops a releasing layer first, then the oldest of the lowest class, and only for a strictly higher newcomer. So a buildup can evict a scene layer, and an accent never evicts a section. `AUTO` gives a layer with no duration the scene class and one with a duration the accent class.

Which layer each episode gets is a visual choice and may change: buildup `BUILDUP_SWELL`, descent `DESCENT_COOL` (both new, both read the episode's `elapsedMs`), drop window `ENERGY_SPIRAL`, tease `MOOD_ARC`, anomaly `DYNAMICS_FLICKER_STORM`. `TensionRamp` in `BeatClock.h` follows the buildup episode, so Rising Tension, Strobe Pulse and Pop Fade develop over its length; Cosmic Chaos reads the drop window and the buildup state. A hand-built block with a positive `f.buildup` and no episode still works, because the harness builds features that way.

## Gotchas

`SceneDirector::attachState` has to be called or `update` returns immediately and nothing draws.

`LEDStripController::update` must call `sceneDirector.maybeInjectReactiveLayer(strips[i].layers(), audio, now)` or transient visual accents will never trigger.

A scene's structural tags come from the catalog entry's mood, and only structural moods are copied. A ladder tag on a scene would win its mood outright and the ranking would never be consulted.

`sceneDistance` includes the recency penalty and exempts the running scene. The director compares the running scene's distance with the best rival's on that same number, so do not compute one of them another way.

## Don't

Do not assign high-energy or full-screen reactive layers (like `TriwaveBeatLayer` or unattenuated `NoiseFloorMistLayer`) as permanent `durMs=0` scene layers. Keep scene beds minimal so base animations can breathe. Do not remove the ladder fallback until the harness builds coordinates in its fixtures. Do not call `mood.update()` from the director; the controller already advances it each frame.
