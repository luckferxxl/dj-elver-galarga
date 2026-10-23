"""Compara el detector de BPM de Ramiro DJ con los BPM que VirtualDJ guardó en
su database.xml (solo lectura). Uso: python validar_bpm.py [n_pistas]
El campo Scan/@Bpm de VirtualDJ es el periodo del pulso en segundos (60/BPM)."""
import os, random, subprocess, sys, xml.etree.ElementTree as ET
sys.stdout.reconfigure(encoding="utf-8")

DB = os.path.expandvars(r"%LOCALAPPDATA%\VirtualDJ\database.xml")
EXE = os.path.join(os.path.dirname(__file__), "..", "build", "rdj_tests.exe")
N = int(sys.argv[1]) if len(sys.argv) > 1 else 40

songs = []
for s in ET.parse(DB).getroot().iter("Song"):
    scan = s.find("Scan")
    path = s.get("FilePath", "")
    if scan is None or not scan.get("Bpm") or not os.path.exists(path):
        continue
    if not path.lower().endswith((".mp3", ".wav", ".flac", ".ogg")):
        continue
    songs.append((path, 60.0 / float(scan.get("Bpm")), float(scan.get("Phase", "0"))))

random.seed(int(sys.argv[2]) if len(sys.argv) > 2 else 7)
sample = random.sample(songs, min(N, len(songs)))
exact = octave = wrong = 0
rows = []
for path, ref, phase in sample:
    r = subprocess.run([EXE, "--bpm-file", path], capture_output=True, text=True, encoding="utf-8")
    try:
        bpm, anchor, conf = map(float, r.stdout.split())
    except ValueError:
        rows.append(f"  ERROR {os.path.basename(path)}: {r.stdout.strip()}")
        wrong += 1
        continue
    if abs(bpm - ref) < 0.5:
        verdict = "igual"; exact += 1
    elif any(abs(bpm * k - ref) < 0.5 for k in (2, 0.5, 1.5, 2 / 3)):
        verdict = "octava"; octave += 1
    else:
        verdict = "DISTINTO"; wrong += 1
    # Hipótesis: Phase de VirtualDJ es el instante de un pulso -> comparar módulo pulso.
    beat = 60.0 / ref
    d = ((anchor - phase) % beat)
    d = min(d, beat - d)
    rows.append(f"  {ref:7.2f} VDJ | {bpm:7.2f} RDJ | {verdict:8} | dif. fase {d*1000:6.1f} ms | {os.path.basename(path)[:60]}")

print("\n".join(rows))
n = exact + octave + wrong
print(f"\nPistas: {n}  iguales (±0,5): {exact}  error de octava: {octave}  distintas: {wrong}")
print(f"Acierto exacto: {100*exact/max(1,n):.0f} %   acierto contando octavas: {100*(exact+octave)/max(1,n):.0f} %")
