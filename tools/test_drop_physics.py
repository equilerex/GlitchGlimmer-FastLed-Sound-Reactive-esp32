import csv
import math
from pathlib import Path
import json

base_dir = Path(__file__).resolve().parent.parent

def eval_params(quiet_bass_thresh, jump_thresh, arm_ms, cooldown_ms):
    scores = {}
    for csv_name, gt_name in [('demo_trace.csv', 'ground_truth.json'), ('jazz_trace.csv', 'jazz_ground_truth.json')]:
        trace_path = base_dir / 'web' / 'audio' / 'local' / csv_name
        gt_path = base_dir / 'web' / 'audio' / 'local' / gt_name
        with open(gt_path, 'r', encoding='utf-8') as f:
            gt = json.load(f)
        gt_drops = gt['drops']
        with open(trace_path, 'r', encoding='utf-8') as f:
            rows = list(csv.DictReader(f))

        fps = 30.0
        dt = 1.0 / fps

        slow_lvl = float(rows[0]['level'])
        fast_lvl = float(rows[0]['level'])
        slow_bass = float(rows[0]['bassLevel'])
        fast_bass = float(rows[0]['bassLevel'])

        quiet_since = 0
        quiet_until = 0
        quiet_held = False
        last_drop_ms = -100000

        alpha_slow_lvl = 1.0 - math.exp(-dt / 10.0)
        alpha_fast_lvl = 1.0 - math.exp(-dt / 0.15)
        alpha_slow_bass = 1.0 - math.exp(-dt / 2.0)
        alpha_fast_bass = 1.0 - math.exp(-dt / 0.15)

        detected = []

        for i, r in enumerate(rows):
            now = int(i * (1000.0 / fps))
            lvl = float(r['level'])
            bass = float(r['bassLevel'])
            mid = float(r['midLevel'])
            treb = float(r['trebleLevel'])

            slow_lvl += (lvl - slow_lvl) * alpha_slow_lvl
            fast_lvl += (lvl - fast_lvl) * alpha_fast_lvl
            slow_bass += (bass - slow_bass) * alpha_slow_bass
            fast_bass += (bass - fast_bass) * alpha_fast_bass

            is_quiet = (slow_lvl < 0.55) or (slow_bass < quiet_bass_thresh) or (fast_bass < quiet_bass_thresh)

            if is_quiet:
                if not quiet_held:
                    quiet_held = True
                    quiet_since = now
                quiet_until = now
            else:
                if now - quiet_until > 350:
                    quiet_held = False

            armed = quiet_held and (now - quiet_since >= arm_ms)

            lvl_jump = lvl - fast_lvl
            bass_jump = bass - fast_bass

            is_slam = (bass_jump >= jump_thresh and bass >= 0.55) or (lvl_jump >= 0.25 and lvl >= 0.65)
            breadth = (bass >= 0.55) and (mid >= 0.50) and (treb >= 0.35)
            cooldown = (now - last_drop_ms >= cooldown_ms)

            if armed and is_slam and breadth and cooldown:
                t = now / 1000.0
                detected.append(round(t, 2))
                last_drop_ms = now
                quiet_held = False

        tp = 0
        matched_gt = set()
        for d in detected:
            match = [g for g in gt_drops if abs(g - d) <= 0.75 and g not in matched_gt]
            if match:
                tp += 1
                matched_gt.add(match[0])
        fp = len(detected) - tp
        fn = len(gt_drops) - tp
        prec = tp / (tp + fp) if tp + fp > 0 else 0
        rec = tp / (tp + fn) if tp + fn > 0 else 0
        f1 = 2 * prec * rec / (prec + rec) if prec + rec > 0 else 0
        scores[csv_name] = (tp, fp, fn, prec, rec, f1, detected)
    return scores

print("Sweeping quiet_bass_thresh and jump_thresh...")
best_f1 = 0
best_cfg = None

for qb in [0.25, 0.30, 0.35, 0.40]:
    for jump in [0.25, 0.30, 0.35]:
        for arm in [200, 250, 300]:
            s = eval_params(qb, jump, arm, 5000)
            d_s = s['demo_trace.csv']
            j_s = s['jazz_trace.csv']
            total_tp = d_s[0] + j_s[0]
            total_fp = d_s[1] + j_s[1]
            total_fn = d_s[2] + j_s[2]
            comb_prec = total_tp / (total_tp + total_fp) if total_tp + total_fp > 0 else 0
            comb_rec = total_tp / (total_tp + total_fn) if total_tp + total_fn > 0 else 0
            comb_f1 = 2 * comb_prec * comb_rec / (comb_prec + comb_rec) if comb_prec + comb_rec > 0 else 0
            if comb_f1 > best_f1:
                best_f1 = comb_f1
                best_cfg = (qb, jump, arm, d_s, j_s, comb_prec, comb_rec, comb_f1)

print(f"Best Config: quiet_bass={best_cfg[0]}, jump={best_cfg[1]}, arm={best_cfg[2]}ms")
print(f"Combined Prec={best_cfg[5]:.2f}, Rec={best_cfg[6]:.2f}, F1={best_cfg[7]:.2f}")
print(f"Demo: TP={best_cfg[3][0]}, FP={best_cfg[3][1]}, FN={best_cfg[3][2]} | Prec={best_cfg[3][3]:.2f}, Rec={best_cfg[3][4]:.2f}, F1={best_cfg[3][5]:.2f}")
print(f"Jazz: TP={best_cfg[4][0]}, FP={best_cfg[4][1]}, FN={best_cfg[4][2]} | Prec={best_cfg[4][3]:.2f}, Rec={best_cfg[4][4]:.2f}, F1={best_cfg[4][5]:.2f}")
print(f"Demo detected: {best_cfg[3][6]}")
print(f"Jazz detected: {best_cfg[4][6]}")
