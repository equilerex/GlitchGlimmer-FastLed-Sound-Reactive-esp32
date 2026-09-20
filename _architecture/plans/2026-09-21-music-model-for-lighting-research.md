# music-model-for-lighting-research

Session: 2026-09-21. Status: phase 1 second pass. Concepts are separated from detectors, sources are graded, and several concepts end as competing hypotheses on purpose. Phase 2 and phase 3 not started. Written without reading the current classifier.

## Context

An earlier draft of this work started from the existing mood classifier and patched it. The owner rejected that. The classifier is an MVP, its labels and ordering are not requirements, and music is not a ladder or fixed journey. Its characteristics vary independently and can jump in any direction.

The first pass of this document fixed the direction but blurred two things: what a musical concept *is*, and one way of *detecting* it. Several rows were really a detector wearing a concept's name. This pass separates them and treats every detector as a hypothesis. It also stops short of designing how animations consume the result. What information an ideal visualization engine should receive comes first.

The research order:

```
what aspects of music matter for visualization
  -> how they can be represented
  -> what evidence might detect them
  -> which are realistically measurable
  -> phase 2 tests those assumptions
```

Constraints taken as given: mono microphone audio of a room, real-time, causal, a small microcontroller. The device already produces the primitives listed under "Measurements versus interpretations". Nothing else about the implementation is assumed.

## Where the research lives now

The durable half of this session moved to `docs/music-research/MODEL.md`: the concepts, what the sources establish, the timescale findings, what an ideal engine should receive, the research method, and per concept an "out of reach when / unlocks with" note. That file is hardware-independent and is not rewritten per iteration. `concepts.json` and `sources.json` beside it are the structured lookup of the same material.

This file keeps what is specific to *this* iteration: an ESP32-S3 wearable with a room microphone, running live, approximate. Scope, feasibility, phase ordering and decisions. A later iteration on stronger hardware, or host-side, or model-driven, gets its own plan file and starts from `MODEL.md` unchanged.

The split exists because a review pass deleted concepts and method for not fitting this build. Nothing is deleted for that reason. A concept the ESP32 cannot reach is still correct about music, and the constraint that blocks it is recorded as a shopping list for whatever comes next.

## Scope of this iteration: approximate wearable, not an instrument

Set by the owner, then corrected after a review pass overshot it. This governs the rest of the document, so the distinction it draws matters.

**What "close enough is good enough" applies to:** how precisely a concept is *inferred*. Not which concepts are worth representing. A concept earns its place by whether representing it would make the lights follow the music better. It is dropped only after a cheap proxy has been tried and found too expensive, too unreliable, or visually unhelpful. Difficulty of exact detection is not grounds for dropping it before that.

The order is: understand the concept, find a cheap signal that might indicate it, test whether the proxy is good enough in practice, drop only on evidence. Groove is the example. It may be worth representing as a rough confidence built from pulse strength, rhythmic regularity, bass activity and onset pattern, even though the literature says a validated groove number from polyphonic audio is an open problem. Rough and useful beats absent.

**What the wearable context actually constrains:**

- **A wrong call costs one wrong-looking second.** Nobody is measuring it. Detector thresholds can be loose.
- **False positives are cheap, misses are not.** A flash that should not have happened reads as part of the show. A drop the strip sleeps through is the failure everyone notices. Bias detectors toward firing, which is the opposite of what the accuracy-oriented literature optimises for.
- **Short window, and this is a real narrowing.** The system tracks now plus a few seconds either side. No song memory, no track-level contour. This does not delete the concepts that were defined at track scale, it means they need reinterpreting at the scale available: climax as a local peak against the last minute rather than the peak of a piece, repetition against a short compressed memory rather than the full past. Some may not survive that reinterpretation, which is a finding, not an assumption.
- **Tempo is the one long-lived signal.** BPM and significant changes to it persist across the short window. The exception, not a precedent.
- **A few hundred ms of latency is free.** Nobody in a crowd can tell the strip reacted 300 ms late. That dissolves most of the causality problem the literature raises: the system can buffer, wait for confirmation, and still look immediate. Late and correct beats instant and wrong. This is a genuine unlock, because most of the offline-versus-causal gap in the sources below is about exactly this.

**Standing rule, restated because a review pass broke it.** A detector is never a definition. "Bass disappeared then came back" is one candidate drop detector. It is not what a drop is, and writing it down as the concept loses everything the concept was for: the preparation, the expectation, the arrival. The concepts below stay defined by what listeners hear, and cues stay as hypotheses under them.

**Not yet in scope:** the existing 37 animations, animation metadata, selection machinery, layers and scene implementation. Those come after the music model is understood well enough, and reaching for them early is the implementation-first anchoring this work exists to avoid.

**Out of scope for this iteration is not out of the model.** Anything this build cannot reach stays in `MODEL.md` with a note saying what would unlock it. That note is the point: it is what a future iteration reads to decide whether better hardware, a host-side pass or a learned model buys anything worth having.

## Signal-path limits (feasibility, not a constraint on the model)

Added in review. These were being carried as an open question for the owner, and the repo answers most of them. They sit here, after the concepts, on purpose: they bound what a *proxy* can see through the current path, and they are an input to phase 2 feasibility. They do not decide what is worth representing.

- **Transform: 512-point FFT at 44100 Hz** (`src/config/Config.h:25-26`). Bin width is 86 Hz and the window is 11.6 ms. Semitone spacing at C3 is about 8 Hz, so chroma, key, mode, tonal tension and harmonic novelty are not "low reliability" through this path, they are **not resolvable by it**. Reaching them needs a different transform (a much longer window, or a constant-Q or band-pass bank on the low registers), which is a design decision, not a tuning one. This applies to the harmonic parts of concepts 4, 6, 7 and 16, and it means the unsettled valence-follows-mode question may be moot on this hardware regardless of how the literature resolves it.
- **Analysis frames are non-contiguous and the hop is not constant.** The I2S DMA ring is 8 x 64 samples, about 11.6 ms (`src/audio/AudioProcessor.cpp:28-29`), drained once per loop pass, and the 33 ms LED frame blocks that loop. Adjacent 512-sample blocks are therefore separated by a variable, unrecorded gap, and `AudioFeatures` carries no sample-time. Every rate or periodicity cue in this document depends on a known hop: onset rate, spectral flux, pulse strength, tempo, beat phase, tempo stability, and Solberg's shortening note values. **Prerequisite for phase 2:** attach a sample-clock timestamp to each analysed block, and either enlarge the ring or move capture to its own task. Without it the device column of the three-source table produces artefacts that will be misread as microphone loss.
- **No PSRAM is configured** (no `board_build` or `BOARD_HAS_PSRAM` in `platformio.ini`), so feature memory lives in the S3's internal SRAM alongside the LED buffers. This is a budget with a number rather than a wall: 60 s of 16 floats at 30 Hz is about 115 KB. Concept 7 should be treated as a memory *budget* question, not as memory-infeasible.
- **Board is `esp32-s3-devkitc-1`**, dual core at 240 MHz, so a dedicated audio core is available and "a small microcontroller" understates the headroom.

**Board choice.** The S3 is the right target and there is no reason to move. The second core is what makes the audio path clean: capture and FFT sit on one core and FastLED plus the animation on the other, so LED output timing stops interfering with the analysis hop. That is worth more here than any single-chip feature.

- **ESP32-C3 is a downgrade that would hurt.** Single RISC-V core at 160 MHz, no PSRAM, no vector instructions. FFT, band analysis and tight-timing WS2812 output would share one core, which is exactly the interference described above, and there is no second core to move it to. Only worth it if the animation set shrinks a lot.
- **If cheaper is needed, the plain ESP32-WROOM is the better fall, not the C3.** Dual core at 240 MHz, PSRAM available, usually cheaper than an S3 devkit and very well supported. It gives up native USB, which costs convenience when flashing and when adding USB charging to a wearable, and it is the older silicon.
- **Neither saves meaningful power.** On a wearable the LEDs dominate the budget by an order of magnitude, so board choice should be decided on core count and USB, not on current draw.

**The harmonic branch is capped by this path, which is not the same as being dropped.** At 86 Hz bins nothing pitch-based is recoverable here, so concepts 4, 6, 7 and 16 cannot be served by the current transform in their harmonic parts. Three ways out, and phase 2 decides between them per concept: a longer window or a low-register filter bank if one of them turns out to matter; a non-harmonic proxy that is good enough for lighting, since emotional character may be largely carried by the tempo, level and brightness cues Juslin's table supports; or the concept does not survive at this scale. What is *not* a reason to close it is that it is hard.

## Phase 2, sized for a wearable

Two passes overshot this section in opposite directions. The first specified a three-source study with a dual capture path, two annotators, continuous ratings and offline MIR as reference: right for a paper, too heavy here. The review then cut it to replay-and-eyeball, which is too light for a different reason — replay shows what the analyser produced, not whether that corresponds to anything a listener heard. Without some perceptual reference a trace can look clean and mean nothing. What follows keeps the comparison and drops the apparatus.

**The tuning loop, which already exists.** `src/sim_main.cpp` replays a `.f32` capture through the real analyser with `--replay`, at the same 33 ms step the device uses. Capture real room audio, replay, adjust, replay again. This is the fast inner loop and it is where thresholds get set.

**The perceptual check, which does not exist yet and is the actual test.** Before looking at any trace, listen to the capture and mark what you heard: where it built, where it arrived, where it got groovy, where the character changed, roughly and by feel. Sparse time marks with a strength, not continuous ratings. Then compare against the traces. The order matters — marking after seeing the trace tests nothing.

This is cheap. One listener, one pass, a label track. It is enough to answer the only question that matters: when the music did something, did the signal do something, and was it the same something. A trace that moves convincingly at moments nothing happened is the failure mode replay alone cannot show.

**What to capture.** Real loud rooms, real microphone, real distance. One honest capture of a live set beats ten curated tracks played into a laptop mic. Worth having among them: clear build-and-drop cycles, a quiet ambient stretch, a breakdown that is not followed by a drop, something with irregular timing, and the gaps between tracks.

**Second listener, if convenient.** Offered by the owner earlier. Worth it only for the subjective concepts where disagreement is the expected result and is itself the finding: groove, tension, perceived build, character. Not needed for every capture and not a blocker.

**Offline reference analysis, if a concept is stuck.** A librosa or Essentia pass on the same capture answers a specific question — did the evidence exist in the audio at all, or did the microphone path destroy it — and separates "this is hard" from "this is hard *here*". Reach for it per concept when the device disagrees with the ears, not as a standing protocol.

**Verdicts.** Per concept: the proxy is good enough, the proxy is not good enough but the concept is worth another proxy, or the concept does not survive at short-window scale. The third is a real finding and should be recorded with what was tried. No statistic to hit; judged by whether the strip looks right when replaying a capture whose marks say it should.

**Carried in from `MODEL.md`:** the concept-versus-detector separation, which is what stops a threshold becoming a definition, and the finding that different aspects need different memory lengths, since one global smoothing constant will not work. The full method there describes dual capture, annotator disagreement and offline reference analysis; this iteration pays for a reduced version of it, which is a budget decision and not a correction to the method.

## Decisions recorded

Decided by the owner:
- Close enough is good enough, applied to how precisely a concept is inferred and not to which concepts are worth representing. See the scope section, which governs the rest.
- Short-window only, with tempo as the one long-lived signal. Concepts defined at track scale get reinterpreted at this scale rather than assumed dead.
- A few hundred ms of reaction latency is acceptable, so the system may buffer and confirm.
- Target board is the ESP32-S3. See the board note in the signal-path section.
- A second listener is available for the subjective concepts. Offline reference analysis is acceptable, used per stuck concept rather than as a standing protocol.
- The phase order stands: concepts, then proxies, then testing, then dropping on evidence. Animation metadata, selection and the existing catalog stay out until phase 3.

## Phase 3, this iteration

Only after phase 2. Inspect the existing audio analysis, mood classifier, scene selection and the 37 animations against the measured model and decide keep, replace or reinterpret. How animations consume the model is designed here, not before. The owner's standing constraints for that design, from the problem-statement plan: independent coordinates rather than ordered labels, animation metadata describing where in the space an animation works, and selection by multidimensional compatibility with hysteresis. They are the owner's constraints, not conclusions of this research.

