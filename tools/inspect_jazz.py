import csv
from pathlib import Path

base_dir = Path(__file__).resolve().parent.parent
trace_path = base_dir / 'web' / 'audio' / 'local' / 'jazz_trace.csv'

with open(trace_path, 'r', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))

for i in range(int(13.5*30), int(16.0*30)):
    r = rows[i]
    t = i / 30.0
    print(f"t={t:5.2f}s lvl={float(r['level']):.2f} bass={float(r['bassLevel']):.2f} mid={float(r['midLevel']):.2f} treb={float(r['trebleLevel']):.2f} cent={float(r['centroid']):.1f}")
