# Decision 001 — Select scenes by distance in music space

Date: 2026-09-21

Status: DECIDED

<!-- Status is one of: DECIDED | TRIAL | REJECTED | DEFERRED | SUPERSEDED
     A superseding decision gets its own number. The superseded file's status changes
     and its body gains a pointer — it is never edited away or deleted.
     All five sections below are required. -->

## Problem

Scenes were picked from a one-dimensional loudness ladder (five rungs plus structural moods). Loudness cannot say which of a dozen quiet or loud animations suits a passage, so the catalog's variety went unused, and the picker's tie-break was a tempo value that the animations only loosely honoured. The music engine now reports eight coordinates with confidence, and the animations were written before any of them existed.

## Options considered

1. Keep the ladder and add more rungs. Still one axis; a fine ladder is a louder and quieter list, not a choice between looks.
2. Tag each animation with categorical labels (rhythmic, ambient, bass-heavy) and match tags. Cheap, but a tag is a yes or no and the music is a position, so two scenes tagged alike could not be ranked.
3. Give each animation a target in the coordinate space and rank by weighted distance to the music. Ranks continuously, and an animation can ignore an axis it does not care about.

## Decision

Option 3. `AnimationProfile.h` holds seven targets per animation (intensity, activity, brightness, weight, pulse, tempo, texture) and `kAny` for an axis it ignores. `profileDistance` weights each axis by the analyser's confidence in that coordinate, floored at 0.35 so an unsettled coordinate still counts a little, and doubles intensity. `SceneRegistry::pickSceneByMusic` returns the nearest scene that is not the running one, narrowing to scenes tagged for a structural event when one is reported, and adds a small penalty for the last three scenes left. `SceneDirector::update` switches on two conditions: a structural event after a 1.5 s dwell, or a rival that beats the running scene by 0.12 for 1.5 s once the minimum duration has passed. A scene that stays best is still rotated at twice its ideal duration.

Snapshots built by hand carry no coordinates (`MusicState::initialized` is false), so the mood ladder and `pickSceneByMood` stay as the fallback for them. The classifier is kept because the structural moods still come from it.

## Why not the alternatives

The ladder is kept only as a fallback because the harness and its fixtures build snapshots by hand. Tags were rejected because they cannot rank. A pure nearest-scene rule with no margin was rejected because two scenes at similar distance would trade places on every frame; the margin and the challenge time are what stop that.

## Next step

The 44 profiles are first estimates from reading each animation, not measurements. Tune them in the browser on real audio (`?source=demo`, then a live microphone), and record which ones move. Remove the ladder fallback only when the harness builds coordinates in its fixtures.
