# Ampliación 0.15.0

Caja PRO implementada con kits propios, samples, envolventes, controles por voz, MIDI y bancos personales. Pruebas de audio y decodificación WAV ejecutadas; sintaxis de interfaz comprobada. Windows e interacción pendientes. Véase `docs/NOVEDADES-0.15.0.md`. Los estados que siguen son históricos.

# Ampliación 0.14.0

Taller con edición de sampler, loops por archivo, metrónomo de preescucha y herramientas de patrones. Se han ejecutado pruebas del motor y comprobaciones de sintaxis; Windows e interacción pendientes. Véase `docs/NOVEDADES-0.14.0.md`. Los estados de versiones anteriores que siguen son históricos.

# Ampliación 0.13.0

Implementadas herramientas LIVE, tres efectos nuevos en rack/platos, EQ/FX por pista del Separador y troceado a pads. Véase `docs/NOVEDADES-0.13.0.md`. Pruebas portables y de sintaxis realizadas; Windows y hardware pendientes. Los estados de versiones anteriores que siguen debajo son históricos.

# DJ Elver Galarga (antes Ramiro DJ, Windows nativo) — Progreso

## Versión 0.11.3 (24-10-2026): botón de efectos en cada plato
Petición del usuario: «en la parte de los decks a veces se quedan también efectos encendidos y es difícil saber cuál es; también un botón en cada deck para que no salga».
- `activeEffects(int onlyDeck)`: con un plato, su ruleta, su efecto, REV (nuevo `Reverso`, también en la lista general) y lo del rack/aislador cuyo origen es ese plato. Cada efecto lleva un nombre corto (`tag`) para el botón. `clearEffectsOf(d)` quita los de un plato (o todo con −1; `clearAllEffects()` lo usa).
- `drawDeckFxButton()`: primer chip de la cabecera del plato (sustituye al chip «FX» violeta), gris «FX» o ámbar con el nombre / «N FX ×»; tooltip con la lista, clic = quitar los de ese plato, clic derecho = elegir. Punto ámbar en las pestañas FX y VINILO.
- Acción MIDI `quitarefectosplato` (MA_DeckFxClear, por plato, en el rango del rack para poder asignarla en mandos con mapeo de Mixxx) con luz por plato.
- Prueba en `--autotest`: 4 efectos en el plato A y 2 en el B; el botón del A quita los 4 y el B sigue con los suyos.

## Versión 0.11.2 (23-10-2026): texto a escala alta
- Visto al hacer capturas a 192 ppp para la web: `PushFont(nullptr, 11 * s)` (rack, bajo la MACRO) y `PushFont(fontHead, 17.f * s)` (mandos reconocidos) multiplicaban dos veces por la escala de la pantalla (ImGui 1.92 ya aplica `FontScaleDpi`). Ahora 11 y 17 sin escalar.

## Versión 0.11.1 (23-10-2026): botón QUITAR FX
Petición del usuario: «tenemos que poner un botón que desactive los filtros arriba, ya que se quedan activados y con tantos no se sabe cuál es».
- `ui_fxclear.cpp`: `activeEffects()` reúne lo que queda puesto (ruleta FILTER/COLOR fuera del centro, efecto del plato encendido, ranuras encendidas o encendidas por la macro/pad, MACRO > 1 %, HOLD del pad, aislador, movimiento grabado sonando); `clearEffect()` quita uno; `clearAllEffects()` todos (primero el movimiento, para que no vuelva a subir la macro). Botón en la barra superior a la izquierda de Grabar: ámbar con el número, tooltip con la lista, clic = quitar todo, clic derecho = elegir cuál.
- Acción MIDI `quitarefectos` (MA_FxClearAll, global, va por `rackMidi`) con luz.
- Prueba en `--autotest`: 4 efectos puestos, 4 quitados de un clic.

## Versión 0.11.0 (23-10-2026): once efectos nuevos y ruleta de color
Petición del usuario: «me gustaría añadir más efectos de DJ al software»; eligió todos los propuestos: riser, spiral, sidechain, shimmer, vinilo lo-fi, teléfono/radio, chorus/trémolo/auto-pan, freeze, ruleta de color por canal y slicer. (Brake, backspin, censor y limitador ya estaban: pestaña VINILO y master.)
- `StFx` (rack.h): 11 tipos al final del enum (los presets guardan el id). `StUnit` recibe la posición en pulsos de la fuente (`beat0/beat1`) para sidechain, trémolo, auto-pan y slicer; el rack lleva un reloj libre al mismo tempo si la fuente está parada o sin rejilla. Nuevos bloques: `Svf` (filtro de estado variable TPT, ahora en dsp.h), `GrainShifter` (cambio de tono por granos, spiral y shimmer). Freeze y slicer graban siempre (`stFxNeedsHistory`) y copian su trozo a una memoria propia al encenderse (hasta 2,75 s), así se pueden encender y apagar seguido.
- Platos: `FxType` amplía la lista en el mismo orden; los nuevos los procesa un `StUnit` por plato (`deckFxToStation`, `deckFxUnitParams`); los de siempre siguen en `FxSlot` sin cambios. Reloj de esos efectos: la rejilla del plato o uno libre.
- Mezclador: `ColorFx` (color_fx.h) sustituye a `DjFilter` en el canal; con FILTER llama exactamente al mismo filtro (comprobado muestra a muestra). `DeckParams::colorFx`, `Settings::colorFx`. Botón en la cabecera del mezclador y clic derecho en la ruleta.
- Interfaz: desplegable por familias con «NUEVO» y descripciones; tiempos permitidos por efecto (`stFxDivAllowed`) con texto propio (`stFxTimeText`: el riser en compases); tooltips de la ruleta (`stFxAmountText`); en la pestaña FX la barra de Cantidad dice qué mueve. 5 presets nuevos. Opción de captura `--fx-demo fxA fxB color ruleta`.
- Ajustes de nivel medidos: shimmer al nivel de la reverb larga (+12 dB como mucho con mezcla 100 %, igual que las otras reverbs), teléfono con compensación lenta (de −15 dB a −5 dB en una pista con muchos graves), SPACE de +13 a +8 dB a tope.
- Pruebas: 585 comprobaciones en `rdj_tests.exe` (254 del rack), `rdj_performance_tests` y `rdj_cover_tests` correctas (Wine con locale UTF-8). Cada efecto nuevo gasta ≈ 1-3 % de un núcleo (medido fuera del motor).

## Versión 0.10.6 (22-10-2026): nuevo nombre, DJ Elver Galarga
Petición del usuario: «vamos a cambiar el nombre y el diseño del logo; la aplicación se llamará DJ ELVER GALARGA».
- Textos visibles (título de la ventana, barra superior, Acerca de, barra de estado, avisos, registros, agente de las carátulas), recurso de versión, `DJElverGalarga.exe` (`OUTPUT_NAME`; el objetivo de CMake sigue llamándose RamiroDJ), instalador `installer/DJElverGalarga.iss` (mismo AppId, así actualiza la instalación anterior y borra `RamiroDJ.exe`), `compilar.ps1`.
- Logo (`DrawAppLogo` en `widgets.cpp`): disco con surcos, arco del color del plato A, etiqueta central, aguja con la punta del color del plato B, placa «DJ» y «ELVER GALARGA» en dos tonos; sigue los colores del diseño elegido. Icono nuevo `res/djelvergalarga.ico` (16 a 256 px, dibujado a cada tamaño; «EG» en la etiqueta desde 48 px).
- Se conservan a propósito la carpeta de datos `%APPDATA%\RamiroDJ` y la del motor del separador, para no perder la biblioteca, los ajustes ni el motor instalado. El diseño «Ramiro Noche» pasa a llamarse «Noche clásica».

## Versión 0.10.5 (21-10-2026): rack que se despliega, grabación de movimientos y FX en modo mantener
Petición del usuario: «ahora no me despliega el rack; si ves alguna mejora métesela, en diseño, efectos y en vivo».
- Diagnóstico: la 0.10.4 pedía 270 px de plato para abrir el rack; en ventanas de ~850 px no cabía y se quedaba en una línea.
- Hecho: el rack vuelve a abrirse con los platos al mínimo (186 px); en pantallas bajas con el rack abierto el navegador puede bajar a 118 px y las ondas a 38 px por fila; la flecha del rack, si no cabe, pone las alturas en automático.
- `ui_motion.cpp`: grabación de movimientos de la MACRO y el pad XY (1, 2 o 4 compases, 32 pasos por pulso, a la rejilla del plato que suena, arranque en el compás, la mano manda sobre lo grabado, relevo al otro plato si se para), en la cabecera del rack y con piloto MOV. Es la «grabación de movimientos» que quedaba pendiente de la segunda fase del rack.
- Pestaña FX de los platos: ACTIVAR con toque (encender/apagar) o mantener (solo mientras se pulsa).
- Pruebas: capturas a 760, 820 y 900 px de alto (el rack se abre en las tres; dos filas bajo los platos desde ~900 px); `--autotest` con una comprobación nueva (rampa de la macro grabada 1 compás: al repetirse, a ±0,01 de lo esperado), 103 correctas, 2 de 2 sin fallos.

## Versión 0.10.4 (21-10-2026): controles de debajo de los platos
Petición del usuario: «¿se han quitado los efectos de reverb y demás? Lo que estaba debajo de los decks».
- Diagnóstico: los efectos siguen; con el rack (0.9) los platos quedaron bajos en ventanas de ~870 px y la zona de pads pasaba a una fila: FX sin «Cantidad», LOOP sin ½, ×2, 1/4 ni QUANT.
- Hecho: botones de pad desde 17 px (antes 20) para que quepan dos filas; con el rack abierto y menos de 1000 px de alto, ondas de 46–56 px por fila; el rack cede antes que los platos (mínimo de plato para abrir el rack 270 px; rack automático desde 200 px); alto por defecto de los platos 256–310 px.
- Comprobado con captura a 1432×866 (las dos filas en LOOP y FX con Cantidad); `--autotest` sin fallos.

## Versión 0.10.3 (21-10-2026): diseño de Ramiro MixLab
Petición del usuario: adjunta `RamiroMixLab.exe` («Ramiro MixLab 1.0.0») y pide sacar sus mejoras, sobre todo su paleta de colores.

**Qué es MixLab**: una variante del mismo código (Dear ImGui) basada en una versión anterior de Ramiro DJ (sin rack, sin mandos reconocidos ni instrumentos). Lo que tiene y Ramiro DJ no: tres diseños propios, la carátula dentro del jog y los avisos de tempo y tonalidad visibles en el mezclador. Se ha comparado con capturas de las dos aplicaciones (Wine) y las cadenas de texto de los dos ejecutables; los colores se han leído de la tabla de diseños del propio ejecutable.

**Hecho**
- `widgets.cpp`: diseños 5, 6 y 7: MixLab Carbon, MixLab Aurora y MixLab Studio, con sus valores exactos (fondo, paneles, platos, acentos, ondas, brillo y esquinas). Se añaden al final para no cambiar el diseño guardado de nadie.
- Ajustes: al actualizar se pone una vez MixLab Carbon (también es el de las instalaciones nuevas).
- Platos: carátula en el centro del jog, velada para leer el BPM y el pitch (`coverInJog`, se puede quitar).
- Mezclador: la ayuda de mezcla (diferencia de BPM y relación de tonalidad) se ve desde 190 px de alto (antes 236).

**Pruebas**: capturas de los tres diseños; `rdj_tests`, `--rack` y `--autotest` como en la 0.10.2.

## Versión 0.10.2 (21-10-2026): el SYNC usa las rejillas de VirtualDJ
Petición del usuario (con una captura de VirtualDJ con «The Way» de ASKAIØ y «Doppelwhipper» de Gabriel Ananda a 125 BPM): «en VirtualDJ le das a sincronizar y la clava».

**Diagnóstico**
- Ramiro DJ solo usaba la rejilla de VirtualDJ si se importaba a mano, y al cargar la «comprobaba» con su propia medida y la movía hasta 80 ms. Con música real esa medida puede fallar y estropear una rejilla buena. Sin importar, usaba su propio análisis, probado solo con pistas sintéticas.

**Hecho**
- `vdjimport.*`: `VdjLookup`, que lee los database.xml del equipo una vez (y otra vez si VirtualDJ los guarda) y busca por ruta.
- `loader.*`: al cargar, si VirtualDJ tiene la pista con rejilla y la nuestra no está hecha a mano, se usan su BPM y su primer pulso (origen de rejilla 8). El único ajuste permitido es un desfase conocido de decodificador: 0 o ± el retardo LAME del MP3 (o 529/1105 muestras si no hay cabecera), solo si nuestra medida lo confirma a menos de 4 ms. Los WAV, FLAC y OGG no se tocan. Los cues encajados en la rejilla anterior se mueven con ella. La segunda carga no vuelve a medir.
- La importación (origen 4) usa la misma regla en vez de la corrección libre de hasta 80 ms.
- Opción `useVdjGrid` (activada): menú de SYNC y diálogo de VirtualDJ.

**Pruebas**
- `rdj_tests`: WAV con rejilla de VirtualDJ 22 ms distinta de la nuestra (se respeta); elección del desfase; rejilla automática mala sustituida por la de VirtualDJ; la manual no se toca; opción apagada; segunda carga sin medir; MP3 real codificado con LAME (retardo 1105 muestras): con la rejilla de VirtualDJ con o sin ese retardo, queda a 0,00 ms del golpe. 477 correctas y los 4 fallos de siempre de Wine.
- Sin probar con el database.xml real del usuario ni con sus canciones.

## Versión 0.10.1 (21-10-2026): rueda calibrada y empujes con SYNC
Petición del usuario: «la sincronización de temas falla, no cuadra bien, y la rueda cuando mueves un poco para intentar cuadrar se sube el pitch una barbaridad o baja; no está calibrado nada».

**Diagnóstico**
- El motor copiaba en el plato que sigue el empujón (bend) del que manda: al empujar el MASTER, el pitch del otro saltaba lo mismo, no se ganaba desfase y la escala del fader del otro se ampliaba sola a ±16 o ±50 % y se quedaba así.
- El empuje del jog se sumaba por mensaje y se atenuaba un 15 % por fotograma: dependía de los mensajes por fotograma y de los fotogramas por segundo. Un giro suave (¼ de vuelta por segundo en un DDJ-400) llegaba a ≈ 19 % (y hasta el 30 % de tope).

**Hecho**
- `engine.cpp`: el que sigue copia el pitch del que manda, no su empujón; un empujón en cualquiera de los dos se trata como desfase manual que se respeta al soltar.
- `app.cpp`, `mandos_controles.cpp`: empuje proporcional a la velocidad de giro, suavizado por tiempo (sube en ~40 ms, se suelta en ~120 ms), igual a cualquier frecuencia de imagen; 0,08 por vuelta/segundo (jog de 720 pasos), tope ±12 % (total con botones ±15 %). Los mandos con mapeo de Mixxx usan la misma escala y el ajuste «Sensibilidad del aro».
- Plato en pantalla: aro exterior = empuje (con indicador EMPUJE ±%), centro = scratch.
- La escala del pitch ampliada por el SYNC vuelve a la de antes cuando el pitch ya cabe.

**Pruebas**
- `rdj_tests`: nueva comprobación (empujar el que manda no mueve el pitch del otro y deja ≈ 16 ms de desfase estable con 0,4 s al 4 %). `rdj_sync_bench`: escenario de empujones adelante y atrás en el maestro, 0,00 ms al final.
- `--autotest`: DDJ-400 simulado girando el aro a 180 pasos/s: empuje máximo 3,2 %, vuelve a 0 al soltar y el pitch no cambia. Arrastre real del ratón por el aro del plato en pantalla (xdotool): EMPUJE +2,2 %.
- Sin probar con un mando físico ni con canciones del usuario (se han pedido).

## Versión 0.10.0 (21-10-2026): SYNC preciso, caja de ritmos, bajo, sampler y efectos nuevos
Petición del usuario: «seguimos mejorando el proyecto con la sincronización, que no la veo muy acertada; más plugins o efectos, tipo caja de ritmos, y las cosas que veas para crear el súper software».

**Diagnóstico del SYNC**: con un banco de pruebas nuevo (`tests/sync_bench.cpp`, pistas generadas con BPM y primer tiempo conocidos) el motor mantiene la fase con 0,00 ms de error en los 10 casos (key lock, pitch del maestro, jog, saltos, loops, PLAY, ×2, scratch). El fallo estaba en el análisis: rejillas 5–22 ms tarde, primer tiempo de compás mal en 6 de 7 pistas y el drum & bass a 87 BPM.

**Hecho (implementado)**
- `analysis.cpp`: ajuste fino de la rejilla a la muestra (seguidor de picos y cruce del 30 % de la subida, mediana ponderada de los golpes fuertes, solo si al menos el 40 % coincide en 4 ms), elección del primer tiempo con cuatro pistas (bombo, caja o palmas en 2 y 4, acentos y cambios de compás, cada una con su contraste), detección de tempo doble para drum & bass.
- `loader.cpp`: las rejillas automáticas antiguas (y las importadas de VirtualDJ comprobadas con el método viejo) se vuelven a medir al cargar con el mismo BPM; los hot cues y el cue pegados a la rejilla (a menos de 2 ms) se mueven con ella. Nuevos códigos de origen de rejilla 6 y 7.
- `instruments.*`: caja de ritmos (8 voces sintetizadas, 8 patrones de 16/32 pasos, acento, swing), bajo acid (PolyBLEP, filtro escalera de 4 polos, envolventes, acento, slide, drive) y sampler de 16 pads (disparo, mantener, loop en el pulso; 8 sonidos generados por el programa). Reloj que sigue al plato que manda (o A, B o tempo propio) a la muestra. Sin reservas ni bloqueos en el audio; cambio de muestra con acuse de recibo.
- `rack.*`: fuente «instrumentos» para los módulos, 4 efectos nuevos (eco de cinta, reverb larga, bitcrusher, repetidor), orden `Cmd::RackPad`.
- Interfaz (`ui_instruments.cpp`): pestañas RITMOS, SAMPLER y BAJO; estilos de partida (9 de ritmos, 5 de bajo), TONO del plato, pads con archivo propio (arrastrar o menú), reloj común, estado en `rack.json`.
- MIDI: 20 acciones nuevas (tocar ritmos, tocar bajo, siguiente patrón, pads 1–16) con luces.
- Opción para capturas: `--rack-instr`.

**Corregido durante la verificación**
- Con los instrumentos parados se seguía generando audio (1,75 % de CPU): ahora solo avanza el reloj.
- Los sonidos incluidos del sampler pasaban de 0 dBFS: normalizados a −0,9 dB.

**Estado de las pruebas**
- Probado automáticamente: `rdj_sync_bench` 8 de 9 pistas bien (fase entre −0,2 y −0,4 ms; falla el primer tiempo de compás en la pista de percusión suave sin bombo) y motor con 0,00 ms en los 10 casos; `rdj_tests --rack` 148 correctas; `rdj_tests` 460 correctas y los 4 fallos de siempre de Wine con nombres con tildes (incluida una prueba nueva de re-medición de una rejilla antigua 12 ms tarde y con el compás corrido).
- Probado en la aplicación (Wine, xdotool): pestañas en pantalla, clic en las casillas de la caja de ritmos, guardado y recuperación de patrones y pads.
- Probado en Windows real: **no**. Escuchado con altavoces: **no** (las pruebas miden el audio). Con mandos físicos: **no**.

## Versión 0.9.0 (21-10-2026): rack de hardware virtual (primera fase)
Petición del usuario: un rack de hardware virtual inspirado en las unidades de efectos, samplers y loop stations que se conectan a las mesas (tipo RMX o Kaoss Pad), con funciones reales en el motor de audio. Primera fase: estación de efectos, pad XY, aislador de tres bandas y loop station; la segunda fase (sampler de 16 pads, caja de ritmos, sintetizador de bajos, delay con saturación y reverb de transición, efectos por stems y grabación de movimientos) queda para después. El usuario habló del proyecto como «Ramiro MixLab» y de un código fuente adjunto que no llegó: se ha trabajado sobre Ramiro DJ 0.8.0 y se mantiene el nombre.

**Hecho (implementado)**
- `src/audio/rack.*`: el rack en el motor. `RackParams` (atómicos que escribe la interfaz), `RackStatus` (lo que publica el audio) y la orden `Cmd::RackLoop` para la loop station. Sin reservas, bloqueos ni E/S en el hilo de audio: las líneas de retardo se reservan en `prepare()` y la memoria de la loop station la reserva la interfaz la primera vez que se graba (`allocLoop`, publicada con un atómico).
- Estación: 4 ranuras (`StUnit`) en un orden configurable, 9 efectos (reutiliza el filtro DJ, la reverb, el flanger, el phaser y el gate de los platos, con dos parámetros opcionales nuevos que por defecto dejan los platos igual; eco de cinta, delay ping-pong, saturación y distorsión nuevos), intensidad y mezcla suavizadas, tiempos sincronizados con el BPM en 10 divisiones, macro con asignación por ranura, inserción o envío y retorno (sin duplicar el original), colas que terminan solas (silencio de 0,3 s o 12 s como mucho), fundidos al encender, apagar, cambiar de tipo, de fuente y de modo.
- Pad XY: dos ejes con 13 destinos posibles, peso que entra en 10 ms y vuelve en ≈ 0,4 s, HOLD y «tocar enciende».
- Aislador: cruces LR4 ajustables en rangos seguros (60–500 Hz y 1–8 kHz, con al menos 2,5 veces de separación) que se deslizan sin reiniciar los filtros; cortes rápidos (4 ms) y recuperación suave (25 ms).
- Loop station: 1, 2, 4 u 8 compases con inicio cuantizado a la muestra exacta en que empieza el compás (posición en pulsos del plato al principio y al final del bloque). Duración exacta según el BPM, unión fin→principio fundida con lo que sigue sonando, entrada en fase al compás, seguir BPM, capas, deshacer (copia de lo sobregrabado en la primera vuelta y restauración por trozos), borrar, volumen, auriculares y exportación a WAV de 32 bits.
- Motor (`engine.cpp`): contexto de tempo por bloque (BPM, pulsos al principio y al final, plato que manda en la mezcla), inserción tras la cadena de cada plato, envío tras el fader, mezcla, retornos y loop antes del volumen master y del limitador. Los medidores de canal muestran el nivel con el rack.
- Interfaz (`ui_rack.cpp`): rack plegable bajo el mezclador con dos módulos a la vez (uno en ventanas estrechas), cabecera con pilotos, presets, consumo y EFECTOS OFF, colores del tema, escala de la interfaz, rótulos en español, sin símbolos de texto como iconos.
- Presets: 5 incluidos (Vacío, Transición suave, Subida techno, Voz con eco, Corte de graves) y los del usuario; estado guardado en `rack.json`.
- MIDI: 34 acciones nuevas («Rack: …»), clic derecho → *Aprender* en cada control, luces de los botones asignados, recogida de valor; en los mandos con mapeo de Mixxx, lo asignado al rack se queda en el rack y lo demás sigue yendo al mapeo.
- Opciones para capturas: `--rack "preset" izquierda derecha macro`, `--rack-loop compases`, `--rack-xy`.

**Corregido durante la verificación**
- Limitador del master: tras limitar fuerte, su recuperación en `float` se atascaba a 0,99979 (−0,002 dB para siempre) y su suma móvil acumulaba redondeos. Ahora la envolvente y la suma van en `double`. Lo destapó la prueba que exige que, al apagar un efecto, la señal vuelva a ser la original.
- La loop station entraba con un fundido de 5 ms que ablandaba el primer golpe del compás: ahora entra en 1 ms (para en 5 ms).
- Exportar el loop justo al acabar de grabar se rechazaba (se está fundiendo la unión unos milisegundos): el botón ya espera a que termine.
- Nombre del WAV exportado sin tildes («… - 4 pulsos.wav»): en Wine no se pueden crear archivos con tildes y así se puede comprobar.

**Estado de las pruebas**
- Probado automáticamente (motor): `rdj_tests --rack`, 111 comprobaciones (ver `docs/RACK.md`), todas correctas.
- Probado automáticamente (aplicación entera, `--autotest` con pistas reales): 8 comprobaciones del rack más las de siempre (93 en total), 4 de 4 ejecuciones sin fallos; 0 cierres en 20 arranques seguidos. Compilación de comprobación de Dear ImGui con los cuatro módulos en pantalla, sin avisos. Arrastre real del ratón sobre el pad XY (xdotool), que mueve la macro y enciende las ranuras.
- Rendimiento: con 4 efectos, el aislador, el loop y dos platos, 10 s de audio en 0,32 s; el rack usa ≈ 1,8 % del tiempo de cada bloque.
- Probado en Windows real: **no** (todo en Wine 9 con un dispositivo de audio simulado). Probado con pantalla táctil: **no**. Probado con mandos físicos: **no**.

## Versión 0.8.0 (21-10-2026): mandos reconocidos al enchufarlos, como VirtualDJ
Petición del usuario: «que reconozca todos los dispositivos que hay en el mercado… que lo conecte y conecte todas las luces, como VirtualDJ reconoce las consolas y no falla ningún botón». Elegido: meter dentro del programa los mapeos de Mixxx (GPL 2+), con lo que Ramiro DJ pasa a ser GPL 2 o posterior.

**Hecho**
- `res/mandos`: 144 mapeos de Mixxx (`.midi.xml`) y sus 132 scripts, metidos en el `.exe` como recursos (CMake genera `mandos.rc`). Carpeta de mapeos del usuario en `%APPDATA%\RamiroDJ\mandos`.
- `src/core/mandos.*`: lector de XML (con entidades de DOCTYPE), mapeo (entradas con sus opciones, salidas/luces, scripts, ajustes), catálogo y reconocimiento por el nombre del puerto (palabras, cifras con más peso, marcas y palabras de relleno, umbral; los mapeos genéricos no se eligen solos).
- QuickJS-ng 0.17.0 (MIT) en `third_party/quickjs`: un intérprete por mando con la API de Mixxx (`engine`, `midi`, `console`, `print`, `ColorMapper`), `common-controller-scripts.js` y `midi-components`; `init`/`shutdown`, `incomingData` (SysEx), temporizadores, conexiones, scratch (filtro alfa-beta), `brake`/`spinback`/`softStart`, *soft takeover*, controles de 14 bits. Corte de scripts que no terminan (interrupción con plazo).
- `src/ui/mandos_controles.cpp`: traducción de los controles de Mixxx a Ramiro DJ (platos, mezclador, EQ con corte, filtro, efecto, biblioteca, Automix, grabación) con su comportamiento (potenciómetros, curvas en dB, botones de pulsar/alternar/ventana, codificadores).
- MIDI: SysEx de entrada y salida, puertos de entrada y salida emparejados, mandos reconocidos aparte de las asignaciones propias, enchufar y desenchufar con el programa abierto (revisión cada 2 s), elección de mapeo por puerto guardada en `midi.json`.
- Interfaz: tarjetas de mandos conectados (estado, mapeo elegido, registro), pestaña *Mandos compatibles* con buscador, nombre del mando en la barra de estado.
- Licencia: `COPYING.txt` (GPL 2 o posterior), Acerca de, instalador (pantalla de licencia) y LEEME.

**Pruebas**
- `--mandos-prueba` (los 144 mapeos sin el aparato, todos sus controles): 134 arrancan, procesan todo y se paran sin errores; 117 mandan luces. Los 10 restantes fallan por errores de su propio script (funciones que no existen en ellos, variables sin definir), no de Ramiro DJ.
- `rdj_tests.exe --mandos`: todos los mapeos y scripts se leen; 15 nombres de puerto reales reconocen su mapeo y 5 que no son mandos no se confunden.
- `--autotest`: comprobaciones funcionales con DDJ-200, DDJ-400, DDJ-FLX4, Inpulse 200 y Mixtrack Platinum FX simulados (PLAY y su luz, volumen con 14 bits, crossfader, auriculares, SYNC, EQ, poner y borrar hot cues con SHIFT, jog con scratch). Lo demás (CUE, loops, navegador, efectos…) solo se ha pasado por `--mandos-prueba`, que comprueba que no da errores, no que haga lo correcto.
- Pruebas de siempre: `rdj_tests` 308 correctas (y los 4 fallos de siempre de Wine con nombres de archivo con tildes), carátulas 21 casos, rendimiento correcto; `--autotest` 4 de 4 sin fallos (85 comprobaciones) y 1 más con la compilación de comprobación de Dear ImGui, que tampoco anotó nada al abrir el diálogo del mando con un DDJ-400 simulado; 0 cierres en 20 arranques seguidos; compilación limpia desde el zip del código fuente.
- Sin probar con mandos de verdad (no hay aparatos en el entorno de pruebas).
- Tamaño: el `.exe` pasa de 5,4 a 17,7 MB por los mapeos (11 MB de texto; 4,5 MB en el zip). Al arrancar solo se lee la cabecera de cada mapeo, y el mapeo entero solo el del mando conectado.

**Corregido durante la verificación**
- Behringer BCR2000 y DDM4000 no arrancaban: preguntan cuántas ruedas tiene el ecualizador (`num_parameters`) y Ramiro DJ respondía 0. Ahora responde 3 (y 3 botones de corte) y 1 para el filtro.

## Versión 0.7.0 (20-10-2026): carátulas como VirtualDJ, SYNC con MASTER y rediseño
Peticiones del usuario: «las carátulas no las coge, ¿por qué VirtualDJ las coge todas a la primera?» y «arregla todo y el plan de antes, mejora la sincronización y añade cosas» (el plan: rediseño UX/UI de MEZCLA, BIBLIOTECA y SEPARADOR sin tocar el motor ni el DSP, con un sistema de diseño, pantallas de 1366×768 a 2560×1440 y sin perder funciones).

**Diagnóstico de las carátulas**: VirtualDJ descarga de Internet la carátula de las pistas que no la traen (opción `coverDownload`) y la guarda en `Documentos\VirtualDJ\Cache\Covers` sin escribirla en el archivo; también lee imágenes `pista.mp3.jpg`. Ramiro DJ solo buscaba en Internet a mano o al cargar en un plato.

**Hecho**
- Carátulas: búsqueda automática en segundo plano para toda la colección (`coverSweepTick`, umbral de parecido más alto que la manual, memoria `caratulas\busquedas.json` con 30 días para las no encontradas, pausa de 10 min sin conexión, indicador en la barra de estado), caché de VirtualDJ emparejada por nombre normalizado, `pista.mp3.jpg`. Preferencia de 3 valores (`coverDownload`).
- SYNC: `syncPhase` en el motor (modo solo tempo); en la interfaz, los dos platos en SYNC con MASTER (`syncMaster_`), cambio automático de MASTER al pararse, el MASTER mueve su pitch sin perder el SYNC, realinear, menú de SYNC (clic derecho), aviso ¿REJILLA?, igualar el tono con la tonalidad que suena. Automix: el plato que suena manda.
- Datos nuevos: género (TCON con números ID3v1, GENRE), valoración (POPM, RATING, FMPS_RATING) y nombres de hot cue (importados de VirtualDJ) en la tabla `track_extra` (compatible con versiones anteriores); lectura en segundo plano para las pistas ya existentes; búsqueda por género; listas inteligentes con valoración mínima.
- Diseño: `ui_style.h` (escalas), iconos nuevos (stop, pasos, rebobinar, silencio, estrella, giro, instrumentos), `IconTextButton`, `RatingStars`, `ChannelFader`, `StereoMeter`, `XFader`; botones sin la rayita de color y con estados más claros; tamaño de la interfaz (90–150 %) y `WM_DPICHANGED`; sin símbolos de texto como iconos.
- MEZCLA: mezclador rehecho (rótulos siempre, medidores estéreo con escala, faders con escala, crossfader grande); CUE/PLAY con estado; plato con marcas, hot cues en el aro y aviso de final; pitch con marca de centro; hot cues con nombre (ondas, pads y menú de clic derecho); etiqueta LOOP; pads en una fila cuando no caben dos (IN/OUT/RELOOP siempre a mano).
- BIBLIOTECA: columnas GÉNERO y VALORACIÓN (se ocultan solas si la lista es estrecha), cifras alineadas, reloj de análisis pendiente, filas cargadas teñidas, tres densidades; Detalles rehecho (datos grandes, secciones plegables, plegado a franja).
- SEPARADOR: panel y reproductor rehechos (tarjetas, cola con progreso, iconos por instrumento, faders propios, atajos Todo / Aislar voz / Sin voz / Silenciar todo, más altura).
- Fundido de 150 ms al cambiar de vista; PLAY que respira en pausa; indicadores de trabajo en segundo plano.

**Corregido durante la verificación**
- Cierre inesperado al arrancar (≈1 de cada 4 arranques en Wine, aleatorio): el código que oculta GÉNERO y VALORACIÓN en listas estrechas se había colado también en la tabla de la Sidelist (4 columnas) y escribía en las columnas 7 y 8, que no existen, estropeando la memoria. Se encontró con una compilación con las comprobaciones de Dear ImGui activadas; tras quitarlo, 0 cierres en 80 arranques seguidos (la 0.6.2 tampoco tenía cierres en 40).
- Programador «Reproducir plato A/B» con el plato al final de la pista (p. ej. tras el Automix): sonaba solo el último instante; ahora arranca desde su CUE. Era un fallo intermitente de la prueba automática desde la 0.6.x.
- Registro de fallos: si el programa se cierra de golpe deja `fallo.txt` en la carpeta de datos con la pila (antes, en Wine, se quedaba colgado en el depurador).
- `tools/verificar_portable.py` volvía a compilar en Linux (estaba roto desde la 0.6.1); rutas `build\rdj_…` rotas en `docs/COMPILAR.md`.

**Verificado** (MinGW-w64 GCC 13 + Wine 9)
- Sin cierres: 80 arranques seguidos cargando dos pistas (con y sin reproducción/SYNC), 10 pruebas automáticas completas; compilación de comprobación (IM_ASSERT de ImGui y `_GLIBCXX_ASSERTIONS`) sin avisos en MEZCLA, BIBLIOTECA y SEPARADOR a 1366, 1920 y 2560, con el reproductor del Separador abierto y el menú de SYNC.
- `rdj_tests` completo: 286 correctas; los 4 fallos son los de siempre en Wine (nombres de archivo con acentos), no en Windows (nuevas: SYNC solo tempo y realineado; nombres de cue de VirtualDJ, valoración, género, búsqueda por género), `rdj_cover_tests` 21/21 (nuevos: `pista.mp3.jpg`, caché de VirtualDJ sin falsos positivos, TCON numérico, POPM), `rdj_performance_tests`, `--autotest` con 6 pasos nuevos de SYNC con MASTER (traspaso al parar, tempo conservado, arranque alineado).
- Capturas de MEZCLA, BIBLIOTECA y SEPARADOR a 1366×768, 1600×900, 1920×1080 y 2560×1440.

**Pendiente de comprobar por el usuario**
1. Que las carátulas vayan apareciendo solas con conexión (abajo a la derecha se ve cuántas quedan) y si alguna sale equivocada.
2. Si las de la caché de VirtualDJ no se emparejan, mandar una captura de `Documentos\VirtualDJ\Cache\Covers` con los nombres.
3. El SYNC con los dos platos y el MASTER, con su música.

---
## Versión 0.6.2 (20-10-2026): tonalidad como en VirtualDJ y ayudas de mezcla
Petición del usuario: «¿Ves alguna mejora para implementar? ¿Cómo es que VirtualDJ sale de otra manera la escala Camelot? Mejora intuitivamente y gráficamente todo lo que puedas».

**Diagnóstico de la diferencia con VirtualDJ**
- La rueda es la misma (8A = Am = La menor, 8B = C), pero: (1) VirtualDJ tiene su propio detector y nuestras pruebas ya daban ~40 % de coincidencia exacta; (2) en modo armónico escribe `08A` con cero delante y colorea cada código con el color de su número en la rueda; (3) nuestros colores recorrían la rueda en sentido contrario; (4) VirtualDJ marca con ✓ las compatibles con lo que suena (tenue = a un semitono); (5) no leíamos la tonalidad escrita en las etiquetas (TKEY / INITIALKEY).

**Hecho**
- `analysis.cpp`: `parseKey` (Camelot, `08A`, Open Key, nombres en inglés y español con ♯/♭, bemol/sostenido, maj/min…), `keyOpenKey`, `keyFromCamelot`, `keysClose`; `detectGridForBpm` (fase de una rejilla con tempo dado, reutiliza las curvas de ataque del detector de BPM).
- `tags.cpp`: tonalidad de TKEY/TKE e INITIALKEY/KEY. `Library`: `useTagKeys`, `checkTagKeys` en segundo plano para las pistas analizadas antes (y al activar la opción), origen de la tonalidad en `key_analyzed` (1 detectada sin mirar etiqueta, 2 detectada, 3 etiqueta, 4 VirtualDJ, 5 a mano), rejilla 4/5 de VirtualDJ, máscara `m<n>;` de hot cues importados, M3U en UTF-8 sin BOM.
- `vdjimport.cpp` (nuevo): busca `database.xml` (Documentos, OneDrive, AppData, raíz de cada disco fijo/extraíble, `RDJ_VDJ_DIR` para pruebas), lector XML mínimo (entidades, rutas relativas de discos externos, `Tags` sobre `Scan`, BPM en segundos por pulso), importación de tonalidad, BPM/rejilla, hot cues en huecos libres y listas `Playlists\*.m3u(8)`.
- `loader.cpp`: rejilla de VirtualDJ comprobada al cargar (desfase de decodificador ≤ 80 ms corregido en la rejilla y en los cues importados); la tonalidad de la etiqueta antes que la detección.
- `ui_keys.cpp` (nuevo): rueda Camelot dibujada (anillos B/A, 12 arriba, compatibles, semitono, marcas de los platos, información al pasar, clic = filtro, clic derecho = menú), relación armónica con nombre y consejo, píldoras del mezclador, diálogo de importación (con oferta la primera vez), tonalidad a mano, sincronización de la tonalidad de los platos con la biblioteca.
- Navegador: filtro armónico (con insignia para quitarlo y sin romper el orden manual de las listas), ✓ en la columna de tonalidad, BPM en verde, submenú Tonalidad con la rueda para ponerla a mano, entrada de importación en el menú de Colección y de la lista; Detalles con vista general en capas, origen de tonalidad y rejilla.
- Platos: compás.pulso, frase de 8, «SUBIDÓN EN N», aviso de final (también en los decks plegados), clic en la tonalidad para filtrar. Colores Camelot corregidos. Casillas sin marcar visibles.

**Verificado** (MinGW-w64 GCC 13 en Linux + Wine 9)
- `rdj_tests`: 102 comprobaciones nuevas (`--tonalidad`): todas las notaciones e ida y vuelta de las 24 claves, TKEY, `database.xml` con entidades y rutas relativas, búsqueda con `RDJ_VDJ_DIR`, importación (tonalidad, rejilla, cues sin pisar los propios, rejilla a mano respetada, listas, reimportar sin duplicar, añadir las que faltan, tonalidad a mano respetada), persistencia y corrección de la rejilla al cargar (22 ms de desfase → +0,8 ms). Total en Wine: 281 correctas y los 4 fallos de rutas Unicode de siempre.
- `rdj_cover_tests` 16/16, `rdj_performance_tests` correcto, `--autotest` todo correcto.
- Capturas a 1600×900 y 1920×1050: oferta de VirtualDJ, importación terminada, rueda en Detalles, información al pasar, filtro aplicado, rueda del menú «Ponerla a mano», mezclador y platos con las ayudas nuevas.

**Pendiente de comprobar por el usuario**
1. Importar su `database.xml` real de VirtualDJ y comparar la tonalidad de varias pistas.
2. Que la rejilla importada caiga en los golpes con sus MP3.

---
## Versión 0.6.1 (20-10-2026): carátulas, biblioteca más grande, ondas, SYNC y diseños
Petición del usuario sobre la 0.6.0 (hecha con otra herramienta): «las carátulas no aparecen; la zona de playlist no se puede hacer más grande, me gustaría que solo se vean los decks y desaparezcan las dos ondas de arriba; los diseños no me gustan; hay que mejorar las ondas (no se ven bien los golpes ni la separación para ver los subidones o poner los hot cues y el SYNC); el SYNC cuando lo activas no cuadra bien».

**Diagnóstico**
- Carátulas: con 11 MP3 de prueba con etiquetas reales mal formadas, el lector de la 0.6.0 fallaba en 5: ID3v2.4 con tamaños no synchsafe, marco vacío antes del APIC, dos etiquetas ID3 seguidas, `AlbumArt_{GUID}_Large.jpg` y (en Wine) WebP. Además FLAC con ID3 delante y APEv2 no se leían.
- SYNC: (1) el BPM se redondeaba a entero si estaba a menos de 0,05 → en pistas de 127,96 la rejilla se separaba ~47 ms en 2,5 min; (2) no aceptaba tempo doble/mitad (64 frente a 128: «tempos demasiado distintos»); (3) solo alineaba la fase al activarlo: si el otro plato se empujaba, saltaba o hacía scratch, se perdía; (4) las ondas se dibujaban en tiempo de pista y con pitch distinto los pulsos no coincidían en pantalla; (5) con el plato al principio de la pista el salto de alineación daba posición negativa y no se movía.

**Hecho**
- `tags.cpp`: lector ID3 tolerante (las dos lecturas de tamaño en v2.4, marcos vacíos, varias etiquetas, etiqueta al final con pie 3DI, desincronización, agrupación, DLI, descripciones mal terminadas, búsqueda de la firma JPEG/PNG como último recurso), APEv2, FLAC con ID3 delante, imagen junto a la pista o de la carpeta (cover/folder/front/portada/AlbumArt Large o la única imagen si la carpeta tiene ≤ 40 pistas). `readCoverArt` dice de dónde sale la imagen.
- `covers.cpp`: estado y diagnóstico por pista, invalidación individual, miniaturas de 256 px y `CoverFetcher` (WinHTTP; iTunes Search API y Deezer, sin clave; comparación por palabras de artista y título; ritmo de 1 petición cada 1,3 s; se guarda en `caratulas\`).
- `analysis.cpp`: forma de onda con filtros de 4.º orden (graves < 180 Hz, agudos > 3 kHz), envolvente por banda y RMS; BPM afinado por regresión de fase en ventanas de 12 s; redondeo a entero solo si la rejilla no se desplaza más de 20 ms en la pista.
- `engine.cpp`: SYNC en el hilo de audio (`syncTo`, `syncFactor`, `syncBar`): tempo del otro plato con pitch y empujón, corrección de fase proporcional (±0,6 %, ±2 % si el error es grande), realineado tras saltos o gestos, respeto del desfase manual, `SetHotCueAt`.
- `ui_waves.cpp` (nuevo): ondas por capas, rejilla encima, frases numeradas, cortes y subidones, vista general por energía con caché, medidor de fase, decks plegados con onda y CUE/PLAY/SYNC, menú de clic derecho en la onda.
- `ui_frame.cpp`: sin la fila de botones de la 0.6; botón de ondas y menú Vista en la barra superior; separadores arrastrables (ondas/decks y decks/navegador, plegado automático); franja de color en cada plato; SYNC con medidor de fase y SYNC ×2/×½.
- Diseños nuevos (Ramiro Noche, Estudio, Club, Grafito, Alto contraste) con radios y fondos de onda propios; fuera los trucos por número de tema de la 0.6.
- Biblioteca: la columna `analyzed` guarda el origen de la rejilla (1 antigua, 2 precisa, 3 a mano). Las antiguas se afinan al cargarlas si la detección coincide (±0,12 BPM y ±35 ms).
- Opciones para capturas: `--pos A B`, `--play`, `--sync`, `--no-waves`, `--collapse`.

**Verificado** (MinGW-w64 GCC 13 en Linux + Wine 9)
- `rdj_cover_tests`: 16 casos (7 nuevos: v2.4 no synchsafe, marco vacío, segunda etiqueta, APEv2, FLAC con ID3, WMP AlbumArt, imagen única).
- `rdj_tests`: nuevos «SYNC 0.6.1» (fase tras cambiar el pitch del otro 0,00 ms, tras empujón, salto sin cuantizar y scratch < 2 ms, desfase manual respetado, tempo doble) y «precisión del BPM» (127,96 → 127,962; 126,37 → 126,370; 128 → 128,000). Los 4 fallos restantes son los de rutas Unicode que Wine ya daba con la 0.4/0.5.
- `--autotest` de la app: todo correcto.
- Capturas a 1024×640, 1440×900 y 1920×1080 en MEZCLA, BIBLIOTECA, SEPARADOR, decks plegados, sin ondas y con los diseños nuevos.

**Pendiente de comprobar por el usuario**
1. Que sus carátulas aparezcan; si alguna no, el panel Detalles dice por qué (y se puede buscar en Internet).
2. El SYNC a oído con su música y su mando, y el medidor de fase.
3. Los diseños nuevos en su pantalla: cuál le gusta y qué cambiaría.

---
(Lo siguiente corresponde a versiones anteriores.)

## Versión 0.5.0 (19-10-2026, noche): diseño nuevo y navegador tipo VirtualDJ
Petición del usuario: «quiero un diseño mejor parecido a este [maqueta Ramiro DJ] y también que el sistema de archivos y listas sea más al de Virtual DJ [captura del navegador de VirtualDJ]».

**Hecho**
- **Diseño «Ramiro»** (nuevo por defecto): azul noche, plato A turquesa y B coral, paneles planos, carátulas, iconos vectoriales propios y fuente Barlow Semi Condensed (OFL) en títulos, BPM y botones. Los cinco diseños anteriores siguen en `⚙ → Diseño` (ahora del 1 al 5). Al actualizar, todos pasan una vez al diseño nuevo.
- **Barra superior**: logotipo «/// RAMIRO DJ», pestañas MEZCLA · BIBLIOTECA · SEPARADOR, medidor master con piloto LIM y volumen, auriculares (volumen, mezcla, CUE A/B), Grabar y ⚙ (Audio, MIDI, Preferencias, Programador, Diseño, carpetas, Acerca de). El estado del audio pasa a la barra de abajo.
- **Ondas**: del color de cada plato (o por bandas, a elegir), banderas numeradas de hot cues, cabezal con triángulos; a la izquierda, letra del plato y tonalidad.
- **Platos** como en la maqueta: carátula, título con corazón de favoritos, artista, tiempos, BPM grande, insignias de tonalidad (nombre + Camelot con su color), etiquetas SYNC/KEY/LOOP/FX, plato giratorio con BPM y pitch en el centro, transporte CUE · PLAY · SYNC · ▾ · hot cues 1-4 · ⋯ y pads por pestañas (CUES 1-8, LOOP, SALTO, REJILLA, TONO, EFECTO). Pitch con ◀◀ ▶▶ en el lado exterior. Se compacta solo en ventanas pequeñas.
- **Mezclador** rehecho: GAIN, HI, MID, LOW, FILTER, medidores, faders, preescucha con icono y crossfader con selector de curva.
- **Carátulas**: ID3 APIC/PIC (con desincronización y v2.4), FLAC PICTURE, OGG METADATA_BLOCK_PICTURE (paquete en varias páginas) y cover/folder/front.jpg; decodificación WIC en un hilo y caché de texturas D3D11.
- **Navegador tipo VirtualDJ** (nuevo `ui_browser.cpp`), en lugar de «Biblioteca» + «Mis listas»:
  - Árbol: Colección, Favoritos, Sugerencias (las *Ideas* de VirtualDJ), Historial por días, Grabaciones, carpetas favoritas con estrella, **Este equipo** (carpetas del usuario y unidades, desplegables), listas y pistas separadas. Buscador de carpetas y listas. Soltar pistas sobre una lista las añade.
  - Abrir cualquier carpeta la lee en segundo plano (`Library::browseFolder`) y sus pistas entran en la colección, como en VirtualDJ; opción de leer subcarpetas.
  - Lista central: carátula, título, artista, remix, BPM, tonalidad, duración, reproducciones, fecha y archivo; ordenar, ocultar y mover columnas (se guardan en `ajustes.json`); selección múltiple; arrastrar varias pistas; reordenar listas arrastrando o con ↑ ↓; Supr; Intro; buscar aquí o en toda la colección; pistas cargadas y ya sonadas marcadas.
  - Panel lateral con pestañas abajo: **Sidelist** (lista contigua), **Automix** y **Estadísticas**. Panel de **detalles** con carátula, tonalidad, Camelot, onda, datos y compatibilidad armónica.
  - Paneles ocultables, con ancho ajustable, y modo **BIBLIOTECA** a pantalla completa.
- Ventana de nombre para listas nuevas, renombrar, duplicar y «guardar la Sidelist como lista».
- Preferencias: color de las ondas, filas compactas y leer subcarpetas.
- Opciones para capturas: `--biblioteca`, `--add-folder`, `--browse`, `--sidelist`, `--select` y `RDJ_SHOT_SECONDS`.

**Verificado** (compilado con MinGW-w64 en Linux y ejecutado con Wine; el usuario compila con su MinGW de Windows)
- `rdj_tests`: 12 comprobaciones nuevas (lectura de carpetas con y sin subcarpetas, colección, búsqueda de listas; carátulas ID3, FLAC, OGG en varias páginas y cover.jpg). En Wine: 167 correctas y 4 fallos que también da la 0.4 en Wine (rutas Unicode del entorno); en Windows se esperan 171/171.
- `--autotest` con las pistas de prueba: todo correcto, incluidos 4 pasos nuevos del navegador (carpeta de la pista A, Sidelist, favoritos y sugerencias sin repetir lo que hay en los platos).
- Prueba de interfaz con ratón y teclado simulados (xdotool en Wine): desplegar «Este equipo» hasta una carpeta, Ctrl+clic, arrastrar dos pistas a la Sidelist, doble clic para cargar, menú del botón derecho, reordenar la Sidelist y una lista arrastrando, ↓, soltar sobre Favoritos en el árbol, Supr, buscar aquí y en toda la colección, crear una lista, menú ⚙, reproducir, SYNC, hot cue, pads y favorito.
- Capturas revisadas a 1024×640, 1440×900, 1600×1000 y 1920×1080, en los modos MEZCLA, BIBLIOTECA y SEPARADOR y con los diseños Ramiro, Neón y Clásico.
- Corregido durante las pruebas: arrastrar una fila no seleccionada empezaba un recuadro de selección; la × del buscador no recibía el clic; el texto de los avisos no se ajustaba al ancho (también en el asistente MIDI y el Separador); el corazón tenía un pico en el trazo.

**Entregado**: `RamiroDJ-Setup-0.5.0.exe` (compilado con MinGW-w64 GCC 13 desde Linux; instalador hecho con Inno Setup 6.3.1 y probado en Wine) y `RamiroDJ-codigo-fuente-0.5.0.zip`. Documentación actualizada: LEEME (diseño, navegador, platos), FUNCIONES 194–221, DECISIONES 11 y COMPILAR.

**Pendiente de comprobar por el usuario**
1. Compilar con `compilar.ps1` en Windows (GCC 16) y pasar `rdj_tests` y `--autotest` con audio real.
2. Usar el navegador con el ratón real: arrastrar desde el Explorador a la lista y a la Sidelist, y el tamaño de los paneles en su pantalla.
3. Que las carátulas de su música se vean (MP3 con portada, FLAC y carpetas con folder.jpg).
4. Revisar el diseño nuevo en su pantalla (tamaño de letra, colores y anchos de los paneles).

---
(Lo siguiente corresponde a versiones anteriores.)

## Versión 0.4.0 (19-10-2026, noche): diseños, tonalidad Camelot y Separador
**Hecho y compilado (159/159 pruebas OK)**
- 5 diseños (menú `Diseño ▾` y Preferencias), verificados con capturas.
- Barra superior nueva: MEZCLA / SEPARADOR, Grabar, Programador, Diseño ▾, Ajustes ▾ (Audio, MIDI, Preferencias, carpetas, Acerca de).
- Tonalidad: detector HPCP con perfil Temperley (38 % exacta / 58 % compatible frente a VirtualDJ en 40 pistas; antes 28 %). Se muestra como `8A · La menor` (tabla Camelot del usuario); notación elegible en Preferencias. La biblioteca se reanaliza sola.
- Separador tipo Moises (Demucs, instalado bajo demanda): arrastrar canción, cola, reproductor por stems con M/S/volumen, llevar stems a los platos, exportar acapella/instrumental. Menú contextual "Separar en pistas" en la biblioteca.
- Instalación real de Demucs y separación de prueba en 6 pistas: OK (motor ya instalado en %LOCALAPPDATA%\RamiroDJ\separador).
- Prueba automática ampliada (pasos 20–24): abrir 6 stems, reproducir, «Instrumental → plato B» y voz al plato A. Todo correcto.
- Corregido: con la ventana minimizada o tapada el reloj de la lógica se paraba (Automix, programador, sesión y pruebas quedaban congelados). Ahora usa un reloj real.
- Opciones `--separador` y `--theme N`; herramienta `rdj_stemtool` para probar el separador sin interfaz.

**Entregado**: `dist\RamiroDJ-Setup-0.4.0.exe` y `dist\RamiroDJ-codigo-fuente-0.4.0.zip`. Documentación actualizada (FUNCIONES 181–193, DECISIONES 8–10, COMPILAR).

## Versión 0.3.0 (19-10-2026, tarde): encargo "software profesional" (18 módulos)
**Decisiones del usuario**
- Mantener el motor actual (miniaudio + ImGui) en vez de migrar a JUCE; motivos en `docs/DECISIONES.md`.
- La lista de 180 funciones la redacta Claude: `docs/FUNCIONES.md`, con estado por función (162 ✅ · 10 🟡 · 8 ⏳).

**Hecho**
- C++20.
- **Key lock** (Signalsmith Stretch, MIT) con lector adelantado e historial de posiciones. **Transposición** ±6 semitonos e "Igualar tono".
- **Tonalidad** (cromagrama + perfil EDMA) y Camelot: en platos, biblioteca (columna Tono, ordenable), información y estadísticas.
- **Biblioteca en SQLite**: migración automática desde JSON, copias diarias, reproducciones, última vez y fecha de alta.
- **Automix** con fundido sincronizado y **Sidelist** (la lista "Preparación" de la 0.2 pasa a llamarse Sidelist).
- Pestañas **Info** y **Estadísticas**: histograma de BPM, distribución de tonalidades, más reproducidas.
- **Programador de eventos**: grabar, Automix, fundido final, play.
- **Recuperación de sesión** cada 5 s y al salir, con aviso tras un cierre inesperado.
- **Frecuencia de muestreo** configurable.
- Investigación REA de los módulos nuevos en `docs/INVESTIGACION.md`.

**Verificado**
- `rdj_tests`: 153/153 (key lock: tono a 440 Hz con +8 %, alineación 1,8 ms, sync 0,00 ms con key lock, +12 st = 880 Hz; tonalidad en acordes sintéticos 4/4; Camelot).
- `--autotest` con audio real: **35/35**, incluidos key lock en la app, Automix completo (arranque, entrada sincronizada, transición) y programador.
- Recuperación de sesión en tres arranques (`--dump-state`): pistas, posiciones, pitch, key lock y crossfader.
- Migración a SQLite sobre una **copia** de los datos reales: 1288 pistas y todas las listas.

**Errores encontrados y corregidos en esta tanda**
- La tabla de la biblioteca empezaba a mitad de panel (`SameLine` sobrante; venía de la 0.2).
- La sesión perdía un plato si se cerraba la app mientras la pista recuperada aún cargaba.
- El estado de audio de la barra superior se montaba sobre los botones.

**Limitaciones y pendiente (⏳ en FUNCIONES.md)**
- **Retransmisión Icecast/Shoutcast** y grabación MP3: necesitan un codificador (libvorbis o LAME); no aparecen en la interfaz.
- Tonalidad de **precisión limitada** frente a VirtualDJ (28 % exacta, 60 % compatible); se avisa en el LEEME.
- Automix inteligente (intro/outro), rejilla de tempo variable, loops guardados en hot cues, más efectos, ASIO, programador como servicio.

**Siguiente paso**
1. El usuario instala la 0.3 (actualiza la 0.2; la biblioteca se migra sola) y revisa los puntos 🟡.
2. Retransmisión Icecast con Ogg Vorbis (libogg/libvorbis, BSD).
3. Mejorar la tonalidad (perfiles por género o un modelo con más contexto armónico).

---
(Lo siguiente corresponde a versiones anteriores.)

## Versión 0.2.0 (19-10-2026, tarde): mando, diseño y zona de listas
Petición del usuario: «los ajustes de la consola no están bien, busca info y más ajustes para que lo adapte, dale otro diseño gráfico, otra zona para guardar play list».

**Hecho**
- **Mando (DDJ-WeGO)**
  - No hay lista MIDI oficial del WeGO original. Se combinó lo que el usuario aprendió con su mando (volumen CC 0x13, pitch CC 0x00, crossfader canal 7 CC 0x1F, canal 7 CC 0x17/0x18 como ganancia) con el patrón de la familia DDJ (WeGO3 comunitario: jog 0x21/0x22/0x36, 720 pasos por vuelta). Detalle en `docs/INVESTIGACION.md`.
  - Nuevo preajuste DDJ-WeGO.
  - **Asistente de asignación** paso a paso (48 pasos, con saltos).
  - **Monitor** con "Asignar…" para mensajes sin asignar.
  - Opciones por asignación: invertir y modo relativo.
  - **Ajustes del mando**: sensibilidad del aro, scratch y desplazamiento; scratch al tocar el jog; soft takeover; invertir pitch; luces con prueba.
  - Controles de **14 bits** detectados solos, capa **SHIFT**, acciones nuevas (tocar y girar el plato, SHIFT, reloop, cargar en plato libre) y **luces** por salida MIDI.
  - Al aprender solo se acepta el tipo de mensaje adecuado.
- **Diseño "estudio neón"**
  - Fuentes de la marca incrustadas (Unbounded y Hanken Grotesk, OFL).
  - Paneles y botones con halo azul, rojo y violeta; fondo con luz azul a la izquierda y roja a la derecha.
  - **Plato giratorio** por plato: progreso, cue y aguja a 33⅓ rpm; scratch con el ratón.
- **Zona «Mis listas»** a la derecha de la biblioteca:
  - Preparación, arrastrar para añadir y reordenar, Supr para quitar.
  - Guardar ahora, Guardar como…, renombrar, exportar/importar M3U8, vaciar y eliminar.
  - Historial automático por día.
- **Ventana**: arreglado el guardado del tamaño (se había guardado 14×0).
- `--load A B` y `--midi` para capturas.
- `RDJ_DATA_DIR` aísla los datos en las pruebas: la prueba integrada ya no toca la biblioteca ni el MIDI del usuario.

**Verificado**
- `rdj_tests`: 141/141 (MIDI: 14 bits, SHIFT, filtro al aprender, pitch invertido, preajuste, persistencia de ajustes).
- `--autotest` con audio real: 29/29, incluidos soft takeover, 14 bits, PLAY y scratch del jog por mensajes MIDI inyectados.
- Capturas del nuevo diseño revisadas.
- El instalador 0.2.0 se generó, pero **no se ha ejecutado**: el usuario tiene la 0.1.0 instalada y se actualizará al instalarlo él.

**Pendiente de comprobar por el usuario**
1. Pasar el **asistente** con el DDJ-WeGO conectado y probar el jog: tacto/scratch y sensibilidades.
2. Que las luces respondan (no hay salida MIDI para probarlas aquí).
3. La nueva interfaz con el ratón y en su pantalla.

**Nota**: hay instalado un "Ramiro DJ Pro 0.2.0" en `AppData\Local\Programs\Ramiro DJ Pro` que no procede de este proyecto; no se ha tocado.

---
(Lo siguiente corresponde a la versión 0.1.0.)

Proyecto: `C:\Users\ramir\RamiroDJ-Win`. Es distinto de la versión web anterior, que está en `C:\Users\ramir\RamiroDJ` y no se ha tocado.
Última actualización: 19-10-2026. Versión: **0.1.0**.

## Entregables
- Instalador: `dist\RamiroDJ-Setup-0.1.0.exe` (3 MB, per-user, sin administrador, en español).
- Código fuente: `dist\RamiroDJ-codigo-fuente-0.1.0.zip` (incluye las dependencias de terceros).
- Recompilar: `docs\COMPILAR.md`, o `powershell -ExecutionPolicy Bypass -File compilar.ps1`.
- Guía de usuario: `LEEME.md`.
- Investigación con REA: `docs\INVESTIGACION.md` (comprobado frente a hipótesis).

## Entorno
- REA: MCP `rea` en `~/.claude.json`, con Ghidra 12.1.4, JDK 21 y JADX. Ghidra está disponible; Hopper e IDA no aplican en Windows; rizin funciona con `execute_rizin_command`.
- Compilador: MinGW-w64 GCC 16.1 (WinLibs UCRT), CMake 4.3, Ninja 1.13. Instalador: Inno Setup 6.7.3, instalado con winget en el perfil del usuario.
- VirtualDJ: `C:\Program Files\VirtualDJ\virtualdj.exe`.
  - La copia de trabajo (687 MB) está en `investigacion\copia-trabajo\`. **Se puede borrar** si no se va a seguir investigando.

## Terminado y verificado
Pruebas automáticas (`build\rdj_tests.exe`): **129 comprobaciones, 0 fallos**, sin hardware. Con `--real-device`, además abre los dispositivos reales en silencio.

| Área | Qué cubren |
|---|---|
| Decodificación | MP3/WAV/FLAC/OGG y rutas Unicode. Rechazo de archivos incompatibles: texto, vacío, basura, truncado, inexistente. |
| EQ y filtro | EQ plano ±0,1 dB; cortes de -34 a -63 dB; filtro. |
| Limitador | ≤ -0,3 dBFS con +12 dB en ambos platos. |
| Crossfader | 3 curvas. |
| BPM y fase | Pistas sintéticas: error < 0,02 BPM y fase < 7 ms. Pistas reales frente a VirtualDJ: 40/40 y 77/80 (99 % contando octavas). |
| Mezcla | Dos pistas a frecuencias distintas (44,1 y 48 kHz), pitch. |
| Transporte | CUE tipo CDJ, hot cues, loops, salto de pulsos, fin de pista, sin chasquidos al saltar. |
| Sync | Desfase de 0,00 ms en 9 s. |
| Grabación | WAV float idéntico bit a bit; 16 bits con tramado; sin pérdidas. |
| Cargador | En segundo plano, con cancelación. |
| Preescucha | Canales 3-4, división mono, dispositivo aparte. |
| Efectos | Eco, flanger, reverb: estables, con cola y apagado. |
| MIDI | Aprendizaje, botones, absolutos, relativos y persistencia. |
| Biblioteca | Etiquetas, escaneo, búsqueda sin tildes, listas, M3U8, persistencia. |

Prueba de extremo a extremo con la app real, audio real y música del usuario (`RamiroDJ.exe --autotest`): **23/23 correctas**. El registro está en `investigacion\autotest_final.txt`.
- Carga de 2 MP3 en 2,2 s; mezcla con sync; CPU de audio 1 %.
- Loop y hot cue.
- Archivo incompatible rechazado sin cortar el audio y sin entrar en la biblioteca.
- Grabación de 11,5 s sin pérdidas ni saturación, reproducida después en un plato.
- Cambio en caliente entre "Auriculares (WH201A)" y "Altavoces (Realtek)" conservando la posición.
- Soltar un archivo sobre un plato.

Instalador probado: instalación silenciosa para el usuario, acceso en el menú Inicio, el programa instalado arranca, la desinstalación limpia.

## Funciones incluidas en la 0.1
- Dos platos y carga por arrastrar y soltar (Explorador o biblioteca); confirmación si el plato está sonando.
- PLAY/PAUSA, CUE (CDJ), búsqueda por la vista general, scratch arrastrando la forma de onda, empujes ±4 %.
- Mezclador: ganancia, EQ aislador de 3 bandas con corte, filtro LP/HP, volumen de canal, crossfader con 3 curvas, master, medidores con retención y aviso de recorte, limitador.
- Formas de onda coloreadas por bandas: vista de detalle con rejilla de compases y vista general.
- Biblioteca: carpetas con escaneo en segundo plano, búsqueda, columnas ordenables, listas (crear, renombrar, eliminar, importar/exportar M3U8), análisis de BPM por lotes con prioridad baja.
- Selección de dispositivo, búfer, modo exclusivo WASAPI, recuperación automática si se pierde el dispositivo y modo simulado sin audio.
- Grabación WAV de 16 bits con tramado o de 32 bits en coma flotante.
- BPM y rejilla editable: BPM manual, ×2, ÷2, TAP, desplazamiento 1/10 ms, "primer tiempo aquí", reanálisis.
- Pitch ±8/16/50 %, sync de tempo y fase con seguimiento, 8 hot cues, loops automáticos y manuales, Q (cuantizar).
- Preescucha: canales 3-4, otro dispositivo o división mono.
- Efectos por plato: eco, flanger y reverberación, sincronizados al BPM.
- MIDI: entrada WinMM, aprendizaje y preajuste DDJ-WeGO (sin verificar).

## Pendiente de comprobar por el usuario (requiere oído o hardware)
1. Calidad de sonido a oído: mezclar dos pistas moviendo EQ, filtro, crossfader y pitch; comprobar que no hay chasquidos.
2. Escuchar la grabación de la prueba en `Música\Ramiro DJ - Grabaciones`.
3. Preescucha con auriculares en un montaje real. Los dos dispositivos de este PC son estéreo, así que los canales 3-4 no se han probado con hardware. El modo "otro dispositivo" se puede probar con los WH201A y los altavoces.
4. Mando **DDJ-WeGO**: el preajuste no está verificado. Probar cada control y corregir con "Aprender".
5. Desconectar físicamente los auriculares Bluetooth mientras suena: debe volver al dispositivo predeterminado y avisar.
6. Búfer de 3-5 ms y modo exclusivo en este equipo.
7. Arrastrar desde el Explorador con el ratón: la lógica está probada, el gesto real no.
8. Ventana a varios tamaños o DPI y uso real de la interfaz con el ratón (potenciómetros, faders, pestañas).

## Problemas conocidos y limitaciones
- Sin ASIO, sin key lock, sin AAC/M4A, sin salida MIDI a LED ni jog con detección de toque.
- Rejilla de tempo constante.
- La preescucha en un dispositivo aparte puede tener microcortes (relojes distintos; la compensación simple descarta o rellena).
- El MIDI se procesa a la frecuencia de fotogramas de la interfaz (~16 ms de latencia añadida). Habría que moverlo a un hilo propio para el jog.
- 1 de cada 80 pistas reales tuvo un BPM con relación 4⁄5 respecto a VirtualDJ (128,15 frente a 128,66: casi igual). Las pistas de drum & bass salen a 174 en lugar de 87 (misma rejilla).

## Siguiente paso recomendado
1. Revisar con el usuario los puntos "pendientes de comprobar" y corregir lo que encuentre.
2. MIDI en hilo propio + salida a LED + jog con detección de toque (scratch) para el DDJ-WeGO.
3. Key lock con time-stretch. Candidato: Signalsmith Stretch (MIT).
4. Decodificación AAC/M4A con Media Foundation.
5. Importar BPM y cues desde `database.xml` de VirtualDJ (formato documentado en INVESTIGACION.md).
6. ASIO: el SDK de Steinberg tiene licencia propia; habría que valorarla.
