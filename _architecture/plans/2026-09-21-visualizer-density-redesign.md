# visualizer-density-redesign

Session: 2026-09-21. Status: designed; implementation not started. Depends on `2026-09-21-structural-event-lifecycle.md` for the structure card's data (displacement, arming, episode state, all produced by the firmware).

## Context

The telemetry column scrolls about three screens, so related values cannot be read together. Most rows are redundant or carry no information while the system is healthy.

- The same data appears twice. Level, dynamics, BPM and energy sit in the header and again under "What the classifier reads". Bass, mid and treble sit in "Frequency drives" and again in "Bands". The eight coordinates sit in "Music coordinates" and again in "Music coordinates (8D state)", where the second copy exists only to show `conf` and `trend`.
- Health values (`*conf`, `signal present`, `gate gain`, `presence`) sit near 100% whenever the system works and use a full row each.
- The Structure section is five rows of zeros. The cause is in the lifecycle plan.
- Tuning sliders take about 15% of the column and are needed only while tuning.

Goal: the values that explain the sound engine fit on one 1080p screen without scrolling, and a fault in a health value is still visible at a glance.

## Health cells

A coordinate or level row has two layers in one cell. The top layer is the actual value (number, short bar, trend arrow). A 2 px strip along the cell's bottom edge is the health value (the confidence for that coordinate), filled left to right. It is coloured by threshold: green at 0.8 and above, amber from 0.5 to 0.8, red below 0.5. The raw number is in the tooltip.

Pure health signals that have no value of their own become booleans:

- `signal present`, `presence`, `gate open`: a dot and a word in one health line.
- `noise floor`, `gate gain`, `spectral flatness`: kept as small numeric chips, because their magnitude means something. They are amber or red only when out of range.

The health line sits under the header as a single row of chips.

## Layout (two columns of rows, no scroll at 1080p)

- Header: level, BPM and beat phase, gate, mood and scene text chips, spectrum with bass, mid, treble as thin vertical meters beside it. "Frequency drives" and "Bands" merge into this.
- Music coordinates: the eight coordinates in two columns of four, each a health cell as above with `trend`. The 8D state card is removed.
- Structure card: the diverging displacement bar and the tease, anomaly and drop badges from the lifecycle plan, next to a taller event stream instead of below it.
- Classifier inputs: `level`, `dynamics`, `bpm` live in the header. Raw `volume`, `peak`, `average` go to a collapsed diagnostics block.
- Output and scene clock: one line for strip lit, brightness and layer count; the scene clock stays a thin bar with its min and ideal markers.
- Progress bars are shortened to about half their current length. Range still reads, and the freed width holds the second column.

## Tuning drawer

The tuning sliders leave the telemetry column. They open in the right-hand panel that currently holds the LED settings (`hw-open`, docked at 1101 px and up, slide-over below), switched by a two-tab header: `LED` and `Tuning`. Only one is visible at a time. Tuning is the case where the values being tuned must stay visible, so on wide screens the panel keeps the telemetry column fully on screen, and on narrow screens the slide-over covers it and the tab is unusable for live tuning. `Reset tuning` moves with the sliders.

## Files

- `web/index.html`: restructure the telemetry cards, health cells, tab header in the right panel.
- `web/style.css`: health-cell strip, two-column rows, shortened bars, chips.
- `web/main.js`, `web/state.js`: the active right-panel tab and its persistence, defaulting to `LED`.
- `web/live.js`: the `SECTIONS` table around line 777 gets the merged rows, the health tags and corrected note text (the `buildup` and `descent` tooltips currently describe a 0..1 window score that does not match the firmware).
- `web/CONTEXT.md`, `_architecture/VISUALIZER_UI.md`: update the layout description afterwards.

## Build order

- [ ] Health cell component and the chip row, verified with a screenshot at 1920x1080 using the Demo Signal button (visual layout only, not detector behaviour).
- [ ] Merge the duplicate cards and move to two columns; measure that the telemetry column height fits in one viewport.
- [ ] Move the tuning sliders into the right panel tab.
- [ ] Slot in the structure card once the lifecycle plan's Task 5 card is available.
- [ ] Check 1101 px and phone widths.
- [ ] Update `web/CONTEXT.md` and `VISUALIZER_UI.md`.

## Implementation deviations
