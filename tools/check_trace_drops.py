import csv
from pathlib import Path

base_dir = Path(__file__).resolve().parent.parent
trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'

with open(trace_path, 'r', encoding='utf-8') as f:
    rows = list(csv.DictReader(f))

# Check every 5 seconds
for t_check in range(15, 75, 5):
    idx = int(t_check * 30)
    r = rows[idx]
    print(f"t={t_check}s | lvl={float(r['level']):.2f} bass={float(r['bassLevel']):.2f} mood={r['mood']}")
