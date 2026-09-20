# CONTEXT — docs/music-research
updated: 2026-09-21

<!-- All four sections stay even when briefly empty — an absent section is
     indistinguishable from an omission.

     Does not go in: anything the code plainly shows; API or parameter docs (those
     belong in code); changelog or commit history (git has it); general framework or
     language behaviour (the model has it).
     Test: if removing a line would not slow a newcomer down, cut it. -->

## What this is

The durable music model for expressive lighting, and the reference data behind it. Hardware-independent and not rewritten per iteration: nothing here assumes an ESP32, a microphone, real-time operation or a wearable.

`MODEL.md` is the narrative — concepts, what the sources establish, timescales, research method, and per concept an "out of reach when / unlocks with" note. The JSON files are the same material as a lookup, so a session can query one entry instead of loading the narrative.

Iteration-specific material — board, FFT size, latency policy, what this build will attempt, phase ordering — lives in a dated plan under `_architecture/plans/` that cites this folder. The current one is `2026-09-21-music-model-for-lighting-research.md`, an ESP32-S3 wearable.

| File | Holds | Query by |
|---|---|---|
| `concepts.json` | 18 concepts, each defined apart from any detector, with kind, candidate cues, competing hypotheses, provisional reliability, source ids | `id` |
| `sources.json` | sources read, with an evidence grade and atomic findings that carry their own `limit` and, where useful, `data` (for example the TenseMusic window table) | finding `id`, referenced from `concepts.json` |
| `reading-queue.json` | sources identified but not yet read, why, and what blocked reading | `needed_for` |
| `phase2-protocol.json` | how the real-audio test is to be run, tracks wanted, annotation rules, decisions made, open constraints | whole file, it is short |
| `MODEL.md` | the narrative model: concepts, findings, timescales, method, unlock notes | read it |

Grades: **A** read the paper, **B** read an abstract or page summary, **H** hypothesis, no source read. Everything in `concepts.json` about reliability is provisional until phase 2.

## Why it's built this way

Findings are stored as one claim each with a limit, so a future session cannot quote a number without the caveat attached. Copyrighted papers are not stored, only the notes and the URL, so re-reading means fetching by URL. Nothing here is a design. It answers what is known about music for visualization and what is not.

The durable/iteration split exists because a review pass deleted concepts and research method for not fitting the current wearable build. That is the failure this layout prevents. A concept the current hardware cannot reach is still correct about music, and the reason it is blocked is recorded as a shopping list — which is exactly what a later host-side, stronger-hardware or model-driven version needs and would otherwise re-derive from the papers.

To read a slice without loading the file:

```bash
python -c "import json;print([c for c in json.load(open('docs/music-research/concepts.json')) if c['id']=='build_up'])"
```

## Gotchas

- Publisher pages (ScienceDirect, Taylor and Francis, University of California Press) return 403 to fetch. PMC returns a bot check. Use the open-access PDF or download manually, then extract the text with `pypdf`.
- TenseMusic's windows and correlations come from Western classical music. They support "different aspects need different memory lengths" and nothing about dance music through a microphone.
- Juslin and Laukka were read only in the tempo, sound level, level variability and high-frequency rows. Do not cite them on mode or articulation.
- The claim that valence follows mode and harmony while arousal follows tempo, loudness and timbre has no source read. It is in `concepts.json` marked as unsettled.

## Don't

- Do not turn a `cues` list into a definition. The concept is what listeners hear, the cues are candidate detectors. The first draft got this wrong for build-up, drop, boundary and climax.
- Do not order the concepts or put them on a scale. The owner rejected a ladder or fixed path.
- Do not drop a concept because exact detection is hard. The wearable scope loosens how precisely things must be inferred, not which concepts are worth representing. A concept leaves on evidence from phase 2, with what was tried recorded, never on difficulty alone.
- Do not let a `reliability` field become a target flag. It is a provisional engineering estimate of a proxy, not a decision about the concept.
- Do not add a finding without a grade and a `limit`.
- Do not write a board, an FFT size, a memory budget or a latency policy into this folder. Those belong in the iteration plan. The only feasibility that lives here is the per-concept "out of reach when / unlocks with" note, which is durable because it describes what a *future* build would have to change.
