# Decisiones técnicas

## 1. Motor de audio: miniaudio y no JUCE (19-10-2026)
El encargo de la versión 0.3 pedía C++20 y JUCE. El usuario eligió **mantener y ampliar** el motor existente:
- **Ya funcionaba y estaba verificado**: más de 140 comprobaciones automáticas y pruebas con audio real. Rehacerlo con JUCE suponía volver a empezar.
- **Licencias**: miniaudio es dominio público / MIT-0. JUCE 8 es AGPLv3 o licencia comercial, lo que obligaría a publicar el código de Ramiro DJ o a cumplir los términos comerciales.
- **Herramientas**: JUCE está pensado para Visual Studio, que no está instalado (varios GB y permisos de administrador). El proyecto compila con MinGW-w64, que ya estaba en el equipo.
- **Requisitos cubiertos sin JUCE**: WASAPI con prioridad MMCSS "Pro Audio", sin reservas de memoria ni bloqueos en el hilo de audio, búfer y frecuencia configurables, y salidas MASTER y CUE separadas.
- El proyecto sí pasó a **C++20**.

## 2. Interfaz: Dear ImGui sobre Direct3D 11
Es de modo inmediato y muy rápido: se dibuja a 60 fps, adecuado para formas de onda y medidores. Licencia MIT. Las ventanas y controles de DJ (potenciómetros, faders, platos, botones de neón) son propios.

## 3. Key lock: Signalsmith Stretch (MIT)
La alternativa comercial de VirtualDJ es zplane élastique. Rubber Band es GPL o comercial. Signalsmith Stretch es MIT, de solo cabeceras y con buena calidad.
- **Integración**: el plato lee a velocidad variable, como un vinilo, y el estirador corrige el tono (transposición = 1/tempo).
- **Latencia**: unos 100 ms. Se compensa con un **lector adelantado** y un **historial de posiciones**, de modo que la posición publicada es la del fotograma que realmente suena.
- **Resultado medido**: error de alineación de 1,8 ms y desfase de 0,00 ms en sincronía con un plato en key lock. Coste: 17 ms de CPU por cada segundo de audio y plato.

## 4. Biblioteca: SQLite (dominio público)
VirtualDJ también lo incrusta (visto con REA). Se usa una transacción por guardado, modo WAL y copia diaria en `copias\` (se guardan 7). La migración desde `biblioteca.json` es automática y el original se conserva como `.migrado`.

## 5. Tonalidad: cromagrama + perfiles de tonalidad
FFT de 8192 puntos a unos 11 kHz, solo picos espectrales de 100 a 1800 Hz y tramas ponderadas por energía. El cromagrama se correlaciona con los perfiles EDMA (Faraldo et al., 2016); se compararon también Krumhansl y Temperley.
- **Validación con `tools/validar_tonalidad.py`**: 100 % en acordes sintéticos, 28 % exacto y 60 % compatible para mezclar frente a VirtualDJ. VirtualDJ no es una verdad de referencia, pero el resultado se marca como **precisión limitada**.
- **Probado sin mejora**: blanqueo espectral, compresión logarítmica, estimación de afinación y rango ampliado.

## 6. Automix
Precarga la siguiente pista en el plato libre. La transición empieza N pulsos antes del final (8 a 64, configurable); la pista que entra arranca en su cue o primer tiempo, sincronizada, y el crossfader se mueve con curva suave. La "mezcla inteligente" por análisis de intro y outro queda pendiente.

## 7. Recuperación de sesión
`sesion.json` se escribe cada 5 s con `clean=false` y al salir con `clean=true`, mediante escritura atómica. Al arrancar se recargan pistas, posiciones, pitch, key lock, mezclador y efectos, con los platos en pausa. Si una pista recuperada aún está cargando, se conserva en la sesión: este fallo se encontró al probarlo y está corregido.

## 8. Diseños: tabla de colores en tiempo de ejecución (0.4)
Los colores de `col::` pasaron de constantes a variables que rellena `applyTheme()`; así cada diseño cambia también los colores de ImGui sin reiniciar. Cinco diseños propios (ningún recurso de terceros).

## 9. Tonalidad: HPCP + perfil Temperley (0.4)
El cromagrama simple acertaba la clave exacta de VirtualDJ en un 28 % de 40 pistas. Un HPCP (36 bandas, picos espectrales, armónicos ponderados, corrección de afinación) con el perfil de Temperley llega al 38 % exacto y al 58 % compatible para mezclar (KK 30 %, EDMA 32 %; variar armónicos o la frecuencia mínima no mejoró). Las claves calculadas antes se marcan para reanálisis (columna de versión = 2). La presentación sigue la tabla Camelot facilitada por el usuario.

## 10. Separador: Demucs en un proceso de Python aparte (0.4)
Demucs (MIT) es el separador libre de referencia y da 6 pistas con `htdemucs_6s`. Se ejecuta en un proceso separado con prioridad baja, así un fallo o la carga de CPU no afectan al hilo de audio. No se incluye en el instalador (≈1,2 GB): se instala bajo demanda desde PyTorch (índice CPU oficial) y PyPI. Los stems se guardan en FLAC de 16 bits; el reproductor de stems vive dentro del motor (comandos StemLoad/StemPlay…) y suma al master antes del limitador.

## 11. Diseño 0.5 y navegador tipo VirtualDJ
- **Aspecto**: se sigue la maqueta del usuario (azul noche, turquesa y coral, carátulas, BPM grande, insignias de tonalidad). Los paneles son planos; el brillo de neón queda para el diseño *Neón*. La fuente de rótulos pasa de Unbounded (muy ancha) a **Barlow Semi Condensed** (OFL), que deja sitio a títulos y cifras; el texto sigue en Hanken Grotesk. Los iconos se dibujan con primitivas de ImGui para no depender de las fuentes de Windows.
- **Carátulas**: lectura propia de las etiquetas (sin bibliotecas nuevas) y decodificación con **WIC** en un hilo aparte, recortadas a 192 × 192. La interfaz solo sube texturas (hasta 12 por fotograma) y libera las que llevan tiempo sin verse.
- **Navegador**: como en VirtualDJ, abrir una carpeta la lee en segundo plano y sus pistas entran en la base de datos (la colección), así se pueden buscar y analizar. Las carpetas favoritas son las de siempre (escaneo recursivo). La Sidelist y el Automix siguen siendo listas normales con nombre reservado, de modo que no cambia el formato de `biblioteca.db`.
- **Selección múltiple**: la de ImGui (`BeginMultiSelect`) con `SelectOnClickRelease`, para que arrastrar una fila no seleccionada no empiece un recuadro de selección.

