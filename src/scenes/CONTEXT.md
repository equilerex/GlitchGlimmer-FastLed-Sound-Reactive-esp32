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

## Gotchas

`SceneDirector::attachState` has to be called or `update` returns immediately and nothing draws.

`LEDStripController::update` must call `sceneDirector.maybeInjectReactiveLayer(strips[i].layers(), audio, now)` or transient visual accents will never trigger.

A scene's structural tags come from the catalog entry's mood, and only structural moods are copied. A ladder tag on a scene would win its mood outright and the ranking would never be consulted.

`sceneDistance` includes the recency penalty and exempts the running scene. The director compares the running scene's distance with the best rival's on that same number, so do not compute one of them another way.

## Don't

Do not assign high-energy or full-screen reactive layers (like `TriwaveBeatLayer` or unattenuated `NoiseFloorMistLayer`) as permanent `durMs=0` scene layers. Keep scene beds minimal so base animations can breathe. Do not remove the ladder fallback until the harness builds coordinates in its fixtures. Do not call `mood.update()` from the director; the controller already advances it each frame.
