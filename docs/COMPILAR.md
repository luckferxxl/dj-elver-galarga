# Nota de la versión 0.6.0

El script compilar.ps1 ejecuta ahora `ctest --test-dir build --output-on-failure`, incluyendo las pruebas originales, `rdj_performance_tests` y `rdj_cover_tests`. Se necesita compilar todos los targets antes de ejecutarlo. Esta entrega contiene código fuente, no binarios 0.6.0 comprobados en Windows. Ver NOVEDADES-0.6.0.md.

---

# Cómo recompilar Ramiro DJ

## Requisitos (todos gratuitos)
| Herramienta | Versión usada | Instalación |
|---|---|---|
| MinGW-w64 (GCC, UCRT, POSIX) con CMake y Ninja | GCC 16.1, CMake 4.3, Ninja 1.13 | `winget install BrechtSanders.WinLibs.POSIX.UCRT` |
| Inno Setup 6 (solo para el instalador) | 6.7.3 | `winget install JRSoftware.InnoSetup` |
| Python 3 (solo herramientas opcionales) | 3.11 | python.org |

Las bibliotecas de terceros van incluidas en `third_party/`, con sus licencias en `licencias/`:
- miniaudio 0.11.25 (dominio público / MIT-0): salida de audio WASAPI/DirectSound/WinMM, decodificación de MP3/WAV/FLAC y escritura de WAV.
- stb_vorbis, incluido en miniaudio (dominio público / MIT): OGG Vorbis.
- Dear ImGui 1.92.9 (MIT), con los backends Win32 y Direct3D 11: interfaz.
- nlohmann/json 3.12.0 (MIT): archivos de configuración.
- Signalsmith Stretch 1.4.0 + Signalsmith Linear 0.6.5 (MIT): key lock y transposición.
- SQLite 3.54.1, amalgamación (dominio público): base de datos de la biblioteca.
- Fuentes Barlow Semi Condensed (seminegrita y negrita) y Hanken Grotesk (OFL), incrustadas como recurso. Las carátulas se decodifican con WIC, que forma parte de Windows.

El proyecto usa **C++20**. Las decisiones técnicas (por qué miniaudio y no JUCE, key lock, SQLite…) están en `DECISIONES.md`. La lista de funciones con su estado, en `FUNCIONES.md`.

Opción de diagnóstico adicional: `--dump-state archivo.txt` arranca, espera a que terminen las cargas, vuelca el estado de los platos y sale (prueba de recuperación de sesión).

No hace falta Visual Studio. Direct3D 11, WinMM y WASAPI forman parte de Windows.

## Todo en un paso
```powershell
powershell -ExecutionPolicy Bypass -File compilar.ps1
```
El script configura, compila, ejecuta las pruebas y deja en `dist\`:
- `RamiroDJ-Setup-0.5.0.exe`: el instalador.
- `RamiroDJ-codigo-fuente-0.5.0.zip`: el código fuente.

Opciones: `-SinPruebas` y `-SinInstalador`.

## A mano
```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\rdj_tests.exe                 # pruebas automáticas (sin hardware)
.\build\rdj_tests.exe --real-device   # + abre los dispositivos reales (en silencio)
.\build\RamiroDJ.exe
```
El ejecutable se enlaza de forma estática y no necesita DLL de MinGW junto a él.

## Opciones de diagnóstico de RamiroDJ.exe
- `--null-audio`: arranca sin dispositivo de audio (modo simulado).
- `--screenshot archivo.png`: guarda una captura de la ventana y sale.
- `--load A B`: carga dos pistas sin reproducir (útil con `--screenshot`).
- `--theme N`: arranca con el diseño N (0 = Ramiro … 5 = Monocromo). `--separador` / `--biblioteca`: arranca en ese modo.
- `--add-folder carpeta`: añade una carpeta favorita y analiza lo pendiente al terminar el escaneo. `--browse carpeta` (o `--browse lista:Nombre`): abre esa carpeta o lista en el navegador. `--sidelist lista.m3u8`: importa la lista y la copia a la Sidelist. `--select archivo`: muestra sus detalles. Variable `RDJ_SHOT_SECONDS`: retrasa la captura de `--screenshot`.
- `--midi`: abre el diálogo del mando. `--dump-state archivo`: escribe el estado de los platos.
- Variable `RDJ_DATA_DIR`: usa otra carpeta de datos (pruebas sin tocar la del usuario).
- `--mandos-prueba informe.txt`: arranca uno a uno los 144 mapeos de mandos sin el aparato, les manda todos sus controles (y valores de prueba a faders y ruedas), los para y escribe un informe: errores de sus scripts, luces enviadas y controles de Mixxx sin equivalente. Devuelve el número de mapeos con errores. Variable `RDJ_MANDO_PRUEBA="DDJ-400"`: simula un puerto MIDI con ese nombre (sin aparato) y arranca el mapeo que le toca, para ver su tarjeta en el diálogo.
- `--rack "preset" izq der macro` (con `--load`): aplica un preset del rack, elige los módulos de cada panel (0 efectos, 1 pad XY, 2 aislador, 3 loop) y la macro, para capturas. `--rack-loop n`: graba un loop de n compases. `--rack-xy`: deja el pad XY pulsado con HOLD.
- Variable `RDJ_MANDOS_DIR`: carpeta de mapeos que se mira antes que los incluidos (después de `%APPDATA%\RamiroDJ\mandos`). `rdj_tests.exe --mandos` la usa para leer `res/mandos` del código fuente.
- `--autotest pistaA pistaB registro.txt`: prueba de extremo a extremo con audio real. Carga, mezcla con sync, usa loop y hot cue, graba, rechaza un archivo incompatible, reproduce la grabación, cambia de dispositivo, prueba MIDI, key lock, Automix, programador, navegador (carpeta, Sidelist, favoritos, sugerencias) y Separador. Escribe el resultado en el registro y devuelve el número de fallos.

## Si el programa se cierra de golpe
Desde la 0.7.0, un cierre inesperado deja `fallo.txt` en la carpeta de datos (`%APPDATA%\RamiroDJ`): fecha, código y la pila como `módulo+desplazamiento`. Con un `RamiroDJ.exe` compilado con símbolos (`-g` y sin `-s` al enlazar) se traduce a archivo y línea con `x86_64-w64-mingw32-addr2line -f -C -e RamiroDJ.exe 0x140000000+desplazamiento`.

Compilación de comprobación (para buscar fallos de memoria de la interfaz): `-DRDJ_IMASSERT_DIAG -DIMGUI_USER_CONFIG="\"<ruta>/tools/imgui_comprobaciones.h\""` en `CMAKE_CXX_FLAGS` (más `-D_GLIBCXX_ASSERTIONS` para los contenedores de C++). Los `IM_ASSERT` de Dear ImGui, que en la versión normal no se comprueban, quedan anotados en `fallo.txt` con su pila. Así se encontró en la 0.7.0 una lista de 4 columnas a la que se le pedían las columnas 7 y 8.

## Mandos (mapeos de Mixxx)
- Los archivos de `res/mandos` (`*.midi.xml` y sus `.js`, más `common-controller-scripts.js`, `midi-components-0.0.js` y `lodash.mixxx.js`) entran en `RamiroDJ.exe` como recursos RCDATA: CMake genera `mandos.rc` y un índice (recurso 2999, líneas «id⟶nombre») al configurar. Si añades o quitas archivos, vuelve a ejecutar CMake.
- Para actualizarlos: copia la carpeta `res/controllers` de Mixxx (rama `main`) y apunta el commit en `res/mandos/LEEME-MAPEOS.txt`. Después ejecuta `--mandos-prueba` y `rdj_tests.exe --mandos`.
- Licencia: los mapeos son GPL 2 o posterior, por eso Ramiro DJ entero se distribuye con esa licencia (`COPYING.txt`) y con el código fuente.
- Comportamiento como en Mixxx: los cambios de valor y `engine.trigger` se encolan; un valor igual no avisa; *soft takeover* con su ventana de 50 ms; potenciómetros con el «64 = centro»; volumen, ganancia y EQ con curva en dB; controles de 14 bits; `scratchEnable`/`scratchTick` con el filtro alfa-beta.

## Rack (0.9)
- Sin dependencias nuevas: los efectos son propios o los de los platos (`effects.h`, `dsp.h`).
- Pruebas: `rdj_tests.exe --rack` (solo el rack) o `rdj_tests.exe` (todo). En `--autotest` hay 8 comprobaciones del rack.
- El rack sigue las reglas del hilo de audio. La memoria de la loop station se reserva desde la interfaz (`Rack::allocLoop`) antes de la primera grabación y se publica con un atómico (`release/acquire`); el audio no la toca mientras no esté lista. `Engine::prepare` solo la reasigna si cambia la frecuencia de muestreo (con el audio parado).
- Documentación de uso, ruteo y comportamiento: `docs/RACK.md`.

## Instrumentos y banco de SYNC (0.10)
- `src/audio/instruments.*` (caja de ritmos, bajo, sampler) entra en `rdj_core`; su interfaz es `src/ui/ui_instruments.cpp`. Sin dependencias nuevas. Al añadir archivos `.cpp` a `src/ui` hay que volver a ejecutar `cmake` (la lista se genera con GLOB).
- `rdj_sync_bench.exe` (no se instala): genera pistas con BPM y primer tiempo conocidos, mide la rejilla que detecta el análisis y el desfase del motor con SYNC en 10 situaciones. Con `RDJ_BPM_DEBUG=1` el análisis escribe sus detalles en la consola de errores.

## Herramientas (carpeta `tools/`)
- `validar_bpm.py [n] [semilla]`: compara el detector de BPM con los BPM que VirtualDJ guardó en su `database.xml` (solo lectura).
- `depurar_bpm.py "nombre.mp3"`: muestra los candidatos de tempo de una pista.
- `generar_icono.py`: regenera `res/ramirodj.ico`.
- `validar_tonalidad.py [n] [semilla] [perfil]`: compara la tonalidad con la de VirtualDJ (usa `build\rdj_keytool.exe`, copia de `rdj_tests.exe`).
- `imgui_comprobaciones.h`: ver «Si el programa se cierra de golpe».
- `stemtool.cpp` → `build\rdj_stemtool.exe [--instalar] [archivo [htdemucs_6s|htdemucs]]`: instala el motor de separación si falta y separa un archivo sin interfaz.

## Estructura del código
```
src/audio/   motor en tiempo real (sin interfaz):
  engine.*     platos, mezclador, limitador, enrutado de auriculares
  dsp.h        biquads, EQ aislador LR4, filtro DJ, limitador
  effects.h    eco, flanger, reverberación
  analysis.*   forma de onda por bandas y detección de BPM/fase
  loader.*     decodificación y análisis en segundo plano
  recorder.*   grabación a WAV en un hilo propio (y exportación de loops)
  rack.*       rack de hardware virtual: estación de efectos, pad XY, aislador y loop station
  audio_io.*   dispositivos (miniaudio)
src/core/    biblioteca, etiquetas, MIDI, preferencias, utilidades
  mandos.*     catálogo de mapeos de Mixxx: lector de XML, reconocimiento por nombre del puerto
src/ui/      ventana Win32 + D3D11, interfaz ImGui, diálogos:
  ui_frame.cpp    barra superior, ondas, platos, mezclador, diálogos
  ui_browser.cpp  navegador tipo VirtualDJ (árbol, lista, Sidelist, detalles)
  ui_separator.cpp modo Separador
  widgets.*       temas, controles e iconos vectoriales
  covers.*        carátulas (WIC → texturas D3D11)
  ui_rack.cpp     rack de hardware virtual: interfaz, presets (rack.json) y MIDI del rack
  mandos_puente.*  mandos reconocidos: un intérprete QuickJS por mando con la API de Mixxx
                   (engine, midi, console, ColorMapper), entradas y luces del XML, SysEx, scratch
  mandos_controles.cpp  traducción de los controles de Mixxx ([Channel1] play, [Master] crossfader…)
res/mandos/  mapeos de Mixxx (GPL 2+) que se meten en el .exe como recursos (CMake genera mandos.rc)
third_party/quickjs/  QuickJS-ng (MIT), intérprete de JavaScript
tests/       pruebas automáticas
```

### Reglas del hilo de audio
`Engine::process()` no reserva memoria, no bloquea y no hace E/S.
- La interfaz envía órdenes por una cola SPSC sin bloqueo y parámetros por atómicos.
- La grabación sale por un anillo SPSC hacia su propio hilo.
- Las pistas se decodifican completas en otro hilo. La interfaz solo libera una pista cuando el motor confirma que ya usa otra (contador de generación).
