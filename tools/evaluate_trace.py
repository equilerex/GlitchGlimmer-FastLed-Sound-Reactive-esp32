import csv
import json
from pathlib import Path

def evaluate(trace_path: Path, gt_path: Path):
    with open(gt_path, 'r', encoding='utf-8') as f:
        gt = json.load(f)

    with open(trace_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        rows = list(reader)

    fps = 30.0
    dt = 1.0 / fps

    print(f"Loaded trace: {len(rows)} frames ({len(rows)*dt:.1f} s)")
    
    # 1. Inspect ground truth drops
    drops = gt['drops']
    print(f"\n--- Ground Truth Drops ({len(drops)} onsets) ---")
    for i, d in enumerate(drops):
        frm = int(round(d * fps))
        if frm < len(rows):
            r = rows[frm]
            print(f"Drop #{i+1:02d} at {d:6.2f}s (frm {frm:5d}): level={float(r['level']):.2f} "
                  f"bassL={float(r['bassLevel']):.2f} midL={float(r['midLevel']):.2f} "
                  f"trebL={float(r['trebleLevel']):.2f} dyn={float(r['dynamics']):.2f} "
                  f"buildup={float(r['buildup']):.2f} descent={float(r['descent']):.2f} "
                  f"mood={r['mood']}")

    # 2. Check detected drops in trace
    detected_drops = []
    for i, r in enumerate(rows):
        if float(r.get('dropDetected', 0.0)) > 0.5:
            detected_drops.append(i * dt)

    print(f"\n--- Detected Drops in Trace: {len(detected_drops)} ---")
    for t in detected_drops:
        print(f"  Detected drop at {t:.2f}s")

    # Match drops within tolerance window (e.g., 0.5s)
    tolerance = 0.5
    matched_gt = set()
    matched_det = set()
    for det_idx, det_t in enumerate(detected_drops):
        for gt_idx, gt_t in enumerate(drops):
            if abs(det_t - gt_t) <= tolerance and gt_idx not in matched_gt:
                matched_gt.add(gt_idx)
                matched_det.add(det_idx)
                break

    tp = len(matched_gt)
    fp = len(detected_drops) - len(matched_det)
    fn = len(drops) - tp
    precision = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    recall = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = 2 * precision * recall / (precision + recall) if (precision + recall) > 0 else 0.0

    print(f"\nDrop Detection Metrics (tol={tolerance}s):")
    print(f"  TP: {tp}, FP: {fp}, FN: {fn}")
    print(f"  Precision: {precision:.2f}, Recall: {recall:.2f}, F1-Score: {f1:.2f}")

    # Inspect the quiet level before drop 1
    d0 = drops[0]
    frm0 = int(round(d0 * fps))
    print(f"\nPre-drop window before Drop #1 (around {d0}s):")
    for f in range(max(0, frm0 - 25), min(len(rows), frm0 + 10)):
        t = f * dt
        r = rows[f]
        print(f"  t={t:5.2f}s: lvl={float(r['level']):.2f} bassL={float(r['bassLevel']):.2f} midL={float(r['midLevel']):.2f} trebL={float(r['trebleLevel']):.2f} descent={float(r['descent']):.2f} mood={r['mood']}")

if __name__ == '__main__':
    import sys
    base_dir = Path(__file__).resolve().parent.parent
    trace = Path(sys.argv[1]) if len(sys.argv) > 1 else base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'
    gt = Path(sys.argv[2]) if len(sys.argv) > 2 else base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json'
    evaluate(trace, gt)
