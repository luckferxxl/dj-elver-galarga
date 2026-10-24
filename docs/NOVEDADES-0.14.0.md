# DJ Elver Galarga 0.14.0 — Taller de preparación y actuación

Entrega de código fuente para Windows. Conserva las herramientas de la 0.13.0 y
amplía preparación de canciones, interpretación con samples y caja de ritmos.

## Acceso

Pulsa LIVE en la cabecera del rack y después «TALLER: loops, sampler, práctica y
ritmos». También puedes abrir el editor desde el menú de un pad del sampler.
La ventana es redimensionable y tiene pestañas Loops A, Loops B, Sampler,
Práctica y Ritmos.

## Sampler: edición no destructiva por pad

- Afinación de -24 a +24 semitonos. También cambia la velocidad y duración;
  no realiza time stretching ni mantiene la tonalidad original.
- Reproducción hacia atrás, desde el final del recorte al disparar.
- Inicio y final editables como fracción de la muestra (0,25 = 25 %).
- Ataque de 0,2 a 250 ms y salida de 1 a 1.000 ms. La salida actúa al soltar
  un pad en modo Mantener, al parar y al cortar otro pad del mismo grupo.
- Cuatro grupos de corte, además del grupo 0 independiente. Lanzar un pad
  hace salir a los otros del mismo grupo. En modo loop cuantizado, el corte
  ocurre al arrancar; una nueva petición del grupo cancela las pendientes.
- Vista simplificada de la muestra, selección de pad, Disparar, Soltar y
  Parar todos. La vista usa puntos del canal izquierdo; no sustituye un editor
  de onda de alta resolución.
- Restablecer edición del pad devuelve tono, reversa, recorte, envolvente y
  grupo a los valores iniciales. No borra ni modifica el archivo original.

La edición se guarda por pad, junto con el rack. También forma parte de las
escenas LIVE, aunque las escenas siguen conservando las muestras que ya estaban
cargadas. Al crear fragmentos mediante el troceador de la 0.13 se restablece la
edición de los ocho pads de destino, para que los trozos se reproduzcan completos.
Los cambios durante la reproducción se aplican al siguiente bloque de audio; no
se han añadido automatizaciones suaves al cambio de dirección o afinación.
Los loops del sampler mantienen su cuantización previa por bloques y no se
estiran automáticamente para ajustar su duración al tempo.

## Ocho loops guardados por canción

En Loops A o Loops B, activa un loop en el plato y pulsa Guardar en una de las
ocho memorias. Guardar sustituye esa memoria. Lanzar recupera la región exacta,
activa el loop y pone el plato en reproducción. Vaciar elimina esa memoria.

Las posiciones se conservan en segundos en rack.json, asociadas a la ruta del
archivo. No modifica audio ni etiquetas. Si mueves o renombras el archivo, las
memorias no se trasladan automáticamente. Los cambios posteriores de rejilla
no desplazan estas regiones guardadas. Los loops que no caben en el archivo
actual se rechazan.

Clic derecho en Lanzar permite aprender un botón MIDI para esa memoria y ese
plato. Las ocho acciones están también en la lista de asignaciones MIDI.

El taller permite mover el loop activo ±1, ±4 o ±16 pulsos. El transporte ahora
limita el desplazamiento en los extremos del archivo, conservando su longitud.
Salir del loop mantiene la reproducción. Si un plato tiene SYNC activo, este
conserva su comportamiento habitual al realizar saltos.

## Práctica y diagnóstico

- Metrónomo del plato A o B exclusivamente en el bus de preescucha, con nivel
  ajustable y acento al principio de cada compás de 4/4.
- Requiere un plato sonando con BPM y rejilla. Configura auriculares en la
  aplicación y lleva su mezcla hacia CUE. El clic se mezcla con la preescucha.
- El clic no entra en el master, grabación ni captura retrospectiva. Al apagarlo,
  puede quedar la breve señal ya almacenada en el limitador de auriculares.
- Se inicia apagado al abrir el programa; no se guarda su activación en escenas.
- Diagnóstico del error de fase publicado por SYNC, expresado en milisegundos,
  y estado alineado/corrigiendo. Es diagnóstico del motor, no una puntuación del
  DJ ni una medición de latencia acústica del equipo.

## Caja de ritmos: variación y herramientas de patrón

- Probabilidad independiente de 0 a 100 % para cada una de las ocho voces.
  Decide si se ejecuta cada golpe programado. Por defecto es 100 %.
- Variación aleatoria de intensidad de hasta el 50 %, con límite de velocidad.
  No altera el tempo ni añade variación temporal. Por defecto está apagada.
- Generador de patrones de 16 o 32 pasos con semilla y densidad. La misma semilla,
  densidad y longitud producen el mismo patrón. No usa IA ni conexión a Internet.
- Rotar un paso adelante o atrás, conservando el contenido del patrón.
- Copiar el patrón elegido al siguiente, incluida su longitud.
- Deshacer una edición de patrón realizada desde este taller. Guarda el estado
  anterior a la última generación, rotación o copia; no es un historial global
  y no deshace cambios posteriores realizados en otros paneles.

Las probabilidades y la variación de intensidad se guardan en el rack y en las
escenas. Las modificaciones del patrón seleccionado pueden oírse mientras suena;
para prepararlo sin afectar a la actuación, edita un patrón que no esté sonando.

## Validación

Pruebas portables ejecutadas con g++ C++20:

- tests/workbench.cpp: recorte y límites de muestra; reversa; afinación; ataque
  y salida; grupos de corte; reproducción simultánea; parámetros no finitos;
  rechazo de muestras incoherentes; probabilidad de disparo; variación real de
  dinámica; generación reproducible; rotación reversible; recuperación exacta
  de loops y desplazamiento limitado en los extremos.
- Integración del metrónomo con el motor real: llega a los canales de auriculares,
  queda fuera del master, de la grabación y de la captura retrospectiva, y se
  apaga tras la latencia de anticipación del limitador.
- Ausencia de reservas de memoria durante las llamadas de audio vigiladas.
- Regresiones anteriores: tests/live.cpp, tests/performance.cpp y
  tests/retrospective.cpp.
- Comprobación de sintaxis de los archivos de interfaz afectados. Las llamadas
  WinAPI se declararon para la comprobación; no se enlazó una aplicación Windows.

Ejecutar las pruebas portables: python tools/probar_live.py
CMake incluye rdj_workbench_tests en CTest. Para compilar Windows, consulta
COMPILAR.md y el script compilar.ps1 de la raíz.

Pendiente: compilación y enlace completos de Windows, revisión visual/interactiva,
pruebas de sesión larga y validación con controladoras y salidas de audio reales.
La comprobación del motor no acredita esos puntos. Esta entrega no incluye un EXE.
