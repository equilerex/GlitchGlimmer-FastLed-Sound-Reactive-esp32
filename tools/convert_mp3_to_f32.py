import struct
from pathlib import Path
import miniaudio

def convert(mp3_path: Path, f32_path: Path, fps: float = 30.0, sample_rate: int = 44100, block_size: int = 512):
    print(f"Decoding {mp3_path}...")
    decoded = miniaudio.decode_file(
        str(mp3_path),
        output_format=miniaudio.SampleFormat.FLOAT32,
        nchannels=1,
        sample_rate=sample_rate
    )
    samples = decoded.samples # memoryview of float32
    total_samples = len(samples)
    duration_s = total_samples / sample_rate
    print(f"Decoded {total_samples} samples ({duration_s:.2f} s) at {sample_rate} Hz mono.")

    step_samples = sample_rate / fps
    total_frames = int(duration_s * fps)
    print(f"Extracting {total_frames} frames ({block_size} samples each, step {step_samples:.2f} samples = {1000/fps:.1f} ms)...")

    # Magic: GGCAP001
    magic = b"GGCAP001"
    
    f32_path.parent.mkdir(parents=True, exist_ok=True)
    with open(f32_path, "wb") as f:
        f.write(magic)
        f.write(struct.pack("<I", total_frames))
        
        zero_pad = [0.0] * block_size
        for frame_idx in range(total_frames):
            start = int(round(frame_idx * step_samples))
            end = start + block_size
            if end <= total_samples:
                chunk = samples[start:end]
            else:
                available = max(0, total_samples - start)
                chunk = list(samples[start:start+available]) + zero_pad[:block_size - available]
            
            # Pack chunk as little-endian floats
            f.write(struct.pack(f"<{block_size}f", *chunk))

    print(f"Wrote {total_frames} frames to {f32_path} ({f32_path.stat().st_size / (1024*1024):.2f} MB)")

if __name__ == '__main__':
    base_dir = Path(__file__).resolve().parent.parent
    mp3 = base_dir / 'web' / 'audio' / 'local' / 'demo.mp3'
    f32 = base_dir / 'web' / 'audio' / 'local' / 'demo.f32'
    convert(mp3, f32)
