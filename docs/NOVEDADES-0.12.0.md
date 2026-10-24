# Novedades 0.12.0 — Captura retrospectiva

En RACK → SAMPLER, haz clic derecho en un pad y abre «Capturar mezcla reciente al pad».
Elige 5, 10, 20 o 30 segundos. Sustituye la muestra del pad elegido y guarda un WAV
estéreo flotante en la carpeta de grabaciones, añadido a la lista Grabaciones.
El archivo permite restaurar la asignación al abrir de nuevo el programa.
Si acaba de arrancar el audio, se recupera solo el historial disponible.

La fuente es el master después de efectos, volumen y limitador, antes del reparto
entre altavoces y auriculares. No incluye la preescucha. No requiere activar REC.
El historial se reinicia cuando se reinicializa el motor de audio y ocupa unos
12 MB a 48 kHz. La captura aplica fundidos de 3 ms para evitar clics.
La captura es por segundos: no se alinea automáticamente al compás ni estira el tempo.

Seguridad: almacenamiento por fotogramas atómicos, copia validada contra sobrescritura,
sin reservas, bloqueos ni escritura a disco en el hilo de audio. La copia y guardado
WAV se realizan en la interfaz; en discos lentos el guardado puede causar una pausa
visual breve. Los pads con un archivo cargándose no aceptan captura.

Validación: prueba portable de historial vacío, longitud parcial, vuelta de búfer,
coherencia estéreo, captura concurrente y reinicio al cambiar frecuencia. Comprobación
de sintaxis C++20 del motor. Esta entrega es código fuente: no se ha compilado ni
probado la interfaz o el ejecutable Windows y no se ha probado con controladoras.

Prueba portable: g++ -std=c++20 -O2 -pthread tests/retrospective.cpp -o retrospective_test
Ejecutar: ./retrospective_test
