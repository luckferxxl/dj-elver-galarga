#!/usr/bin/env python3
"""Linux checks; does not build or certify the Windows executable / WIC / MIDI."""
from pathlib import Path
import subprocess, tempfile, os
root=Path(__file__).resolve().parents[1]
os.chdir(root)
with tempfile.TemporaryDirectory(prefix='rdj-verify-') as tmp:
    def run(args): subprocess.run(args,check=True)
    common=['g++','-std=c++20','-O2','-I','src']
    run(common+['-I','third_party/signalsmith-stretch','-I','third_party/signalsmith-linear/include','tests/performance.cpp','src/audio/engine.cpp','src/audio/stretch.cpp','-o',tmp+'/performance'])
    run([tmp+'/performance'])
    run(common+['-include','tests/native/crt.h','tests/cover_art.cpp','src/core/tags.cpp','src/audio/analysis.cpp','tests/native/util.cpp','-o',tmp+'/covers'])
    run([tmp+'/covers'])
    run(['g++','-std=c++20','-fsyntax-only','-I','tests/native','-I','third_party/imgui','-I','third_party/miniaudio','-I','third_party/json',
         'src/ui/ui_frame.cpp','src/ui/ui_browser.cpp','src/ui/ui_workflow.cpp','src/ui/app.cpp','src/ui/widgets.cpp','src/core/settings.cpp'])
    print('PASS portable UI/settings syntax (Windows declarations only; no Windows runtime validation)')
