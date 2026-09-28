import csv
import json
from pathlib import Path

def evaluate_bpm():
    base_dir = Path(__file__).resolve().parent.parent
    trace_path = base_dir / 'web' / 'audio' / 'local' / 'demo_trace.csv'
    gt_path = base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json'

    with open(gt_path, 'r', encoding='utf-8') as f:
        gt = json.load(f)

    with open(trace_path, 'r', encoding='utf-8') as f:
        rows = list(csv.DictReader(f))

    fps = 30.0
    gt_bpm = 128.0

    bpms = [float(r['bpm']) for r in rows]
    beat_confs = [float(r['beatConfidence']) for r in rows]

    # Filter out initial silence / settling (first 5 seconds)
    active_bpms = bpms[int(5 * fps):]
    active_confs = beat_confs[int(5 * fps):]

    # Measure how many frames are within 5% of 128 BPM (121.6 - 134.4)
    tolerance_pct = 0.05
    correct = [b for b in active_bpms if abs(b - gt_bpm) <= gt_bpm * tolerance_pct]
    octave_errors = [b for b in active_bpms if abs(b - gt_bpm*0.5) <= 5 or abs(b - gt_bpm*2.0) <= 5]

    pct_correct = len(correct) / len(active_bpms) * 100
    mean_bpm = sum(active_bpms) / len(active_bpms)

    print(f"BPM Ground Truth: {gt_bpm} BPM")
    print(f"Firmware Mean BPM: {mean_bpm:.2f} BPM")
    print(f"Frames within 5% of 128 BPM: {pct_correct:.1f}% ({len(correct)}/{len(active_bpms)})")
    print(f"Frames with half/double tempo errors: {len(octave_errors)/len(active_bpms)*100:.1f}%")

    # Inspect BPM per section in ground truth
    print("\n--- Section BPM Performance ---")
    for sec in gt.get('sections', [])[:8]:
        s_start, s_end = int(sec['start_s'] * fps), int(sec['end_s'] * fps)
        sec_bpms = bpms[s_start:s_end]
        if sec_bpms:
            sec_mean = sum(sec_bpms) / len(sec_bpms)
            print(f"  {sec['cycle']} {sec['section'][:20]:20s} ({sec['start_s']:5.1f}s - {sec['end_s']:5.1f}s): "
                  f"Firmware Mean={sec_mean:5.1f} BPM | GT={sec['bpm']} BPM (Conf={sec['bpm_confidence']})")

if __name__ == '__main__':
    evaluate_bpm()
