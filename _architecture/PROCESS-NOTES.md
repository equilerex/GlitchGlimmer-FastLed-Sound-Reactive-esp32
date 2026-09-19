# Process notes

What worked, what was wasted, and what to change next time. Written 2026-09-18 after the
ten-task visualiser rebuild, but the recommendations are meant to outlive that task.

Kept as a living file. Add to it after any session worth learning from; delete advice that stops
being true.

## The single most expensive mistake

**The design was under-scoped, and one whole task was built and thrown away.**

The session started by building an interactive mockup, which the owner approved. From there the
spec described *swapping the renderer inside the existing page*. What the owner actually wanted
was *the mockup, shipped, with the existing diagnostics folded into it*. That gap sat unnoticed
through seven tasks and surfaced only when the owner looked at the running page.

The cost: one task rebuilt from scratch, a plan rewritten mid-run, and a stretch of work whose
output was correct against the spec and useless against the intent.

The cause was not a missing question — the question was asked early, and answered. The cause is
that the answer was translated into a spec that quietly narrowed it, and the narrowing was never
read back to the owner.

**Rule for next time.** When a visual artefact has been approved, say in one line whether it is
*the target* or *a reference*, and get that confirmed before writing a plan. If it is the target,
the artefact goes in the repo as the design source and the plan's job is porting functionality
into it, not the reverse. Doing this late worked well — `web/design/mockup-reference.html` in the
repo, read directly by the implementer, produced the right result first try. Doing it at the start
would have saved the whole detour.

## Most defects were in the plan, not the implementation

Roughly nine findings came out of review across ten tasks. Six were defects in the plan's own
text, not implementer errors:

- a float-equality assertion that fails against correct code (`120 * 16.7 !== 2004`)
- a test asserting behaviour the plan's own data could not produce (silicone "fuses")
- line numbers into a file that had drifted
- a tick-label loop with no clamp against the canvas edge
- a `try/catch` around only the parse, leaving the code after it to kill the page
- a one-shot binding against an architecture where the object being bound gets replaced

The pattern is specific: **the plan's prose was sound; its code blocks were not.** They were
written from memory and pasted into briefs without being run once. Every one of those six would
have been caught by executing the snippet before shipping it.

**Rule for next time.** Any pure-logic code that goes into a plan gets run once before the plan is
finished — the test assertions especially, since a wrong expected value is invisible until an
implementer wastes a cycle on it. Code that needs a browser can stay unproven, but say so in the
brief so the implementer treats it as a sketch.

What worked as a counterweight: every dispatch carried the instruction *if the brief contradicts
the code, stop and report rather than adjust either side to fit*. That instruction caught the
silicone contradiction, the mode-switch binding bug, and two others. It costs one line and is
worth keeping in every dispatch forever.

## Briefs written far ahead go stale

Briefs for tasks 5 through 10 were extracted before tasks 4 through 9 existed. Every one of them
contradicted the code by the time it ran: a dead import the brief told the implementer to re-add,
a CSS block in an idiom that had been replaced, a function the brief extended that no longer
existed. Each needed a correction in the dispatch.

That is survivable but it means the brief is no longer the single source of requirements, which is
the property that makes the whole pattern work.

**Rule for next time.** Write plans in full, but **extract briefs no more than two tasks ahead of
the work**, and re-extract after any task that changes an interface. Cheap, mechanical, and it
removes a whole class of dispatch-time patching.

## Working in a dirty tree cost more than it saved

`web/app.js`, `web/live.js`, `web/index.html` and `web/style.css` were already heavily modified
before this work began, from an unrelated in-flight migration. Consequences, all real:

- Review packages contained changes the task did not make, and every reviewer had to be told in
  its prompt which parts to ignore. That is prompt space spent on bookkeeping.
- The plan's constraint "app.js and live.js are never modified" was true of the tasks and false of
  the tree, so the final review flagged it as a Critical it could not resolve.
- No baseline existed to diff against, so binding preservation across a file rewrite had to be
  verified by extracting every `v-model` and `{{ }}` from HEAD and comparing sets by hand.

**Rule for next time.** Commit before starting agent-driven work. Not for safety — for
reviewability. A clean baseline is what makes every downstream diff mean something.

A second instance of the same problem appeared at the very end: a parallel session edited
`path.js` and `src/` while this one was finishing, breaking a test. **One repo, one writer at a
time**, or the two sessions will produce diffs neither can explain.

## What was worth the tokens

**Per-task review.** Every single task's review found something. Not one was ceremony. The ones
that mattered most were the ones asked a specific question rather than "review this": *does the
bench agree with the stage byte for byte*, *can a degenerate path produce NaN in render
coordinates*, *do these tests fail against the old behaviour*. Open-ended review prompts produced
open-ended findings; pointed ones produced bugs.

**Controller-side browser verification.** Keeping the runtime checks rather than delegating them
caught four things no code review could have: a layout feedback loop growing the page by a
viewport per resize, telemetry cells that never updated because nothing was painting, a stale
renderer instance, and a corrupted pose after a mode switch. Cheap, and the highest-yield activity
in the session.

**The ledger.** Twelve rulings with reasons and costs, written as they were made. It survived a
rate-limit interruption and made the final handoff possible. Worth the few hundred tokens per
entry.

**The "stop and report" instruction.** Already covered above. Highest value per word of anything
in the dispatches.

## What was not worth the tokens

**Scoped re-reviews of two-line fixes.** A full agent spun up to confirm that a `lostpointercapture`
listener was added and a comment was written. The process says every fix round ends in a review;
for a change this size that is a round trip to confirm something already stated precisely in the
fix instruction. Batch trivial fixes into the next task's review instead.

**Oversized review packages.** The final package was 3,380 lines because it inlined every new file
in full. Reviewers read what they need; a file list plus the diff would have done.

**Re-extracting all briefs after the re-plan.** Mechanical, avoidable by the two-tasks-ahead rule
above.

**Uniform model tiering.** Every implementer ran on the same model, including tasks whose brief
contained the complete code and whose real work was transcription plus running the tests. Those
could have run a tier cheaper. The judgement-heavy ones — the shell rebuild, the wiring task —
earned what they cost.

## Environment friction worth writing down

- **`node --test <dir>` does not work on Node 24.** A directory argument is treated as a module to
  execute and fails with `MODULE_NOT_FOUND`. Only explicit files and glob patterns run, and globs
  need Node 22+. There is no argument form that spans Node 18 and Node 24, which is why the
  engines floor moved to 22.
- **Long heredocs fail** in this shell with `ENAMETOOLONG`. Write the content to a file and
  concatenate instead of piping a large document through a shell argument.
- **The test browser blocks the microphone**, and the demo signal produces silence there. The
  spectrum and level meter cannot be verified from it — that is an environment limit, not a page
  bug, and it should be stated as unverified rather than assumed working.
- **Browser-pane viewport emulation resets** between turns and `navigate` sometimes opens a new
  tab. Set the viewport again in the same batch as the navigation.
- **Rate limits land mid-run.** The ledger is what made that survivable; without it the session
  would have restarted work already done.

## Context that would have saved the most, had it been known up front

Ranked by what it cost not to know:

1. **The approved artefact was the target, not a reference.** Cost: one task rebuilt, one plan
   rewritten.
2. **The tree was mid-migration.** Cost: noisy review packages, a false constraint, an unresolvable
   Critical in the final review.
3. **The demo signal is silent in a headless browser.** Cost: a detour investigating a blank
   spectrum panel that was working correctly.
4. **Pixel counts are 100 and 10.** Minor, but several checks were written generically before this
   was known.

**Rule for next time.** Before planning: state the target artefact, the tree's cleanliness, and any
fixture values the work will be verified against. Three lines, and they remove most of the above.

## Checklist for the next agent-driven build

- [ ] Commit the tree first. A clean baseline is the precondition, not a nicety.
- [ ] One writer per repo at a time.
- [ ] If a design artefact was approved, put it in the repo and say whether it is the target.
- [ ] Run every pure-logic snippet the plan contains before the plan is called finished.
- [ ] Extract briefs at most two tasks ahead; re-extract after any interface change.
- [ ] Put "if the brief contradicts the code, stop and report" in every dispatch.
- [ ] Ask each review a specific question, not "review this".
- [ ] Keep runtime verification on the controller. Do not delegate the browser.
- [ ] Ledger every ruling with its cost if wrong, as it is made.
- [ ] Match the model to the task: transcription is not architecture.
