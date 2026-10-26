# DJ Elver Galarga 0.14.0 Pro — Auditoría Técnica y Arquitectura de 7 Fases

## Fase 1: Auditoría de la Base Existente y Stack Tecnológico
- **Lenguaje y Estándar:** C++20 nativo (`MinGW-w64 UCRT 64-bit`, compilado en un único ejecutable autónomo sin dependencias externas de DLL).
- **Motor Gráfico (UI):** `Dear ImGui` sobre `Direct3D 11` + `Win32 API`, con 12 temas de color profesionales y selector RGB en tiempo real.
- **Motor de Audio de Baja Latencia (`src/audio/`):**
  - Salida `WASAPI` (modo compartido de baja latencia y modo exclusivo) + enrutamiento independiente de auriculares (Canales 3/4, segundo dispositivo de audio o división mono).
  - Hilo de audio estrictamente *lock-free* (comunicación por `std::atomic` y colas circulares `SPSC`, `0` asignaciones de memoria dinámica durante `Engine::process()`).
  - *Time-stretching* y *Pitch-shifting* polifónico de alta calidad (`Signalsmith Stretch`, licencia MIT): permite **Key Lock (Master Tempo)** y **Key Shift independiente (`-12` a `+12` semitonos)**.
  - Sincronización (`SYNC`) de tempo y fase a la muestra con relevo automático de `MASTER`, cuantización (`Quantize`), `Slip Mode` real con cabezal fantasma, 8 Hot Cues con nombre y color, loops automáticos y manuales, 24 efectos en cadena + ruleta de color (`FILTER`, `SPACE`, `NOISE`, `CRUSH`), aislador de 3 bandas, *Loop Station*, caja de ritmos, sintetizador de bajo acid, sampler de 16 pads y grabadora WAV (16-bit dithered / 32-bit float) con limitador *lookahead*.
- **Base de Datos y Análisis (`src/core/`):**
  - Base de datos transaccional `SQLite3` (`%APPDATA%\RamiroDJ\biblioteca.db`) con copias de seguridad diarias automáticas.
  - Estudio integrado **`ELVERKEY / MIX LAB`** a pantalla completa: rueda Camelot de 24 tonos, semáforo armónico en vivo, banco de transiciones `A → B` con ensayo automatizado en directo, generador `Auto-Set` con 5 perfiles de curva de energía, banderas `MIX IN ▶` / `◀ MIX OUT` con cuenta atrás en compases y auditor de duplicados y salud de biblioteca.

---

## Fase 2 y 3: Separación de Stems Híbrida (Tiempo Real 0 ms + IA Demucs en Caché)
1. **Separación de 4 Stems en Tiempo Real por Plato (`DeckStemProcessor` en `src/audio/dsp.h` — Implementado y Verificado):**
   - Opera directamente dentro de cada plato (`Plato A` y `Plato B`) con **0 ms de latencia añadida** y **0 reservas de memoria en el callback de audio**:
     - `🎤 VOZ (Vocals)`: aislamiento del centro armónico focal (`Mid` en banda `210–4200 Hz` con cruces Linkwitz-Riley de 4.º orden y sustracción de transitorios percusivos).
     - `🥁 BATERÍA (Drums)`: extracción de transitorios percusivos mediante detector diferencial de envolvente rápida/lenta sobre pegada de graves y agudos.
     - `🎸 BAJO (Bass)`: subgraves y línea de bajo sostenida (`< 195 Hz` LR4 libre del pico transitorio del bombo).
     - `🎹 INSTRUMENTOS (Instruments)`: complemento diferencial exacto (`in - (voc + drm + bas)`), lo que garantiza **reconstrucción matemáticamente bit-exacta (`error < 1e-5`)** cuando los 4 stems están al 100 %.
   - Controles por stem en cada plato: `MUTE` instantáneo con rampa anticlip, `Volumen (0–150 %)`, `Efecto/Filtro individual por Stem (0–100 %)` y modos rápidos `SOLO VOZ (Acapella)`, `SOLO BASE (Instrumental)` y `SOLO BATERÍA + BAJO`.
2. **Separación por IA Profunda Offline con Caché Persistente (`StemSeparator` en `src/core/stems.h/.cpp` — Implementado y Verificado):**
   - Integra **Demucs (`htdemucs` 4 pistas y `htdemucs_6s` 6 pistas: Voz, Batería, Bajo, Guitarra, Piano, Otros)** mediante PyTorch, almacenando los stems separados en caché persistente y cargándolos en el reproductor multipista sincronizado o en los platos sin interrumpir nunca el audio en vivo.

---

## Fase 4 y 5: Reconocimiento Universal de +365 Controladoras DJ (11 Marcas) e Integración de Stems
- **Catálogo Universal (`mandos::universalCatalog()` en `src/core/mandos.cpp` — Implementado y Verificado con Tests):**
  - Registra **368 modelos de las 11 marcas**: *Pioneer DJ / AlphaTheta, Denon DJ, Numark, Hercules, Native Instruments (Traktor), Reloop, Roland, Allen & Heath (Xone), Akai Professional, Behringer y Vestax*.
  - Incluye **144 mapeos XML + scripts JS nativos del proyecto Mixxx (GPLv2+)** compilados dentro del propio ejecutable (`mandos.rc` + motor `QuickJS`), además del preajuste nativo de 14 bits para **Pioneer DDJ-WeGO** y aprendizaje `MIDI Learn` con *Soft-Takeover*.
- **Transparencia de Validación (Criterio de Calidad 8):**
  - Cada modelo muestra explícitamente en la interfaz su estado real:
    1. `Verificado Hardware`: probado en controlador físico real (serie DDJ-WeGO / DDJ-200).
    2. `Mapeo Nativo Mixxx XML Incluido`: mapeo oficial completo con scripts JS y LEDs verificado por test automatizado de carga y ejecución.
    3. `Perfil MIDI Estándar Auto-Adaptativo + Learn`: reconocimiento de modelo y asignación asistida pendiente de validación en unidad física.
- **Control Físico de Stems y Detector de Conflictos MIDI:**
  - 16 nuevas acciones MIDI por plato (`stemvocalmute`, `stemdrumsmute`, `stembassmute`, `steminstmute`, volúmenes y efectos por stem, `keyup`, `keydown`, `keyreset`, `quantize`) con retroalimentación LED bidireccional.
  - `MidiManager::detectConflicts()` audita en tiempo real todas las asignaciones y alerta si dos acciones comparten canal y nota/CC.

---

## Fase 6 y 7: Pruebas Automatizadas y Métricas de Rendimiento
- **Baterías de pruebas ejecutadas (`ctest`):**
  1. `rdj_tests` (`core` + `testPro0140`): verifica decodificación, EQ LR4, precisión de BPM/rejilla, SYNC lock, tonalidad Camelot, reconstrucción bit-exacta y aislamiento del `DeckStemProcessor`, catálogo universal de `>= 350` controladoras en las 11 marcas, `identifyController()` y `detectConflicts()`.
  2. `rdj_performance_tests`: mide el consumo real de CPU del motor de audio (`< 3 %` de un núcleo para 2 platos + rack + stems RT a 48 kHz).
  3. `rdj_cover_tests`: verifica lectura de etiquetas y carátulas.
