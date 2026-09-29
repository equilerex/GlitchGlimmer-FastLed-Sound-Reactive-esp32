import csv
import json
import math
from pathlib import Path

def test_tuning(
    quiet_level=0.55,
    quiet_bass=0.25,
    drop_scale=0.35,
    drop_bass_scale=0.40,
    band_level=0.50,
    arm_ms=400,
    cooldown_ms=5000,
    tau_sec=3.0
):
    base_dir = Path(__file__).resolve().parent.parent
    trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'
    gt_path = base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json'

    with open(gt_path, 'r', encoding='utf-8') as f:
        gt = json.load(f)
    ground_truth_drops = gt['drops']

    with open(trace_path, 'r', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))

    fps = 30.0
    dt_default = 1.0 / fps

    slow_level = 0.0
    slow_bass = 0.0
    seeded = False

    quiet_held = False
    quiet_since_ms = 0
    last_drop_ms = -100000

    detected_drops = []

    for i, r in enumerate(rows):
        now_ms = i * (1000.0 / fps)
        lvl = float(r['level'])
        bass = float(r['bassLevel'])
        mid = float(r['midLevel'])
        treb = float(r['trebleLevel'])

        if not seeded:
            slow_level = lvl
            slow_bass = bass
            seeded = True

        dt = dt_default
        alpha = 1.0 - math.exp(-dt / tau_sec)
        slow_level += (lvl - slow_level) * alpha
        slow_bass += (bass - slow_bass) * alpha

        displacement = lvl - slow_level
        bass_disp = bass - slow_bass

        # Quiet condition: either overall level is quiet OR bass is quiet/withdrawn
        is_quiet = (slow_level < quiet_level) or (slow_bass < quiet_bass) or (bass < quiet_bass)
        if is_quiet:
            if not quiet_held:
                quiet_held = True
                quiet_since_ms = now_ms
        else:
            quiet_held = False

        armed = quiet_held and (now_ms - quiet_since_ms >= arm_ms)

        # Drop trigger: armed + slam (overall displacement OR bass displacement) + bands + cooldown
        hit = (displacement >= drop_scale) or (bass_disp >= drop_bass_scale and bass >= band_level)
        bands_ok = (bass >= band_level) and (mid >= 0.40) and (treb >= 0.35)

        if armed and hit and bands_ok and (now_ms - last_drop_ms >= cooldown_ms):
            detected_drops.append(now_ms / 1000.0)
            last_drop_ms = now_ms
            quiet_held = False # Reset arming

    # Match with ground truth drops (tolerance 1.0s)
    tolerance = 1.0
    matched_gt = set()
    matched_det = set()
    for det_idx, det_t in enumerate(detected_drops):
        for gt_idx, gt_t in enumerate(ground_truth_drops):
            if abs(det_t - gt_t) <= tolerance and gt_idx not in matched_gt:
                matched_gt.add(gt_idx)
                matched_det.add(det_idx)
                break

    tp = len(matched_gt)
    fp = len(detected_drops) - len(matched_det)
    fn = len(ground_truth_drops) - tp
    prec = tp / (tp + fp) if (tp + fp) > 0 else 0.0
    rec = tp / (tp + fn) if (tp + fn) > 0 else 0.0
    f1 = 2 * prec * rec / (prec + rec) if (prec + rec) > 0 else 0.0

    return {
        'tp': tp, 'fp': fp, 'fn': fn,
        'prec': prec, 'rec': rec, 'f1': f1,
        'detected': detected_drops,
        'gt': ground_truth_drops
    }

if __name__ == '__main__':
    res = test_tuning()
    print(f"TP: {res['tp']}/15, FP: {res['fp']}, FN: {res['fn']}")
    print(f"Precision: {res['prec']:.2f}, Recall: {res['rec']:.2f}, F1: {res['f1']:.2f}")
    print(f"Detected timestamps: {[round(t, 2) for t in res['detected']]}")
