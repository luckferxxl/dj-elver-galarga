"""Genera res/ramirodj.ico (disco de vinilo, acentos azul y rojo) sin dependencias."""
import math, os, struct, zlib

def png(size):
    rows = []
    c = (size - 1) / 2
    for y in range(size):
        row = bytearray(b"\x00")
        for x in range(size):
            dx, dy = x - c, y - c
            r = math.hypot(dx, dy) / (size / 2)
            a = 0
            col = (0, 0, 0)
            if r <= 0.98:
                a = 255 if r <= 0.96 else int(255 * (0.98 - r) / 0.02)
                groove = 0.5 + 0.5 * math.cos(r * 90)
                base = 22 + int(10 * groove)
                col = (base, base + 2, base + 6)
                ang = math.atan2(dy, dx)
                # reflejos
                shine = max(0.0, math.cos(2 * (ang - 0.8))) ** 8 * 40
                col = tuple(min(255, int(v + shine)) for v in col)
                if r < 0.38:  # etiqueta: mitad azul, mitad roja
                    col = (58, 160, 255) if dx < 0 else (255, 59, 92)
                    if r < 0.06:
                        col, a = (0, 0, 0), 0
                if 0.38 <= r < 0.40:
                    col = (10, 12, 16)
            row += bytes((*col, a))
        rows.append(bytes(row))
    raw = b"".join(rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")

sizes = [16, 24, 32, 48, 64, 128, 256]
imgs = [png(s) for s in sizes]
out = os.path.join(os.path.dirname(__file__), "..", "res", "ramirodj.ico")
os.makedirs(os.path.dirname(out), exist_ok=True)
with open(out, "wb") as f:
    f.write(struct.pack("<HHH", 0, 1, len(sizes)))
    off = 6 + 16 * len(sizes)
    for s, d in zip(sizes, imgs):
        f.write(struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(d), off))
        off += len(d)
    for d in imgs:
        f.write(d)
print("icono:", os.path.abspath(out))
