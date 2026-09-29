import csv
import json
from pathlib import Path

def inspect_all_drops():
    base_dir = Path(__file__).resolve().parent.parent
    trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'
    gt_path = base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json'

    with open(gt_path, 'r', encoding='utf-8') as f:
        gt = json.load(f)

    with open(trace_path, 'r', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))

    fps = 30.0
    print(f"Total drops: {len(gt['drops'])}")
    for i, drop_t in enumerate(gt['drops']):
        frm = int(round(drop_t * fps))
        
        # Look at pre-drop (0.5s before) and at-drop
        pre_frms = rows[max(0, frm-15):frm]
        at_frms = rows[frm:min(len(rows), frm+10)]

        pre_bass_min = min(float(r['bassLevel']) for r in pre_frms) if pre_frms else 0
        pre_bass_avg = sum(float(r['bassLevel']) for r in pre_frms)/len(pre_frms) if pre_frms else 0
        pre_lvl_min = min(float(r['level']) for r in pre_frms) if pre_frms else 0

        at_bass_max = max(float(r['bassLevel']) for r in at_frms) if at_frms else 0
        at_lvl_max = max(float(r['level']) for r in at_frms) if at_frms else 0
        at_mid = max(float(r['midLevel']) for r in at_frms) if at_frms else 0
        at_treb = max(float(r['trebleLevel']) for r in at_frms) if at_frms else 0

        # Also find what cycle or description this was in raw_events
        raw_info = [e for e in gt['raw_events'] if abs(e['start_s'] - drop_t) < 0.01]
        cycle = raw_info[0]['cycle'] if raw_info else '?'
        conf = raw_info[0]['confidence'] if raw_info else '?'

        print(f"Drop #{i+1:02d} | t={drop_t:6.2f}s | {cycle} ({conf:6s}) | "
              f"Pre: bass_min={pre_bass_min:.2f} avg={pre_bass_avg:.2f} lvl_min={pre_lvl_min:.2f} | "
              f"At: bass_max={at_bass_max:.2f} lvl_max={at_lvl_max:.2f} mid={at_mid:.2f} treb={at_treb:.2f}")

if __name__ == '__main__':
    inspect_all_drops()
