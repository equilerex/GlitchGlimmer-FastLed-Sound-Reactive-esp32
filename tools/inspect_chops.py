import csv
from pathlib import Path

base_dir = Path(__file__).resolve().parent.parent
trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'

with open(trace_path, 'r', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))

chops = [
    (25.15, 26.25, 'C02 pre-drop chop'),
    (76.75, 77.75, 'C04 pre-drop chop'),
    (114.65, 116.05, 'C06 pre-drop chop'),
    (202.0, 202.65, 'C08 pre-drop chop'),
    (283.85, 284.95, 'C10 pre-drop chop'),
    (287.05, 288.25, 'C11 pre-drop chop'),
    (340.55, 341.45, 'C13 pre-drop chop'),
    (371.55, 372.35, 'C14 pre-drop chop'),
]
fps = 30.0
for start, end, name in chops:
    s_idx = int(start * fps)
    e_idx = int(end * fps)
    print(f"\n=== {name} ({start}s - {end}s) ===")
    for i in range(max(0, s_idx - 2), min(len(rows), e_idx + 4)):
        r = rows[i]
        t = i / fps
        tag = "<-- DROP ONSET" if abs(t - end) < 0.04 else ("<-- CHOP / FAKE-OUT" if start <= t < end else "")
        print(f"  t={t:5.2f}s lvl={float(r['level']):.2f} bass={float(r['bassLevel']):.2f} mid={float(r['midLevel']):.2f} treb={float(r['trebleLevel']):.2f} dyn={float(r['dynamics']):.2f} act={float(r['activity']):.2f} {tag}")
