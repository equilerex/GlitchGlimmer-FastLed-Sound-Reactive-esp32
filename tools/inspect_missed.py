import csv
from pathlib import Path

base_dir = Path(__file__).resolve().parent.parent
trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'

with open(trace_path, 'r', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))

missed = [26.25, 77.75, 116.05, 202.65, 284.95, 288.25, 341.45, 372.35]
fps = 30.0
for t in missed:
    idx = int(t * fps)
    print(f"=== Target Drop at {t}s (idx {idx}) ===")
    for i in range(max(0, idx - 15), min(len(rows), idx + 10)):
        r = rows[i]
        t_cur = i / fps
        print(f"  t={t_cur:5.2f}s lvl={float(r['level']):.2f} bass={float(r['bassLevel']):.2f} mid={float(r['midLevel']):.2f} treb={float(r['trebleLevel']):.2f} act={float(r['activity']):.2f} bld={float(r['buildup']):.2f} drop={float(r['dropDetected']):.1f} mood={r['mood']}")
