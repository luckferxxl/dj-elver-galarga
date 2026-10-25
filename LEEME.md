# DJ Elver Galarga 0.13.0 Super

Aplicación de DJ para Windows 10/11 (64 bits): dos platos, mezclador, biblioteca, grabación de la mezcla y MIDI.

Es **software libre** (licencia GPL versión 2 o posterior, ver `COPYING.txt`); el código fuente va con cada versión.

## Novedades de la 0.13.0 Super: ElverKey Studio / Mix Lab integrado a pantalla completa
- **ELVERKEY / MIX LAB integrado dentro del programa (a pantalla completa o en ventana flotante)**:
  - **01 · Descubrir Mezclas**: rueda Camelot interactiva de 24 tonos, buscador instantáneo de canciones, filtros de afinidad, margen de BPM y estilo, y envío o arrastre directo a cualquier Playlist o Plato.
  - **02 · Banco de Transiciones A→B (Mix Lab)**: visualización de los 8 Hot Cues y las curvas de cruce superpuestas con el punto de **intercambio de graves (`EQ LOW SWAP`)**, y botón **▶ Ensayar Transición en Vivo (15 s)** que automatiza el crossfader y los graves sobre los platos reales.
  - **03 · Generador Auto-Set por Estilo y Curva de Energía**: 5 perfiles (*Libre, Warm-Up → Peak Time, Peak Time, Montaña Rusa y After/Cierre*) con gráfica interactiva de energía/BPM y reordenación en vivo (`↑` / `↓`).
  - **04 · Ideas Guardadas y Auditoría de Biblioteca**: cuaderno de mezclas `A→B`, detector de posibles duplicados cara a cara (`Cargar A/B`) y corrector de mitad/doble BPM (`×2 / ÷2`).
  - **Selector de 12 temas de color en vivo + personalizador RGB** (`Color A`, `Color B`, `Fondo`).
- **Semáforo armónico en vivo en la Biblioteca** (verde = mezcla perfecta, azul = subida de energía, ámbar = ajuste BPM/puente) y botón **💡 Siguiente** en la cabecera de cada plato con las 8 mejores canciones compatibles a 1 clic.
- **Banderas visuales `MIX IN ▶` y `◀ MIX OUT`** en las ondas grandes y en cada plato con **cuenta atrás en vivo en compases y segundos** antes del punto de salida.
- **Columna `ESTILO` siempre visible** en la biblioteca (con inferencia automática multicapa y lectura de `genre=` y `cues=` en `#EXTELVER:`).
- **Fader de pitch en sentido natural** (`+` arriba, `−` abajo, configurable con clic derecho) con protección anti-salto (*Soft-Takeover*) y barra interactiva de aviso cuando queda un pitch anterior activo al cargar una pista.

## Novedades de la 0.11.3: botón de efectos en cada plato
- En la cabecera de cada plato, a la derecha de los tiempos, hay un botón de efectos que está **siempre a la vista**, también en las pestañas CUES, LOOP, SALTO, etc.:
  - en gris **FX**: ese plato no tiene nada puesto;
  - en ámbar **con el nombre** del efecto («Sidechain ×», «SPACE ×», «REV ×», «Rack: Freeze ×») o con cuántos hay («3 FX ×»).
- Pasa el ratón para ver la lista. **Clic**: quita los efectos **solo de ese plato**: su efecto (pestaña FX), REV, su ruleta FILTER/COLOR y el rack o el aislador si suenan sobre ese plato. El otro plato no se toca. **Clic derecho**: elegir cuál quitar.
- Las pestañas **FX** y **VINILO** llevan un punto ámbar mientras tienen algo puesto (efecto del plato encendido, REV encendido).
- Mando MIDI: nueva acción por plato «Quitar los efectos del plato», con luz mientras quede alguno puesto.
- El botón QUITAR FX de arriba sigue quitándolo todo, y ahora cuenta también el REV.

## Novedades de la 0.11.2
- Corregido: con Windows al 200 % (pantallas 4K o portátiles con escala alta), el texto bajo la MACRO del rack y el nombre de los mandos reconocidos salían enormes.

## Novedades de la 0.11.1: botón QUITAR FX
- Arriba, a la izquierda de **Grabar**, el botón **QUITAR FX** se enciende en ámbar y dice cuántos efectos o filtros se han quedado puestos. Pasa el ratón por encima para ver **cuáles** son (ruletas FILTER/COLOR de los canales, efecto de cada plato, ranuras del rack, MACRO, pad XY en HOLD, aislador y movimiento grabado).
- **Clic**: los quita todos de golpe (las colas de eco y reverb terminan solas, sin cortes). **Clic derecho**: lista para quitar solo el que quieras. Sin nada puesto, el botón dice «SIN FX» en gris.
- También desde el mando: en `⚙ → Mando MIDI… → Asignaciones`, la acción «Quitar todos los efectos y filtros» (su luz se enciende mientras quede alguno puesto).
- No toca los EQ, el volumen, los loops ni la elección de color de las ruletas: solo devuelve todo a neutro.

## Novedades de la 0.11.0: once efectos nuevos y ruleta de color
- **11 efectos nuevos**, en la estación del rack (pestaña EFECTOS) y en la pestaña **FX de cada plato**:
  - **Riser de ruido**: ruido blanco que sube con un filtro durante 1, 2, 4 u 8 compases y se queda arriba; al apagarlo se corta (suéltalo en el drop).
  - **Spiral**: eco sincronizado cuyas repeticiones suben de tono (+1, +2, +5, +7 o +12 semitonos cada vez). Para salir de un tema.
  - **Sidechain**: bombeo a tempo; baja el volumen en cada pulso y lo recupera antes del siguiente.
  - **Shimmer**: reverb con una octava por encima que crece sola. Para los breaks.
  - **Vinilo lo-fi**: crujido, vaivén de velocidad, imagen más estrecha y agudos recortados.
  - **Teléfono / radio**: solo los medios, de radio AM a teléfono, con el nivel compensado.
  - **Chorus**, **Trémolo** y **Auto-pan** (estos dos, a tempo).
  - **Freeze**: congela el último trozo (30-500 ms) y lo deja sonando mientras está encendido.
  - **Slicer**: corta el último compás en 8 trozos y los toca en otro orden (8 patrones), en la rejilla.
- El desplegable de efectos del rack va ahora **por familias** (Filtro y color, Eco y espacio, Modulación, Ritmo y cortes, Subidas). Los nuevos llevan la etiqueta NUEVO y cada efecto explica qué hace al pasar el ratón.
- En la pestaña FX del plato, **Cantidad** mueve el control principal del efecto nuevo y la barra dice qué es («BOMBEO 60 %», «Patrón 3 de 8»…). El tiempo (1/4 … 8) es el de siempre; en el riser son compases.
- **Ruleta de color** en el mezclador (como los «Sound Color FX» de las mesas Pioneer): la ruleta FILTER de cada canal puede ser **FILTER** (el filtro de siempre), **SPACE** (reverb), **NOISE** (ruido) o **CRUSH** (sonido roto). Se elige con el botón que hay a la izquierda de la curva del crossfader o con clic derecho en la ruleta, y vale para los dos canales. En el centro no hace nada; SPACE deja sonar su cola al volver al centro. Los mandos MIDI que movían el filtro mueven ahora esta ruleta.
- **5 presets nuevos** del rack: Subida con riser, Bombeo house, Lo-fi, Salida spiral y Cortes al pad.

## Novedades de la 0.10.6
- **Nuevo nombre: DJ Elver Galarga** (antes Ramiro DJ), con logo nuevo: un disco con su surco verde lima, la aguja y «DJ ELVER GALARGA» en dos tonos. También es nuevo el icono de Windows. El programa se llama ahora `DJElverGalarga.exe`.
- Tu biblioteca, tus ajustes, el rack y las asignaciones MIDI siguen donde estaban (`%APPDATA%\RamiroDJ`), así que no se pierde nada. Las grabaciones nuevas van a `Música\DJ Elver Galarga - Grabaciones`; las de antes siguen en `Música\Ramiro DJ - Grabaciones`.

## Novedades de la 0.10.5
- Corregido: en la 0.10.4 el rack no se desplegaba en pantallas de unos 850 px de alto. Ahora se abre (el navegador y las ondas encogen un poco) y, si aun así no cabe, la flecha del rack ajusta las alturas sola.
- **Grabación de movimientos** (cabecera del rack, «MOV.»): elige 1, 2 o 4 compases, pulsa **GRABAR** y mueve la **MACRO** o el **pad XY**. Empieza en el siguiente compás y después se repite en bucle, a tempo con el plato que suena. **PARAR** lo detiene (la macro vuelve a como estaba) y **BORRAR** lo quita. Si tocas la macro mientras suena, mandas tú. El piloto **MOV** de la cabecera se enciende mientras graba o suena.
- **Efecto del plato en modo mantener** (pestaña FX): un toque en ACTIVAR lo enciende y lo deja; si lo **mantienes pulsado**, suena solo mientras lo pulsas (para golpes de eco, reverb o filtro en directo).

## Novedades de la 0.10.4
- Corregido: con el rack abierto, en pantallas de unos 900 px de alto los platos quedaban bajos y la zona de debajo (CUES, LOOP, FX, ROLL…) se quedaba en una fila: en **FX** desaparecía el control de **Cantidad** y en **LOOP** la segunda fila (1/4, ½, ×2, QUANT…). Ahora los platos tienen prioridad: las ondas de arriba son algo más finas y el rack unos píxeles más bajo. Si aun así ves una sola fila, arrastra hacia abajo la rayita que hay entre los platos y el rack.

## Novedades de la 0.10.3
Lo mejor del diseño de **Ramiro MixLab 1.0**:
- Sus tres paletas de colores, con los mismos colores exactos: **MixLab Carbon** (grafito, verde lima y azul hielo), **MixLab Aurora** (cian y violeta sobre negro azulado) y **MixLab Studio** (grafito cálido, ámbar y azul). Al actualizar se pone una vez MixLab Carbon; puedes cambiarla en ⚙ → *Tema* o en el menú de vista (icono de la barra de arriba).
- **Carátula en el centro de los platos** (se puede quitar en los mismos sitios).
- En el mezclador se ven casi siempre la **diferencia de BPM** entre los platos y si sus **tonalidades casan**.

## Novedades de la 0.10.2
**El SYNC usa las rejillas de VirtualDJ.** Si tienes VirtualDJ en el equipo y la canción ya está analizada allí, al cargarla DJ Elver Galarga usa su BPM y su rejilla, sin importar nada. Así el SYNC cuadra igual que en VirtualDJ.
- Solo se corrige el desfase conocido de los MP3 entre programas (unos 25 ms), y solo si la medida lo confirma claramente.
- Las rejillas que hayas ajustado a mano en DJ Elver Galarga no se tocan.
- Se puede apagar: clic derecho en SYNC → *Usar las rejillas de VirtualDJ*.
- Corregido: la rejilla importada de VirtualDJ se «corregía» al cargar con una medida propia de hasta 80 ms, que con música real podía estropear una rejilla buena. Las canciones afectadas se arreglan solas al volver a cargarlas, si siguen en VirtualDJ.

## Novedades de la 0.10.1
**Rueda (jog) calibrada para cuadrar a mano:**
- El empuje ahora depende de lo rápido que giras, como en un plato de verdad. Un toque suave da un 1–3 % y un giro rápido no pasa del 12 %; antes, un giro suave podía pasar del 19 %, y además cambiaba según los fotogramas por segundo.
- Corregido: al empujar el plato que manda (MASTER), el otro copiaba el empujón en su pitch. El pitch saltaba muchísimo y la escala del fader se ampliaba sola a ±16 o ±50 % (el fader se volvía muy sensible). Ahora empujar cualquiera de los dos deja el desfase que elijas, y la escala vuelve sola a la de antes.
- Plato en pantalla: el **aro exterior** empuja el tempo para cuadrar (pone EMPUJE y el %), y el **centro** hace scratch.
- Si el fader de pitch te sigue pareciendo muy sensible, mira el botón de su escala (±8 / ±16 / ±50) encima del fader: con **±8** es más fino. Con Mayús se mueve aún más fino.

## Novedades de la 0.10.0
**SYNC más preciso.** El motor de sincronización ya iba bien; el fallo estaba en las rejillas que calculaba el análisis:
- Los pulsos se colocaban entre 5 y 22 ms tarde (se notaba un «flam» en el bombo). Ahora caen a menos de medio milisegundo.
- El primer tiempo del compás salía mal en casi todas las pistas. Ahora se elige mirando el bombo, la caja o las palmas en los tiempos 2 y 4 y los cambios de cada compás.
- El drum & bass ya no se detecta a la mitad (87 en vez de 174).
- Las pistas ya analizadas con versiones anteriores se vuelven a medir solas al cargarlas (con el mismo BPM). Los hot cues pegados a la rejilla se mueven con ella; los que pusiste a mano no se tocan.

**Instrumentos en el rack** (tres pestañas nuevas, todo sintetizado por el programa, sin muestras de terceros):
- **RITMOS**: caja de ritmos de 8 sonidos, 16 o 32 pasos, 8 patrones, swing y acentos. Va con el pulso del plato que suena. Estilos de partida: House, Techno, Tech house, Breakbeat, Hip-hop, Trap, Drum&bass, Electro y Reguetón.
- **BAJO**: sintetizador de bajos tipo acid (filtro con resonancia, envolvente, acento, slide, drive) con secuenciador de 16 pasos y patrones de partida. **TONO** pone la tónica de la canción que suena.
- **SAMPLER**: 16 pads. Trae 8 sonidos (bocina, sirena, láser, impacto, subida, bajada, barrido y sub 808) y puedes cargar tus archivos (clic derecho o arrastrándolos). Modos disparo, mantener y loop.
- Los módulos de efectos, el aislador y la loop station pueden usar los instrumentos como fuente (**INSTR.**).

**Cuatro efectos nuevos** en la estación: eco de cinta, reverb larga, bitcrusher y repetidor (beat repeat).

## Novedades de la 0.9.0
**Rack de hardware virtual** debajo del mezclador, con cuatro módulos que procesan el audio de verdad:
- **Estación de efectos**: 4 ranuras en cadena que se pueden reordenar, con filtro, eco, delay, reverb, flanger, phaser, gate, saturación y distorsión. Cada una tiene intensidad, mezcla y tiempo sincronizado con el BPM. Una **MACRO** mueve varias a la vez para subidas y transiciones. Funciona en inserción o en envío y retorno.
- **Pad XY**: dos parámetros a la vez con el ratón; al soltar vuelven suaves; HOLD para dejarlos.
- **Aislador de tres bandas**: ruletas grandes, corte total por banda y frecuencias de cruce ajustables.
- **Loop station**: graba 1, 2, 4 u 8 compases cuadrados con el compás. Sigue sonando aunque cambies de canción, y tiene capas, deshacer y exportación a WAV.
- Cada módulo elige su **fuente** (plato A, plato B o la mezcla). Hay un botón **EFECTOS OFF**, **presets** (Transición suave, Subida techno, Voz con eco, Corte de graves y los tuyos) y **MIDI** con clic derecho → *Aprender*.
- Corregido: tras limitar fuerte, el limitador del master se quedaba 0,002 dB por debajo para siempre (inaudible).
- Guía completa del rack: `RACK.md` (junto a este archivo).

## Novedades de la 0.8.0
**Los mandos se reconocen solos, como en VirtualDJ.** Enchufas el mando por USB y en unos segundos funciona, con **todos sus botones y sus luces**, sin asignar nada.
- Lleva dentro **144 mapeos** de mandos de Pioneer, Numark, Hercules, Denon, Reloop, Behringer, Vestax, Stanton, Novation, Roland, Allen & Heath, Traktor y más (por ejemplo DDJ-200, DDJ-400, DDJ-FLX4, DDJ-SB3, Mixtrack Platinum FX, Inpulse 200/300/500, MC7000). Son los de **Mixxx**, un programa de DJ libre que los mantiene con sus usuarios.
- Las **luces** se encienden igual que en el programa original: PLAY, CUE, SYNC, hot cues con su color, loops, medidores de nivel…
- Se puede **enchufar y desenchufar** con el programa abierto.
- En `⚙ → Mando MIDI…` ves cada mando conectado, con qué mapeo funciona y su registro. La pestaña **Mandos compatibles** tiene la lista completa, con buscador.
- El DDJ-WeGO no está entre ellos: sigue con su preajuste y el asistente, como antes.
- Por llevar los mapeos de Mixxx, DJ Elver Galarga pasa a ser **software libre** (GPL).

## Novedades de la 0.7.0
**Carátulas como en VirtualDJ.** VirtualDJ no saca las carátulas de tus MP3: cuando una canción no trae imagen, la **descarga de Internet** él solo (opción *coverDownload*) y la guarda en `Documentos\VirtualDJ\Cache\Covers`, sin meterla en el archivo. Por eso allí salían todas y aquí no. Ahora DJ Elver Galarga hace lo mismo:
- **Busca en segundo plano las carátulas de toda la colección** que no tengan imagen (iTunes y Deezer, por artista y título; pide más parecido que la búsqueda manual para no equivocarse). Se guardan aparte: el audio no se toca. No repite las que ya tenían ni, durante 30 días, las que no encontró; si no hay conexión, espera 10 minutos. Se elige en *Preferencias → Buscar carátulas en Internet* (toda la colección / lo que cargo en los platos / solo cuando lo pido). Abajo a la derecha se ve cuántas quedan.
- Usa las que **VirtualDJ ya descargó** (si su nombre coincide con el de la pista) y las imágenes `cancion.mp3.jpg` que dejan VirtualDJ y otros reproductores junto a la pista.

**SYNC mejorado**
- **Los dos platos en SYNC con MASTER**, como en rekordbox o Serato: el que marca el tempo lleva la etiqueta **MASTER**. Si lo paras (o termina), el MASTER pasa al que sigue sonando **sin cambiar el tempo**, y al volver a darle PLAY sale alineado con el nuevo.
- El MASTER puede **mover su pitch sin perder el SYNC**: el otro le sigue.
- **Clic derecho en SYNC** (o menú ▾ → SYNC): elegir MASTER, **realinear la fase ya**, modo **«solo tempo»** (iguala el BPM y la fase la pones tú), MASTER automático y alinear compases completos.
- Aviso **¿REJILLA?** cuando un plato sincronizado no consigue cuadrar la fase (casi siempre es una rejilla mal puesta).
- **Igualar el tono** con lo que suena de verdad en el otro plato (con su transposición), desde TONO o el menú ▾.

**Rediseño (vistas MEZCLA, BIBLIOTECA y SEPARADOR)**
- **Mezclador nuevo**: GAIN, HI, MID, LOW y FILTER **siempre rotulados**; medidores **estéreo** por canal con escala en dB, pico y piloto de saturación; faders con escala (0, 6, 12, 20, 30 dB) y tirador grande; **crossfader** más grande con marca central y relleno hacia el lado que gana; cada canal con su color.
- **Platos**: CUE se enciende cuando estás parado en el punto cue; PLAY «respira» cuando el plato está en pausa; el plato giratorio tiene marcas de referencia, los hot cues en el aro y se pone rojo al final de la pista; el pitch tiene marca de centro.
- **Ondas**: los hot cues con **nombre** («2 Drop»), etiqueta del loop activo («LOOP 4»).
- **Hot cues con nombre**: clic derecho en un hot cue → nombre (Intro, Voz, Break, Subida, Drop, Outro o el que escribas) o borrarlo. Los nombres de VirtualDJ también se importan.
- **Biblioteca**: columnas nuevas **GÉNERO** (de la etiqueta) y **VALORACIÓN** (estrellas: clic para valorar; solo se guarda en DJ Elver Galarga); cifras alineadas; reloj en las pistas pendientes de analizar; las pistas cargadas se tiñen del color de su plato; **tres densidades de fila** (cómodas, compactas y muy compactas para miles de pistas). En pantallas estrechas, GÉNERO y VALORACIÓN se ocultan solas para que se lea el título.
- **Detalles**: carátula y título destacados, valoración, tres datos grandes (BPM, tonalidad y duración), secciones plegables (rueda armónica y datos) y el panel se **pliega a una franja** con un clic.
- **Separador**: iconos propios (sin símbolos que dependan de fuentes), carriles con el icono y el color de cada instrumento, faders propios, onda con lo ya sonado más apagado, **atajos Todo / Aislar voz / Sin voz / Silenciar todo**, canciones separadas en tarjetas con los colores de sus pistas, cola con barra de progreso y más altura para los carriles.
- **Tamaño de la interfaz** (90 a 150 %) en *Preferencias → Diseño* y en *Vista*; se adapta solo al pasar la ventana a otro monitor con otra escala.
- En 1366×768 ya no se pierde la fila del loop (IN, OUT, RELOOP): con poco alto se juntan en una fila.
- Botones más sobrios (sin la rayita de color), casillas visibles, fundido corto al cambiar de vista e indicador de trabajo en segundo plano (análisis, etiquetas, carátulas) en la barra de abajo.
- **Listas inteligentes**: nueva condición de valoración mínima; el texto también busca en el género.

## Novedades de la 0.6.2
- **La misma tonalidad que en VirtualDJ**. VirtualDJ usa su propio detector y por eso a menudo no coincidía con la de DJ Elver Galarga. Ahora puedes **importar su análisis** (`⚙ → Importar de VirtualDJ…`, o botón derecho sobre *Colección*): tonalidad, BPM y rejilla, hot cues y sus listas de `VirtualDJ\Playlists` (como «VDJ · nombre»). Solo se lee su `database.xml`; VirtualDJ no se toca. La primera vez que abras la 0.6.2, si hay VirtualDJ en el equipo, te lo ofrece. Nunca pisa tus hot cues ni lo que hayas ajustado a mano.
- **Tonalidad de las etiquetas**: si el archivo trae la tonalidad escrita (Mixed In Key, Beatport, rekordbox… en `TKEY` / `INITIALKEY`), se usa esa en vez de detectarla. Se entienden todas las notaciones: `8A`, `08A`, `Am`, `A minor`, `La menor`, `F♯m`, Open Key (`1m`)…
- **Notación como VirtualDJ**: en `⚙ → Preferencias → Tonalidad`, «Como VirtualDJ en modo armónico» enseña `08A` en una insignia del color de su sitio en la rueda; también hay Open Key de Traktor (`1m`). Los colores son ahora los de la rueda Camelot de verdad (antes giraban al revés).
- **Rueda armónica** en el panel Detalles: fuera las mayores (B), dentro las menores (A); la tonalidad de la pista resaltada, las compatibles encendidas y las que encajan moviendo un semitono, tenues. **A** y **B** marcan lo que suena en cada plato. Al pasar el ratón explica la relación (+1 sube la energía, relativa, diagonal, +2 subidón, choca…). **Clic**: la lista enseña solo las que encajan con esa tonalidad (insignia «Encajan con 8A ×» junto al título para quitarlo). **Clic derecho**: poner esa tonalidad a mano.
- **Marcas ✓ en la lista** (como VirtualDJ): verde = la tonalidad encaja con lo que suena; tenue = encaja moviendo un semitono. El **BPM en verde** entra con menos de un 2 % de pitch (también al doble o la mitad).
- **Mezclador**: dos píldoras arriba con la diferencia de tempo entre platos (`BPM +3,2 %`) y la relación de tonalidad (`9A ✓ 10A`).
- **Platos**: contador de **compás.pulso** con los 4 pulsos y la **frase de 8 compases**; aviso **«SUBIDÓN EN 8»** antes de que vuelva el bombo; los últimos **30 s** de la pista parpadean en rojo (se cambia en Preferencias → Ayudas de mezcla). Clic en la tonalidad del plato: la lista enseña lo que encaja con ella.
- **Detalles**: la vista general en capas (con subidones y hot cues; clic = ir ahí), de dónde sale la tonalidad (VirtualDJ, etiqueta, detectada, a mano) y la rejilla.
- Botón derecho en una pista → **Tonalidad**: ver las compatibles o **ponerla a mano** eligiéndola en la rueda.
- Las casillas sin marcar ya se ven en todas las ventanas.

## Novedades de la 0.6.1
- **Carátulas**: ahora se leen también las etiquetas "casi correctas" que antes se quedaban sin imagen (ID3v2.4 con tamaños mal escritos, marcos vacíos, dos etiquetas seguidas, FLAC con ID3 delante, APEv2) y las imágenes de la carpeta con cualquier nombre habitual (`cover`, `folder`, `front`, `portada`, `AlbumArt_{…}_Large.jpg` de Windows Media Player o la única imagen de una carpeta de álbum). El panel **Detalles** dice de dónde sale la carátula o por qué no hay, y tiene **Buscar en Internet** (iTunes y Deezer, por artista y título; se guarda aparte, el MP3 no se toca).
- **Más sitio para la biblioteca**: el botón de **ondas** de la barra superior quita o pone las dos ondas grandes; la línea entre los decks y el navegador se **arrastra** (si la subes del todo, los decks se pliegan en una línea con su onda, CUE, PLAY y SYNC); la línea bajo las ondas cambia su alto. Botón **Vista** (al lado): todo lo de pantalla y el diseño. La fila de botones de la 0.6 (Plegar decks, Organizar biblioteca, Configurar grabación, Apariencia) pasa a **Vista** y al menú ⚙.
- **Ondas nuevas**: graves, medios y agudos en capas (como en el software de cabina), así cada golpe de bombo se ve separado; **rejilla** encima (compases a toda altura, marcas de pulso, cada 8 compases una línea más gruesa con su número); **cortes** sin bombo sombreados y **SUBIDÓN** donde vuelve el bombo; la vista general del plato es más alta y enseña la estructura, los hot cues numerados y la zona que se ve arriba. Tres colores a elegir en **Vista**. **Clic derecho** en la onda grande: poner un hot cue, el cue o el primer tiempo de compás justo ahí.
- **SYNC rehecho**: lo lleva el motor de audio cada 5 ms. Sigue el tempo y la **fase** del otro plato todo el rato (también cuando lo empujas o le mueves el pitch), acepta pistas analizadas a **tempo doble o mitad** (64/128, 87/174), se realinea tras un salto o un scratch y, si empujas tú el plato sincronizado, respeta el desfase que elijas. La barrita dentro del botón SYNC (y junto a las ondas) es el **medidor de fase**: centrada y verde = golpes a la vez. Opción de alinear **compases completos** en el menú ▾ del plato.
- **BPM más preciso**: el análisis afina hasta milésimas (antes redondeaba a entero cualquier tempo a menos de 0,05 BPM, y en pistas de vinilo o aceleradas la rejilla se iba separando). Las pistas ya analizadas se afinan solas al cargarlas, sin tocar las rejillas que hayas corregido a mano.
- **Diseños nuevos**: Ramiro Noche (azul noche, turquesa y coral), Estudio (grafito, azul y naranja), Club (negro, cian y magenta, ondas de color), Grafito (gris claro) y Alto contraste. Al actualizar se pone Ramiro Noche; se cambia en **Vista**.

## Primeros pasos

1. **Audio**: abre la rueda `⚙` (arriba a la derecha) `→ Audio…` y elige la salida. Por defecto se usa el dispositivo predeterminado de Windows con WASAPI y un búfer de 10 ms.
2. **Música**: en el navegador (abajo), despliega **Este equipo** y abre cualquier carpeta, como en VirtualDJ; sus canciones aparecen en el centro. Para tener una carpeta siempre a mano, `+ Carpeta favorita` (sale con estrella). También puedes arrastrar archivos o carpetas desde el Explorador.
3. **Cargar**: arrastra una pista sobre un plato (o sobre su forma de onda). También puedes hacer doble clic (o pulsar Intro) para cargarla en el plato que no esté sonando, o usar el menú del botón derecho.
4. **Mezclar**: `PLAY` en los dos platos, `SYNC` en el que entra y el crossfader para pasar de uno a otro.

Formatos admitidos: **MP3, WAV, FLAC y OGG Vorbis**. Cualquier otro archivo se rechaza con un mensaje, sin afectar a lo que suena.

## Menú principal (barra superior)
| Botón | Qué hace |
|---|---|
| **MEZCLA** | Modo normal: ondas (si están activadas), dos platos, mezclador y navegador. |
| **BIBLIOTECA** | Navegador a pantalla completa para preparar la sesión. Arriba quedan las dos ondas con la carátula, el título, el BPM, el tiempo que falta y un botón de reproducir de cada plato (o, con las ondas ocultas, los decks plegados). |
| **SEPARADOR** | Modo tipo Moises: separa una canción en voz e instrumentos (ver abajo). Entre paréntesis, las separaciones pendientes. Los platos siguen sonando mientras estás en este modo. |
| **Ondas** (icono de onda) | Muestra u oculta las dos ondas grandes de arriba. Encendido = visibles. |
| **Vista** (icono de paneles) | Ondas, decks plegados, pads, rejilla, cortes y subidones, color de las ondas, diseño y «Restablecer tamaños». |
| **Master** | Medidor de la salida, piloto **LIM** (se enciende cuando actúa el limitador) y volumen master (doble clic: valor por defecto). |
| **🎧** | Auriculares: volumen, mezcla preescucha/master y botones CUE A / CUE B. Se ilumina si hay un canal en preescucha. |
| **● Grabar** | Graba la salida master en WAV. Mientras graba muestra el tiempo; otro clic la detiene. |
| **⚙** | Audio, Mando MIDI, Preferencias, Programador de eventos, Configurar grabación, Organizar biblioteca, Vista y diseño, carpetas de grabaciones y de stems, Acerca de. El punto verde indica mando conectado. |

## Diseño y vista
En **Vista** (barra superior) eliges el diseño: **Ramiro Noche** (por defecto: azul noche, turquesa y coral), **Estudio** (grafito, azul y naranja, ondas de tres bandas), **Club** (negro puro, cian y magenta, ondas de colores por frecuencia), **Grafito** (gris medio, para sitios con mucha luz) y **Alto contraste**. Se guarda solo.

**Tamaños**: arrastra la línea que hay entre las ondas y los decks (alto de las ondas) y la que hay entre los decks y el navegador (alto de los decks; si la subes del todo, se pliegan en una línea). Doble clic en una línea: tamaño automático. Con pocos píxeles los pads se ocultan solos.

**Ondas** (Vista → Ondas):
- *Color de cada plato*: graves en el color del plato, medios más claros y agudos casi blancos, en capas.
- *Tres bandas*: graves, medios y agudos con los colores del diseño (azul, ámbar y blanco en Estudio).
- *Espectro de color*: un solo trazo cuyo color mezcla rojo (graves), verde (medios) y azul (agudos).
- *Rejilla*: compases a toda altura, cada 8 compases una línea más gruesa con su número; los pulsos son marcas cortas arriba y abajo.
- *Cortes y subidones*: los tramos de 4 compases o más sin bombo se sombrean en violeta y donde vuelve el bombo aparece **SUBIDÓN** (en la vista general, un triángulo ámbar).

Las ondas grandes se dibujan en tiempo real (teniendo en cuenta el pitch), así que con SYNC los golpes de A y B caen en la misma vertical.

**Carátulas**: se leen de las etiquetas de cada archivo (MP3, FLAC, OGG y WAV, incluidas etiquetas mal formadas habituales) o, si no tiene, de una imagen junto a la pista o en su carpeta. El panel **Detalles** dice de dónde sale («ID3v2.3 (portada) · JPEG», «imagen de la carpeta: folder.jpg»…) o por qué no hay. Si no hay: **Buscar en Internet** (o botón derecho → Carátula, o `⋯` de la lista → *Buscar en Internet las carátulas que faltan*), o **Asignar…** una imagen. Las carátulas descargadas o asignadas se guardan en `%APPDATA%\RamiroDJ\caratulas`; el archivo de audio no se modifica. Las WebP necesitan *Extensiones de imagen WebP* de Microsoft Store.

## Navegador (como VirtualDJ)
Ocupa la parte de abajo (o toda la ventana en **BIBLIOTECA**). Tiene cuatro zonas; los botones de la cabecera de la lista ocultan o muestran las laterales y los bordes entre zonas se arrastran para cambiar el ancho (doble clic: ancho automático).

**1. Árbol (izquierda)**. Arriba, un buscador de carpetas y listas.
- **Colección**: todas las pistas conocidas.
- **Favoritos**: las que marcas con el corazón del plato o con el botón derecho.
- **Sugerencias** (como las *Ideas* de VirtualDJ): pistas que encajan con lo que suena: BPM a menos de un 8 % (también al doble o a la mitad) y tonalidad compatible. Las que ya sonaron hoy van al final.
- **Historial**: una lista por día con lo que sonó más de 30 s.
- **Grabaciones**: tus mezclas grabadas.
- **CARPETAS**: tus **carpetas favoritas** (con estrella; se escanean enteras) y **Este equipo**: Escritorio, Música, Descargas, Documentos, Vídeos y las unidades. Despliega con la flecha y haz clic en una carpeta para ver sus canciones. Al abrir una carpeta, sus pistas pasan a la colección, como en VirtualDJ. Botón derecho: *Añadir a carpetas favoritas*, *Leer también las subcarpetas*, *Abrir en el Explorador*…
- **LISTAS**: tus listas (`+` crea una). Suelta pistas encima para añadirlas. Botón derecho: renombrar, duplicar, añadir a la Sidelist, pasar al Automix, exportar M3U8, vaciar y eliminar.
- **Pistas separadas**: las canciones del Separador; al abrir una aparecen sus stems (voz, batería…) listos para cargar.

**2. Lista (centro)**. Título del origen con el número de temas y la duración total.
- Columnas: carátula y título, artista, BPM, tonalidad (insignias de color Camelot, rellenas si encajan con lo que suena) y duración. Con el botón derecho en la cabecera puedes mostrar *Remix* (lo que va entre paréntesis en el título), *Reproducciones*, *Añadida* y *Archivo*. Clic en una cabecera para ordenar; las columnas se pueden mover y se recuerdan.
- Selección múltiple: Ctrl + clic, Mayús + clic o arrastrar un recuadro desde un hueco. Se arrastran todas a la vez a un plato (la primera), a la Sidelist, a una lista del árbol o a otra posición de la lista.
- En las listas: arrastrar para reordenar, `↑` `↓` para mover la selección y `Supr` para quitar.
- Las pistas cargadas llevan una marca del color de su plato (▶ si suena) y las que ya sonaron hoy salen más apagadas, con ✓.
- Buscador: busca en la vista actual; el botón del globo busca en toda la colección.
- `Añadir temas`: archivos o una carpeta favorita. `⋯`: opciones de la lista, leer subcarpetas, importar M3U, filas compactas.

**3. Panel lateral con pestañas abajo**: **Sidelist** (la *lista contigua* de VirtualDJ, para tener a mano lo que vas a pinchar: arrastra, reordena, `Supr` quita, doble clic carga; `⋯` → *Guardar como lista nueva*, *Pasar todo al Automix*, exportar o vaciar), **Automix** y **Estadísticas**. Puedes soltar pistas también sobre las pestañas.

**4. Detalles del tema (derecha)**: carátula, título, artista, género y **valoración** (estrellas); tres datos grandes (**BPM**, **tonalidad** y **duración**); la vista general si la pista está en un plato; la **rueda armónica** (compatibles, lo que suena en A y B, clic para filtrar la lista) y los **datos** (rejilla, origen de la tonalidad, fecha de adición, reproducciones, hot cues con sus nombres, tamaño y carpeta; el icono la abre). Las dos secciones se pliegan, y el panel entero se pliega a una franja con la flecha de arriba.

## Tonalidad (código Camelot)
La tonalidad se muestra por defecto como **código Camelot + nombre en español**: en las insignias, `La m` + `8A` (A = menor, B = mayor, rueda del 1 al 12); en los textos, `8A · La menor`. En `⚙ → Preferencias → Tonalidad` puedes elegir solo Camelot (`8A`), notación musical (`Am`), solo español (`La menor`), **como VirtualDJ** (`08A` en una insignia de color) u **Open Key** de Traktor (`1m`). Todas son la misma rueda: `8A` = La menor = `Am` = `1m`. Cada número tiene el color de la rueda Camelot y las pistas compatibles (misma clave, ±1 en la rueda o relativa) con lo que suena llevan una ✓ verde.

**¿Por qué VirtualDJ enseña otra tonalidad?** La escala es la misma; lo que cambia es el **detector**: cada programa calcula la tonalidad a su manera y en música electrónica es fácil que difieran (sobre todo entre una clave y su relativa o su quinta). De dónde sale la tonalidad de cada pista, por orden:
1. **Puesta a mano** (botón derecho → Tonalidad → Ponerla a mano, o clic derecho en la rueda). Nada la cambia después.
2. **De VirtualDJ**, si importas su base de datos (`⚙ → Importar de VirtualDJ…`).
3. **De la etiqueta del archivo** (`TKEY` / `INITIALKEY`), si la trae y está activada la opción en Preferencias.
4. **Detectada** por DJ Elver Galarga. Es una estimación: coincide con la clave exacta en alrededor del 40 % de las pistas probadas y es compatible para mezclar en torno al 60 %; si dudas, fíate del oído.

El panel Detalles dice de cuál viene cada una.

## Rack: efectos, pad XY, aislador y loop (debajo del mezclador)
En la vista MEZCLA. La cabecera siempre se ve. `▾` pliega o despliega el rack (plegado deja más sitio a la biblioteca). Tiene los pilotos FX · XY · ISO · LOOP, los **Presets**, **Guardar…** y **EFECTOS ON/OFF**, que lo apaga todo con un fundido mientras las colas terminan solas. Se ven dos módulos a la vez; elige cuál en las pestañas de cada panel.

- **Fuente** de cada módulo: **A** o **B** (antes del fader del plato) o **MEZCLA** (antes del volumen master).
- **Estación de efectos**:
  - El número de cada tarjeta la enciende. El desplegable elige el efecto y `‹ ›` cambia su orden en la cadena.
  - Ruletas de intensidad (filtro, repeticiones, tamaño, drive…) y **MEZCLA**. El tiempo va en pulsos y sigue el BPM del plato.
  - **INSERCIÓN**: el efecto se aplica a la señal. **ENVÍO**: el original no cambia y solo vuelven el eco, el delay y la reverb (nivel RET).
  - **MACRO**: clic derecho en una tarjeta para decir cuánto la mueve. Con la macro subida, esas ranuras se encienden solas.
  - Los puntitos ámbar muestran el valor que suena de verdad.
- **Pad XY**: arrastra con el ratón. Elige qué mueve cada eje (intensidad o mezcla de una ranura, la macro o una banda del aislador). Al soltar vuelve solo a su sitio (el círculo vacío); **HOLD** lo deja donde está. *Tocar enciende* activa la ranura de destino al tocar.
- **Aislador**: graves, medios y agudos desde el corte total hasta +6 dB, con botón **CORTE** por banda (vuelve suave). Las frecuencias de cruce van de 60–500 Hz y de 1–8 kHz.
- **Loop station**:
  - Elige compases (1, 2, 4, 8) y pulsa **GRABAR**: espera al siguiente compás y graba justo esos compases. **SONAR** entra en el compás siguiente.
  - Sigue sonando aunque cambies la canción del plato.
  - **CAPA** sobregraba encima y **DESHACER** quita la última capa. También tiene **BORRAR**, **VOL** y **WAV** (exporta a la carpeta de grabaciones).
  - Opciones: **CUANTIZAR**, **SEGUIR BPM** (cambia de velocidad con el plato), **AURIC.** (a los auriculares) y **AUTO** (suena nada más grabar; si no, queda lista para no duplicar el plato).
  - Máximo 30 s por loop.
- **MIDI**: clic derecho sobre cualquier control del rack → *Aprender* y mueve el control del mando. También funciona con los mandos reconocidos: ese control pasa al rack y lo demás sigue igual. Para el rack no hay asignaciones de fábrica para ningún mando.
- **Presets**: guardan la cadena, los parámetros, las fuentes, la macro, los ejes del pad, el aislador y las opciones del loop. El estado se recuerda solo.
- Más detalles (ruteo, qué pasa al pausar, saltar o cambiar de pista, rendimiento y pruebas): `RACK.md`.

## Instrumentos del rack: RITMOS, BAJO y SAMPLER
Son tres pestañas más de cada panel del rack. Suenan por su propio canal, que va a la mezcla (y a los auriculares si lo eliges).

- **Reloj** (el botón con el BPM, arriba a la derecha): por defecto siguen al plato que más suena, con su pitch, su SYNC y sus empujones. También pueden seguir a A, a B o ir a su propio tempo. Si el plato se para, siguen solos a su último tempo. Aquí está también el volumen del canal de instrumentos.
- **RITMOS**:
  - **TOCAR** entra en el siguiente compás (parpadea mientras espera).
  - **A…H** son 8 patrones; el que eliges entra al acabar el que suena. **16 / 32** pasos.
  - Clic en una casilla: poner o quitar el golpe. Clic derecho (o Mayús + clic): acento.
  - Clic en el nombre de un sonido: silenciarlo. Clic derecho: nivel, afinación y caída.
  - **SWING**: retrasa las semicorcheas pares. **Estilos…** carga un ritmo en el patrón que ves (también vacía o copia el patrón).
- **BAJO**:
  - Fila de arriba de cada paso: clic para que suene o no; rueda del ratón para subir o bajar la nota; clic derecho para elegirla.
  - Filas **A** (acento) y **S** (slide: liga con la siguiente nota deslizando).
  - **SIERRA / CUADR.**, tónica, **TONO** (tónica de la canción del plato) y octava.
  - Ruletas: CORTE, RESON., ENV, CAÍDA, ACENTO, DRIVE y VOL.
- **SAMPLER**:
  - Clic en un pad para tocarlo. Clic derecho: cargar un archivo, sonidos incluidos, modo, volumen, asignar al mando o vaciar. También puedes arrastrar un archivo del navegador al pad.
  - **DISPARO** suena entero; **MANTENER** suena mientras lo pulsas; **LOOP** un toque arranca y otro para (entra en el pulso si **LOOPS EN EL PULSO** está encendido).
  - Muestras de hasta 2 minutos. **PARAR** para todos los pads.
- **MIDI**: clic derecho en TOCAR → *Aprender*; en los pads, desde su menú. Acciones nuevas: «Rack: tocar o parar la caja de ritmos / el bajo», «Rack: siguiente patrón de ritmos» y «Rack: pad 1…16», con luces.
- Los patrones, los sonidos y los pads se guardan solos (en `rack.json`). Los presets del rack no cambian los instrumentos.

## Separador de pistas (voz e instrumentos)
1. Pulsa **SEPARADOR** arriba.
2. La primera vez, pulsa **Instalar el motor**. Necesita Python 3.10–3.13 (python.org) y descarga unos 1,2 GB desde las fuentes oficiales (PyTorch, PyPI y los modelos Demucs). Después funciona sin Internet.
3. **Arrastra una canción** al recuadro discontinuo (desde el Explorador o desde el navegador), haz clic en él para elegir un archivo, o usa el botón derecho en el navegador → *Separar en pistas*.
4. Elige **6 pistas** (voz, batería, bajo, guitarra, piano y otros) o **4 pistas** (más rápido). La separación se hace en el procesador y tarda unos minutos por canción; puedes seguir mezclando mientras tanto.
5. Al terminar se abre sola. Cada pista tiene su icono y color, su forma de onda, **M** (silenciar), **S** (solo), volumen y medidor. Arriba, atajos: **Todo**, **Aislar voz**, **Sin voz** y **Silenciar todo**. Barra espaciadora = reproducir/pausa; clic en la onda = saltar.
6. Menú de los tres puntos de cada pista: cargarla en el plato A o B, guardarla como WAV o mostrar el archivo. Abajo: **Voz al plato A**, **Instrumental al plato B**, **Exportar lo que suena…** y **Original al plato A**.
7. Las canciones separadas quedan en la lista de la izquierda (botón derecho: abrir, abrir carpeta, separar de nuevo, eliminar) y en el árbol del navegador, en **Pistas separadas**. Los archivos están en `%LOCALAPPDATA%\RamiroDJ\stems`.

`Auriculares` en el reproductor del Separador manda los stems también a la preescucha.

## Platos
Cada plato muestra, de izquierda a derecha: la **carátula** (con la letra del plato), el **título** con el **corazón** de favoritos, el artista, el tiempo transcurrido y el que falta (en rojo parpadeante en los últimos 30 s), el contador **compás.pulso** con los 4 pulsos y los 8 compases de la frase, el **BPM** grande, la **tonalidad** que suena (clic: ver en la lista lo que encaja) y, debajo, la vista general de la pista. «SUBIDÓN EN N» avisa de cuántos compases faltan para que vuelva el bombo tras un corte. A la derecha, el **plato giratorio** con el BPM y el pitch en el centro. Unas etiquetas indican lo que está activo: SYNC, KEY (key lock), transposición, LOOP y FX.

- Fila de transporte: **CUE** (encendido si estás parado en el cue), **PLAY/pausa** (respira en pausa), **SYNC** (clic derecho: MASTER y opciones), **▾** (key lock, cuantizar, rango, pitch a 0, SYNC e igualar el tono), **hot cues 1–4** (clic derecho: nombre o borrar) y el menú de los tres puntos (favoritos, Sidelist, volver a analizar, Explorador, expulsar). Con la ventana muy estrecha, los hot cues quedan solo en la pestaña CUES.
- **Pads por pestañas**: **CUES** (hot cues 1–8 con su tiempo), **LOOP** (1/4 a 16 pulsos, IN, OUT, ½, ×2, RELOOP/SALIR y **QUANT**), **SALTO** (±1, 4, 8 y 16 pulsos, al inicio, al cue, al primer pulso y expulsar), **REJILLA**, **TONO** y **EFECTO**.
- Columna de pitch, en el lado exterior: rango (±8/16/50), fader, **◀◀ / ▶▶** (frenar o acelerar un 4 % mientras se mantienen) y **0**.

## Mezclador
Por canal: **GAIN**, **HI**, **MID**, **LOW** y **FILTER** (siempre rotulados; doble clic = valor por defecto), medidor estéreo con escala en dB y piloto rojo de saturación, fader de volumen con escala y botón **CUE** de auriculares (preescucha). Abajo, el crossfader (doble clic: centro); el botón de arriba en el centro elige su curva. Debajo, dos píldoras: la diferencia de **tempo** entre platos (verde si es menor del 1 %) y la relación de **tonalidad** entre lo que suena (verde encaja, ámbar con cuidado, rojo choca); pasa el ratón para la explicación.

## Importar de VirtualDJ
`⚙ → Importar de VirtualDJ…` (también en el menú de *Colección* y en Preferencias). Busca el `database.xml` en `Documentos\VirtualDJ`, en `%LOCALAPPDATA%` y en la raíz de cada disco (`X:\VirtualDJ`, discos externos); si lo tienes en otro sitio, «Elegir otro database.xml…». Puedes traer:
- **Tonalidad**: la que ves en VirtualDJ (la de la etiqueta si la hay; si no, la de su análisis).
- **BPM y rejilla**: su tempo y su primer tiempo de compás. Desde la 0.10.2 no hace falta importarlos: al cargar una canción que VirtualDJ tiene analizada, se usan solos (casilla *Usar siempre la rejilla de VirtualDJ*). En los MP3 solo se corrige el desfase conocido entre decodificadores (unos 25 ms) si la medida lo confirma; en el resto, la rejilla queda exactamente como en VirtualDJ.
- **Hot cues**: solo en los huecos libres.
- **Listas**: las de `VirtualDJ\Playlists`, como «VDJ · nombre» (si las vuelves a importar, se sustituyen).
- Opcional: añadir a la colección las pistas que aún no tengas.
Las pistas se emparejan por ruta. Las rejillas corregidas y las tonalidades puestas a mano se respetan (casilla). Si una pista está sonando, conserva su rejilla y sus cues hasta que la vuelvas a cargar.

## Controles
| Control | Uso |
|---|---|
| Potenciómetros | Arrastrar arriba/abajo o rueda del ratón. **Doble clic**: valor por defecto. **Mayús**: ajuste fino. |
| Faders | Arrastrar, clic en la guía para saltar, rueda. Doble clic: valor por defecto. |
| Ondas de arriba | **Arrastrar = scratch**. Rueda = zoom (2-32 s). Las banderas numeradas son los hot cues. **Clic derecho**: hot cue, CUE o primer tiempo de compás justo ahí. |
| Vista general del plato | Clic o arrastrar para ir a ese punto. |
| CUE | En pausa fija el punto cue. Mantenerlo pulsado sobre el cue = escuchar mientras se mantiene. Sonando = vuelve al cue y para. |
| Hot cues | Clic en uno vacío: lo guarda. Clic en uno ocupado: salta y reproduce. **Clic derecho: borra**. |
| ◀◀ / ▶▶ | Frenan o aceleran un 4 % mientras se mantienen (para ajustar a oído). |
| QUANT | Cuantizar: cues, hot cues y loops se ajustan al pulso más cercano. |
| Pitch | Arriba = más lento, abajo = más rápido. El botón de arriba cambia el rango (±8, ±16, ±50 %). `0` lo pone a cero. |

### Sincronización
`SYNC` iguala el tempo con el otro plato y alinea los pulsos. Desde la 0.6.1 lo mantiene el motor de audio cada 5 ms:
- Sigue el tempo y la **fase** del otro plato todo el rato: si le mueves el pitch al que manda, el sincronizado le sigue.
- Si la pista está analizada a **tempo doble o mitad** (por ejemplo 64 frente a 128), lo detecta y aparece **SYNC ×2** o **SYNC ×½**.
- Tras un salto (hot cue, salto de pulsos) o un scratch en cualquiera de los dos platos, se realinea al momento.
- Si empujas cualquiera de los dos platos (jog, aro del plato en pantalla, ◀◀ ▶▶), se quedan con el desfase que elijas (para corregir a oído una rejilla). *Realinear la fase* (clic derecho en SYNC) lo quita.
- Si la diferencia no cabe en el rango de pitch, el rango se amplía solo. Si mueves a mano el pitch del plato que sigue, su SYNC se desactiva; el del MASTER se puede mover y el otro le sigue.
- **SYNC en los dos platos**: uno es el **MASTER** (marca el tempo) y el otro le sigue. Si el MASTER se para o termina, el MASTER pasa al que suena sin cambiar el tempo. Clic derecho en SYNC para elegir el MASTER, realinear la fase o pasar a **solo tempo**.
- **¿REJILLA?**: aparece si la fase no se estabiliza en unos segundos; revisa la rejilla de las dos pistas.
- **Medidor de fase**: la barrita de dentro del botón SYNC (y la de junto a las ondas): centrada y verde = golpes a la vez; a la derecha, este plato va adelantado.
- Menú ▾ del plato → *SYNC: alinear compases completos*: además de los pulsos, coinciden los primeros tiempos de compás (necesita las rejillas bien puestas).
- Al pulsar PLAY en un plato con SYNC sale ya alineado; si arranca el que manda mientras el otro suena sincronizado, el que arranca es el que se alinea.

Desde la 0.10.0 las rejillas automáticas caen a menos de 1 ms del golpe y se recalculan solas en las pistas antiguas (la etiqueta de la rejilla en Detalles lo indica). Si el SYNC «no cuadra» al oído, casi siempre es la rejilla de una de las pistas: acerca la onda con la rueda, haz **clic derecho** justo en el bombo con el que empieza un compás y elige *Empieza un compás aquí*, o usa la pestaña **REJILLA**.

### Loops (pestaña LOOP de los pads)
- Loops automáticos de 1/4 a 16 pulsos. Si pulsas otra vez el activo, sales del loop.
- Entrada/Salida manuales, ½ y ×2.
- Salir/Reloop.
- Saltos de 1, 4, 8 y 16 pulsos en la pestaña SALTO.

### Rejilla de pulsos (pestaña REJILLA)
El BPM y la rejilla se calculan al cargar la pista y se guardan. Si no encajan:
- Escribe el BPM, usa ×2 o ÷2, o pulsa `TAP` al ritmo (4 veces o más).
- Desplaza la rejilla en pasos de 1 o 10 ms.
- `1.º aquí` coloca el inicio de compás en la posición actual.
- `Analizar` vuelve a calcular.

### Efectos (pestaña EFECTO)
Eco, Flanger y Reverberación por plato, sincronizados al BPM. El eco y la reverberación dejan sonar su cola al apagarlos.

### Detalles del mezclador
- Ganancia de -12 a +12 dB.
- EQ de 3 bandas tipo aislador. Al mínimo, la banda se corta del todo.
- Filtro: a la izquierda paso bajo, a la derecha paso alto.
- Volumen de canal y crossfader con tres curvas.
- **Limitador** en el master: la salida nunca supera -0,3 dBFS. El piloto **LIM** de la barra superior se enciende cuando actúa.

### Preescucha por auriculares
Se configura en `⚙ → Audio → Auriculares`:
- **Canales 3-4 del mismo dispositivo**: solo con tarjetas o controladores de 4 salidas.
- **Otro dispositivo de salida**: por ejemplo, auriculares USB o Bluetooth. Al ser relojes distintos, puede haber microcortes en la preescucha. El master no se ve afectado.
- **División mono**: con unos auriculares en una salida estéreo, el izquierdo lleva la preescucha y el derecho el master.

Después, el botón de auriculares de cada canal (en el mezclador) y el botón 🎧 de la barra superior para el volumen y la mezcla preescucha/master.

### Grabación
`● Grabar` (barra superior) graba el master, tal como sale tras el limitador, en un WAV. El formato por defecto es 16 bits con tramado; en Preferencias puedes elegir 32 bits en coma flotante. Se guarda en `Música\DJ Elver Galarga - Grabaciones` y aparece en **Grabaciones**, en el árbol del navegador.

### Tono y key lock (menú ▾ del plato y pestaña TONO)
- **Key lock**: cambia el tempo con el pitch **sin cambiar el tono**. Sin key lock, el pitch funciona como un vinilo. Cuando está activo, el plato muestra la etiqueta KEY.
- **Pestaña TONO**:
  - Transposición de −6 a +6 semitonos; clic en el valor para volver a 0.
  - **Igualar**: transporta el plato a una tonalidad compatible con el otro según la rueda Camelot.
- **Tonalidad detectada**: aparece en la cabecera del plato, junto a la onda y en la columna *Tonalidad* del navegador, con la insignia rellena si es compatible con lo que suena. La detección es **aproximada**: acierta acordes de prueba, pero coincide con VirtualDJ en el 28 % de las pistas (60 % compatibles para mezclar). Confírmala a oído.

### Automix (pestaña Automix del panel lateral del navegador)
Arrastra pistas a la lista (o usa *Pasar a la lista de Automix* desde una lista o la Sidelist) y pulsa **INICIAR AUTOMIX**:
- La primera suena en el plato A.
- La siguiente se precarga en el otro plato y entra **N pulsos antes del final**, sincronizada; el crossfader se mueve solo.

Puedes elegir la duración del fundido, sincronizar o no, y repetir la lista. Arrastra filas para reordenar; con clic derecho, "Siguiente en sonar".

### Detalles y estadísticas
- **Detalles del tema** (panel derecho del navegador): ver arriba, en *Navegador*.
- **Estadísticas** (pestaña del panel lateral): pistas, horas de música, histograma de BPM, distribución de tonalidades y las más reproducidas.

### Programador (`⚙ → Programador de eventos…`)
Acciones a una hora del día: iniciar o detener la grabación, iniciar o detener el Automix, fundido final del master (15 s) o reproducir un plato. Cada evento se ejecuta una vez al día, con la aplicación abierta.

### Sesión
La sesión se guarda cada 5 segundos. Al abrir el programa se recuperan las pistas cargadas, sus posiciones, pitch, key lock, mezclador y efectos, con los platos en pausa. Si el programa se cerró de forma inesperada, lo indica abajo.

### Listas
Las listas están en el árbol del navegador (**LISTAS**). Se crean con `+` o con *Lista nueva con estas pistas…* del botón derecho, y la **Sidelist** sirve de lista de preparación (como la *lista contigua* de VirtualDJ). Las listas se guardan solas cada pocos segundos y al salir. Para guardar la sesión de hoy: `⋯` de la Sidelist → *Guardar como lista nueva*.

Cada pista que suena más de 30 s se añade sola a **Historial** (una lista por día).

### Mandos reconocidos (`⚙ → Mando MIDI…`, arriba)
- Al enchufar un mando por **cable USB**, DJ Elver Galarga busca su nombre en la lista de 144 mandos y carga su mapeo: botones, ruedas, faders, jog y luces ya asignados. No hace falta el asistente.
- Cada mando conectado sale en una tarjeta: **verde** = reconocido y listo; **ámbar** = el mapeo dio avisos (botón *Registro*); **gris** = no está en la lista.
- El desplegable de la tarjeta elige el mapeo: *Automático* (el que le toca por su nombre), *Ninguno: mis asignaciones* (para usar el asistente) o cualquier otro de la lista, por si tu mando es una variante con otro nombre. Se recuerda para la próxima vez.
- *Buscar mandos* vuelve a mirar qué hay conectado (también se hace solo cada pocos segundos).
- Como en Mixxx, la mayoría de los mapeos hacen que los **faders y ruedas recojan el valor**: si no coinciden con la pantalla, no hacen nada hasta que los llevas a su posición. Así no hay saltos de volumen.
- Usa el cable USB: por Bluetooth los mandos no envían MIDI a Windows. Algunos mandos (como el DDJ-200) **no tienen tarjeta de sonido**: el sonido sale por el ordenador y la preescucha necesita otra salida.
- **Mapeos propios**: si tienes un mapeo de Mixxx (`.midi.xml` y sus `.js`), cópialo en `%APPDATA%\RamiroDJ\mandos`: se añade a la lista y, si se llama igual que uno incluido, lo sustituye.
- Mientras un mando usa su mapeo, tus asignaciones propias no se le aplican (se guardan igual); elige *Ninguno: mis asignaciones* para usarlas.

### Mando MIDI: asignaciones propias
- El navegador del mando (rueda BROWSE) mueve la selección de la lista central y LOAD carga la pista seleccionada.
- **Asistente**: te pide mover cada control del mando, uno a uno (PLAY, CUE, jog, faders, EQ…), y lo asigna solo. Es la forma más fiable de adaptar cualquier mando. Si tu mando no tiene un control, pulsa *Saltar*.
- **Asignaciones**: lista de acciones.
  - La fila se ilumina al usar el control.
  - `Aprender` cambia una asignación.
  - Los faders se pueden **invertir**.
  - El jog se puede codificar como *centro 64* o *complemento a 2*: si el jog va al revés o a saltos, prueba la otra opción.
- **Ajustes del mando**:
  - Sensibilidad del aro del jog (empuje de tempo), del scratch y del desplazamiento en pausa.
  - Scratch al tocar la parte de arriba del jog (modo vinilo).
  - **Recoger el valor** (soft takeover): un fader del mando no hace saltar el volumen si lo cambiaste con el ratón; actúa cuando lo llevas a la posición de la pantalla.
  - Invertir los faders de pitch.
  - **Luces** del mando, con prueba de luces.
- **Monitor**: muestra cada mensaje que llega. Los que no hacen nada se pueden asignar desde ahí.
- Los controles de 14 bits (pitch, faders y EQ de los Pioneer) se detectan solos y dan más precisión. Hay capa **SHIFT**: SHIFT + hot cue borra el hot cue y SHIFT + navegador avanza de 10 en 10.

**Preajuste Pioneer DDJ-WeGO**:
- **Comprobado con tu mando** (con Aprender): volumen de canal CC 19, pitch CC 0, crossfader canal 7 CC 31, y canal 7 CC 23/24, que asignaste a la ganancia.
- **Patrón de la familia DDJ**, sin comprobar en tu mando: jog (aro CC 33, plato CC 34, tocar nota 54), SHIFT, navegador, carga y auriculares.

Pásale el asistente para confirmar el resto.

Si el DDJ-WeGO también es tu tarjeta de sonido: en `⚙ → Audio…`, elige el WeGO como dispositivo y *Canales 3-4* para los auriculares.

### Plato giratorio
El disco de cada plato:
- Muestra en el centro el BPM (verde si está en SYNC) y el pitch.
- Su marca gira con la pista (33⅓ rpm) y el aro de color muestra el progreso de la pista.
- El punto naranja marca el cue.
- **Aro exterior** (con la canción sonando): gíralo con el ratón para adelantar o atrasar un poco la canción y cuadrarla a oído. En el centro pone EMPUJE y cuánto.
- **Centro**: gíralo con el ratón para hacer scratch.

## Dónde se guardan los datos
- Biblioteca en SQLite (`biblioteca.db`, con copias diarias en `copias\`; género, valoración y nombres de cue en una tabla aparte, así una versión anterior sigue pudiendo abrirla), sesión, programador, preferencias (también los anchos de los paneles y las columnas del navegador) y MIDI: `%APPDATA%\RamiroDJ`. Las carátulas se leen de los archivos cada vez; las descargadas o asignadas se guardan en `caratulas\` (y `caratulas\busquedas.json` recuerda qué se buscó). Al pasar de la 0.2 a la 0.3, `biblioteca.json` se migra solo y se conserva como `biblioteca.json.migrado`.
- Grabaciones: `Música\DJ Elver Galarga - Grabaciones`.
- Separador: motor en `%LOCALAPPDATA%\RamiroDJ\separador` (≈1,2 GB) y canciones separadas en `%LOCALAPPDATA%\RamiroDJ\stems`. Puedes borrar esas carpetas para recuperar espacio.

Desinstalar la aplicación no borra estos datos.

## Limitaciones conocidas de esta versión
- **ASIO no está disponible**. Se usa WASAPI (compartido o exclusivo), con DirectSound y WinMM como alternativas.
- **Retransmisión (Icecast/Shoutcast) y grabación en MP3**: pendientes.
- **Sin AAC/M4A**.
- Rejilla de BPM constante: no sigue pistas con el tempo variable (grabaciones en directo).
- MIDI: el preajuste del DDJ-WeGO no está verificado por completo (usa el asistente). La lógica MIDI se procesa a la frecuencia de la pantalla (~16 ms).
- Rack: probado automáticamente y en Wine, no en Windows real, ni con pantalla táctil ni con mandos físicos. Retardos de 4 s como máximo y loops de 30 s como máximo. La sobregrabación con SEGUIR BPM y el tempo cambiado escribe a la muestra más cercana. Faltan los efectos por stems y la grabación de movimientos.
- Instrumentos (0.10): sin sonido real comprobado a oído en Windows (las pruebas miden el audio que generan). Los pads leen la muestra a su velocidad original (no se adaptan al tempo). La caja de ritmos sigue el pulso del plato: si la rejilla de la pista está mal, los ritmos irán igual de mal.
- Rejilla automática: en pistas sin bombo ni caja (solo percusión suave) el primer tiempo del compás puede salir mal; se corrige con *Empieza un compás aquí*.
- Mandos reconocidos: los 144 mapeos se han probado **simulando** cada mando (todos sus botones y luces), no con los aparatos de verdad. DJ Elver Galarga tiene un solo efecto por plato y un filtro, así que los botones de los mandos para la 3.ª y 4.ª cubierta, los samplers y los efectos avanzados de Mixxx no hacen nada. Diez mapeos tienen fallos propios que dan avisos en el *Registro* (Xone K2, Hercules AIR, Ion Discover DJ Pro, Numark NS4FX, NS6II y NS7, Pioneer DDJ-SB3, Reloop Beatpad, Vestax Spin, VCI-100 Hile); casi todo funciona igual.
- La preescucha en un dispositivo aparte puede tener microcortes.
- Grabación solo en WAV (no MP3).
- Separador: solo usa el procesador (sin GPU), así que tarda varios minutos por canción.
- Las pistas de las carpetas que abres en el navegador quedan en la **Colección** (como en VirtualDJ); aún no hay una opción para quitarlas de ahí.

## Solución de problemas
- **El rack no se ve**: está en la vista MEZCLA, debajo de los platos. Si la ventana es muy baja se queda en una línea; pliega las ondas o los platos (`Vista`) o agranda la ventana.
- **No suena ningún efecto**: mira que EFECTOS esté en ON (no OFF), que la ranura esté encendida y con la MEZCLA subida, y que la fuente sea el plato que suena.
- **La loop station no empieza a grabar**: con CUANTIZAR espera al siguiente compás del plato de la fuente; si el plato está parado, espera (lo indica). Quita CUANTIZAR para grabar ya.
- **Mi mando no se reconoce**: comprueba que va por USB y que no está abierto en otro programa (VirtualDJ, rekordbox…), y pulsa *Buscar mandos*. Si su nombre no está en la lista, prueba en el desplegable un mapeo del mismo modelo o de uno parecido, o usa el asistente.
- **Un botón del mando no hace lo esperado**: abre *Registro* en su tarjeta; si pone que un control no existe en DJ Elver Galarga, es una función que aquí no hay (samplers, cubiertas 3 y 4…).
- **El separador dice que falta Python**: instala Python 3.12 desde python.org (marca "Add python.exe to PATH") y vuelve a pulsar Instalar.
- **No suena**: mira la barra de estado (abajo a la derecha). Si pone "modo simulado", no había salida disponible; abre `⚙ → Audio…`, elige un dispositivo y pulsa Aplicar.
- **Cortes o chasquidos**: sube el búfer a 20 ms en `⚙ → Audio…`.
- **No veo una carpeta en el árbol**: las carpetas ocultas y del sistema no se muestran. Botón derecho sobre la carpeta de arriba → *Actualizar subcarpetas* si la acabas de crear.
- **La lista central sale vacía en una carpeta**: puede que las canciones estén en subcarpetas; `⋯` → *Leer también las subcarpetas*.
- **Se desconectó el dispositivo**: la aplicación vuelve al predeterminado sola y lo indica abajo.
- **El mando MIDI no aparece**: ciérralo en otros programas (VirtualDJ, por ejemplo) y pulsa `Volver a buscar`.
- **El programa se cerró de golpe**: en `%APPDATA%\RamiroDJ` queda `fallo.txt` con lo que estaba haciendo. Mándalo para poder arreglarlo.
