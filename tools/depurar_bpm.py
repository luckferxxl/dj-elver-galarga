"""Muestra los candidatos de tempo para pistas concretas (por nombre de archivo)."""
import os, subprocess, sys, xml.etree.ElementTree as ET
sys.stdout.reconfigure(encoding="utf-8")
DB = os.path.expandvars(r"%LOCALAPPDATA%\VirtualDJ\database.xml")
EXE = os.path.join(os.path.dirname(__file__), "..", "build", "rdj_tests.exe")
names = sys.argv[1:]
for s in ET.parse(DB).getroot().iter("Song"):
    p = s.get("FilePath", "")
    if os.path.basename(p) in names and os.path.exists(p):
        scan = s.find("Scan")
        ref = 60 / float(scan.get("Bpm")) if scan is not None and scan.get("Bpm") else 0
        r = subprocess.run([EXE, "--bpm-file", p], capture_output=True, text=True, encoding="utf-8", env=dict(os.environ, RDJ_BPM_DEBUG="1"))
        print(f"== {os.path.basename(p)}  (VirtualDJ {ref:.2f})\n{r.stderr}{r.stdout}")
