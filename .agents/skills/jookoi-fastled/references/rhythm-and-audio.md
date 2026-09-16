# Rhythm, envelopes, and audio

An effect with rhythm reads as alive even when the pattern is trivial. This file is about where rhythm comes from when the input is a signal rather than a clock — and about the filter work that has to happen between a microphone and a pixel.

Two things make this its own problem:

- **Audio is fast, LEDs are slow.** A buffer push costs roughly 30µs per LED, so a 300-LED strip caps you near 30-60 fps. Anything that modulates per *sample* is wasted work. The job is to convert a signal that changes thousands of times a second into a control value that changes tens of times a second.
- **Raw signal is unusable as a control value.** Volume wanders, silence isn't zero, and a bass hit is a 5ms spike. Mapped straight to brightness, that's a strobe, not a performance.

Everything below is technique, not API. The library's own detectors (`fl/audio/detector/`) implement some of it for you — check `references/effects.md` first — but knowing the mechanism is what lets you tune them, and what you need when you're working from a plain analog input or a bare envelope follower.

## The one primitive: one-pole smoothing

Almost every problem here reduces to this line:

```cpp
smoothed += (raw - smoothed) * k;      // k in 0..1
```

`k` is the whole dial. Small `k` (0.05) gives a slow, heavy filter — smooth, laggy, good for a background wash. Large `k` (0.6) gives a fast, twitchy one — immediate, good for an accent. It's a fixed-point-friendly operation: with integers, `smoothed += (raw - smoothed) * k >> 8`, using `int16_t` for the difference so the negative half doesn't wrap.

The trap is using one `k` for everything. A single smoothed value has to compromise between "react to the hit" and "don't flicker", and it does both jobs badly.

## Attack and release

Split it into two coefficients and most of the problem disappears:

```cpp
const int kAttack  = 200;    // fast — hit lands on the frame it happens
const int kRelease = 15;     // slow — decays over a visible moment

int16_t delta = raw - level;
level += (delta * ((delta > 0) ? kAttack : kRelease)) >> 8;
```

Fast attack, slow release. This one change is the difference between an effect that flinches and an effect that breathes. It's also why a decaying tail looks like a physical object: real things get hit instantly and lose energy gradually, so eyes read asymmetric envelopes as motion.

Use it on brightness, on layer opacity, on hue offset — anything the beat should drive.

## Normalize before you map

A threshold or a mapping tuned at one volume is wrong at every other volume. Two trackers fix this:

```cpp
// Rolling peak — rises instantly, falls slowly
if (raw > peak) peak = raw;
else peak = peak - (peak >> 8);          // ~1-2s decay at 60fps

// Rolling floor — falls instantly, rises slowly
if (raw < floor) floor = raw;
else floor = floor + ((raw - floor) >> 10);

uint8_t normalized = (peak > floor) ? ((raw - floor) * 255) / (peak - floor) : 0;
```

Now `normalized` is 0-255 regardless of whether the room is quiet or the music is loud, and a fade-in reads as dynamics instead of as nothing. Gate it: if `peak - floor` is below a threshold, the input is silence and the effect should fall back to a non-audio animation rather than amplifying noise into flicker.

A fast-attack/slow-release *peak* is the same envelope as above, applied to a different variable. Same idea, different job.

## Dynamics beats level

Average volume tells you how loud it is. The more useful number for rhythm is how much it's *changing*:

```cpp
float dynamics = (peak - average) / (peak + 1);    // 0 = steady, 1 = punchy
```

A steady loud track has high level and low dynamics. A track with a hard kick and quiet verses has the same level and high dynamics. The second one is the one you want lighting an effect — and level alone can't tell them apart. Route dynamics into motion speed or accent probability, route level into overall brightness.

## Beat detection without an FFT

The classic energy-ratio detector is short and works well enough for visuals:

1. Track a rolling average of energy over the last ~1-2 seconds.
2. Compute instantaneous energy; compare the ratio `instant / average`.
3. Fire when the ratio exceeds a threshold (1.3-1.5 is a reasonable start).
4. **Refractory period** — ignore new beats for ~250ms after one fires, or a single kick triggers on the attack *and* the decay.
5. Require a minimum absolute level too, so silence doesn't beat constantly.

Two failure modes to expect and design around. Slow builds cross the threshold continuously and produce a beat every refractory period — a false train. And a tempo below ~30 BPM (interval > 2000ms) is almost always a missed beat rather than a slow one, so ignore intervals outside roughly 250-2000ms.

If you keep a short ring buffer of past energy values, this needs no library at all and costs a few hundred bytes. It only needs energy — bass energy specifically, since kicks live there and hi-hats will confuse a full-band detector.

## Turn the beat into an envelope, not a flag

A `bool beatDetected` that's true for one frame is close to useless: every consumer of it fires on the same frame, everything happens at once, and frame-rate changes alter the result. Latch it into a decaying value instead:

```cpp
static uint8_t beatEnv = 0;
if (beat) beatEnv = 255;
else beatEnv = scale8(beatEnv, 235);      // ~exponential decay over ~0.5s
```

Now the beat is a continuous signal you can use several ways at once — sparkle probability from `beatEnv >> 4`, brightness bump from `beatEnv`, a layer's opacity from `beatEnv`. Different consumers can read it at different points in its decay, which is what makes a hit feel like it has a shape rather than an edge.

Add a slow "anticipation" envelope on top of the same signal (a smoothed *derivative*, rising before the hit) and you get builds for free.

## Tempo is a phase problem, not a number

`60000 / interval` gives a BPM, and it will be wrong — half or double the real tempo is the normal failure, because the detector can't distinguish a beat from a half-beat. If you don't need to *display* BPM, don't compute it: what an effect actually consumes is **phase** — where we are within the current beat.

Do it by timestamp instead:

```cpp
uint32_t beatPeriod = smooth(beatPeriod, now - lastBeat);   // heavily smoothed
uint8_t  phase = ((now - lastBeat) * 255) / beatPeriod;     // 0..255 through the beat
```

Phase is robust because it self-corrects: each real beat resets it. A wrong-by-2x period produces a phase that resyncs every other beat, which looks like swing rather than looking broken. Route on `phase` for anything that should line up with the music, and only reach for BPM if the user asked to see it.

## Map to state, then to pixels

The instinct is to wire audio straight into the draw call — bass into brightness, treble into hue. That produces an effect that *looks* like a signal analyser: technically responsive, unmemorable, and prone to strobing.

A better structure: smooth the features, classify them into a small set of named states, and let each state carry a whole parameter preset.

```cpp
struct Params { uint8_t brightness, speed, hueBase, sat, blurAmt; };

Params CALM  = {  70,  3, 150, 160, 60 };
Params GROOVE= { 160, 14,  20, 200, 20 };
Params INTENSE={ 255, 30, 200, 230, 10 };
```

Classification is a few threshold comparisons on smoothed `energy`, `dynamics`, and tempo — deliberately coarse. The value isn't accuracy, it's that the whole look changes coherently: a "calm" state can be dim *and* slow *and* desaturated, which is what a mood actually is. Wiring each feature independently to its own pixel parameter can't produce that.

Two rules for the classifier:

- **Hysteresis.** A state change should require crossing the threshold by a margin, not just touching it. Without this, a value hovering at a boundary makes the effect flicker between two looks, which reads as a bug.
- **Minimum dwell.** Once a state is entered, hold it for at least a second or two. Visual states that last three frames look like glitches; the eye needs time to register a mood.

Keep a short history ring (the last 30-60 seconds at your frame rate is plenty — a few hundred structs) rather than only the current value. History is what lets you ask "is this loud *compared to the last minute*", estimate beat rate by counting events in a window, and smooth state transitions against the recent past instead of one noisy frame.

## Fades between states, not cuts

Switching parameter sets on a state change hard-cuts, which is jarring. Keep a current value *and* a target value per parameter, and smooth toward the target every frame — the attack/release filter from the top of this file, with a slow coefficient.

```cpp
cur.brightness += (target.brightness - cur.brightness) * kScene >> 8;
```

A 0.3-0.5s transition is usually right. Long enough to read as a morph, short enough that the state change still feels like it happened. Longer and the effect feels like it's lagging the music, which is much worse than cutting.

## Chance beats certainty

Real music isn't deterministic, and neither should the reaction be. Two cheap tricks carry a lot:

**Probabilistic events.** `if (random8() < 40)` on a beat rather than always. At 40/255 not every beat gets a sparkle, which reads as organic and stops the accent layer from becoming a metronome. Reserve 255 for genuine downbeats.

**Rate limits and layer caps.** Before injecting an accent, check that the last one was more than ~800ms ago and that the active layer count is under a ceiling (4 is a reasonable maximum for a small strip). Without limits, a busy track layers accents until the strip is saturated and every layer is illegible. Rationing attention is the whole job — the same "darkness is a resource" argument from `references/look-and-feel.md`, applied to events instead of pixels.

## A working order

1. Get one control value: smoothed energy with fast attack, slow release.
2. Add normalization (rolling peak and floor) so it survives a volume change.
3. Add an energy-ratio beat detector with a refractory period, and latch it into a decaying envelope.
4. Derive phase from beat timing; drive anything that must line up with the music from that.
5. Classify into 3-4 states with hysteresis and a minimum dwell; give each state a full parameter preset.
6. Smooth between states.
7. Add probabilistic accents last, rate-limited and capped.

Steps 1-3 are worth doing even for a single-effect sketch. Steps 5-6 are what turn three effects into something that feels like it's performing.
