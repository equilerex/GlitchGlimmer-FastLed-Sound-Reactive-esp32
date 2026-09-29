import json
import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

def get_row_cells(row, sst, ns):
    cells = []
    for c in row.findall('d:c', ns):
        t = c.attrib.get('t')
        v = c.find('d:v', ns)
        val = v.text if v is not None else ''
        if t == 's' and val.isdigit() and int(val) < len(sst):
            val = sst[int(val)]
        elif t == 'inlineStr':
            is_elem = c.find('d:is', ns)
            if is_elem is not None:
                val = ''.join(is_elem.itertext())
        cells.append(val.strip())
    return cells

def extract(xlsx_path: Path, output_json_path: Path):
    z = zipfile.ZipFile(xlsx_path)
    sst = []
    if 'xl/sharedStrings.xml' in z.namelist():
        root = ET.fromstring(z.read('xl/sharedStrings.xml'))
        for si in root.findall('{http://schemas.openxmlformats.org/spreadsheetml/2006/main}si'):
            sst.append(''.join(si.itertext()))

    ns = {'d': 'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}

    # 1. Parse Event Log (sheet 2)
    events = []
    if 'xl/worksheets/sheet2.xml' in z.namelist():
        root2 = ET.fromstring(z.read('xl/worksheets/sheet2.xml'))
        rows2 = root2.findall('.//d:row', ns)
        header_idx = -1
        for i, r in enumerate(rows2[:10]):
            c = get_row_cells(r, sst, ns)
            if any('event' in val.lower() for val in c) and any('start' in val.lower() for val in c):
                header_idx = i
                break

        start_row = header_idx + 1 if header_idx >= 0 else 4
        for r in rows2[start_row:]:
            cells = get_row_cells(r, sst, ns)
            if len(cells) >= 5 and cells[0] and cells[2]:
                try:
                    events.append({
                        'id': cells[0],
                        'cycle': cells[1] if len(cells) > 1 else '',
                        'event': cells[2],
                        'start_s': float(cells[3]),
                        'end_s': float(cells[4]),
                        'duration_s': float(cells[5]) if len(cells) > 5 and cells[5] else float(cells[4]) - float(cells[3]),
                        'confidence': cells[8] if len(cells) > 8 else ''
                    })
                except ValueError:
                    continue

    # 2. Parse Section Summaries (sheet 3) if present
    sections = []
    if 'xl/worksheets/sheet3.xml' in z.namelist():
        root3 = ET.fromstring(z.read('xl/worksheets/sheet3.xml'))
        rows3 = root3.findall('.//d:row', ns)
        header_idx = -1
        headers = []
        for i, r in enumerate(rows3[:10]):
            c = get_row_cells(r, sst, ns)
            if any('cycle' in val.lower() for val in c) and any('section' in val.lower() for val in c):
                header_idx = i
                headers = [val.lower().replace(' ', '_').replace('(', '').replace(')', '').replace('/', '_') for val in c]
                break

        if header_idx >= 0:
            for r in rows3[header_idx + 1:]:
                cells = get_row_cells(r, sst, ns)
                if len(cells) >= 4 and cells[0]:
                    row_dict = {}
                    for col_idx, col_name in enumerate(headers):
                        if col_idx < len(cells):
                            val = cells[col_idx]
                            try:
                                val_num = float(val)
                                row_dict[col_name] = val_num
                            except ValueError:
                                row_dict[col_name] = val
                    if 'start' in row_dict or 'start_s' in row_dict:
                        start_s = row_dict.get('start_s', row_dict.get('start', 0.0))
                        end_s = row_dict.get('end_s', row_dict.get('end', 0.0))
                        cycle = row_dict.get('cycle', '')
                        section = row_dict.get('perceived_section', row_dict.get('section', ''))
                        bpm = row_dict.get('bpm', 0.0)
                        sections.append({
                            'cycle': str(cycle),
                            'section': str(section),
                            'start_s': float(start_s),
                            'end_s': float(end_s),
                            'duration_s': float(row_dict.get('duration_s', row_dict.get('duration', end_s - start_s))),
                            'bpm': float(bpm) if isinstance(bpm, (int, float)) else 0.0,
                            'bpm_confidence': float(row_dict.get('bpm_confidence', 0.0)) if isinstance(row_dict.get('bpm_confidence', 0.0), (int, float)) else 0.0,
                            'bass_share': float(row_dict.get('bass_share', 0.0)) if isinstance(row_dict.get('bass_share', 0.0), (int, float)) else 0.0,
                            'mid_share': float(row_dict.get('mid_share', 0.0)) if isinstance(row_dict.get('mid_share', 0.0), (int, float)) else 0.0,
                            'treble_share': float(row_dict.get('treble_share', 0.0)) if isinstance(row_dict.get('treble_share', 0.0), (int, float)) else 0.0,
                            'intensity_proxy': float(row_dict.get('intensity_proxy', 0.0)) if isinstance(row_dict.get('intensity_proxy', 0.0), (int, float)) else 0.0,
                            'activity_proxy': float(row_dict.get('activity_proxy', 0.0)) if isinstance(row_dict.get('activity_proxy', 0.0), (int, float)) else 0.0,
                        })

    drops = [e['start_s'] for e in events if 'drop onset' in e['event'].lower()]
    buildups = [[e['start_s'], e['end_s']] for e in events if 'buildup' in e['event'].lower()]
    payoffs = [[e['start_s'], e['end_s']] for e in events if 'payoff' in e['event'].lower()]
    pauses = [[e['start_s'], e['end_s']] for e in events if 'pause' in e['event'].lower() or 'fake-out' in e['event'].lower()]
    aftermaths = [[e['start_s'], e['end_s']] for e in events if 'aftermath' in e['event'].lower() or 'transition' in e['event'].lower()]

    result = {
        'source_file': str(xlsx_path),
        'total_events': len(events),
        'drops': drops,
        'buildups': buildups,
        'payoffs': payoffs,
        'pauses': pauses,
        'aftermaths': aftermaths,
        'sections': sections,
        'raw_events': events
    }

    output_json_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_json_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=2)

    print(f"Extracted {len(events)} events and {len(sections)} section summaries from {xlsx_path.name} to {output_json_path.name}")
    print(f"  Drops: {len(drops)}")
    print(f"  Buildups: {len(buildups)}")
    print(f"  Payoffs: {len(payoffs)}")
    print(f"  Pre-drop pauses / fake-outs: {len(pauses)}")
    print(f"  Sections with BPM & coordinates: {len(sections)}")

if __name__ == '__main__':
    base_dir = Path(__file__).resolve().parent.parent
    # 1. Process EDM track
    edm_xlsx = base_dir / 'web' / 'audio' / 'local' / 'edm-dynamic-event-map -new.xlsx'
    if not edm_xlsx.exists():
        edm_xlsx = base_dir / 'web' / 'audio' / 'local' / 'edm-dynamic-event-map.xlsx'
    extract(edm_xlsx, base_dir / 'web' / 'audio' / 'local' / 'ground_truth.json')

    # 2. Process Jazz track
    jazz_xlsx = base_dir / 'web' / 'audio' / 'local' / 'jazz-edm-dynamic-event-map.xlsx'
    if jazz_xlsx.exists():
        extract(jazz_xlsx, base_dir / 'web' / 'audio' / 'local' / 'jazz_ground_truth.json')
