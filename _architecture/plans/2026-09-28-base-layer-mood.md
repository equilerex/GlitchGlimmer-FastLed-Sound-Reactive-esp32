# Base layer mood

Session: 28-09-2026 and 29-09-2026. Status: built 29-09-2026, steps 1 and 4 to 9. Steps 2 and 3 (recordings with mood marks, tuning the breakpoints on them) are the owner's and are open. Every breakpoint, time constant and fit is a draft.

## Context

The base scene has been chosen by whatever name `MoodType` happened to hold. That enum mixed the character of the music with episodes (buildup, descent, tease, anomaly, drop window). A track is almost always rising or falling, so scenes tagged for a buildup or a descent kept winning and the character of the music never got a turn. The symptom fixes since then (tease out of the enum, buildup, descent and weird no longer returned by `classifyMood`) are not a design. This plan is the design.

Sources: `docs/music-research/MODEL.md` and `concepts.json`. Concept numbers below are theirs.

## Decisions

Owner calls, 28 and 29-09-2026. The rest of the plan implements these.

1. A mood is a named nature of the music: floaty, intense, flowing, heavy and so on. Each is defined by readings the research says are measurable. Emotional labels (dreamy, ominous, euphoric, concept 16) are not moods.
2. The list of moods is open. The earlier five were how the code happened to be set up. Adding a mood is adding a row with its measurement.
3. Music has each mood to a degree between 0 and 1, several at once. Nothing about the base is decided by one boolean.
4. An animation fits several moods, each to a degree. Animations form overlapping sets. A silence animation may also suit floaty.
5. Loudness does not choose the mood or the scene. It is the gain reference.
6. Band balance separates very different music: all bands full and level, heavy bass with few highs, bright highs with little bass. Tempo and the other readings separate further.
7. Episodes (buildup, descent) add weight to the base pick but never define it. Tease, anomaly and the rest are layers only.
8. The drop is a distinct event and the only episode that forces a base change, and only once it is identified. While the drop window is open and unconfirmed, the drop adds layers and the base stays.
9. Silence is a state of the sound, not an episode, and counts toward the base like a mood. Quiet is not silence. The device is a wearable and never goes fully dark. What quiet and silent passages look like today (Moonlight, Forest Canopy) is right and stays.
10. The base rotates within the fitting set while the mood holds. Picking the same scene for the same reading is too repetitive for music.
11. Each scene keeps a fixed layer list, tuned by hand, because a kind of music fits a kind of animation. Live layers fill the remaining slots.
12. No prescribed order of passages.
13. Nothing is tuned against the synthetic signal. Every threshold is checked on a `.f32` recorded from the microphone.
14. Microphone calibration is the owner's, done in the web emulator, and much of it is already done. The plan does not build an adaptive baseline.
15. Tempo is reliable enough to use. It is not dismissed for half and double errors, and DnB is where that is checked.
16. After a confirmed drop, the drop base holds at least 4 to 5 s, ends early after that on a drastic shift (the sound cuts off, the rhythm changes, the BPM changes), and is forced to end at 10 to 15 s.
17. The music is whatever people dance or vibe to: mostly EDM, techno and drum and bass, also melodic rock, organic and chill. Probably not jazz.

## Readings

What the moods are measured from. Checked in the code on 28-09-2026.

| Reading | Source | Relative or absolute | Concept |
|---|---|---|---|
| activity | spectral flux over a decaying maximum (`AudioProcessor::updateMusicState`) | relative to recent flux | 2 |
| texture | spectral flatness, a ratio | gain-free, room-coloured | 2, 15 |
| brightness | spectral centroid, a ratio | gain-free, room-coloured | 15 |
| pulse | beat-interval regularity. Cannot tell a strong beat from a regular one | absolute | 3 |
| tempo | bpm. Reliable per the owner's calibration. Half-time reads on DnB checked on recordings | absolute, in BPM | 3 |
| punch (new) | rise in the low-band share block to block, the kick's attack, from `subBands[0..1]` | relative to recent punch | 3 (low-band flux, H) |
| body (new) | share of the mid bands, `subBands[2..4]`, where guitars, keys and vocals sit | gain-free | 15 |
| dynamics | span the loudness covered in the last seconds | relative | 1 |
| presence | `gateGain`, level against the noise floor | level test | 17 |
| tilt (new) | low share minus high share of `subBands[8]` (`spectralTilt` exists) | gain-free, room-coloured | 15 |
| evenness (new) | how equal the eight `subBands` shares are | gain-free, room-coloured | 2, 15 |

- Tilt, evenness, punch and body become coordinates on `MusicState`, with value, confidence and trend like the others. Concept 15 lists band balance as a cue and concept 3 lists low-band flux as a pulse cue, so they meet the rule that a new reading names its concept. Punch rests on a hypothesis grade, so the recordings decide whether it stays. Evenness is not flatness: flatness is per bin and reads noise, evenness is per band and reads a full mix.
- `weight` (`bassLevel`) is not used by moods. It compares bass with the room's own recent bass, so it reads near 1 whenever the bass is near its recent peak, on a bass-heavy track and a thin one alike.
- `intensity` is not used by moods or selection. It is 40% absolute `level`. It stays on `MusicState` for display.
- The room-coloured readings (texture, brightness, tilt, evenness, body) are compared against fixed per-device values from the owner's calibration, not against the last few seconds, which would make a bass-heavy track look balanced against itself, and not against a slow adaptive average, which would make a genre played for hours look average.

## Moods

Draft, not validated. "High" and "low" are against the calibration for room-coloured readings, in BPM for tempo, and on the reading's own 0 to 1 scale otherwise.

Flowing and Driving, from the research (the owner deferred this to it). Both have a strong regular pulse. Concept 3's groove hypothesis (a), grade H, names low-frequency flux and event density beside pulse clarity, and concept 1 names tempo and onset rate as arousal cues. So Driving is a strong pulse with a punchy low end, a percussive kick hitting the low bands, at a higher onset rate. Flowing is the same pulse with the low end sustained rather than struck, a rolling bassline, and moderate activity. The reading that separates them is punch, and tempo leans them apart. Syncopation would separate them better and is not measurable yet.

| Mood | Sounds like | Measured as | Concept | Measurable now |
|---|---|---|---|---|
| Floaty | pads, ambient, no felt beat | pulse low, activity low, dynamics low | 2, 3 | yes |
| Calm | gentle and slow, a soft beat | pulse present, tempo low, activity low, tilt not low | 1, 3 | yes |
| Flowing | a groove that rolls rather than hits, house, organic house | pulse high, tempo 110 to 128 BPM, activity mid, tilt low, punch low to mid | 3 | yes, punch at grade H |
| Driving | relentless four-on-the-floor, techno | pulse high, tempo 125 to 150 BPM, activity mid to high, punch high | 1, 3, 15 | yes, punch at grade H |
| Rushing | fast breakbeats over heavy sub, drum and bass | tempo 160 BPM and up, activity high, tilt low | 1, 3, 15 | yes, if half-time reads are rare |
| Warm | guitars, keys and voices in front, melodic rock, organic | body high, evenness mid to high, brightness mid, pulse mid (live drums drift) | 3, 15 | yes |
| Intense | everything busy and full, peak sections | activity high, evenness high, brightness high, dynamics high | 1, 2, 15 | yes, medium reliability |
| Heavy | dark and low, halftime bass music | tilt low (bass-heavy), brightness low, tempo any | 15 | yes |
| Bright | airy leads, shimmering highs, thin low end | tilt high, brightness high | 15 | yes |
| Full | a wall of sound across the range | evenness high | 2, 15 | yes |
| Chaotic | dense, noisy, irregular, glitch | texture flat, activity high, pulse low, presence high | 2, 3 | yes |
| Sparse | minimal, few elements, space | activity low, texture low, pulse present | 2 | medium to low |
| Quiet | room sound, gaps, silence | presence low, and no periodicity in the onsets | 17 | presence yes, periodicity not built |
| Syncopated | off-grid, broken beats | accents displaced from the pulse | 3 | no. Needs a beat grid. Stays a target, not in the selector |

How a mood's strength is computed:

- Each condition in a row is a ramp between two breakpoints: 0 below the first, 1 above the second, linear between (falling for "low"). The breakpoints are thresholds and are set on recordings.
- A mood's strength is the minimum of its ramps, so every condition has to hold, and near a border the strength slides instead of flipping.
- The strength is multiplied by the lowest confidence among its readings.
- The strength is smoothed over time with one time constant per mood, starting at 2 s. `MODEL.md` "Timescales" says one constant does not suit every reading, so these are tuned on recordings too.
- Chaotic requires presence high because room noise is also flat and noise-like. Quiet requires no periodicity because the level gate alone misreads quiet music as silence, which the owner has seen in the log.
- All breakpoints live in one table in one header, compiled by device, native and wasm alike.

Every row is checked on a microphone `.f32` of music that has that mood. Mark by ear first which passages have it, then compare the strength (`phase2-protocol.json`, "order_is_fixed"). A row that does not follow is rewritten or marked not measurable, with what was tried.

## Fit lists

One shape serves animations, episodes and layers.

- Each animation has a sparse list of pairs: a mood or an episode, and a fit between 0 and 1. For example Moonlight: quiet 0.9, floaty 0.7, calm 0.4. Adding a mood adds entries only where it fits.
- A fit of 1.0 means the animation was written for that mood. An animation does not get 1.0 on several moods. This stops generalists from winning every mixed passage.
- Episode entries (buildup, descent) are the episode weights. Beat Scanner with buildup 0.8 gains while a buildup is open.
- A drop entry marks an animation as a drop animation. Only these can be the drop base.
- Layers have fit lists of the same shape.
- The fit lists replace the `MoodType` column in `AnimationCatalog.cpp` and the `AnimationProfile` table. Old tags are evidence of intent when writing the lists, not the answer.
- Known constraints: Heartbeat is a bad long base, fit for a short wait or a buildup, so it is a layer or a low-fit base. Lava Lamp fits quiet or sparse, not rhythmic passages.

## Picking the base

Score of an animation, each time the director asks:

```
score = Σ_moods (strength × fit) / Σ_moods strength
      + episodeWeight × Σ_open episodes (fit)
      − the existing recency penalty
```

The first term is the fit averaged over the moods the music has, weighted by how strongly it has each, so it stays between 0 and 1 whatever the music. `episodeWeight` starts at 0.15 and is tuned on recordings. Tease and anomaly have no entries, so they cannot move the base.

- **Bucket.** The animations whose score is within 0.10 of the best, at least 2 and at most 5. Both numbers are tuned on recordings.
- **Pick.** A draw from the bucket, weighted by score, excluding the running scene. The draw uses a seed the harness can fix, so tests stay deterministic. This replaces the rule in `SceneRegistry.cpp:133` that no tie-break is a draw.
- **Rotation.** At `sceneIdealDurationMs` (12 s) the director draws again from the current bucket.
- **Early change.** After `sceneMinDurationMs` (5 s), if the running scene has been out of the bucket for `kChallengeMs` (1.5 s), the director draws from the bucket. This replaces the rival-by-margin rule in `SceneDirector.h:143-160`.
- **Drop.** On the rising edge of `dropConfirmed`, if the last drop base started at least a minimum spacing ago, the base switches to the drop animation with the highest mood score. The spacing starts at the detector's `DROP_COOLDOWN_MS`. `dropConfirmed` already means the drop followed enough preparation (`StructuralEpisodes.h:330`), which is the research definition of a drop (concept 9).
- **Drop hold.** The drop base holds at least 4.5 s. From the start it watches for a drastic shift, a joint change across readings (concept 14): presence or activity falling away (the sound cuts off), pulse breaking (the rhythm changes), or tempo moving (the BPM changes). A shift seen after the minimum ends the drop base at once, and one seen during the minimum ends it when the minimum is up. At 12 s it ends regardless. The next base is a draw from the bucket. The two times start at 4.5 s and 12 s and are tuned on recordings: the payoff and pause sections in `web/audio/local/ground_truth.json` mark where drop sections end.
- **Quiet.** Quiet is a mood, so quiet and silent passages pick Moonlight, Forest Canopy and the like through their fit, not through a cut.

## Layers

- **Scene layers.** Each scene keeps its fixed `layerTypes` list, reviewed while writing the fit lists.
- **Episode layers.** The existing bindings stay: swell for a buildup, cool for a descent, spiral for a confirmed drop window, arc for a tease, flicker for an anomaly, and the drop onset's one-shots.
- **Episode edges.** Layers may also fire on the start and the end of a buildup and a descent. The director derives the edges from `EpisodeStatus`: a new `episodeId` is a start, and a return to `EP_IDLE` is an end.
- **Live layers.** The free slots are filled by layer fit against the current moods, replacing `SceneDirector::maybeInjectReactiveLayer`. Its rules go as follows:
  - The surge on `level > 0.85`: removed. Loudness chooses nothing.
  - The contrast accent on `deltaIntensity`: rebuilt on a joint jump in the mood strengths (concept 14), or removed.
  - The beat pop: kept, gated by pulse confidence, without the coin flip.
  - The flicker on `buildupClimax`: removed, since flicker means anomaly.
  - The random mood arc: removed, since arc means tease.
- **One meaning per layer.** After this, arc means tease, flicker means anomaly, and HIGHLIGHT with ENERGY means the drop onset.

## Board

The large text shows the one or two strongest moods with their strengths. Episodes stay on the chips, labelled as episodes. A small line says why the scene is up: bucket, rotation, drop or quiet. `predicted` and the catalog tag line go.

## Web emulator

Owner call, 29-09-2026: everything this plan adds is visible and tunable in the web emulator, because that is where calibration and tuning happen. The page computes nothing itself. Every number comes from the C++ through the wasm exports (`VISUALIZER_UI.md`, "Firmware Authority Over Logic").

- **Readings.** Tilt, evenness, punch and body are appended to `gg_feature` after index 85 as value, confidence and trend, the same layout as indices 23 to 46. Existing indices are not renumbered. They show in the Music coordinates panel beside the others.
- **Moods.** `gg_mood_count()`, `gg_mood_name_by_index(i)` and `gg_mood_strength(i)`. The page builds a mood strength panel, one bar per mood, from these calls and hard-codes no mood names, so adding a row in C++ adds a bar with no page edit. Rows not in the selector yet (Syncopated) show greyed.
- **Mood tuning.** Every breakpoint, the per-mood time constant, and the calibration values for the room-coloured readings are dials, through an indexed pair like `gg_tuning` (for example `gg_mood_param(i)` and `gg_set_mood_param(i, v)`, with `gg_mood_param_count()` and a name per index). The page builds a mood section in the Tuning tab from that table. A `Copy mood table` button writes the current values as the C++ table from the moods header, so a tuned value is pasted into the code and survives a rebuild.
- **Selection.** Per scene: its mood score, episode bonus, recency penalty and whether it is in the bucket, readable through indexed exports. The page shows the bucket as a ranked list with scores. The reason for the running scene (bucket, rotation, early change, drop, quiet) and the drop-hold state (holding, minimum left, shift seen, cap) are exports too.
- **Selection dials.** Bucket margin, bucket minimum and maximum, `episodeWeight`, drop hold minimum and cap, and drop base spacing are appended to `gg_tuning` after entry 14.
- **Fit lists.** The scene catalog shows each animation's fit list in place of the single `gg_scene_mood_by_index` tag. Editing fit lists stays in C++.
- **Board.** As in "Board" above. The session weight board counts time share per mood from the strengths.
- **Recording view.** The harness writes the new readings and the mood strengths as channels into `web/data/` (the channel table at `src/sim_main.cpp:3018` onward). Where a recording has a ground truth with `moods` marks, the timeline draws the marks under the strengths, so a row that does not follow the ear is visible at a glance.
- **Legacy dials.** Tuning entries 3 to 7 drive the level-name classifier (`MoodHistory` hold, confirm, smoothing, dynamics thresholds). They go with it in cleanup.

## What goes

- `classifyMood` naming the passage from `level`, `ladderRank`, `ladderMood`, and the `MoodType` catalog column.
- `pickSceneByMusic`'s filter to tagged scenes (`SceneRegistry.cpp:182-186`).
- `structuralOf` (`SceneDirector.h:56-59`), including the silence cut and the cut on the drop onset.
- `AnimationProfile`, `profileDistance`, and ranking by `sceneDistance`. The recency penalty moves into the score.
- `pickSceneByMood` and the hand-built snapshot path, once fixtures build a `MusicState`.
- `MoodType` itself, after the board has moved to the mood list. Moods get their own enum in their own header. Episodes stay as flags on `MusicState` and `EpisodeStatus`.

## Tests

Existing tests in `src/sim_main.cpp` that encode the old model:

| Test | Line | Action |
|---|---|---|
| quiet music selects a quiet scene and loud music a loud one | 1464 | rewrite: a quiet reading picks from animations with quiet fit |
| the music map reaches a wide part of the catalog | 1483 | rewrite over mood strengths: every animation with a fit list wins somewhere |
| a structural event picks a scene written for it | 1497 | delete |
| never the running scene, a scene just left is not next, unsure coordinates count less | 1509 onward | keep, ported to the score |

New tests:

- The same readings with a very different level give the same strengths, the same bucket, and with the same seed the same scene.
- An open buildup changes the scores only through buildup entries, and the pick stays among animations that fit the mood.
- A drop onset without `dropConfirmed` never changes the base. A confirmed drop switches to a drop animation, and a second confirmed drop inside the spacing does not.
- The drop base holds through a shift inside the first 4.5 s, ends on a shift after it, and ends at 12 s with no shift.
- A specialist with fit 1.0 on the dominant mood beats a generalist with 0.5 on every mood present.
- Rotation happens at the ideal duration and stays in the bucket.
- A quiet reading never produces a fully dark frame.
- Adding a mood row with no fit entries changes no pick.

## Build order

Each step names its check. Device-reachable code is not done until `pio run` builds, because only the device build compiles at `-std=gnu++11`.

1. **Readings and moods, measure only.** Add tilt, evenness, punch and body to `MusicState`. Add the mood enum and the strength function with the draft breakpoints in one header. Emulator: the four readings in the Music coordinates panel, the mood strength panel, the mood tuning section with `Copy mood table`, and the recording-view channels with mood marks drawn under them ("Web emulator"). The selector is not touched. Check: `npm run native`, `pio run`, `npm run wasm`, and on the live page the strengths move on `demo.f32` and a dial change moves them.
2. **Recordings (owner).** `web/audio/local` already holds `demo.f32` (EDM, about 7 minutes) and `jazz.f32` (electronic music despite the name, about 36 s), each with a `ground_truth.json` that marks drops, buildups, payoffs, pauses and aftermaths, with per-section BPM and band shares. They mark events, not moods. Add one 60 to 120 s `.f32` each of techno, drum and bass, melodic rock, organic and chill, recorded with the live page's `Record` button. Mark moods by ear before looking at any reading, as a `moods` list per section in the same ground-truth format, and add mood marks to the demo's existing sections.
3. **Tune the moods.** Set breakpoints and time constants against the recordings in the emulator's mood tuning section, then paste `Copy mood table` into the header. Calibration stays the owner's. Mark rows that do not follow. Check: each row's strength against the marks, written into this plan.
4. **Fit lists.** Write the lists for animations and layers, including episode and drop entries, and review each scene's layer list. Emulator: the scene catalog shows fit lists. Check: `npm run native`, `pio run` (static tables), `npm run wasm`.
5. **Tests.** Rewrite and delete the tests in the table above and add the new ones, before the selector changes, so the failures are on record. Check: the new tests fail for the stated reasons.
6. **Selector.** Score, bucket, draw, rotation, early change, drop rule, drop hold and quiet. Remove the structural filter, `structuralOf`, and ranking by `sceneDistance`. Emulator: the bucket list with scores, the reason line, the drop-hold state, and the selection dials in the Tuning tab. Check: the tests pass, `pio run`, `npm run wasm`, and on the live page with `demo.f32` a confirmed drop switches the base and the hold ends near the ground truth's payoff ends.
7. **Layers.** Add the episode edges, replace `maybeInjectReactiveLayer`, and separate the layers that carry two meanings. Emulator: the active layer list says why each layer is up (scene, episode, edge, mood). Check: `npm run native`, `pio run`, `npm run wasm`.
8. **Board.** Strongest moods, episode chips and the reason line on the now-board, and time share per mood in the session weight board. Check: the live page against a recording.
9. **Cleanup.** Remove `MoodType`, the level names, `ladderRank`, `pickSceneByMood`, `AnimationProfile`, and tuning entries 3 to 7 with their page rows. Update `ARCHITECTURE.md`, `VISUALIZER_UI.md` (the feature index table and the tuning entries), `VISUALIZER_UI.md`, the affected `CONTEXT.md` files and the repo's `AGENTS.md`.

Out of this plan, parked: fixing the silence detector's level-only gate beyond the periodicity condition in the Quiet row. The owner ranks it below this work because what quiet looks like today is fine.

## What this plan refuses

- An emotional mood label.
- Loudness choosing a mood, a scene or a layer.
- A single boolean choosing the base, including a catalog tag used as a filter.
- An episode other than a confirmed drop cutting the base.
- A threshold tuned on the synthetic signal.
- Another flag excluded from `classifyMood` as a fix.

## Implementation deviations

### Step 1, 29-09-2026

Built: `src/audio/Moods.h` (enum `MoodNature`, `kMoodTable`, `MoodModel`), the four readings in `AudioProcessor::updateMusicState`, `MusicState::mood[]`, wasm exports `gg_mood_*` and `gg_feature` 86 to 97, the page's four coordinates, mood strength card and Mood tuning section with `Copy mood table`, and the replay report columns in `src/sim_main.cpp` (`kTracked`). Checks: `npm run native` 571 of 571, `pio run`, `npm run wasm` build. A replay of `jazz.f32` moves every reading and most strengths, Full and Bright highest. Not checked: the live page in a browser.

Deviations and calls the plan did not specify:

- **Enum name.** `MOOD_*` collides with the old `MoodType` enumerators until cleanup, so the new ones are `MN_*` (`MoodNature`).
- **Tilt direction.** The Readings table defines tilt as low share minus high share, but every mood row uses "tilt low" for dark and bass-heavy. The coordinate is `0.5 + 0.5 * (high share - low share)`, so 0 is bass-heavy and the rows read as written.
- **Presence confidence.** Presence enters the moods with confidence 1. Its own confidence is `gateGain`, which would zero Quiet exactly when it should be high.
- **Calibration dials.** No separate calibration offsets for the room-coloured readings. Their breakpoints in the table are the calibration and are dials.
- **Quiet.** Presence low only, the periodicity condition is not built (as the plan says). Syncopated has no conditions and stays 0.
- **Draft breakpoints for brightness** are guesses, since the centroid's scale on real audio was not known. Set them first in step 3.
- **Recording view.** It replays scenario frames and has no audio timeline, so the mood channels went into the replay report and trace (`--replay`) instead, and the mood marks under the strengths wait for step 2's ground truth and a timeline.
- **Mood dials are not persisted** in the browser. `Copy mood table` and the header are the record.

### Board pulled forward from step 8, 29-09-2026

Owner call after step 1: the board's big text read Intense on nearly all loud music, and on calm passages after a loud one. Replayed through the old `classifyMood`, `demo.f32` was Intense for 85% of frames and `jazz.f32` for 86%. Cause: `level` is the block volume over the loudest recent block, so sustained loud music sits at 0.8 to 1.0, and the hard-coded nudges (BPM above 120, wide dynamics span) each add a rung. The hold and confirm dials are not involved. This is decision 5 (loudness chooses nothing) as a symptom, and the classifier goes in step 9.

Done now: `now-mood` shows the top two mood strengths at 5% or more (`Full 85% · Bright 27%`). The old level name is not shown; it still drives the scene picker until step 6. The scene picker is unchanged until step 6, so the board and the picked scene disagree until then. Not cleaned up: the `MOOD →` lines in the event log and Copy snapshot still print the old name. Step 8 still owns the reason line, episode chips and the session weight board.

### Steps 4 to 9, 29-09-2026

Built in one pass after the owner said the system had to run end to end before any tuning, and that recordings are what tuning happens on later. Checks: `npm run native` 580 of 580, `pio run`, `npm run wasm`, and the page loaded in Chrome with the demo track (no console errors, the moods, the bucket and the reason line update, scenes rotate and leave the bucket).

What exists: fit lists and fixed scene layers (`src/animations/AnimationFit.h`), `SceneRegistry::score`, `buildBucket`, `drawFromBucket` and `pickDropBase`, the new `SceneDirector` (rotation, early change, drop base and hold, edges, mood jump, live layers, `feedLayers`), `SceneState` without the mood snapshot, `LayerWhy` on every layer, the wasm selector exports, the page's bucket list, reason line, drop hold, fit lists, layer why, mood share in the session weight board, and the selector dials. The harness got `attachFixtureMusic`, `checkSelector` and `checkLiveLayers`, and its replay now prints the strongest mood and the scenes the selector would pick.

Removed: `MoodHistory.h` (`MoodType`, the level names, `ladderRank`, `classifyMood`, `predictNextMood`, the dynamics window, the hold and confirm), `AnimationProfile.h`, `pickSceneByMood`, `pickSceneByMusic`, `sceneDistance`, `structuralOf`, `maybeInjectReactiveLayer`, the catalog's mood and `preferredTempo` columns, `DROP_PIN_MS`, and tuning entries 3 to 7. Tuning entries were renumbered (0 to 16) and the page's saved dials moved to `gg-tuning-v2`, so an old saved set is ignored.

Calls the plan did not specify:

- **Drop hold shift.** The reference is the activity, pulse and tempo at the moment of confirmation. A shift is the gate under 0.4 or activity under 0.35 of its reference (the sound cuts off), pulse under half its reference, or a tempo move over 15 BPM. Constants in `SceneDirector.h`, to be tuned against the payoff and pause sections.
- **Unserved moods.** A mood no scene has a fit for is left out of the score's average. Without that, adding a row to the mood table would dilute every score and could change the bucket, which the plan's test ("adding a mood row with no fit entries changes no pick") forbids.
- **Ballast.** The average carries a 0.05 ballast so it is defined when no mood is present, and collapses to 0.5 for every scene then.
- **Edge layers.** A buildup or descent opening fires a glow wipe (700 ms), its ending a sparkle (600 ms). An end caused by a drop or by the gate fires nothing, because the drop has its own layers and silence needs no accent.
- **Live layers.** Only layers with no reserved meaning are candidates. Reserved: highlight with energy, the arc, the flicker, the spiral, the buildup and descent layers, the glow wipe and the sparkle (edges), the reactive layer (beat pop) and base. One candidate at a time, at most every 6 s, for 5 s, when its fit against the moods is at least 0.6 and a slot is free.
- **Mood jump.** The strengths against a 1.5 s slow copy of themselves: a total change of 0.6 across at least two moods fires one highlight, at most every 2.5 s.
- **Per-strip memory.** The beat pop and live-layer cooldowns, and the last edge and jump a strip answered, moved to `LayerManager`. The old director kept one cooldown for every strip, so the first strip spent it and the second never popped.
- **Fit tables were rebalanced once on the emulator.** Full sits near 0.85 on the demo track, so scenes with a large fit on Full dominated. Full fits on everything but Hybrid were cut to 0.4 and Three Sin Two moved to Bright.
- **The scene clock keeps its inputs.** Minimum by tempo, ideal duration by level and dynamics, from the three dials. They set how long a scene runs and not which one runs.
- **Recording view.** Still has no audio timeline, so the mood marks under the strengths are not built. `--replay` carries the channels and the scene picks.
- **Fixtures.** Hand-built features get mood strengths from `attachFixtureMusic`, which runs the firmware's own `MoodModel` unsmoothed. The scripted phases were given readings so silence is Quiet, the calm phase a soft beat, the loud phase a full driving one, and the last a beatless drift.

### First calibration to the real reading scale, 29-09-2026

The owner saw most mood strengths sit at 0 on the live page. Cause: the draft breakpoints were guesses on scales the readings do not have. Replaying `web/audio/local/demo.f32` and `jazz.f32` (real music through the analyser) showed texture at 0.06 to 0.15, punch 0.06 to 0.2 (0.6 on a hit), brightness 0.07 to 0.16, activity 0.2 to 0.5, tilt 0.4 to 0.75, evenness 0.72 to 0.88 and pulse above 0.92 on a steady beat. The breakpoints asked for texture 0.4 to 0.6 and punch 0.4 to 0.7, so their ramps never rose.

Done: the replay now prints each reading's percentiles and, for every mood, each condition's mean ramp and how often it set the strength (`npm run native -- --replay <file>`, with the WinLibs runtime on PATH). The breakpoints in `kMoodTable` were moved onto those ranges. This fixes the scale and not the meaning, so where each mood begins is still the owner's to set by ear. `moodReadingsOf` in `MusicState.h` is now the one place the readings are gathered, for the analyser and the replay.

Result on `demo.f32`: Flowing, Driving, Rushing, Warm, Intense, Heavy, Bright, Full and Sparse all move (strongest mood over 424 s: Full 43%, Flowing 28%, Heavy 14%, Sparse 7%, Bright 6%). Floaty, Calm and Chaotic stay at 0, correctly, since an EDM track has no beatless drift or noise. They need recordings of that kind of music. Driving and Intense are weak on EDM (means 0.05 and 0.006), so their tempo, punch and activity ramps are the first to check.

### Room calibration, 29-09-2026

The first calibration moved the breakpoints onto the scale of two recordings and the owner still saw Sparse, Bright and Heavy dominating everything on the live page. The replays of the owner's other microphone captures (`~/Downloads/glitchglimmer-*.f32`) showed why: the same five readings sit in very different places in different rooms and sessions. Tilt ran from 0.32 to 0.84, texture from 0.02 to 0.34, brightness from 0.04 to 0.22 and body from 0.2 to 0.7. A fixed cut on any of them pins one mood on for a whole session and leaves its opposite at zero.

Call, changing the plan's "compared against fixed per-device values" for these five: texture, brightness, tilt, evenness and body are read relative to a per-device room calibration, `kRoomDefault` in `src/audio/Moods.h` (a centre and a spread each). The reading becomes `0.5 + (v - centre) / (6 * spread)`, clamped to 0..1, so 0.5 is what the device usually hears and the mood table's breakpoints on those five are on that scale. It is still fixed per device and not adaptive: it changes only when the owner presses `Measure this music` in the emulator (a running mean and standard deviation over the music playing, kept in the browser's localStorage and written out by `Copy mood table`), or edits a slider. The defaults are the demo track through this microphone. Pulse, activity, punch, dynamics and tempo stay on their own scales, since activity and punch are already relative to their own recent maximum.

A relative reading puts a passage above or below typical about half the time, so the opposite pairs (Bright and Heavy, Sparse and Chaotic) would split a session between them if their breakpoints sat at the middle. They sit at about +0.7 to +1.8 spreads and -0.7 to -1.8 spreads, so a mood needs a real deviation. Replaying every mic capture against itself (`--replay <file> --calibrate`, one pass to measure then one to read) gives a spread of moods per file and a "none" share when nothing stands out, which is intended.

Also changed: the board and the `MOOD →` log name a mood only at 15% or more, so they no longer flap at the 5% noise floor. The replay report now carries readings percentiles and each condition's mean ramp and limiting share, and `--calibrate`.

### Two families and a wider room calibration, 29-09-2026

Owner reports on the live page: Bright, Heavy and Sparse took over everything, "the music is always in one of those bands", and an intense rushed trance track read Bright 100%. Two design faults, both mine.

- **Tone moods are not character moods.** Warm, Heavy, Bright, Full and Sparse describe the colour of the sound and are always partly true of any music. They were scored in one average with Floaty, Calm, Flowing, Driving, Rushing, Intense, Chaotic and Quiet, which say what the music does, so a saturated tone at 100% drowned a character at 60%. They are now two families (`tone` on the `kMoodTable` row). `SceneRegistry::moodTerm` averages each family apart and joins them as `(1 - toneWeight) * character + toneWeight * tone`, with `toneWeight` 0.30 (a dial), so a tone shades the score and cannot decide it. The board shows the two apart, the big text is the strongest character then the strongest tone, and the session weight board has separate Character and Tone lists. The tone list was where the "accumulation" showed: tones are always present, so they took most of the time share.
- **More readings depend on the room.** Activity, punch and dynamics vary between sessions as much as tilt and texture do (dynamics 0.03 to 0.7, punch 0.06 to 0.98), so a fixed cut on dynamics left Intense at zero on the owner's microphone. They join texture, brightness, tilt, evenness and body on the room calibration, eight readings in all. Pulse, tempo and presence stay as they come. The breakpoints on those eight are on the 0.5-is-typical scale.

The demo track goes into the page digitally through a 0.12 gain and has no room, so it is not a stand-in for the microphone, and the first defaults (taken from it) were wrong for the mic. `kRoomDefault` is now the pooled medians of five of the owner's microphone captures, and `Measure this music` replaces them with the owner's own.

The browser's wasm and the native harness give identical output on the same samples: `jazz.f32`, 1089 frames, largest difference in any mood or reading 0.00000. The page opens the microphone with echo cancellation, noise suppression and automatic gain off and feeds it through the same analyser and `gg_step` as the demo, minus the demo's 0.12 gain node. So the difference the owner sees between the microphone and the demo is the acoustic path (speakers, room, the microphone's own response), which the room calibration exists for.

### Self-measured room and partial credit, 29-09-2026

Owner, with a snapshot of the live page: every character mood at 0 or 1% while the tone moods went to 100%, so the tones always took over, and there are no fixed minimums or maximums in the input to balance against. Two causes.

- **The calibration had to be pressed for.** The moods read eight readings against a per-device calibration, and the defaults did not fit the owner's session, so most ramps sat at zero until `Measure this music` was pressed. The firmware now measures it itself: the first 20 s of music after each change of input (presence at or above 0.95, silence not counted), then it is fixed. It is still not adaptive. An owner measurement or slider edit outranks it, is kept across source changes and is never overwritten, and `Reset room` returns to the defaults and the self-measurement. The page shows which of the four states it is in (defaults and waiting, measuring N of 20 s, measured by the firmware, set by the owner) and the sliders follow the firmware's measurement.
- **A strict minimum made a many-condition mood unreachable.** Rushing has three ramps, Flowing seven, Bright two, Full one. The minimum of seven ramps is near zero on almost any music while the minimum of one is the ramp itself, so the families sat at different heights by construction. A mood's strength is now its lowest ramp plus 0.35 of the way to the mean of its ramps (`mood partial credit`, a dial), so a near miss costs strength without erasing the mood. Replayed self-calibrated over five captures, character moods now peak at 0.1 to 0.9 and tone moods at 0.1 to 0.85 instead of 0 to 0.05 against 1.0. The cost is a low floor on moods that should be absent (Calm at 0.1 to 0.17 mean on a house track), which the dial trades against how much a real mood is erased.

Not solved, and not something a formula settles: Flowing is the strongest character mood on almost every capture, since all of them are house-tempo music with a steady pulse. Whether Flowing is too wide, or the captures really are that, is for the owner to say by ear.
