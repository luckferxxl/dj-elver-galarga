# DJ Elver Galarga 0.15.0 — Caja PRO

## Acceso

LIVE → TALLER → CAJA PRO. También desde RITMOS → Estilos → CAJA PRO, o desde
el menú de una voz de la caja de ritmos.

## 24 kits y 192 sonidos propios

La librería incluye 24 recetas de síntesis, cada una con ocho sonidos: bombo,
caja, palmas, charles cerrado, charles abierto, tom, rim y platillo. Son sonidos
originales generados por el programa, no grabaciones ni emulaciones certificadas
de máquinas comerciales. Los kits se generan al seleccionarlos, sin descargar
paquetes y sin mantener los 192 sonidos a la vez en memoria.

| Kits 1–8 | Kits 9–16 | Kits 17–24 |
|---|---|---|
| Club seco | Electro brillante | Dub largo |
| Analógico redondo | Acid percusivo | Disco compacto |
| Sub profundo | Breaks crujientes | Rave agresivo |
| Techno duro | Lo-fi cinta | Percusión metálica |
| Industrial | Digital roto | Hip-hop pesado |
| Warehouse | Vintage oscuro | Drum and bass |
| House cálido | Garage ligero | Ambient suave |
| Minimal corto | Tribal grave | Micro percusión |

Las recetas varían afinación, caída, saturación, filtrado y cuantización.
Los sonidos de fábrica se normalizan a un pico de 0,8. Los ocho sonidos de un kit
se pueden combinar con voces de otros kits: por ejemplo, bombo Sub profundo,
caja Industrial y charles Minimal corto.

## Samples propios

Selecciona una voz y pulsa Cargar sample propio. Usa el decodificador existente
para WAV, MP3, FLAC y OGG. Los sonidos de batería admiten desde dos fotogramas
hasta 10 segundos de audio; se permiten golpes muy breves. El límite se comprueba
antes de reservar la memoria de todo el archivo cuando se conoce su duración,
y también durante la lectura cuando no se conoce.

La preparación y decodificación se realizan en segundo plano. Una carga fallida
no sustituye el kit. Cambiar solo una fuente conserva las otras voces que ya
estén sonando. Al sustituir una fuente, esa voz se detiene; no se hace un fundido
entre kits completos. La reproducción queda limitada a una nota simultánea por
voz, además de las otras siete voces.

## Envolventes y controles por voz

Los «sobres» se han interpretado como envolventes. Esta versión añade:

- Volumen AHD: ataque de 0,2 a 200 ms, hold de 0 a 500 ms y decay de 5 a 5.000 ms.
- Tono: profundidad de -24 a +24 semitonos y caída de 2 a 1.000 ms.
- Afinación base de -12 a +12 semitonos.
- Filtro paso bajo de 100 a 20.000 Hz, limitado también por la frecuencia de muestreo.
- Saturación, panorama y nivel independientes.
- Mute y Solo por voz; se pueden combinar varios solos.
- Cuatro grupos de corte. El charles abierto y el cerrado comparten grupo 1
  por defecto. Un nuevo golpe del grupo desvanece los anteriores en 5 ms.

La envolvente multiplica el audio del sample: hold y decay no alargan un archivo
que ya terminó ni reconstruyen un sonido que se apagó. La afinación cambia
velocidad y duración. Los parámetros de envolvente y color se toman al disparar
el siguiente golpe; nivel, mute y solo siguen actuando durante la reproducción.
El control Caída del panel anterior escala el decay, dentro de su límite máximo.
Los controles nuevos requieren una fuente de sample; si no pudo restaurarse,
el programa conserva la síntesis anterior como alternativa.

Escuchar golpe dispara inmediatamente por el bus de instrumentos hacia la mezcla,
respetando Mute y Solo. Clic derecho permite aprender un botón o pad MIDI. Las
ocho acciones de disparo también aparecen en el listado de asignaciones.

## Bancos personales y persistencia

Puedes guardar hasta 128 bancos personales, con fuentes y controles de las ocho
voces. Guardar con un nombre existente lo sustituye. Clic derecho sobre un banco
en Mis bancos permite eliminarlo de la lista sin borrar sus archivos de audio.

Cargar un banco conserva el patrón seleccionado, swing, probabilidad, variación
de intensidad y estado del transporte. Los bancos cambian el sonido del patrón,
no la secuencia de golpes. Los controles y el kit actual se restauran al abrir.
Las escenas LIVE incluyen los controles de las voces; siguen sin cambiar las
fuentes de samples ni cargar un banco de sonidos al lanzar una escena.

Exportar banco JSON guarda la configuración y referencias. Importar banco JSON
la carga; usa Guardar banco personal para incorporarla a Mis bancos.
Los JSON no contienen los samples externos: conserva sus archivos y rutas o
compártelos por separado. Los sonidos de fábrica se regeneran desde sus IDs.

En la primera apertura desde una versión anterior se prepara Club seco y se
conservan los controles existentes de las voces. El cambio no modifica los
archivos de audio originales ni los patrones guardados.

## Validación y alcance

Ejecutado en Linux, con C++20:

- Generación de los 192 sonidos: audio finito, normalizado y no silencioso;
  comprobación de que los 192 resultados son distintos.
- Envolventes de ataque, hold y decay; cambios audibles por envolvente de tono,
  filtro y saturación; panorama, grupos de corte y solo por voz.
- Disparo real mediante comandos del motor y confirmación de cambio de sample.
- Ausencia de reservas de memoria durante las llamadas de audio vigiladas.
- Decodificación WAV real: golpes de 10 ms, un archivo exactamente de 10 segundos,
  rechazo de archivos de más de 10 segundos, errores de archivo inexistente y
  conservación del comportamiento del cargador normal de canciones.
- Regresiones de TALLER, efectos, stems, transporte y captura retrospectiva.
- Comprobación de sintaxis de los archivos de interfaz afectados. No se ha
  ejecutado la interfaz Windows ni probado bancos con hardware físico.

Pruebas reproducibles: python tools/probar_live.py
CMake/CTest incorpora drum_pro y drum_decode.

Esta entrega es código fuente. Quedan pendientes la compilación/enlace completos
para Windows, la revisión visual e interactiva y las pruebas con controladoras y
salidas reales. El importador JSON y su interfaz no se han probado de extremo a
extremo en Windows. El motor probado no acredita esas validaciones.
