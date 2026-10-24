#!/usr/bin/env python3
"""Pruebas DSP portables. No valida la interfaz ni los drivers de Windows."""
from pathlib import Path
import os, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
cxx = os.environ.get('CXX', 'g++')
with tempfile.TemporaryDirectory(prefix='dj-live-') as temp:
    dest=Path(temp)
    flags=['-std=c++20','-O2','-pthread','-I'+str(root/'src'),
        '-I'+str(root/'third_party/signalsmith-stretch'),'-I'+str(root/'third_party/signalsmith-linear/include')]
    objects=[]
    for name in ['engine','rack','instruments','stretch']:
        obj=dest/(name+'.o')
        subprocess.run([cxx,*flags,'-c',str(root/'src/audio'/(name+'.cpp')),'-o',str(obj)],check=True)
        objects.append(str(obj))
    for test in ['drum_pro','workbench','live','performance','retrospective']:
        exe=dest/(test+('.exe' if os.name=='nt' else ''))
        subprocess.run([cxx,*flags,str(root/'tests'/(test+'.cpp')),*objects,'-o',str(exe)],check=True)
        print('Running '+test,flush=True)
        subprocess.run([str(exe)],check=True)

    # Isolate the existing track-loader sections from the bounded sample decoder.
    decoder=dest/'ma_decoder.o';loader=dest/'loader.o';exe=dest/('drum_decode.exe' if os.name=='nt' else 'drum_decode')
    includes=['-I'+str(root/'third_party/miniaudio')]
    sections=['-ffunction-sections','-fdata-sections']
    subprocess.run([os.environ.get('CC','gcc'),'-O1','-DMA_NO_DEVICE_IO',*sections,*includes,'-c',str(root/'src/audio/ma_impl.c'),'-o',str(decoder)],check=True)
    subprocess.run([cxx,*flags,*sections,*includes,'-c',str(root/'src/audio/loader.cpp'),'-o',str(loader)],check=True)
    libs=['-lole32','-lwinmm','-lavrt'] if os.name=='nt' else ['-ldl','-lm']
    subprocess.run([cxx,*flags,'-Wl,--gc-sections',str(root/'tests/drum_decode.cpp'),str(loader),str(decoder),*libs,'-o',str(exe)],check=True)
    print('Running drum_decode',flush=True)
    subprocess.run([str(exe)],check=True)
