# The music model for expressive lighting

Durable. Hardware-independent. This is the part that outlives any one build.

What aspects of music are worth representing if you want lights to follow it well, what the literature establishes about them, and where the evidence runs out. Nothing here assumes an ESP32, a microphone, real-time operation or a wearable. A concept stays in this document even when the current build cannot detect it, because the constraint that blocks it is a property of *that build*, not of the music. A later version on stronger hardware, or a host-side or model-driven one, starts from this file rather than re-deriving it.

**What belongs here:** concepts, what listeners hear, what the sources establish and what they do not, candidate evidence as hypotheses, competing representations left unresolved, research method that would apply to any iteration.

**What does not:** board choice, FFT size, CPU and memory budgets, latency policy, phase ordering, what this iteration decided to attempt. Those live in the dated iteration plan under `_architecture/plans/`, which cites this file rather than editing it. Per-iteration feasibility is recorded here only as an "out of reach when" note that says what would unlock the concept — which is durable, because it is the shopping list for the next build.

The structured lookup of the same material is in `concepts.json` and `sources.json` beside this file. Read those to query one entry; read this for the reasoning.

## Evidence grades

Claims below carry a grade. Anything ungraded is the author's reasoning, not a finding.

- **A**: the underlying paper or chapter was read in this session.
- **B**: an abstract or a summary of the source was read, or only part of it.
- **H**: hypothesis. Search-result summaries, second-hand statements, or plausible reasoning with no source read. Not imported into the model as fact.

Reliability ratings from a room microphone are **provisional engineering hypotheses** throughout. Phase 2 is where they survive or fail.

## What the reading established

Short, because the point is what it does to the design.

1. **Tension has been modelled from audio, and each input feature needed its own time window** (A, [Barchet et al., TenseMusic, PLOS ONE 2024](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0296385)). Six features: loudness, onset frequency, tempo, pitch height, tonal tension, roughness. Best fitted attentional/memory windows in seconds: loudness 3/3, roughness 3/4, pitch 5/12, tonal tension 7/12, tempo 3/20, onset frequency 3/20. The model used a 4.5 s lag against listener ratings. Cross-validated correlation with listener ratings was about r = .6 (mean r .61, SD .31, so very uneven across pieces, and 8 of 38 pieces predicted poorly). Data: 24 analysed listeners, 38 Western classical pieces, 60-90 s each. The authors say it applies mainly to Western tonal and atonal classical music and that prominent percussion degraded feature extraction. **Consequence:** the finding that different aspects need different memory lengths is well supported. The specific numbers and the feature list are from classical music and are not evidence about dance music heard through a room microphone.
2. **Groove has an inverted-U relationship with syncopation** (A for the abstract and design, [Witek et al., PLOS ONE 2014](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446)). 66 participants rated 50 synthesized drum breaks. Medium syncopation gave the strongest desire to move and pleasure. Dancing experience mattered, formal training and familiarity mostly did not. Limitation stated by the authors: isolated drum breaks, not full polyphonic music. **Consequence:** "more syncopation means more groove" is wrong, and an audio-feature groove estimate from polyphonic music is an open problem, not a solved one.
3. **Structure is ambiguous and hierarchical, and listeners disagree** (B, [Nieto et al., TISMIR 2020](https://transactions.ismir.net/articles/10.5334/tismir.54), read as a page summary). Listeners disagree about the structure of even Western popular music. Structure exists at several timescales at once. Automatic boundary detection reached about 60-70% F1 at 3 s tolerance against a human agreement ceiling estimated near 90%. The review says it remains to be seen how segments could be labelled in real-time causal use. **Consequence:** there is no single "true" section boundary to detect. Boundary-ness is graded and depends on which musical dimension is changing. Causal boundary detection is unsolved in the literature read.
4. **Structure analysis is built on three ideas: homogeneity, repetition and novelty, usually via self-similarity matrices** (B, [Müller, FMP ch. 4](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C4/C4.html)). Different feature types (harmonic, timbral, rhythmic) give different segmentations. Raw matrices need smoothing and tuning to be usable.
5. **A drop is defined by the community as a release that follows a build and re-introduces the bassline, with no precise recipe** (A, [Yadati et al., ISMIR 2014](https://archives.ismir.net/ismir2014/paper/000297.pdf)). Their detector segmented on chroma novelty (average distance from a real drop to the nearest boundary was 2.5 s and under 8% of drops were missed), then classified segments using spectrogram statistics, MFCCs and rhythm-pattern features with a linear SVM. Their own statement: the variability of drop realisations makes detection hard. Offline, whole-track, one platform's data. **Consequence:** even the detection literature treats a drop as a structural event defined by its role (arrival after preparation), and finds it by first locating a change point and then judging its role.
6. **Build-ups and drops in EDM are produced by identifiable techniques** (A for the abstract and the analysis sections read, [Solberg, Dancecult 2014](https://dj.dancecult.net/index.php/dancecult/article/view/451)). From a descriptive analysis of two tracks using spectrograms and expectancy theory: uplifters (rising sweeps), the "drum roll effect" (progressively shorter note values), large frequency changes, removal and reintroduction of bass and bass drum, and a contrasting ambient breakdown. Evidence base is two tracks and theory, with no listener data. **Consequence:** these are a documented *catalogue of mechanisms*, and several are not loudness rises at all (a breakdown lowers energy on purpose, the bass is removed). This directly contradicts a "build means rising loudness" definition.
7. **Acoustic cues to emotional expression are partly shared across vocal and musical channels** (A for the cue table in [Juslin and Laukka, Psychological Bulletin 2003](https://www.brainmusic.org/EducationalActivities/Juslin_emotion2003.pdf), which reviews 104 vocal and 41 music-performance studies). In the table read, for music performance: anger and happiness are mostly fast tempo, sadness slow, anger high sound level and high-frequency energy, sadness low sound level. Sound level variability, articulation and others are also tabulated. Mostly performer-intended emotion, mostly Western, mostly controlled recordings. **Consequence:** tempo, sound level and brightness carry emotional information, but as a *partial* description, and the cue patterns overlap between emotions.

Read only as search summaries, therefore **H** and not used as findings: the claim that valence is associated with mode and harmony while arousal is tied to tempo, loudness and timbre (music-emotion-recognition literature), the groove audio-feature studies (pulse clarity correlating with attack and low-band flux), and the groove review at [ScienceDirect](https://www.sciencedirect.com/science/article/pii/S0149763423004918). They are useful pointers to read next, not evidence.

## Measurements versus interpretations

The owner states the device already produces: normalized level plus raw volume, peak, average and energy, an adaptive noise floor and signal gate, bpm, a dynamics measure over the recent observed range, bass, mid and treble with normalized band drives, spectral centroid, dominant band, and scene and mood timing. These are inputs, not the model.

A measurement is a number (centroid 4200 Hz, bpm 128, level up 35%). An interpretation is a claim about the music that a visualization can act on (brighter and more brittle over the last five seconds, a locked-in groove, a sustained build toward a likely release). Research starts from interpretations and works down to the evidence.


## Concepts

Each concept is defined by what listeners experience or what the music does, independent of any detector. "Cues" are candidate evidence and are all hypotheses unless a grade says otherwise. "Kind" is state, trend, structural state, or event. Where more than one representation is plausible, they are listed as competing rather than resolved.

### 1. Arousal / intensity

- **Concept:** how energetic or activated the music feels, as heard, across a stretch of time.
- **Kind:** state, slowly varying.
- **Why it is more than loudness:** level is one contributor. Listeners also hear activity, register, timbral sharpness and rhythmic drive as intensity, and a quiet fast dense passage can feel intense while a loud sustained pad may not (H, reasoning).
- **Candidate cues:** loudness against a baseline, onset rate, brightness, tempo, sound-level variability (the last three from Juslin's cue table, A).
- **Competing representations:** one scalar, or several (loudness-driven, activity-driven, brightness-driven) kept apart because they can disagree.
- **Provisional reliability:** loudness and onset rate high, the composite medium.

### 2. Activity and density

Two different things that both get called "busy".

- **Temporal activity:** how many events per unit time. State. Cues: onset rate overall and per band, spectral flux.
- **Textural density:** how many simultaneous layers or spectral regions are occupied. State. Cues: occupancy across bands, spectral flatness, count of concurrent periodicities.
- **Competing hypothesis:** they may be one property in practice for the music of interest, or separable (a dense pad wall with few onsets). Phase 2 should look for tracks that separate them.
- **Provisional reliability:** temporal activity high, textural density medium to low.

### 3. Pulse, regularity, syncopation, groove

- **Pulse strength:** how clearly a beat is present. State. Cues (H): strength of periodicity in the onset envelope, low-band flux.
- **Tempo and phase:** where the beat is. State plus a phase variable. Cues: onset periodicity. Known failure (H, from general knowledge): half and double tempo errors.
- **Rhythmic regularity and tempo stability:** machine-steady versus drifting or irregular. State.
- **Syncopation:** accents displaced from the expected metric grid. State. Needs a trustworthy grid first.
- **Groove:** the pull to move with the music. Perceptual and listener-dependent (Witek: dancing experience mattered, A). Relationship to syncopation is inverted-U in drum breaks (A), unknown for polyphonic music.
- **Competing hypotheses for groove:** (a) a composite of pulse clarity, low-frequency flux, event density and moderate syncopation (H, from search summaries), (b) not a single dimension but two, "pulse present" and "pulse interestingly displaced", (c) not reliably derivable from audio at all and best treated as annotation-only. Phase 2 decides.
- **Provisional reliability:** pulse strength medium to high on electronic music, low on acoustic or irregular. Syncopation and groove low to medium.

### 4. Tension and release

- **Concept:** felt anticipation, instability and resolution, tied to expectation (Solberg frames build-ups through expectancy, A). Listeners' ratings lag the audio by seconds (4.5 s lag fitted in TenseMusic, A).
- **Kind:** state and trend together. Release is closely linked to events (drop) but is broader (a harmonic resolution is release without a drop).
- **Candidate cues:** the six TenseMusic features (A, classical only), plus in EDM the production techniques catalogued by Solberg (A, two tracks).
- **Competing hypotheses:** (a) a tension estimate from weighted feature slopes across separate windows, as TenseMusic does, (b) tension as expectation violation and fulfilment, requiring a model of what was expected, which is harder and possibly needed for drops, (c) different genres need different cue sets. For dance music none of these is validated.
- **Provisional reliability:** the loudness and onset parts high, harmonic tension from room audio of polyphonic music low (H), the composite unknown.
- **Out of reach when:** the transform cannot resolve pitch. Tonal tension needs semitone resolution in the low registers, a few Hz below 200 Hz. **Unlocks with:** a long-window FFT, a constant-Q transform or a low-register filter bank. The loudness and onset parts of tension need none of that and are available on almost anything.

### 5. Momentum and direction

- **Concept:** the music moving toward or away from something, at any timescale.
- **Kind:** trend.
- **Candidate cues:** slopes of any measurable state over its own window. There is no reason it is one number, since the loudness may fall while density rises.
- **Competing representations:** a trend attached to each state coordinate, or a small number of composite directions such as "preparing" and "releasing". Which is better depends on what phase 2 shows about how directions co-occur.
- **Provisional reliability:** medium.

### 6. Structural boundary and section

- **Concept:** the point at which listeners hear a new part begin. Not an instant in the audio so much as a judgement that some aspect of the music has changed character (Nieto, B: annotators disagree and structure is hierarchical).
- **Kind:** event, with a graded strength, and a hierarchy of scales.
- **Candidate evidence, each a different detector, none the definition:** novelty in timbre, in harmony, in rhythm, in texture, or in several at once (Müller, B). Repetition boundaries (a returning section). Silence or a break. Fading in or out.
- **Competing hypotheses:** (a) boundary as a change point in a feature space, (b) boundary as a change in *role*, seen only relative to what preceded it (Yadati find a change point first and then judge its role, A), (c) boundaries are inherently listener-dependent and a system should output a graded "something changed" per dimension instead of a boundary time.
- **Causal problem:** the literature read does not report a causal method (Nieto, B). Offline methods can look ahead and a device cannot.
- **Provisional reliability:** low to medium. Per-dimension change strength is likely more reliable than a single boundary.
- **Out of reach when:** only a few seconds of history are kept, or look-ahead is forbidden. The literature read reports no causal method. **Unlocks with:** tens of seconds of feature memory, or a buffer that permits confirming a boundary slightly after it passed. Offline or host-side this is a largely solved shape, at 60-70% F1 against a 90% human ceiling.

### 7. Repetition and novelty as perceptual properties

- **Concept:** the passage sounds like something already heard, or sounds new. Distinct from being a detector for boundaries. Repetition gives familiarity and a sense of pattern, novelty gives surprise.
- **Kind:** state.
- **Candidate cues:** self-similarity of a chosen feature against the past (Müller, B), at several memory lengths. Needs memory of tens of seconds to minutes. This is a budget rather than a wall, see the signal-path section: a compressed per-dimension summary at a low rate costs on the order of a hundred KB for a minute.
- **Competing hypotheses:** measure similarity in one feature space, or separately per dimension (rhythmic repetition versus harmonic versus timbral repetition, which can differ). Compressed summaries of the past may be enough.
- **Provisional reliability:** unknown. The limit is which feature space stays meaningful over minutes through a room microphone, not the storage.
- **Out of reach when:** memory is shorter than the repetition being detected. **Unlocks with:** minutes of compressed per-dimension summary — a modest budget on anything with external RAM, infeasible in a few hundred KB shared with frame buffers.

### 8. Build-up (preparation)

- **Concept:** a stretch that makes the listener anticipate an arrival. Defined by its role, not by any one property of the audio.
- **Kind:** structural state that unfolds over time. It can be *confirmed* while in progress and cannot be known at its start.
- **Documented mechanisms** (A, Solberg, two EDM tracks, no listener data): rising sweeps (uplifters), progressively shorter note values, large frequency changes, removal of bass and bass drum, a contrasting ambient breakdown before it. Yadati (A) also note that the build is visible in the spectrogram before a drop.
- **Other plausible mechanisms outside EDM (H):** harmonic pedal or dominant preparation, an accelerando, a rising melodic line, growing dynamics, thinning texture, a rhythmic subdivision that increases.
- **Why not "loudness, onset rate and brightness rising, bass falling":** that is one mechanism (a riser with a bass cut), not the concept. A build can hold constant loudness and be made entirely of pitch, subdivision or texture change. A breakdown-then-build has low energy at its start.
- **Competing hypotheses:** (a) build as a detectable pattern of co-moving trends, with the set of trends varying by mechanism, (b) build as a *probability of imminent arrival* that rises with recognised preparation cues, (c) build as a listener judgement that must be learned from annotation, with no fixed cue definition.
- **Provisional reliability:** low to medium, confidence grows only as the passage continues.
- **Out of reach when:** never entirely. A build is confirmable while in progress on any hardware, since the core cues are trends in loudness, onset rate, brightness and band balance. What varies is how many mechanisms are recognisable: the pitch-based ones (rising melodic line, harmonic preparation, accelerando) need pitch, the rest do not.

### 9. Drop, impact, arrival

- **Concept:** an arrival that releases prior preparation. Defined by the relationship to what came before, not by its own acoustic profile. Yadati (A): a release after a build with the bassline re-introduced, no precise recipe.
- **Kind:** event.
- **Distinguish:** an arrival after preparation (a drop), an impact with no preparation (a hit or stab), and a return after a breakdown that was not built up to. These may need to be different events.
- **Candidate evidence (all hypotheses):** an abrupt change in low-frequency energy, rhythm pattern or density coinciding with the end of a preparation. A change in rhythm at the drop was noted by Yadati (A).
- **Competing hypotheses:** (a) drop as a change point that is then judged by what preceded it, as in Yadati (A), (b) drop as fulfilment of a raised expectation, so it is defined by how much preparation existed, (c) drop as purely annotation-defined.
- **Latency:** a causal system recognises the arrival after it happens. What it can supply beforehand is the expectation from preparation, if a build can be recognised (concept 8).
- **Provisional reliability:** medium for something abrupt happening, low for correctly calling it a drop.
- **Out of reach when:** no preparation state exists to relate the arrival to. Detecting *something abrupt* is cheap everywhere; calling it a drop rather than a stab requires concept 8 to be running. **Unlocks with:** a working build estimate, not with better audio features.

### 10. Breakdown and withdrawal

- **Concept:** a deliberate thinning or removal of layers, usually of rhythm and bass, often as contrast (Solberg, A).
- **Kind:** structural state.
- **Candidate evidence:** fall in density and low-frequency content that is held, and a change in the ratio of sustained to percussive content (H).
- **Distinguish from:** a gradual fade, a quiet verse, and silence. These differ by how the withdrawal happens and what is left.
- **Provisional reliability:** medium.

### 11. Tease and withheld release

- **Concept:** an expected arrival that is delayed, partial or refused.
- **Kind:** event, recognised in retrospect.
- **Depends on** an expectation existing, so it cannot be defined without concept 8 or 9. It may not be a separate concept from a partial drop.
- **Provisional reliability:** low. Possibly annotation-only.
- **Out of reach when:** no expectation is modelled. Strictly downstream of concepts 8 and 9 — a tease cannot exist without a raised expectation to refuse. **Unlocks with:** whatever makes build-up reliable, plus a way to represent an expectation that went unmet, which no iteration has attempted.

### 12. Climax

- **Concept:** a peak in the musical journey, structurally and contextually. Not the loudest moment and not "near the running maximum", which is a description of a level and misses that a climax is a peak relative to the surrounding development, and a track can have several or none.
- **Kind:** structural state, judged relative to context, often known only in hindsight.
- **Candidate evidence (H):** combination of sustained high arousal, density and register after a preparation, which resembles a drop's aftermath.
- **Competing hypotheses:** climax as a property of the track-level contour needing look-back over minutes, or as a local peak that can be flagged causally with lower confidence.
- **Provisional reliability:** low.
- **Out of reach when:** history is shorter than the contour being judged. The track-scale reading needs minutes and hindsight. **Unlocks with:** whole-piece analysis, a host or offline capability. A short-window reinterpretation (a local peak against the last minute) is a weaker and different thing, and whether it is worth having is untested.

### 13. Beginning, ending, fades and cadences

- **Concept:** entering and leaving a piece, and its large-scale closure.
- **Kinds:** fade-out and fade-in are trends. Silence is a state. A cadential ending is harmonic and rhythmic closure, an event.
- **Candidate evidence:** long monotone decay toward the noise floor for a fade (H, likely reliable). Cadential ending needs harmonic and rhythmic closure detection (H, low).
- **Provisional reliability:** fades high, cadential endings low, intros unknown.

### 14. Abrupt change between very different states

- **Concept:** the music jumps to a passage that differs on many dimensions at once. Relevant because the owner expects arbitrary jumps.
- **Kind:** event.
- **Candidate evidence:** several independent change measures firing together, which is a claim about co-occurrence of changes, and it should be checked that a joint change is what listeners hear as a jump (H).
- **Provisional reliability:** medium.

### 15. Timbral and spectral character

- **Concept:** the sound's colour, from dark and warm to bright and sharp, smooth to rough, thin to full.
- **Kind:** state.
- **Candidate cues:** spectral centroid and its trend, spectral spread and flatness, band balance, roughness (a TenseMusic input, A, computed with a psychoacoustic algorithm and probably too costly for a device), high-frequency energy (Juslin cue for anger, A).
- **Caveat:** room microphone response, distance and noise change absolute values, so any use must be relative.
- **Provisional reliability:** brightness high, roughness low to medium.

### 16. Emotional character

- **Concept:** what the music feels like at a level above intensity: dreamy, ominous, euphoric, playful, tender. Whether it is one dimension, several, or a set of categories is itself unsettled in the literature and not decided here.
- **Kind:** state, slowly varying.
- **What the reading supports (A, Juslin and Laukka):** tempo, sound level, level variability and high-frequency energy carry information about intended emotion, and vocal and musical channels share cue patterns. This is a partial account.
- **What it does not settle (H):** whether harmony, mode, tonality, melodic contour, timbre, instrumentation and roughness contribute *independently* of those cues. The pointer that valence relates to mode and harmony while arousal relates to tempo, loudness and timbre comes only from search summaries. If true it means an important part of emotional character lies in exactly the information (harmony, tonality) that a room microphone on a microcontroller measures worst.
- **Competing hypotheses:** (a) emotional character is largely recoverable from the arousal-type coordinates plus timbre, so no separate detector is needed, (b) a separable harmonic and tonal component matters and needs its own evidence such as key mode or chroma stability, (c) part of it is instrumentation and genre convention and is not recoverable by simple acoustic features at all, which would make it annotation and offline-analysis territory.
- **Provisional reliability:** unknown. Not to be assumed derivable from other coordinates.
- **Out of reach when:** the transform cannot resolve pitch, *if* hypothesis (b) is right and a separable harmonic component matters. If (a) is right, the tempo, level, level-variability and brightness cues suffice and this needs nothing special. **Unlocks with:** pitch resolution, and separately with anything that can apply a learned model, since (c) holds that part of this is instrumentation and genre convention that simple acoustic features will not reach at all. The concept most likely to be transformed by a host-side or model-driven iteration.

### 17. Track change and no-music

Added in review. Every concept above is about the music. The device hears a *room*, and two of the states it will spend real time in are not musical at all.

- **Concept:** one piece ends and another begins, or nothing is playing (silence, talking, a gap between tracks, noise).
- **Kind:** event (track change) and state (no-music).
- **Why it belongs here:** a track change is the moment every accumulated baseline, running range, short memory and hysteresis state is known to be invalid, and clearing it is exactly what hysteresis otherwise prevents. No-music matters because without it the strip animates the noise floor and room chatter as if it were music, and at a live event that state happens constantly.
- **Priority, set by the owner:** lower than the review claimed. Worth representing, not worth a dedicated detector. A large BPM shift or a joint jump across coordinates is expected to be a good enough proxy, which is a hypothesis for phase 2 like any other. No-music is the more valuable half.
- **Candidate evidence (H):** a joint change across most coordinates at once (this is concept 14 with a longer confirmation window), a gap or near-silence, an abrupt loudness and spectral discontinuity, a tempo that stops matching. For no-music: gate state, absence of periodicity in the onset envelope, speech-like spectral behaviour.
- **Provisional reliability:** no-music high. Track change unknown, and likely to fall out of the coordinates rather than need its own evidence.

## Timescales

The one verified statement about timescales is that different aspects of a perceptual property needed different memory lengths (TenseMusic, A, table above), from about 3 s to 20 s, with a listener lag of about 4.5 s. Structure operates hierarchically from seconds to minutes (Nieto, B). Yadati's drop candidates came from segments averaging 16.5 s (A).

Beyond that, a timescale assigned to a concept above is a hypothesis. A single global smoothing constant is unlikely to suit all of them, and that is the only conclusion drawn.

## What an ideal engine should receive

Provisional and about information, not consumption. No decision here about how animations use it.

- The properties that phase 2 shows to be measurable, each with its own timescale, and for each a measure of how confident the system is.
- Change and trend information per property instead of one global "mood".
- Structural states and events kept separate from states, since they differ in how they arise and when they can be known.
- For events, a latency and a confidence, and where possible an *expectation* signal before the event.
- Multiple competing readings where the research has not settled one, rather than a single forced answer. Whether the engine can use "uncertain between two readings" is a consumer question left for later.
- Nothing that presupposes an order or a path between passages.

## Research method

Durable. How you find out whether a representation of music is any good, independent of what is being built. An iteration picks how much of this it can afford; a later one with more budget picks more. A review pass cut most of this as over-engineering for a wearable, which confused *what this iteration will pay for* with *what the method is*. Recorded in full so the choice stays available.

**Three sources, which answer different questions and must not be merged.**

| Source | What it tells you |
|---|---|
| Human annotation | what was perceived |
| Offline computer MIR analysis | what acoustic and musical evidence existed |
| The runtime pipeline | which of that evidence survives the capture path in the conditions the build runs in |

Offline MIR is a **reference measurement, not ground truth**. Agreement with annotation says the evidence exists in the audio. Disagreement is informative and is not an error to be corrected. The third row separates "this concept is hard" from "this concept is hard *through this path*", which is the distinction that decides whether better hardware would help.

**Dual capture.** Where possible record each piece twice at once: a clean digital copy, and the room-microphone capture of it playing. Running the same offline analysis on both isolates what the microphone path destroys from what the concept itself makes hard. Cheap to do, and the only way to attribute a failure correctly. Worth doing even when an iteration has no budget for the rest of this.

**Material selection: chosen to make hypotheses fail, not to confirm them.** A steady dance track, an ambient track, an aggressive or noisy one, several build-and-drop cycles, a fake or withheld drop, a breakdown not followed by a drop, acoustic or irregular timing, a fade-out, an abrupt cut between very different passages, a passage where density and activity separate, and a continuous stretch spanning several pieces with the real gaps and talking between them. Several minutes each, since anything with a window longer than a few seconds needs it.

**Annotation.**

- Annotate concepts independently of any detector. Mark what was *heard* as a build and how strongly, never "where loudness rose". Marking after looking at a trace tests nothing, so the order is fixed: listen and mark first, compare after.
- Continuous ratings for states, time marks with a strength for events. Continuous rating is expensive and fatiguing — TenseMusic needed 24 listeners on 60-90 s excerpts for one dimension — so an iteration that cannot afford it uses sparse marks and segment labels and accepts the coarser result.
- **Keep disagreement between annotators; do not resolve it.** For boundaries and groove, disagreement is the expected result and is itself the finding (Nieto). A protocol that averages it away destroys the main thing it had to report.
- A second listener matters most for the subjective concepts: groove, tension, emotional character, perceived build. Two annotators on everything is not required.
- Annotate hierarchy loosely, allowing coarse and fine boundaries, since structure is hierarchical and a single boundary layer forces a false choice.
- Pick the annotation format before recording. It determines the analysis code, and re-annotating because the format was wrong costs more than the annotation did.

**Analysis.** Per concept, compare several candidate detectors against the annotations and against each other, and record where they disagree. **A concept can finish as "no reliable detector found", which is a valid result and must be recorded with what was tried** — that record is what stops the next iteration repeating the same attempt, and what tells it which constraint to lift.

## Sources

The findings, concepts, reading queue and phase 2 protocol from this document are also kept as queryable data in `docs/music-research/`. Start there in a later session and read this file only for the reasoning.

Read in this session (A):
- [Barchet et al., TenseMusic, PLOS ONE 2024](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0296385)
- [Yadati et al., Detecting drops in electronic dance music, ISMIR 2014](https://archives.ismir.net/ismir2014/paper/000297.pdf)
- [Solberg, Waiting for the Bass to Drop, Dancecult 2014](https://dj.dancecult.net/index.php/dancecult/article/view/451) (abstract and analysis sections)
- [Juslin and Laukka, Communication of emotions in vocal expression and music performance, Psychological Bulletin 2003](https://www.brainmusic.org/EducationalActivities/Juslin_emotion2003.pdf) (cue table)
- [Witek et al., Syncopation, body-movement and pleasure in groove music, PLOS ONE 2014](https://journals.plos.org/plosone/article?id=10.1371/journal.pone.0094446) (design and findings as summarised on the page)

Partly read or read as a page summary (B):
- [Nieto et al., Audio-based music structure analysis, TISMIR 2020](https://transactions.ismir.net/articles/10.5334/tismir.54)
- [Müller, Fundamentals of Music Processing, ch. 4](https://www.audiolabs-erlangen.de/resources/MIR/FMP/C4/C4.html)
- [Essentia streaming music extractor](https://essentia.upf.edu/streaming_extractor_music.html) (descriptor families only)

Pointers only (H), not yet read. Fetching these returned HTTP 403 (ScienceDirect, Taylor and Francis, University of California Press) or a bot check (PMC), so they need open-access copies or manual download:
- [Review of research on musical groove](https://www.sciencedirect.com/science/article/pii/S0149763423004918)
- [Audio features underlying perceived groove and sensorimotor synchronization](https://www.researchgate.net/publication/291351443_Audio_Features_Underlying_Perceived_Groove_and_Sensorimotor_Synchronization_in_Music)
- [Syncopation and groove in polyphonic music, Music Perception 39(5)](https://online.ucpress.edu/mp/article/39/5/503/182325/Syncopation-and-Groove-in-Polyphonic-MusicPatterns)
- [Audio features dedicated to the detection of arousal and valence](https://www.tandfonline.com/doi/full/10.1080/24751839.2018.1463749)
