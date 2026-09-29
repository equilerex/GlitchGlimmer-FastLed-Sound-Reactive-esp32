// The page learns its pixel counts from two builds: the live WASM engine reports
// the counts it was compiled with, and web/data/manifest.json carries the counts
// stamped by the native harness that recorded it. Both come from Config.h, but a
// stale web/data/ or a hand-edited manifest breaks that. This names the
// disagreement so the page can show it rather than silently rebuild the bench.
//
// Returns null while either side is unknown or when they agree.
export function countMismatch(liveCounts, recordingCounts) {
  if (!liveCounts || !recordingCounts) return null;
  const same = liveCounts.length === recordingCounts.length &&
    liveCounts.every((c, i) => c === recordingCounts[i]);
  if (same) return null;
  return `Pixel counts differ: live engine ${liveCounts.join(' + ')}, ` +
    `recording ${recordingCounts.join(' + ')}. ` +
    'web/data/ was recorded against a different Config.h. Rerun npm run frames -- --build.';
}
