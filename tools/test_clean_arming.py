import csv
import json
import math
from pathlib import Path

def test_fixed_arming():
    base_dir = Path(__file__).resolve().parent.parent
    trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'
    gt_path = base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json'

    with open(gt_path, 'r', encoding='utf-8') as f:
        gt = json.load(f)
    ground_truth_drops = gt['drops']

    with open(trace_path, 'r', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))

    fps = 30.0
    dt = 1.0 / fps
    n = len(rows)

    alpha_lvl = 1.0 - math.exp(-dt / 10.0)
    alpha_bass = 1.0 - math.exp(-dt / 2.0)

    slow_l = float(rows[0]['level'])
    slow_b = float(rows[0]['bassLevel'])

    quiet_start_ms = 0
    in_quiet = False
    armed = False
    armed_until_ms = 0
    last_drop_ms = -100000

    DROP_ARM_MS = 250
    ARM_WINDOW_MS = 250
    COOLDOWN_MS = 3000

    detected = []

    for i, r in enumerate(rows):
        now_ms = i * (1000.0 / fps)
        lvl = float(r['level'])
        b = float(r['bassLevel'])
        m = float(r['midLevel'])
        tr = float(r['trebleLevel'])

        slow_l += (lvl - slow_l) * alpha_lvl
        slow_b += (b - slow_b) * alpha_bass

        disp_l = lvl - slow_l
        disp_b = b - slow_b

        # Quiet condition: slowLevel < 0.55 OR slowBass < 0.25
        is_quiet = (slow_l < 0.55) or (slow_b < 0.25)

        if is_quiet:
            if not in_quiet:
                in_quiet = True
                quiet_start_ms = now_ms
            elif now_ms - quiet_start_ms >= DROP_ARM_MS:
                armed = True
                armed_until_ms = now_ms + ARM_WINDOW_MS
        else:
            in_quiet = False

        is_armed = armed and (now_ms <= armed_until_ms)
        if now_ms > armed_until_ms:
            armed = False

        slam = (disp_l >= 0.35) or (disp_b >= 0.45 and b >= 0.55)
        breadth = (b >= 0.55) and (m >= 0.50) and (tr >= 0.35)

        if is_armed and slam and breadth and (now_ms - last_drop_ms >= COOLDOWN_MS):
            detected.append(now_ms / 1000.0)
            last_drop_ms = now_ms
            armed = False

    matched_gt = set()
    matched_det = set()
    for det_idx, det_t in enumerate(detected):
        for gt_idx, gt_t in enumerate(ground_truth_drops):
            if abs(det_t - gt_t) <= 0.5 and gt_idx not in matched_gt:
                matched_gt.add(gt_idx)
                matched_det.add(det_idx)
                break

    tp = len(matched_gt)
    fp = len(detected) - len(matched_det)
    fn = len(ground_truth_drops) - tp
    prec = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    rec = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = 2 * prec * rec / (prec + rec) if (prec + rec) > 0 else 0.0

    print(f"Fixed Arming Logic:")
    print(f"  TP: {tp}/15, FP: {fp}, FN: {fn}")
    print(f"  Precision: {prec:.2f}, Recall: {rec:.2f}, F1: {f1:.2f}")
    print(f"  Detected timestamps: {[round(t, 2) for t in detected]}")

if __name__ == '__main__':
    test_fixed_arming()
