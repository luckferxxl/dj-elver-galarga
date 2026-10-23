# Investigación de VirtualDJ con REA

Objetivo: entender cómo resuelve VirtualDJ las funciones que Ramiro DJ necesita, para diseñar una implementación **propia**. No se ha reutilizado código, skins ni recursos de VirtualDJ. No se ha leído ni modificado su mecanismo de licencia (`license.dat` no se abrió).

## Entorno y método
- REA (MCP `rea`, ya instalado y configurado): proveedores disponibles en este Windows:
  - **Ghidra 12.1.4**: disponible.
  - **rizin**: vía `execute_rizin_command`.
  - Hopper e IDA: no disponibles en Windows o sin configurar.
- Análisis sobre una **copia de trabajo**: `investigacion/copia-trabajo/virtualdj.exe`, con el mismo SHA-256 que el original (`c8e454fd…d5c9`). La instalación de `C:\Program Files\VirtualDJ` no se ha tocado.
- Alcance: cabeceras PE, secciones, importaciones, cadenas de `.rdata` y los archivos de datos del usuario (`settings.xml` y `database.xml`, solo lectura). Los volcados están en `investigacion/volcados/`.
- **No se hizo una descompilación completa**: el ejecutable tiene 77 MB de código. Un análisis completo con Ghidra llevaría muchas horas y no era necesario para el objetivo funcional.

## Resultados comprobados (observados en el binario o en los datos)

### Binario
- PE32+ x64, enlazador MSVC 14.51, firmado, compilado el 20-09-2026.
- `.text` 77 MB, `.rsrc` 590 MB: skins y recursos incrustados en el propio exe.

### Audio
- No importa `dsound.dll` ni funciones de WASAPI directamente: usa `CoCreateInstance` (COM).
- Las cadenas `(ASIO)`, `(WASAPI)`, `(WDM)`, `ASIOError*`, `WASAPIError*` y `GetMixFormat failed (WASAPI)` muestran **tres modelos de controlador seleccionables**.
- Importa `AvSetMmThreadCharacteristicsW` (avrt.dll) y contiene la cadena `Pro Audio`: el hilo de audio se registra en **MMCSS "Pro Audio"**.
- Ajuste del usuario: `latency = 480` (muestras; 10 ms a 48 kHz) y `exclusiveAudioAccess = no`.
- **Decisión en Ramiro DJ**: WASAPI con `ma_wasapi_usage_pro_audio` (MMCSS), periodo de 10 ms por defecto, modo exclusivo opcional y alternativas DirectSound y WinMM. ASIO queda fuera de la v0.1.

### MIDI y controladores
- Usa WinMM `midiIn*`/`midiOut*`, HID (`hid.dll`) y `setupapi`.
- **Decisión**: MIDI de entrada por WinMM. La salida a LED queda pendiente.

### Decodificación y codificación
- Cadenas de FFmpeg (libavformat/libavcodec) enlazado de forma estática.
- LAME 3.100 para codificar MP3 (grabación).
- **Decisión**: miniaudio (MP3/WAV/FLAC) + stb_vorbis (OGG). Ofrecen menos formatos que FFmpeg, pero sin dependencias pesadas ni licencias LGPL que gestionar. La grabación es en WAV.

### Time-stretch / key lock
- La cadena `Elastique offset:` indica zplane élastique (comercial).
- **Decisión**: en la v0.1 el pitch es tipo vinilo (tempo y tono juntos, con interpolación cúbica Hermite). Key lock pendiente.

### Arquitectura de control
En `.rdata` hay una tabla de **verbos** con nombre que conectan skins, teclado y controladores con el motor. Ejemplos: `play_pause`, `cue_stop`, `sync`, `pitch_range`, `pitch_reset`, `hot_cue`, `loop_in`, `loop_out`, `loop_double`, `beatgrid`, `beat_tap`, `crossfader`, `eq_low/mid/high`, `headphone_mix`, `master_volume`, `key_lock`, `slip_mode` y `get_*` para consultas.

**Decisión**: Ramiro DJ separa también "acciones" de "controles". La interfaz y el MIDI llaman a las mismas órdenes del motor (`Cmd::*`), y las asignaciones MIDI se guardan con identificadores estables (`play`, `cue`, `loop4`…).

### Ajustes de VirtualDJ del usuario (`settings.xml`)
| Ajuste | Valor |
|---|---|
| Frecuencias del EQ | 200 / 1700 / 6500 Hz |
| `cueMode` | cue |
| `hotcueMode` | play (al pulsar un hot cue, salta y reproduce) |
| `globalQuantize` | 1 beat |
| `crossfaderCurve` | Full |
| `masterTempo` | yes |
| `autoBpmStabilizer` | yes |
| `recordBitDepth` | 16-bit |

**Decisión**: mismo comportamiento de CUE (CDJ) y de hot cue ("play"), cuantización al pulso activada por defecto y grabación a 16 bits con tramado por defecto.

### Formato de análisis (`database.xml`, 5226 pistas)
- `<Scan Bpm="0.446916" …>`: **Bpm es el periodo del pulso en segundos**, no los BPM. 60/0,446916 = 134,25 BPM.
  - Confirmación independiente: el detector propio de Ramiro DJ coincide con este valor (±0,5 BPM) en **40/40** pistas del conjunto de ajuste y **77/80 (96 %)** de un conjunto independiente. Contando las equivalencias de octava (drum & bass 87 ↔ 174), **79/80 (99 %)**. Ver `investigacion/validacion_bpm.txt`.
- `<Poi Type="cue" Num="n" Pos="segundos" Name="…">`: hot cues por número y posición en segundos. También hay `Type="automix"` (realStart, fadeStart, tempoStart…) y `Type="remix"`.
- **Decisión**: Ramiro DJ guarda BPM, ancla de rejilla, cue y 8 hot cues (en segundos) por pista en su propio `biblioteca.json`.

## Tercera tanda (19-10-2026, tarde): módulos de la versión 0.3
Cadenas de `.rdata` de la copia de trabajo (volcado `investigacion/volcados/rdata_strings.txt`, obtenido con rizin a través de REA y un script propio):

**Comprobado**
- **SQLite** está incrustado (cadenas internas de SQLite: `sqlite_master`, `sqlite_schema`…).
  - `%LOCALAPPDATA%\VirtualDJ\extra.db`, leído solo para lectura, es SQLite con las tablas `related_tracks`, `track_data` y `lyrics`. La biblioteca principal sigue en `database.xml`.
- **Copias de seguridad automáticas de la base de datos**: `Automatic Database Backup.zip`, `%Y-%m-%d %H-%M Database Backup.zip`, `Pre-Restore Database Backup.zip`.
- **Automix**: modos `AutomixNone`, `AutomixFade`, `AutomixFadeEx`, `AutomixSmart`, `AutomixSmartCut`, `AutomixRadio`, y un "AutoMix Editor".
  - En `settings.xml` del usuario: `automixMode=smart`, `autoMixBeatMatchOnFade=yes`, `automixTempoMode=keep pitch`, `automixSkipLength=4.0`.
  - En `database.xml`: puntos por pista `Poi Type="automix"` (realStart, fadeStart, tempoStart, tempoEnd, fadeEnd, realEnd).
- **Sidelist / Sideview**: `AddToSideList`, `LoadInSideList`, `\Sideview\automix.subfolders`.
- **Tonalidad**: verbos `key_lock`, `pitch_lock`, `get_harmonic`, cadena `camelot`, texto "in order to make it harmonically compatible with the other deck"; `settings.xml`: `keyMatching=standard`, `resetKeyOnLoad=yes`.
- **Event Scheduler**: `eventscheduler`, `eventscheduler_start`, `Event Scheduler`.
- **Retransmisión**: `Broadcast-Server`, `Broadcast-Direct`, `Broadcast-Podcast`, `Broadcasting to {} clients`, `BROADCASTING CANNOT KEEP UP`.
- **Recuperación**: `CrashGuard8`, "Crash caught releasing wasapi object", "Crash handled trying to open midi in/out".

**Datos de referencia del usuario**: `database.xml` guarda la tonalidad detectada por VirtualDJ en 1540 pistas (`Scan/@Key`). Con ella se validó el detector propio (`tools/validar_tonalidad.py`).

**Hipótesis**
- `AutomixSmart` usaría los puntos `Poi automix` (calculados en el análisis) para decidir dónde empieza y acaba cada mezcla.
- `CrashGuard` sería una capa que captura excepciones al liberar objetos WASAPI y MIDI.

Ramiro DJ implementa su propia versión de cada módulo: ver `DECISIONES.md`.

## Mando Pioneer DDJ-WeGO (19-10-2026, segunda tanda)
Búsqueda de la lista MIDI del DDJ-WeGO original:
- **Pioneer no publica** una lista de mensajes del primer modelo. Solo hay manuales y guías de inicio rápido (Serato), sin bytes MIDI.
- **Mixxx** no incluye un mapeo del WeGO en su repositorio oficial (sí DDJ-200/400/SB/SB2/SB3/SX/FLX4).
- Existe un **mapeo comunitario del DDJ-WeGO3** (gldnspud/mixxx-pioneer-ddj-wego3). Solo se tomaron números MIDI, que son datos de hecho, y la calibración del jog, no código:
  - PLAY 0x0B, CUE 0x0C, SYNC 0x58 en el canal del plato.
  - Jog: aro CC 0x21, plato CC 0x22, tocar nota 0x36, valores centrados en 0x40, **720 pasos por vuelta a 33⅓ rpm**.
  - Controles de 14 bits: byte alto CC n y byte bajo CC n+0x20.
  - Navegador CC 0x40 y nota 0x41 en el canal 7; SHIFT nota 0x4F en el canal 7.
  - Luces = nota con valor 0x7F.
- VirtualDJ no tiene definiciones de mandos en claro en `%LOCALAPPDATA%\VirtualDJ\Devices` y `Mappers` (vacías). En el ejecutable solo aparece la imagen del mando y una cadena "DDJWEGO" junto al código de **validación de licencias**, que no se analizó por indicación expresa.

**Comprobado con el mando del usuario** (su `midi.json`, creado con «Aprender»):
| Control | Mensaje |
|---|---|
| Volumen de canal | CC 0x13 en el canal del plato (no en el canal 7 como el WeGO3) |
| Pitch | CC 0x00 |
| Crossfader | canal 7, CC 0x1F |
| Asignado por el usuario a ganancia | canal 7, CC 0x17/0x18 |

**Conclusión**: el WeGO original no coincide del todo con el WeGO3. Por eso se añadió un **asistente de asignación** que adapta cualquier mando, más un preajuste que combina lo comprobado con lo probable. El preajuste está marcado como "por revisar".

## Hipótesis (no confirmadas)
- `Scan/@Phase` parece una referencia de fase de la rejilla. Comparada con el primer tiempo detectado por Ramiro DJ (módulo un pulso), la diferencia va de 0 a 200 ms sin un patrón claro. **No se puede afirmar** que sea "el instante del primer pulso".
- Que el callback WASAPI sea por eventos y el tamaño de búfer real: no se descompiló esa parte.
- Que la sincronización de VirtualDJ corrija la fase de forma continua. Lo sugieren `autoBpmStabilizer` y `cueLoopAutoSync`, sin verificar.
  - Ramiro DJ iguala tempo y alinea la fase al pulsar SYNC y al arrancar un plato sincronizado. Con tempos idénticos no hay deriva: medido 0,00 ms en 9 s en las pruebas.

## Limitaciones del análisis
- Solo estático y superficial: cabeceras, importaciones, cadenas y configuración. Sin trazas en ejecución ni descompilación de funciones.
- rizin no extrae cadenas de regiones de más de 10 MB. Se extrajeron de `.rdata` con un script propio (`ASCII` y `UTF-16LE`).
- Las cadenas indican qué componentes existen, no cómo se usan. Las conclusiones de diseño son decisiones propias inspiradas en esas pistas.

## Implementación propia derivada
| Función | Implementación en Ramiro DJ |
|---|---|
| Transporte | Lectura con interpolación cúbica; fundido de 4 ms en arranque/parada y de 192 muestras en saltos y bucles (sin chasquidos, comprobado). La parada deja la posición exacta. |
| Mezcla | EQ aislador con cruces Linkwitz-Riley de 4.º orden (250 Hz / 2,5 kHz) y compensación de fase (suma plana ±0,1 dB, comprobado); filtro DJ de 24 dB/oct; crossfader con 3 curvas. |
| Saturación | Limitador con anticipación de 1,5 ms que garantiza ≤ -0,3 dBFS (comprobado con +12 dB en ambos platos). |
| Tempo / sync | Pitch ±8/16/50 %, sync de tempo + fase, seguimiento continuo del tempo del otro plato. |
| BPM | Flujo espectral → DFT de la curva de ataques (60-180 BPM) → candidatos emparentados (¾, 4⁄5…) → puntuación por autocorrelación a 1, 2, 4 y 8 pulsos (repetición de compás) → afinado → fase con la curva de graves → primer tiempo de compás por energía de graves. |
| Formas de onda | Bins de 128 muestras con pico y energía por bandas (graves/medios/agudos) para el color. |
| Cues / loops | Cue tipo CDJ, 8 hot cues, loops automáticos 1/4-16 pulsos cuantizados, entrada/salida manual, ½/×2, salto de pulsos. |
