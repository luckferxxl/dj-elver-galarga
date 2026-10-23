"""Compara la tonalidad detectada por Ramiro DJ con la que VirtualDJ guardó en su
database.xml (solo lectura). Uso: python validar_tonalidad.py [n] [semilla] [perfil]
Cuenta aciertos exactos, de quinta (vecina en la rueda Camelot), relativa y paralela."""
import os, random, subprocess, sys, xml.etree.ElementTree as ET
sys.stdout.reconfigure(encoding="utf-8")

DB = os.path.expandvars(r"%LOCALAPPDATA%\VirtualDJ\database.xml")
EXE = os.path.join(os.path.dirname(__file__), "..", "build", "rdj_keytool.exe")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 40
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 7
PROFILE = sys.argv[3] if len(sys.argv) > 3 else None

NAMES = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6, "Gb": 6, "G": 7,
         "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}

def parse(k):
    m = k.endswith("m")
    root = NAMES.get(k[:-1] if m else k)
    return None if root is None else (root, m)

def camelot(root, minor):
    major = (root + 3) % 12 if minor else root
    return ((7 * major) % 12 + 7) % 12 + 1, minor

songs = []
for s in ET.parse(DB).getroot().iter("Song"):
    scan = s.find("Scan")
    p = s.get("FilePath", "")
    if scan is None or not scan.get("Key") or not os.path.exists(p) or not p.lower().endswith((".mp3", ".wav", ".flac", ".ogg")):
        continue
    if parse(scan.get("Key")):
        songs.append((p, scan.get("Key")))
random.seed(SEED)
sample = random.sample(songs, min(N, len(songs)))
env = dict(os.environ)
if PROFILE is not None:
    env["RDJ_KEY_PROFILE"] = PROFILE
cnt = {"exacta": 0, "quinta": 0, "relativa": 0, "paralela": 0, "otra": 0}
for path, ref in sample:
    out = subprocess.run([EXE, "--bpm-file", path], capture_output=True, text=True, encoding="utf-8", env=env).stdout.split()
    if len(out) < 4 or not parse(out[3]):
        cnt["otra"] += 1
        continue
    a, b = parse(ref), parse(out[3])
    ca, cb = camelot(*a), camelot(*b)
    if a == b: v = "exacta"
    elif ca[1] == cb[1] and abs(ca[0] - cb[0]) in (1, 11): v = "quinta"
    elif ca[0] == cb[0]: v = "relativa"
    elif a[0] == b[0]: v = "paralela"
    else: v = "otra"
    cnt[v] += 1
    if v != "exacta":
        print(f"  VDJ {ref:4} | RDJ {out[3]:4} | {v:8} | {os.path.basename(path)[:60]}")
n = sum(cnt.values())
print(f"\nPistas {n}: " + ", ".join(f"{k} {v}" for k, v in cnt.items()))
print(f"Exacta {100*cnt['exacta']/n:.0f} %  ·  exacta+quinta+relativa (compatibles para mezclar) {100*(cnt['exacta']+cnt['quinta']+cnt['relativa'])/n:.0f} %")
