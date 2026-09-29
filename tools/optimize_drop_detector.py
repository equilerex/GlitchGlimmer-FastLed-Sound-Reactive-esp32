import csv
import json
import math
from pathlib import Path

def run_fine_optimizer():
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

    n = len(rows)
    levels = [float(r['level']) for r in rows]
    bass = [float(r['bassLevel']) for r in rows]
    mids = [float(r['midLevel']) for r in rows]
    trebs = [float(r['trebleLevel']) for r in rows]

    tau_sec = 2.0
    alpha = 1.0 - math.exp(-dt_default / tau_sec)

    slow_levels = [0.0] * n
    slow_bass = [0.0] * n
    sl = levels[0]
    sb = bass[0]
    for i in range(n):
        sl += (levels[i] - sl) * alpha
        sb += (bass[i] - sb) * alpha
        slow_levels[i] = sl
        slow_bass[i] = sb

    best_score = -1.0
    best_params = None
    best_res = None

    # Fine grid
    for quiet_bass in [0.20, 0.25, 0.30, 0.35]:
        for arm_ms in [100, 150, 200, 250]:
            for drop_bass_disp in [0.30, 0.35, 0.40, 0.45]:
                for min_bass in [0.50, 0.55, 0.60]:
                    for min_mid in [0.45, 0.55, 0.65]:
                        for min_treb in [0.35, 0.45]:
                            for cooldown_ms in [2000, 2500, 3000]:
                                quiet_held = False
                                quiet_since_ms = 0
                                last_drop_ms = -100000
                                detected = []

                                for i in range(n):
                                    now_ms = i * (1000.0 / fps)
                                    b = bass[i]
                                    sb_i = slow_bass[i]

                                    # Arming: bass was withdrawn or track was quiet
                                    if b < quiet_bass or sb_i < quiet_bass or slow_levels[i] < 0.60:
                                        if not quiet_held:
                                            quiet_held = True
                                            quiet_since_ms = now_ms
                                    else:
                                        if now_ms - quiet_since_ms > arm_ms * 2.5:
                                            quiet_held = False

                                    armed = quiet_held and (now_ms - quiet_since_ms >= arm_ms)

                                    disp_b = b - sb_i
                                    hit = (disp_b >= drop_bass_disp) and (b >= min_bass)
                                    breadth = (mids[i] >= min_mid) and (trebs[i] >= min_treb)

                                    if armed and hit and breadth and (now_ms - last_drop_ms >= cooldown_ms):
                                        detected.append(now_ms / 1000.0)
                                        last_drop_ms = now_ms
                                        quiet_held = False

                                matched_gt = set()
                                matched_det = set()
                                for det_idx, det_t in enumerate(detected):
                                    for gt_idx, gt_t in enumerate(ground_truth_drops):
                                        if abs(det_t - gt_t) <= 1.0 and gt_idx not in matched_gt:
                                            matched_gt.add(gt_idx)
                                            matched_det.add(det_idx)
                                            break

                                tp = len(matched_gt)
                                fp = len(detected) - len(matched_det)
                                fn = len(ground_truth_drops) - tp
                                prec = tp / (tp + fp) if (tp + fp) > 0 else 0.0
                                rec = tp / (tp + fn) if (tp + fn) > 0 else 0.0
                                f1 = 2 * prec * rec / (prec + rec) if (prec + rec) > 0 else 0.0

                                # Objective: maximize F1, penalize excess FPs
                                score = f1 - 0.05 * fp

                                if score > best_score:
                                    best_score = score
                                    best_params = {
                                        'quiet_bass': quiet_bass,
                                        'arm_ms': arm_ms,
                                        'drop_bass_disp': drop_bass_disp,
                                        'min_bass': min_bass,
                                        'min_mid': min_mid,
                                        'min_treb': min_treb,
                                        'cooldown_ms': cooldown_ms
                                    }
                                    best_res = {
                                        'tp': tp, 'fp': fp, 'fn': fn,
                                        'prec': prec, 'rec': rec, 'f1': f1,
                                        'score': score,
                                        'detected': detected
                                    }

    print("=== Fine Optimizer Complete ===")
    print(f"Best Score: {best_score:.2f} (F1: {best_res['f1']:.2f})")
    print(f"Parameters: {best_params}")
    print(f"TP: {best_res['tp']}/15, FP: {best_res['fp']}, FN: {best_res['fn']}")
    print(f"Precision: {best_res['prec']:.2f}, Recall: {best_res['rec']:.2f}")
    print(f"Detected timestamps: {[round(t, 2) for t in best_res['detected']]}")

if __name__ == '__main__':
    run_fine_optimizer()
