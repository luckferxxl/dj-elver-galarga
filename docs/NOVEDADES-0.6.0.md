# RamiroDJ 0.6.0 — mejoras de interpretación y biblioteca

Entrega de código fuente, basada en el ZIP 0.5.0 aportado. No incluye un ejecutable o instalador 0.6.0 compilado. La validación de Windows, WASAPI, Direct3D/WIC y controladoras físicas sigue pendiente.

## Cómo encontrar las novedades

- Barra bajo el logotipo: **Plegar decks / Expandir decks**, **Organizar biblioteca**, **Configurar grabación** y **Apariencia**.
- Arrastra la línea entre decks y biblioteca para cambiar la altura. La distribución se conserva en las preferencias; los paneles laterales existentes siguen siendo ajustables.
- Apariencia ofrece los tres entornos principales: **Ramiro Classic**, **Ramiro Neon** y **Ramiro Studio**. Los temas anteriores siguen accesibles en Preferencias. Neon añade aros geométricos y acabados luminosos; Studio usa controles rectos, detalles ámbar y una altura inicial más compacta. Los controles mantienen posiciones coherentes. No se crean ventanas flotantes independientes.
- La pestaña **ROLL** de cada deck ofrece 1/16, 1/8, 1/4, 1/2, 1, 2, 4 y 8 pulsos. Mantén pulsado y suelta para recuperar el avance original. Recupera también un loop previo. Sin BPM conocido se usa 120 BPM como referencia; la calidad de la cuantización depende de la rejilla.
- La pestaña **VINILO** contiene modo vinilo, Slip, Reverse, Backspin y freno. Reverse tiene modo fijo y un botón momentáneo. Backspin/freno permiten duraciones de 0,15 a 4 s. Si el deck está muy reducido, clic derecho en FRENO abre los controles adicionales.
- Preferencias incluye sensibilidad de scratch con ratón. El scratch y la curva de crossfader ya existentes se conservan. Los gestos de ratón se liberan al ocultarse la ventana.
- En **FX** se mantienen Eco, Flanger y Reverb, y se añaden **Echo Out, Gate/Trance, Auto Filter, Phaser, Bitcrusher y Distorsión**. Una ranura por deck. Los efectos rítmicos usan el BPM; Bitcrusher y Distorsión son efectos de textura. Echo Out reduce el original y deja el eco según la intensidad; al desactivar vuelve el original.
- En Asignaciones MIDI aparecen acciones nuevas para Slip, Reverse fijo/momentáneo, Roll de 1/4 y 1 pulso, Backspin, freno y modo vinilo. No se cambian los identificadores de asignaciones previas. Las acciones nuevas requieren aprendizaje/asignación; no se promete compatibilidad automática con todas las controladoras.

## Carátulas

La lectura conserva APIC/PIC de MP3, PICTURE de FLAC y comentarios de OGG. Se corrige la lectura ID3 dentro de contenedores WAV y se amplía el manejo de ID3v2.4 con agrupación y desincronización. Busca imágenes JPEG/PNG con el nombre de la canción y cover/folder/front/albumart en su carpeta.

Menú derecho de un tema → **Carátula**:

- **Asignar imagen**: guarda una copia en los datos de RamiroDJ, sin modificar el audio ni necesitar escribir en la carpeta de música. Límite: 16 MiB.
- **Recargar carátulas**: invalida la caché y vuelve a leer.
- **Usar carátula original**: retira la asignación manual.

La decodificación sigue en un hilo separado. Se rechazan resultados antiguos después de una recarga y se limita la permanencia de entradas sin portada en caché. Una portada ausente no se descarga de Internet. M4A/AAC sigue fuera de los formatos reproducibles del proyecto; esta versión no añade ese decodificador. No se ha recibido un archivo del usuario que reproduzca su fallo concreto de portada.

## Separador

Las separaciones nuevas usan carpetas **Artista - Tema [identificador]**, con nombres válidos en Windows. Si faltan etiquetas se usa el nombre del archivo. El identificador distingue archivos con el mismo título. Las carpetas antiguas se conservan y siguen accesibles; no se renombra ni elimina una separación anterior automáticamente.

Se mantienen los controles existentes para abrir carpeta, escuchar en solo y cargar una pista separada en A o B. No se cambia el modelo Demucs ni se ha ejecutado una separación completa en este entorno.

## Grabaciones

Panel **Configurar grabación** (también clic derecho en Grabar): carpeta de destino, nombre de sesión, WAV 16 bits o 32 bits flotantes y listado opcional de temas. Se añade fecha/hora al nombre y un sufijo si ya existe. Configuración bloqueada durante la toma para que lo mostrado corresponda a la grabación activa.

El panel muestra duración, tamaño aproximado, espacio disponible y avisos de escritura/pérdida de muestras. El motor de escritura en segundo plano se conserva. Al terminar, la mezcla se añade a Grabaciones como antes.

El TXT registra cuándo empiezan a reproducirse pistas en A/B durante la grabación. **No es detección de lo que se oye**: puede incluir un deck con el fader cerrado. No registra las pistas individuales del reproductor del separador. No se añaden MP3, pausa/reanudación ni división automática de WAV largos.

## Organización

- Etiquetas rápidas Apertura, Vocal, Subidón, Cierre y Techno desde el menú de canciones; texto libre desde Organizar biblioteca.
- Colores de fila y búsqueda por etiquetas, además de los campos existentes.
- Playlists inteligentes por rango BPM, palabras/etiquetas y condición «sin reproducir». Se evalúan sobre la colección y se actualizan al cambiar sus datos; no duplican los archivos. Se crean, abren y eliminan desde Organizar biblioteca, y aparecen en el árbol.
- Sugerencias configurables: referencia automática/A/B, margen de BPM 1–25 % y filtro opcional de tonalidad conocida compatible. Compara también medio/doble tempo; penaliza temas reproducidos hoy. La explicación es general, no un informe individual de puntuación por fila. La compatibilidad usa la tonalidad analizada original del tema.
- Las etiquetas y reglas se guardan en ajustes.json. La base musical SQLite y las listas existentes se conservan.

## Verificación realizada y pendiente

Realizada en Linux:

- Suite del motor con audio sintético: avance, Roll y recuperación de loop/pausa, Slip con reverse/scratch/loop, freno, Backspin, transiciones con key lock, descarga de pista, fin de archivo y salidas finitas de todos los efectos.
- Ocho casos de extracción de portadas: MP3 ID3v2.2/v2.3/v2.4, WAV, FLAC, JPEG lateral, carpeta y archivos truncados. Se comprobaron límites con AddressSanitizer/UndefinedBehaviorSanitizer; la detección de fugas no estaba disponible en el contenedor.
- Comprobación de sintaxis C++ de interfaz, preferencias y coordinación usando declaraciones sustitutivas de Win32. Esto no equivale a enlazar un ejecutable Windows.
- Revisión de un render de los paneles reales de decks y controles compactos con datos sintéticos y un renderizador de prueba. No es una prueba de Direct3D ni de la aplicación completa.

Pendiente en Windows:

1. Compilar y ejecutar **todas** las suites CTest, incluidas las pruebas originales del proyecto.
2. Probar el render de portadas mediante WIC/Direct3D con canciones reales del usuario.
3. Escuchar transiciones/efectos y probar scratch/MIDI con la controladora real.
4. Probar dispositivos de salida y una grabación real, incluyendo cambio de carpeta y listado TXT.
5. Probar una separación Demucs completa, varias resoluciones/DPI y restauración de ajustes.

## Compilación

Con CMake, Ninja y MinGW-w64 disponibles en Windows:

```powershell
powershell -ExecutionPolicy Bypass -File .\compilar.ps1 -SinInstalador
```

Para generar además el instalador, instala Inno Setup 6 y ejecuta compilar.ps1 sin `-SinInstalador`. El script usa CTest y se detiene si fallan las pruebas. Consulta también COMPILAR.md. No hay un .exe 0.6.0 incluido en este ZIP.

Comprobaciones portables en Linux con Python 3 y g++:

```bash
python3 tools/verificar_portable.py
```
