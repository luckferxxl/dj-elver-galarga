# DJ Elver Galarga 0.13.0 — Herramientas LIVE

Esta entrega contiene código fuente. Se han ejecutado las pruebas portables del
motor; no se ha construido ni probado el ejecutable Windows en este entorno.

## Banco de ocho escenas

Pulsa LIVE en la cabecera del rack. Guarda hasta ocho configuraciones con nombre:
ranuras y rutas de efectos, macro, aislador, ajustes de efectos del Separador,
patrones y parámetros de batería/bajo, volúmenes de instrumentos y estado de
reproducción de batería/bajo. Se conservan en rack.json al cerrar y entre sesiones.

Las escenas NO cambian canciones cargadas, muestras de pads, audio grabado del
looper ni la posición de las pistas. No son una copia completa de la sesión.
El botón Guardar solicita confirmación al sustituir una escena; Borrar también.

Con cuantización activada, Lanzar espera el siguiente compás del plato audible
con BPM. Si no hay plato sonando, pide desactivar cuantización para lanzar ahora.
Una pausa cancela el lanzamiento pendiente; un salto grande lo vuelve a preparar
para el siguiente compás. La evaluación ocurre en la interfaz y puede variar un
fotograma: no es un lanzamiento con precisión de muestra. Los instrumentos
conservan sus reglas existentes de cambio de patrón y arranque cuantizado.
El lanzamiento detiene movimientos anteriores para que no sobrescriban la macro.

Clic derecho en Lanzar permite aprender un botón MIDI para esa escena. Las ocho
acciones también están en el listado MIDI. No se ha probado un mando físico.

## Transiciones de macro

En LIVE, elige 8, 16 o 32 compases y un destino de macro entre 0 y 1. Asigna primero
la macro a intensidad/mezcla de las ranuras del rack. Preparar transición espera
el siguiente compás y dibuja una curva suave desde la posición actual al destino.
No mueve automáticamente el crossfader, EQ ni volúmenes de los platos.

Mover manualmente la macro, detener/saltar en el plato o pulsar Detener cancela
la transición. QUITAR FX cancela también transiciones y escenas pendientes.
El transporte de los platos permanece bajo el control del DJ.

## Tres nuevos efectos

Disponibles en la estación del rack, en la pestaña FX de cada plato y en el
Separador. En el rack aparecen en «Texturas y carácter».

- Granular: cuatro granos con ventana suave, dispersión temporal y tono entre
  -12 y +12 semitonos. Está pensado para texturas; introduce un retardo audible
  de unas décimas de segundo, no para conservar el ataque del bombo. Mantiene
  historial mientras está seleccionado para reducir el vacío al activarlo.
- Resonador cromático: realimentación limitada y afinación de 110 a 440 Hz por
  pasos de semitono. No sigue automáticamente la tonalidad del tema.
- Saturación multibanda: deja sin saturar la rama grave filtrada a 120 Hz y
  distorsiona las ramas de medios/agudos. No sustituye un limitador ni usa oversampling.

Usan fundidos y el limitador final existentes. Las identificaciones anteriores
se mantienen: los efectos se han añadido al final de las enumeraciones.

## EQ y FX independientes en el Separador

En cada pista separada, abre el menú de tres puntos. Hay EQ de tres bandas,
selección de efecto, encendido, intensidad, mezcla y división temporal.
El BPM de efectos se ajusta manualmente: no modifica el tempo del audio.
Mute y Solo actúan después del procesamiento; la preescucha recibe la mezcla
procesada. QUITAR FX incluye los efectos del Separador y deja terminar sus colas.
El botón del menú permite restaurar EQ y apagar el efecto de esa pista.
Los ajustes se guardan en el rack y se pueden incluir en escenas.

Esta función actúa en el reproductor del Separador. No añade todavía un mezclador
multistem a cada plato. La exportación WAV del Separador conserva su comportamiento
anterior: mezcla seca con ganancias/mute/solo, sin imprimir EQ ni efectos nuevos.
Para registrar el procesamiento en directo, usa la grabación del master.

## Troceado de samples

SAMPLER → clic derecho en una muestra → Trocear muestra en 8 pads. Elige sustituir
pads 1–8 o 9–16. Se generan ocho fragmentos de igual duración, con fundidos de 3 ms,
se guardan los WAV en Grabaciones y se asignan al banco en modo Disparo.
No detecta transitorios ni estira automáticamente los fragmentos al BPM.
Las asignaciones anteriores del banco elegido se sustituyen; sus archivos no se
borran. No se permite trocear a un banco que tenga muestras todavía cargándose.
La escritura a disco ocurre en la interfaz y puede pausarla brevemente; el hilo
de audio no escribe archivos.

## Correcciones

- Guardado del rack/escenas: reemplazo del archivo existente en Windows mediante
  MoveFileExW; los fallos de escritura se notifican.
- Si la cola del motor rechaza una carga de pad, se conserva la muestra anterior
  y su memoria; ya no se sustituye desde la interfaz sin confirmación de envío.
- Exportar una pista separada conserva la selección de solos que había antes.
- El reproductor de stems calcula la posición según la frecuencia de cada pista.

## Validación y límites

Ejecutado en Linux con g++ C++20:

- tests/live.cpp: DSP a 44,1/48/96 kHz, salida finita/acotada, independencia estéreo,
  ausencia de reservas durante las llamadas probadas, troceado sin pérdida de
  fotogramas y sin modificar el original, cuantización/cancelación/saltos,
  aislamiento real entre stems, Mute y Solo, y paso de los tres efectos nuevos
  por el procesamiento real de los platos.
- tests/performance.cpp: regresiones existentes de transporte, loops, slip,
  reverse, key lock y efectos anteriores.
- tests/retrospective.cpp: historial, vuelta del búfer y capturas concurrentes.
- Comprobación de sintaxis de los archivos de interfaz modificados. Para los
  archivos que usan WinAPI se usaron declaraciones de prueba, sin enlazar Windows.

Para repetir las pruebas DSP portables: python tools/probar_live.py
Para Windows, sigue docs/COMPILAR.md; CMake incluye rdj_live_tests en CTest.

Pendiente de validar: compilación completa de Windows, interacción visual y
rendimiento en una sesión larga con controladoras/tarjetas reales. No se certifica
compatibilidad universal ni superioridad frente a otras aplicaciones.
