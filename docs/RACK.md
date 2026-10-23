# Rack de hardware virtual (DJ Elver Galarga 0.11.1)

Primera fase: **estación de efectos, pad XY, aislador de tres bandas y loop station**. Inspirados en cómo se usan los aparatos de efectos y loops que se enchufan a una mesa de mezclas (tipo RMX o Kaoss Pad), con diseño y código propios: no se copia ninguna marca, gráfico ni código de esos aparatos.

## Dónde está
En la vista **MEZCLA**, debajo de los platos y del mezclador. La cabecera (una línea) siempre se ve:
- `▾` pliega o despliega el rack (también en `Vista → Rack de efectos…`). Plegado ocupa una línea y la biblioteca gana sitio. Si la ventana es tan baja que no cabe, se queda plegado solo.
- Pilotos **FX · XY · ISO · LOOP**: qué está actuando (el de LOOP es rojo al grabar, verde al sonar y parpadea en ámbar si espera al compás).
- **Presets** y **Guardar…**.
- **CPU rack**: lo que gasta el rack de cada bloque de audio.
- **EFECTOS ON / OFF**: apaga todo con un fundido (las colas de eco, delay y reverb terminan solas). No para la loop station.

Se ven **dos módulos a la vez**; cada panel tiene sus pestañas (EFECTOS, PAD XY, AISLADOR, LOOP). Si eliges en un panel el módulo que ya muestra el otro, se intercambian. En ventanas estrechas (menos de 900 px a escala 100 %) se ve uno solo. La raya bajo el rack cambia su altura (doble clic: automática).

## Fuentes y ruteo
Cada módulo elige su fuente: **A**, **B** o **MEZCLA** (el sampler llegará en la segunda fase).

```
plato A ─► EQ/filtro/efecto del plato ─► [AISLADOR] ─► [ESTACIÓN inserción] ─► (captura LOOP) ─► fader ─► crossfader ─┐
plato B ─► …                                          (lo mismo si su fuente es B)                                    ├─► suma
reproductor de stems (modo Separador) ────────────────────────────────────────────────────────────────────────────────┘
suma ─► [AISLADOR] ─► [ESTACIÓN inserción] ─► (captura LOOP) ─► + retorno de la ESTACIÓN (envío) ─► + LOOP ─► volumen master ─► limitador
```
- **Inserción**: los efectos se aplican a la señal. En un plato van **antes de su fader** (como un efecto de canal), así que la preescucha también los lleva.
- **Envío y retorno**: el original **no se toca**. La señal de envío se toma **después del fader y del crossfader** del plato (o de la mezcla) y solo vuelve lo que **añaden** el eco, el delay y la reverb (con el nivel RET). Los efectos de color (filtro, phaser, gate…) colocados antes dan forma a lo que entra en ellos. Si no hay ningún efecto con cola, no vuelve nada: no se puede duplicar el original.
- Si el aislador y la estación tienen la misma fuente, va primero el aislador.
- **Sin realimentación**: la loop station con fuente MEZCLA graba antes de los retornos y antes de su propia salida; el envío de la mezcla también se toma antes de los retornos.
- **Activar un módulo no cambia el volumen**: el aislador con todo a 0 dB suma plano; una ranura apagada no toca la señal (bit a bit).

## Estación de efectos
Cuatro ranuras en cadena (se ejecutan de izquierda a derecha; las flechas `‹ ›` de cada tarjeta cambian el orden).
- **Número** de la ranura: la enciende o apaga. Al apagar, eco, delay y reverb dejan sonar su cola; los demás se van en 15 ms.
- **Tipo**: 24 efectos, por familias en el desplegable (Filtro y color, Eco y espacio, Modulación, Ritmo y cortes, Subidas); los de la 0.11 llevan «NUEVO» y todos explican qué hacen al pasar el ratón. Al cambiar de tipo con la ranura sonando, se funde (10 ms) y entra el nuevo. Al elegir uno se ponen valores de partida con sentido (p. ej. el riser, 4 compases; el sidechain, mezcla al 100 %).
- **Ruleta de intensidad** (su nombre cambia según el efecto):
  | Efecto | Intensidad | Tiempo | Notas |
  |---|---|---|---|
  | Filtro | izquierda paso bajo, centro nada, derecha paso alto | — | el filtro de los platos |
  | Eco | repeticiones (realimentación 0,2–0,9) | sí | eco de cinta, cada vuelta más oscura |
  | Delay | repeticiones (0,15–0,85) | sí | rebota entre izquierda y derecha, sin acumular graves |
  | Reverb | tamaño de la sala | no | la reverb de los platos (Freeverb) |
  | Flanger | profundidad y realimentación | sí (mín. 1 pulso) | el de los platos |
  | Phaser | recorrido | sí | el de los platos |
  | Gate | cuánto corta | sí | el de los platos |
  | Saturación | drive (1–6) con compensación de nivel | — | nueva |
  | Distorsión | drive (2–30), recorte asimétrico y paso bajo | — | nueva |
- **MEZCLA**: original ↔ procesado. En eco, delay y reverb el original se mantiene entero hasta la mitad y el efecto entra entero desde la mitad; en los demás es un fundido.
- **Tiempo** (pulsos): 1/16, 1/8, 1/4, 1/3, 1/2, 3/4, 1, 2, 4 y 8, **sincronizado con el BPM** de la fuente (con MEZCLA, el del plato que más suena). Sin rejilla se usa 120 BPM (lo indica arriba). El retardo como mucho dura 4 s.
- **MACRO**: una ruleta que mueve a la vez las ranuras que tú decidas (clic derecho en una tarjeta: cuánto mueve la intensidad y la mezcla, de −100 % a +100 %). Con la macro subida, las ranuras asignadas se encienden solas. Es la forma de hacer **subidas y transiciones** con un solo control.
- Los puntitos ámbar en las ruletas muestran el valor que de verdad suena (con macro o pad XY).

## Pad XY
- Arrastra con el ratón: el eje X y el eje Y mueven dos parámetros a elegir (intensidad o mezcla de cualquier ranura, la macro o una banda del aislador).
- **Tocar enciende**: al tocar, la ranura (o el aislador) de destino se enciende sola, como en un Kaoss Pad.
- Al soltar, los parámetros **vuelven solos y suaves** a su sitio (≈ 0,4 s). El círculo vacío marca adónde vuelven.
- **HOLD**: el pad se queda donde lo sueltas hasta que vuelvas a pulsar HOLD.
- Pantalla táctil: Windows convierte el toque en ratón, así que debería funcionar, pero **no se ha probado** en una pantalla táctil.

## Aislador de tres bandas
- Ruletas grandes de **graves, medios y agudos**: a la izquierda hasta el corte total, centro 0 dB, derecha hasta +6 dB.
- **CORTE** quita la banda entera (en 4 ms) y al soltarlo vuelve suave (25 ms), sin chasquidos ni saltos.
- **Cruces ajustables** dentro de rangos seguros: graves | medios de 60 a 500 Hz, medios | agudos de 1 a 8 kHz (siempre al menos 2,5 veces el primero). Se pueden mover con música: se deslizan sin reiniciar los filtros.
- Filtros Linkwitz-Riley de 24 dB/oct: con las tres bandas a 0 dB la suma es plana.

## Loop station
- **Fuente** (A, B o MEZCLA) y **compases** (1, 2, 4 u 8).
- **GRABAR**: con **CUANTIZAR** espera al siguiente compás de la fuente y graba exactamente esos compases (la duración sale del BPM). Sin rejilla o sin cuantizar empieza ya (sin rejilla, a 120 BPM). Pulsa otra vez para cancelar. Grabar sustituye al loop anterior.
- **SONAR / PARAR**: entra en el siguiente compás (en fase con el plato). Por defecto, al terminar de grabar el loop queda **listo** y no suena solo, para no duplicar el plato; con **AUTO** suena nada más terminar.
- **El loop sigue sonando aunque cambies la canción del plato** o lo pares: es independiente.
- **CAPA** (sobregrabar): lo que suena en la fuente se suma al loop mientras esté encendido. **DESHACER** quita la última capa (una).
- **BORRAR** y **VOL**. **AURIC.**: el loop también va a los auriculares.
- **SEGUIR BPM**: si cambias el tempo del plato, el loop cambia de velocidad con él (también de tono, como un vinilo).
- **WAV**: exporta el loop (32 bits) a la carpeta de grabaciones; aparece en la lista *Grabaciones*.
- La pantalla dice qué hace (esperando, grabando, sonando, sobregrabando) con una barra dividida en compases y pulsos.
- Como mucho 30 s por loop (8 compases a 64 BPM). Si no cabe, avisa.
- La memoria del loop (≈ 25 MB a 48 kHz) se reserva la primera vez que se graba, no al arrancar.

## Instrumentos (0.10)
Tres módulos más (RITMOS, SAMPLER y BAJO) que generan audio en su propio bus, el **bus de instrumentos**. El bus pasa por los módulos del rack que lo tengan como fuente (**INSTR.**: inserción de la estación y del aislador, loop station), se suma a la mezcla antes del volumen master y del limitador, y opcionalmente a los auriculares.

- **Reloj**: los secuenciadores leen la posición en pulsos del plato que siguen (el que manda en la mezcla, A, B o tempo propio) al principio y al final de cada bloque, así que los golpes caen en la muestra exacta del pulso y siguen el pitch, el SYNC, los empujones y los saltos del plato. Si el plato se para, el reloj sigue a su último tempo. El arranque (TOCAR) y los cambios de patrón entran en el compás.
- **Caja de ritmos**: 8 voces sintetizadas (bombo con caída de tono, caja con cuerpo y ruido, palmas con 3 golpes, charles cerrado y abierto con 6 osciladores cuadrados y paso alto (el abierto lo corta el cerrado), tom, rim y platillo). 8 patrones de 16 o 32 pasos, acento, swing del 50 al 75 % (retrasa las semicorcheas pares), nivel, afinación, caída y silencio por voz.
- **Bajo**: oscilador de sierra o cuadrada sin aliasing (PolyBLEP), filtro escalera de 4 polos con resonancia y saturación, envolvente de filtro y de amplitud, acento (más volumen y más apertura acumulativa), slide (glide de unos 60 ms sin volver a atacar) y drive. 8 patrones de 16 pasos con nota de 0 a 24 semitonos sobre la tónica.
- **Sampler**: 16 pads, cada uno con modo (disparo, mantener, loop) y volumen; los loops pueden esperar al siguiente pulso. Las muestras las decodifica la interfaz y el audio solo las lee: al cambiar una, la vieja no se libera hasta que el audio confirma que usa la nueva. 8 sonidos incluidos generados por el programa (`makeFxSample`).
- **Nada se reserva en el hilo de audio**; cuando ningún instrumento suena, solo avanza el reloj (coste casi nulo).

## Efectos nuevos de la estación (0.10)
- **Eco de cinta**: eco con wow y flutter (modulación lenta y rápida del tiempo) y saturación en la realimentación.
- **Reverb larga**: reverb de transición con pre-retardo de 50 ms y paso alto a 250 Hz en la entrada (no embarra los graves).
- **Bitcrusher**: de 12 a 3 bits y reducción de muestreo.
- **Repetidor**: repite el último trozo (división sincronizada) con caída; graba siempre aunque esté apagado, así que al encenderlo repite ya.

## Efectos nuevos (0.11)
| Efecto | Intensidad | Tiempo | Cómo funciona |
|---|---|---|---|
| Riser de ruido | RUIDO: nivel del ruido | compases (1, 2, 4, 8) | Ruido blanco por un paso banda que sube de 300 Hz a 12 kHz en el tiempo elegido y se queda arriba; la música pierde graves según sube. Cada vez que se enciende empieza desde abajo; al apagarlo se corta. |
| Spiral | SUBIDA: +1, +2, +5, +7 o +12 semitonos | sí | Eco sincronizado con un cambio de tono por granos dentro del bucle (cada repetición sube el intervalo). Se compensa la latencia del cambio de tono para que siga a tempo. Deja cola. |
| Sidechain | BOMBEO: hasta −26 dB | 1/4 … 4 pulsos | Baja en 6 ms al empezar cada ciclo de la **rejilla** y se recupera en curva hasta el siguiente. Si la fuente está parada o sin rejilla, sigue un reloj propio al mismo tempo. |
| Shimmer | BRILLO: cuánto vuelve la octava | no | Reverb cuya salida vuelve a entrar una octava más aguda (con un bloque de retraso y saturación suave: la ganancia del bucle se queda muy por debajo de 1). Nivel parecido al de la reverb larga. Deja cola. |
| Vinilo lo-fi | DESGASTE | no | Vaivén de 0,55 Hz (33 rpm) y 7 Hz, imagen más estrecha, paso bajo de 11 a 3,3 kHz, paso alto, algo de saturación, crujidos sueltos (2 a 40 por segundo) y siseo. |
| Teléfono / radio | BANDA: radio (250–5000 Hz) ↔ teléfono (600–2800 Hz) | no | Mono, 24 dB/oct por cada lado, algo de distorsión; una compensación lenta (≈ 0,3 s, hasta +18 dB) devuelve el nivel que se pierde. |
| Chorus | PROFUND. | no | Dos voces por canal a 12-16 ms, moduladas en direcciones opuestas (0,3-0,8 Hz). |
| Trémolo | PROFUND. | sí | Sube y baja el volumen a tempo, en fase con la rejilla. |
| Auto-pan | ANCHO | sí | Lleva el centro de un lado a otro a tempo (potencia constante) y estrecha un poco la imagen al moverse. |
| Freeze | TAMAÑO: 30-500 ms | no | Graba siempre lo que suena; al encenderlo copia el último medio segundo y repite su final con dos cabezas cruzadas (ventana de Hann): un sonido continuo. El tamaño se puede mover en marcha. |
| Slicer | PATRÓN: 8 patrones | 1, 2 o 4 pulsos | Graba siempre lo que suena; al encenderlo copia el último ciclo **completo de la rejilla**, lo parte en 8 y los toca en el orden del patrón (1: en orden, 2: la mitad dos veces, 3: dobles, 4: tartamudeo, 5: parejas al revés, 6: salteado, 7: al revés, 8: redoble). Fundidos de 2 ms en los cortes y fundido cruzado donde el ciclo enlaza consigo mismo. Se puede encender y apagar seguido sin esperar. |

**En la pestaña FX de los platos**: los once están también ahí (debajo de «De siempre»). Usan una ranura como las del rack, con el BPM y la rejilla del plato. **Cantidad** mueve la intensidad (con la mezcla al 100 %), salvo en Spiral, Shimmer y Freeze, donde es cuánto se oye. Los efectos de siempre del plato no cambian.

## Ruleta de color del mezclador (0.11)
La ruleta FILTER de cada canal tiene ahora un **color** para toda la mesa (botón a la izquierda de la curva del crossfader, o clic derecho en la ruleta). Centro = nada en todos.
| Color | Izquierda | Derecha |
|---|---|---|
| FILTER | paso bajo | paso alto (el filtro de siempre, idéntico muestra a muestra) |
| SPACE | reverb sin agudos | reverb sin graves (la cola sigue al volver al centro y se apaga sola) |
| NOISE | ruido oscuro (paso bajo que baja) | ruido fino (paso alto que sube) |
| CRUSH | sonido roto con distorsión | sonido roto quitando graves |
Al cambiar de color con la ruleta girada, el anterior se va en 10 ms y entra el nuevo (sin chasquidos). Se guarda en los ajustes. Los mandos MIDI que movían el filtro mueven esta ruleta.

## Comportamiento definido
| Situación | Qué pasa |
|---|---|
| Cambia el BPM | Los tiempos de eco y delay se deslizan al nuevo (como una cinta); los LFO siguen el BPM. El loop cambia de velocidad si SEGUIR BPM está activo. |
| Pausa del plato | Los efectos en inserción siguen: la cola del eco o la reverb termina de sonar. El loop no se para. |
| Salto a un cue o hot cue | Las colas siguen; la loop station no se ve afectada. Una grabación en curso graba lo que suene. |
| Cambio de pista en el plato | Las colas siguen; el loop sigue sonando. |
| Cambio de fuente o de modo de un módulo | Fundido de salida, cambio y fundido de entrada (10–20 ms). |
| EFECTOS OFF | Las ranuras se apagan (15 ms), el aislador vuelve a plano y el pad XY se suelta; las colas siguen. La loop station sigue. |
| Valores no finitos o desbocados | Se sustituyen por valores seguros; si un bloque saliera mal, se descarta y el módulo se reinicia (contador interno). El limitador del master protege la salida. |
| Cambio de dispositivo de audio | Se conserva el loop si no cambia la frecuencia de muestreo; si cambia, se vacía. |

## Presets
Guardan la cadena (tipos y orden), los parámetros, las fuentes y modos, la macro, los destinos del pad XY, el aislador y las opciones de la loop station (no el audio del loop). Los tuyos se guardan con **Guardar…** y el estado actual se recuerda solo (`%APPDATA%\RamiroDJ\rack.json`).

| Preset | Qué hace |
|---|---|
| Vacío | Todo a cero. |
| Transición suave | Sobre el plato A: al subir la MACRO, el filtro cierra, entra un eco de 1/2 pulso y la reverb. |
| Subida techno | Sobre la mezcla: la MACRO (o el eje Y del pad) abre el filtro paso alto, sube el delay de 1/4, la reverb y un gate de 1/8. |
| Voz con eco | Envío y retorno sobre el plato A: eco de 3/4 sin graves y algo de reverb; el original no cambia. Con stems por separado llegará en la segunda fase. |
| Corte de graves | Aislador en el plato B con los graves cortados (cambio de bajo). |
| Subida con riser (0.11) | Sobre la mezcla: al subir la MACRO entra el riser (sube solo en 4 compases), el filtro abre y crece la reverb larga. Bájala en el drop. |
| Bombeo house (0.11) | Sidechain en cada pulso sobre la mezcla; pad X = cuánto bombea, Y = cuánto se nota. |
| Lo-fi (0.11) | Vinilo encendido; la MACRO o el eje Y meten la radio; el eje X, el desgaste. |
| Salida spiral (0.11) | Sobre el plato A: al subir la MACRO el filtro cierra y entra un spiral de 3/4 que sube de tono. |
| Cortes al pad (0.11) | Al tocar el pad: X elige el patrón del slicer (último compás en 8 trozos) e Y mete el freeze. |

## MIDI
- **Clic derecho** sobre cualquier ruleta, botón o eje del rack → *Aprender*: mueve el control del mando y queda asignado. También están en `⚙ → Mando MIDI… → Asignaciones` (las acciones «Rack: …»).
- Acciones: EFECTOS OFF, macro, ejes X e Y del pad, tocar y HOLD, encender/intensidad/mezcla de cada ranura, retorno, aislador (encender, tres bandas y sus cortes), loop (grabar, sonar/parar, capa, deshacer, borrar, volumen) y preset siguiente/anterior. Las ruletas recogen el valor (soft takeover). Las luces del mando se encienden para los botones asignados.
- **Con mandos reconocidos (mapeos de Mixxx)**: puedes asignar controles al rack; ese control deja de ir al mapeo y lo demás sigue igual. Sus propios botones de efectos siguen controlando el efecto de cada plato, no el rack.
- Ejes del pad con ruletas del mando: mover la ruleta «toca» el pad y lo suelta a los 0,8 s sin moverla (o mantén un botón asignado a «tocar»).
- No se afirma compatibilidad de fábrica con ningún mando para el rack: hay que asignarlo con *Aprender*.

## Rendimiento (medido)
- Con las cuatro ranuras encendidas (reverb, delay, flanger y phaser), el aislador y el loop sonando, con dos platos: **10 s de audio en 0,32 s** (≈ 3 % de un núcleo, en Wine), y el rack usa ≈ **1,8 %** del tiempo de cada bloque.
- Sin nada encendido, el rack no procesa (solo comprueba si tiene trabajo).
- Memoria: ≈ 8 MB de líneas de retardo al arrancar el audio; ≈ 25 MB más la primera vez que se graba un loop (a 48 kHz).

## Pruebas
- `rdj_tests.exe --rack` (111 comprobaciones, también dentro de `rdj_tests.exe`): transparencia con todo apagado, aislador plano y cortes (−42 dB), recuperación suave, cruces en marcha, filtro y mezcla, eco sincronizado (1/2 pulso a 120 y 150 BPM con 2 ms de error), colas que terminan, envío sin duplicar el original, los nueve efectos (estables, cambian el audio y al apagarlos la señal vuelve a ser la original), EFECTOS OFF, cambios de fuente sin chasquidos, pad XY (enciende, vuelve suave, HOLD, macro), loop cuantizado (empieza en el compás con precisión de muestra, dura exactamente lo que debe, entra en fase, sigue al cambiar de pista, capa y deshacer exactos, seguir BPM, límite de 30 s), 2000 cambios aleatorios de parámetros sin valores no finitos, parámetros no finitos, rendimiento y MIDI (incluido un mando integrado).
- `--autotest` (la aplicación entera con pistas de verdad): preset, macro, audio sin valores no finitos, EFECTOS OFF desde un control MIDI, guardar/recuperar el estado, loop de 1 compás cuantizado con su duración exacta, exportación a WAV y loop que sigue al cambiar de pista.
- 0.11, dentro de `rdj_tests.exe --rack` (254 comprobaciones en total): los once efectos nuevos pasan la prueba de todos (estables, cambian el audio y al apagarlos la señal vuelve a ser la original, bit a bit tras la cola); sidechain en la rejilla a 120 y 150 BPM (10 % al empezar el pulso, 99 % al acabarlo, sin chasquidos); riser que sube y se corta; spiral (1.ª repetición a 1335 Hz y 2.ª a 1782 Hz para +5 semitonos); freeze que mantiene el sonido con la pista en silencio; slicer (el patrón en orden reproduce el compás a −155 dB del original y se puede encender y apagar seguido); los once en la pestaña FX del plato (y el cambio a un efecto de siempre sin chasquido); ruleta de color (en el centro no toca nada, girada sí, SPACE deja cola y se apaga, cambio de color sin chasquidos).
- 0.10, dentro de `rdj_tests.exe --rack` (148 comprobaciones en total): los cuatro efectos nuevos; caja de ritmos (golpes en la muestra del pulso del plato, contando la latencia de 1,5 ms del limitador; swing; cambio de patrón al acabar el compás; seguir el pitch); bajo (nota a la frecuencia correcta, slide sin ataque nuevo, acento); sampler (disparo, mantener, loop en el pulso, cambio de muestra con acuse de recibo); los instrumentos como fuente de los módulos (el aislador filtra el charles y no toca el plato).
- Compilación de comprobación de Dear ImGui con los módulos en pantalla: sin avisos.
- **No probado** en Windows de verdad (todo en Wine), con pantalla táctil ni con mandos físicos.
