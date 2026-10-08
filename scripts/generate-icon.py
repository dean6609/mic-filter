"""Generate the native multi-size MicFilter icon using only the Python standard library.

The geometry matches the project's microphone mark. Checked-in output makes builds offline.
"""
from pathlib import Path
import math
import struct


def capsule(x, y, ax, ay, bx, by, radius):
    dx, dy = bx - ax, by - ay
    t = max(0, min(1, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(x - ax - t * dx, y - ay - t * dy) <= radius


def pixel(x, y):
    # Rounded teal tile with a white microphone and a short stand.
    outside = math.hypot(max(abs(x - .5) - .31, 0), max(abs(y - .5) - .31, 0)) > .16
    if outside:
        return (0, 0, 0, 0)
    white = capsule(x, y, .5, .25, .5, .49, .095)
    ring = .172 <= math.hypot(x - .5, y - .47) <= .211 and y >= .46
    white |= ring or capsule(x, y, .5, .68, .5, .78, .021) or capsule(x, y, .39, .79, .61, .79, .021)
    return (248, 255, 255, 255) if white else (13, 148, 136, 255)


def frame(size):
    bits = bytearray()
    for row in reversed(range(size)):
        for col in range(size):
            samples = [pixel((col + (sx + .5) / 4) / size, (row + (sy + .5) / 4) / size)
                       for sx in range(4) for sy in range(4)]
            alpha = sum(p[3] for p in samples)
            rgb = [round(sum(p[c] * p[3] for p in samples) / alpha) if alpha else 0 for c in range(3)]
            bits.extend((*reversed(rgb), round(alpha / 16)))
    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    for row in range(size):
        for col in range(size):
            if bits[(row * size + col) * 4 + 3] == 0:
                mask[row * mask_stride + col // 8] |= 0x80 >> (col % 8)
    header = struct.pack('<IiiHHIIiiII', 40, size, size * 2, 1, 32, 0, len(bits), 0, 0, 0, 0)
    return header + bits + mask


sizes = (16, 24, 32, 48, 64, 128, 256)
frames = [frame(size) for size in sizes]
offset = 6 + 16 * len(sizes)
entries = bytearray()
for size, data in zip(sizes, frames):
    entries.extend(struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset))
    offset += len(data)
output = Path(__file__).resolve().parents[1] / 'assets' / 'micfilter.ico'
output.write_bytes(struct.pack('<HHH', 0, 1, len(sizes)) + entries + b''.join(frames))
print(f'Generated {output.name}: {len(sizes)} sizes')
