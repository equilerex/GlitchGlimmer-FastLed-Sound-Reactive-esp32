// Mode switch between the two players.
//
// Both draw to the same canvas, so exactly one may be driving it at a time:
// switching stops the outgoing player rather than only hiding its controls, or
// the two would paint over each other at their own frame rates.

import * as recording from './app.js';
import * as live from './live.js';

const panes = {
  recording: document.getElementById('pane-recording'),
  live: document.getElementById('pane-live'),
};
const modeButtons = Array.from(document.querySelectorAll('button[data-mode]'));

let mode = null;

async function select(next) {
  if (next === mode) return;

  if (mode === 'recording') recording.stop();
  if (mode === 'live') live.stop();

  mode = next;
  for (const [id, pane] of Object.entries(panes)) pane.hidden = id !== next;
  for (const button of modeButtons) {
    button.setAttribute('aria-pressed', String(button.dataset.mode === next));
  }

  if (next === 'recording') await recording.start();
  else await live.start();
}

for (const button of modeButtons) {
  button.addEventListener('click', () => select(button.dataset.mode));
}

live.init();

// Live is the default view. The page exists to show what the firmware does with
// sound, and the recording is a fixed timeline that needs a click before it is
// worth anything. ?mode=recording opens the recording instead, which is what a
// screenshot of that view needs.
const params = new URLSearchParams(window.location.search);
select(params.get('mode') === 'recording' ? 'recording' : 'live');
