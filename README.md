# DJ Elver Galarga

Programa de DJ para Windows (C++, Dear ImGui + miniaudio), pensado para el mando **Pioneer DDJ-WeGO**, aunque incluye mapeos para muchos otros mandos MIDI.

Versión: **0.14.0 Pro**

## Compilar

Requisitos: CMake, Ninja, MinGW-w64 y, para el instalador, Inno Setup 6.

```powershell
powershell -ExecutionPolicy Bypass -File compilar.ps1            # compila, pruebas, instalador y zip
powershell -ExecutionPolicy Bypass -File compilar.ps1 -SinPruebas -SinInstalador
```

O a mano:

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Más detalles en `docs/COMPILAR.md` y `LEEME.md`.

## Licencia

GPL v2 o posterior (ver `COPYING.txt`), porque incluye los mapeos de mandos de Mixxx. Los componentes de terceros conservan sus licencias (carpeta `licencias/`).
