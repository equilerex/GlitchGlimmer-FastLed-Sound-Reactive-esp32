import csv
import json
import math
from pathlib import Path

base_dir = Path(__file__).resolve().parent.parent

def run_simulation():
    for name, trace_file, gt_file in [
        ('DEMO', 'web/audio/local/demo_trace.csv', 'web/audio/local/ground_truth.json'),
        ('JAZZ', 'web/audio/local/jazz_trace.csv', 'web/audio/local/jazz_ground_truth.json')
    ]:
        with open(base_dir / gt_file, 'r', encoding='utf-8') as f:
            gt = json.load(f)
        gt_drops = gt['drops']

        with open(base_dir / trace_file, 'r', encoding='utf-8') as f:
            rows = list(csv.DictReader(f))

        fps = 30.0
        dt = 1.0 / fps

        slow_lvl = float(rows[0]['level'])
        fast_lvl = float(rows[0]['level'])
        slow_bass = float(rows[0]['bassLevel'])
        fast_bass = float(rows[0]['bassLevel'])
        slow_treb = float(rows[0]['trebleLevel'])
        fast_treb = float(rows[0]['trebleLevel'])

        alpha_slow = 1.0 - math.exp(-dt / 10.0)
        alpha_fast = 1.0 - math.exp(-dt / 0.15)
        alpha_bass = 1.0 - math.exp(-dt / 2.0)

        quiet_since = 0
        quiet_last_seen = 0
        quiet_held = False
        last_drop_ms = -100000

        buildup_active = False
        buildup_since = 0
        buildup_hold_since = 0
        buildup_from_lvl = 0.0
        buildup_peak_ms = 0
        chop_since_ms = 0

        detected_drops = []

        in_drop_payoff = False
        drop_opened_ms = -100000
        drop_last_held_ms = -100000
        drop_plateau = 0.0

        for i, r in enumerate(rows):
            now = int(i * (1000.0 / fps))
            lvl = float(r['level'])
            bass = float(r['bassLevel'])
            mid = float(r['midLevel'])
            treb = float(r['trebleLevel'])
            act = float(r.get('activity', 0.0))

            slow_lvl += (lvl - slow_lvl) * alpha_slow
            fast_lvl += (lvl - fast_lvl) * alpha_fast
            slow_bass += (bass - slow_bass) * alpha_bass
            fast_bass += (bass - fast_bass) * alpha_fast
            slow_treb += (treb - slow_treb) * alpha_slow
            fast_treb += (treb - fast_treb) * alpha_fast

            displacement = lvl - slow_lvl
            bass_displacement = bass - slow_bass
            treb_displacement = treb - slow_treb
            bass_jump = bass - fast_bass

            # Check if current drop payoff has ended (requires sustained dip)
            if in_drop_payoff:
                is_holding = (lvl >= drop_plateau * 0.65) and (bass >= 0.35)
                if is_holding:
                    drop_last_held_ms = now
                else:
                    if now - drop_last_held_ms > 2000 or (now - drop_opened_ms > 45000):
                        in_drop_payoff = False

            # 1. Buildup detection: must be a directional rise (energy climb, bass withdrawal, or treble climb)
            # Cannot start or exceed while in an active drop payoff
            band_min = min(bass, mid, treb)
            band_max = max(bass, mid, treb)
            bands_equalized = (band_min >= 0.65) and (band_max - band_min <= 0.30) and (lvl >= 0.80)
            
            treble_riser = (treb_displacement >= 0.20)
            spectral_riser = (slow_bass >= 0.35) and (slow_bass - bass >= 0.20) and (mid >= 0.60 or treb >= 0.60) and (lvl >= 0.50)
            energy_buildup = (displacement >= 0.20)

            is_buildup = (not in_drop_payoff) and (energy_buildup or spectral_riser or treble_riser)

            if is_buildup:
                if buildup_hold_since == 0:
                    buildup_hold_since = now
                    buildup_from_lvl = lvl
                if now - buildup_hold_since >= 1000:
                    if not buildup_active:
                        buildup_since = now
                    buildup_active = True
                if buildup_active and (bands_equalized or (treb >= 0.85 and act >= 0.40)):
                    buildup_peak_ms = now
            else:
                if now - buildup_since > 3000:
                    buildup_hold_since = 0

            # 2. Pre-drop chop detection (during or right after buildup, bass cuts out or fill occurs)
            if (buildup_active or (now - buildup_peak_ms <= 2000)) and not in_drop_payoff:
                if bass < 0.30 or (fast_bass - bass >= 0.25):
                    chop_since_ms = now

            # 3. Quiet arming
            is_quiet = (slow_lvl < 0.55) or (slow_bass < 0.40) or (fast_bass < 0.40)
            if is_quiet:
                if not quiet_held:
                    quiet_held = True
                    quiet_since = now
                quiet_last_seen = now
            else:
                if now - quiet_last_seen > 350:
                    quiet_held = False

            # 4. Armed state
            quiet_armed = quiet_held and (now - quiet_since >= 600)
            buildup_armed = (buildup_active and (now - buildup_since >= 1500)) or (buildup_peak_ms != 0 and (now - buildup_peak_ms <= 1500))
            chop_armed = (chop_since_ms != 0 and (now - chop_since_ms <= 1500))

            armed = (not in_drop_payoff) and (quiet_armed or buildup_armed or chop_armed)

            # 5. Drop trigger: requires low-end explosion / arrival
            slam = (displacement >= 0.35) or \
                   (bass_displacement >= 0.45 and bass >= 0.60) or \
                   (bass_jump >= 0.30 and bass >= 0.60) or \
                   ((buildup_armed or chop_armed) and bass_jump >= 0.25 and bass >= 0.65)

            breadth = (bass >= 0.60) and (mid >= 0.45)

            if now >= 2000 and armed and slam and breadth and (now - last_drop_ms >= 3000):
                detected_drops.append(i * dt)
                last_drop_ms = now
                in_drop_payoff = True
                drop_opened_ms = now
                drop_plateau = lvl
                quiet_held = False
                buildup_active = False
                buildup_hold_since = 0
                buildup_peak_ms = 0
                chop_since_ms = 0

        # Evaluate against ground truth
        tolerance = 0.5
        matched_gt = set()
        matched_det = set()
        for det_idx, det_t in enumerate(detected_drops):
            for gt_idx, gt_t in enumerate(gt_drops):
                if abs(det_t - gt_t) <= tolerance and gt_idx not in matched_gt:
                    matched_gt.add(gt_idx)
                    matched_det.add(det_idx)
                    break

        tp = len(matched_gt)
        fp = len(detected_drops) - len(matched_det)
        fn = len(gt_drops) - tp
        p = tp / (tp + fp) if (tp + fp) > 0 else 0
        r = tp / (tp + fn) if (tp + fn) > 0 else 0
        f1 = 2 * p * r / (p + r) if (p + r) > 0 else 0

        print(f"=== {name} RESULTS ===")
        print(f"Detected: {len(detected_drops)}, TP: {tp}, FP: {fp}, FN: {fn}")
        print(f"Precision: {p:.2f}, Recall: {r:.2f}, F1: {f1:.2f}")
        for t in detected_drops:
            match = [gt_t for gt_t in gt_drops if abs(t - gt_t) <= tolerance]
            status = f"MATCH -> {match[0]:.2f}s" if match else "FP"
            print(f"  {t:6.2f}s: {status}")

if __name__ == '__main__':
    run_simulation()
