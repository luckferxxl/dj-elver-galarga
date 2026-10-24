# Ramiro DJ — Especificación de funciones y estado

Lista redactada a partir de los 18 módulos del encargo, usando VirtualDJ como referencia funcional (investigación con REA en `INVESTIGACION.md`).

**Estado**
- ✅ Implementada y verificada con pruebas automáticas (`rdj_tests` y/o `RamiroDJ --autotest` con audio real).
- 🟡 Implementada, pero falta comprobarla con hardware o a oído (ver "Pendiente de comprobar" en `PROGRESO.md`).
- ⏳ Pendiente: **no aparece en la interfaz**.

Resumen (0.5, 221 funciones): **201 ✅ · 12 🟡 · 8 ⏳**

Las filas 🟡 con una parte automática (por ejemplo, "motor ✅ / hardware 🟡") cuentan como 🟡.

## 1. Barra superior, configuración e interfaz
| # | Función | Estado |
|---|---|---|
| 1 | Identidad visual propia (logotipo «/// RAMIRO DJ», fuentes Barlow Semi Condensed y Hanken Grotesk) | ✅ |
| 2 | Configuración de audio (dispositivo, búfer, frecuencia, exclusivo, auriculares) | ✅ |
| 3 | Ventana del mando MIDI | ✅ |
| 4 | Preferencias | ✅ |
| 5 | Programador de eventos | ✅ |
| 6 | Acerca de, con las licencias de terceros | ✅ |
| 7 | Grabar mezcla desde la barra, con tiempo y parpadeo | ✅ |
| 8 | Estado del audio: dispositivo, sistema, Hz, latencia y CPU | ✅ |
| 9 | Ventana redimensionable; tamaño y maximizado guardados | ✅ |
| 10 | Escalado según el DPI de la pantalla | 🟡 |

## 2. Ondas
| # | Función | Estado |
|---|---|---|
| 11 | Onda de detalle por plato coloreada por bandas (graves, medios, agudos) | ✅ |
| 12 | Vista general de la pista con cabezal | ✅ |
| 13 | Zoom de 2 a 32 s con la rueda | ✅ |
| 14 | Rejilla de pulsos con número de compás | ✅ |
| 15 | Marcas de cue y hot cues en las ondas | ✅ |
| 16 | Región de loop sombreada | ✅ |
| 17 | Búsqueda con clic o arrastre en la vista general | ✅ |
| 18 | Scratch arrastrando la onda | 🟡 |
| 19 | Parte ya reproducida atenuada | ✅ |
| 20 | Ondas A y B apiladas para cuadrar a ojo | ✅ |

## 3. Platos A y B
| # | Función | Estado |
|---|---|---|
| 21 | Dos platos independientes, cada uno con su lector y su cadena de procesado | ✅ |
| 22 | Cargar arrastrando desde el Explorador | ✅ (lógica) / 🟡 (gesto real) |
| 23 | Cargar desde la biblioteca: arrastrar, doble clic o menú | ✅ |
| 24 | Confirmación al cargar sobre un plato que suena | ✅ |
| 25 | Título, artista, BPM efectivo, pitch y tonalidad | ✅ |
| 26 | Expulsar | ✅ |
| 27 | Mensajes de error de carga sin cortar lo que suena | ✅ |
| 28 | Barra de progreso de carga y análisis | ✅ |
| 29 | Tiempo transcurrido y restante | ✅ |
| 30 | Compatibilidad armónica con el otro plato | ✅ |

## 4. Jog wheels y scratch
| # | Función | Estado |
|---|---|---|
| 31 | Plato virtual que gira a 33⅓ rpm | ✅ |
| 32 | Scratch con el ratón girando el plato | 🟡 (sensación) |
| 33 | Aro con el progreso de la pista | ✅ |
| 34 | Marca del cue en el aro | ✅ |
| 35 | Jog MIDI: aro = empuje de tempo | ✅ |
| 36 | Jog MIDI: tocar = scratch (modo vinilo, 720 pasos por vuelta) | ✅ |
| 37 | Jog MIDI en pausa = búsqueda fina | ✅ |
| 38 | Sensibilidades del aro, del scratch y de la búsqueda | ✅ |
| 39 | Velocidad de scratch suavizada (8 ms) | ✅ |
| 40 | Activar o desactivar el modo vinilo | ✅ |

## 5. Reproducción, CUE, SYNC y velocidad
| # | Función | Estado |
|---|---|---|
| 41 | Play y pausa con fundido anticlic; la pausa deja la posición exacta | ✅ |
| 42 | CUE tipo CDJ: fijar, escuchar mientras se mantiene, volver | ✅ |
| 43 | Búsqueda y salto sin chasquidos | ✅ |
| 44 | Pitch ±8/16/50 % | ✅ |
| 45 | Empuje temporal ±4 % | ✅ |
| 46 | SYNC de tempo y fase (0,00 ms de desfase) | ✅ |
| 47 | El plato sincronizado sigue el tempo del otro | ✅ |
| 48 | Ampliación automática del rango de pitch al sincronizar | ✅ |
| 49 | Key lock: tempo sin cambio de tono (Signalsmith Stretch) | ✅ |
| 50 | Transposición ±6 semitonos e "Igualar tono" | ✅ |

## 6. Hot cues
| # | Función | Estado |
|---|---|---|
| 51 | 8 hot cues por plato | ✅ |
| 52 | Guardar con un clic | ✅ |
| 53 | Saltar y reproducir | ✅ |
| 54 | Borrar con clic derecho o SHIFT + pad | ✅ |
| 55 | Color por hot cue | ✅ |
| 56 | Tiempo en el pad | ✅ |
| 57 | Se guardan por pista | ✅ |
| 58 | Cuantización al pulso | ✅ |
| 59 | Marcas en las ondas | ✅ |
| 60 | Hot cues por MIDI con luces | ✅ (MIDI) / 🟡 (luces) |

## 7. Loops
| # | Función | Estado |
|---|---|---|
| 61 | Loop automático de 1/4 a 16 pulsos | ✅ |
| 62 | Entrada y salida manual | ✅ |
| 63 | Mitad y doble | ✅ |
| 64 | Salir y reloop | ✅ |
| 65 | Cuantizado al pulso | ✅ |
| 66 | Vuelta del loop sin cortes (fundido) | ✅ |
| 67 | Salto de 1 y 4 pulsos | ✅ |
| 68 | Loops con key lock sin desfase | ✅ |
| 69 | Indicador LOOP n | ✅ |
| 70 | Guardar loops en un hot cue | ⏳ |

## 8. Efectos
| # | Función | Estado |
|---|---|---|
| 71 | Eco sincronizado al BPM | ✅ |
| 72 | Flanger sincronizado | ✅ |
| 73 | Reverberación | ✅ |
| 74 | Cola al apagar (eco y reverberación) | ✅ |
| 75 | Cantidad | ✅ |
| 76 | Pulsos o tamaño | ✅ |
| 77 | Un efecto por plato | ✅ |
| 78 | Filtro paso bajo / paso alto resonante | ✅ |
| 79 | Efecto por MIDI (encender y cantidad) | ✅ |
| 80 | Más efectos (bitcrusher, gate, roll, brake) | ✅ (0.11: más los once nuevos de la pestaña FX) |

## 9. Mezclador
| # | Función | Estado |
|---|---|---|
| 81 | Ganancia de -12 a +12 dB | ✅ |
| 82 | EQ aislador de 3 bandas con corte total | ✅ |
| 83 | Volumen de canal | ✅ |
| 84 | Crossfader con 3 curvas | ✅ |
| 85 | Volumen master | ✅ |
| 86 | Medidores de canal con retención y recorte | ✅ |
| 87 | Medidores master | ✅ |
| 88 | Limitador: la salida nunca supera -0,3 dBFS | ✅ |
| 89 | Indicador LIMIT | ✅ |
| 90 | Rampas de parámetros sin zipper | ✅ |

## 10. Auriculares
| # | Función | Estado |
|---|---|---|
| 91 | Preescucha (PFL) por canal | ✅ |
| 92 | Volumen de auriculares | ✅ |
| 93 | Mezcla preescucha/master | ✅ |
| 94 | Salida por los canales 3-4 del mismo dispositivo | ✅ (motor) / 🟡 (hardware) |
| 95 | Salida por otro dispositivo | ✅ (motor) / 🟡 |
| 96 | División mono (izquierda preescucha, derecha master) | ✅ |
| 97 | Limitador de auriculares | ✅ |
| 98 | Compensación de deriva entre dos dispositivos | 🟡 |
| 99 | Controles desactivados con explicación si no hay salida | ✅ |
| 100 | PFL por MIDI | ✅ |

## 11. Grabación y retransmisión
| # | Función | Estado |
|---|---|---|
| 101 | Grabar el master en WAV de 16 bits con tramado | ✅ |
| 102 | WAV de 32 bits en coma flotante (idéntico bit a bit) | ✅ |
| 103 | Hilo propio, sin E/S en el hilo de audio | ✅ |
| 104 | Aviso de muestras perdidas | ✅ |
| 105 | Lista *Grabaciones* automática | ✅ |
| 106 | Abrir la carpeta de grabaciones | ✅ |
| 107 | Grabar a una hora (programador) | ✅ |
| 108 | La grabación se puede reproducir en un plato | ✅ |
| 109 | **Retransmisión Icecast/Shoutcast** | ⏳ |
| 110 | Grabación en MP3 | ⏳ |

## 12. Explorador y base de datos SQLite
| # | Función | Estado |
|---|---|---|
| 111 | Biblioteca en SQLite (`biblioteca.db`) | ✅ |
| 112 | Migración automática desde `biblioteca.json` | ✅ |
| 113 | Copia de seguridad diaria (se guardan 7) | ✅ |
| 114 | Carpetas escaneadas en segundo plano | ✅ |
| 115 | Etiquetas ID3v2/v1, Vorbis y RIFF | ✅ |
| 116 | Búsqueda sin tildes y por varias palabras | ✅ |
| 117 | Ordenar por artista, título, BPM, tono, duración o archivo | ✅ |
| 118 | Archivos no encontrados marcados | ✅ |
| 119 | Análisis de BPM y tono por lotes, con prioridad baja | ✅ |
| 120 | Mostrar en el Explorador | ✅ |

## 13. Playlists, Sidelist y Automix
| # | Función | Estado |
|---|---|---|
| 121 | Crear, renombrar y eliminar listas | ✅ |
| 122 | Reordenar arrastrando; quitar con Supr | ✅ |
| 123 | Sidelist (lista de preparación) | ✅ |
| 124 | Guardar como… (copia) y Guardar ahora | ✅ |
| 125 | Importar y exportar M3U/M3U8 | ✅ |
| 126 | Historial diario automático | ✅ |
| 127 | Automix con fundido sincronizado | ✅ |
| 128 | Automix: duración del fundido en pulsos, sincronizar, repetir | ✅ |
| 129 | Lista de Automix reordenable, "siguiente en sonar" | ✅ |
| 130 | Automix inteligente (puntos de mezcla por análisis de intro y outro) | ⏳ |

## 14. Información y estadísticas
| # | Función | Estado |
|---|---|---|
| 131 | Panel de información de la pista | ✅ |
| 132 | Reproducciones y última vez | ✅ |
| 133 | Fecha en que se añadió | ✅ |
| 134 | Compatibilidad armónica con lo que suena | ✅ |
| 135 | Totales: pistas, horas, analizadas, reproducidas | ✅ |
| 136 | Histograma de BPM | ✅ |
| 137 | Las más reproducidas | ✅ |
| 138 | Pistas pendientes de análisis | ✅ |
| 139 | Tamaño y ruta del archivo | ✅ |
| 140 | Distribución de tonalidades (Camelot) | ✅ |

## 15. BPM, beatgrid y tonalidad
| # | Función | Estado |
|---|---|---|
| 141 | BPM automático (96 % igual que VirtualDJ en 80 pistas) | ✅ |
| 142 | Fase y primer tiempo de compás | ✅ |
| 143 | BPM editable, ×2 y ÷2 | ✅ |
| 144 | TAP | ✅ |
| 145 | Desplazar la rejilla 1/10 ms | ✅ |
| 146 | "Primer tiempo aquí" | ✅ |
| 147 | Reanalizar | ✅ |
| 148 | Tonalidad automática: 100 % en acordes sintéticos, 28 % igual que VirtualDJ, 60 % compatible | 🟡 precisión limitada |
| 149 | Notación Camelot y compatibilidad | ✅ |
| 150 | Rejilla de tempo variable | ⏳ |

## 16. MIDI y dispositivos de audio
| # | Función | Estado |
|---|---|---|
| 151 | Elegir el dispositivo de salida | ✅ |
| 152 | Frecuencia de muestreo configurable | ✅ |
| 153 | Búfer de 3 a 40 ms | ✅ |
| 154 | Modo exclusivo WASAPI | ✅ |
| 155 | Recuperación ante dispositivo desconectado o inexistente | ✅ |
| 156 | Asistente de asignación paso a paso | ✅ |
| 157 | Aprender y monitor MIDI | ✅ |
| 158 | 14 bits, capa SHIFT y soft takeover | ✅ |
| 159 | Luces del mando | ✅ (mandos reconocidos) / 🟡 (asignaciones propias) |
| 160 | ASIO | ⏳ |
| 222 | Mandos reconocidos al enchufarlos: 144 mapeos de Mixxx incluidos, con botones y luces | ✅ (simulados; sin probar con los aparatos) |
| 223 | Enchufar y desenchufar mandos con el programa abierto | ✅ |
| 224 | Elegir el mapeo de cada mando; mapeos propios en `%APPDATA%\RamiroDJ\mandos` | ✅ |
| 225 | SysEx de entrada y salida | ✅ |

## 17. Preferencias y persistencia
| # | Función | Estado |
|---|---|---|
| 161 | Ajustes guardados automáticamente | ✅ |
| 162 | Rango de búsqueda de BPM | ✅ |
| 163 | Formato de grabación | ✅ |
| 164 | Confirmación de carga | ✅ |
| 165 | Cues, hot cues, rejilla y tonalidad por pista | ✅ |
| 166 | Volúmenes y curva | ✅ |
| 167 | Asignaciones y ajustes del mando | ✅ |
| 168 | Zoom de onda | ✅ |
| 169 | Carpeta de datos aislable (`RDJ_DATA_DIR`) | ✅ |
| 170 | Tamaño de ventana con protección contra valores absurdos | ✅ |

## 18. Programador y recuperación de sesión
| # | Función | Estado |
|---|---|---|
| 171 | Eventos a una hora del día | ✅ |
| 172 | Acciones: grabar, Automix, fundido final, play | ✅ |
| 173 | Fundido final de 15 s y pausa | ✅ |
| 174 | Sesión guardada cada 5 s | ✅ |
| 175 | Recuperación tras un cierre inesperado, con aviso | ✅ |
| 176 | Al abrir: pistas, posiciones, pitch, key lock y mezclador | ✅ |
| 177 | La sesión conserva las pistas que aún se están cargando | ✅ |
| 178 | Copia de seguridad de la biblioteca | ✅ |
| 179 | Cada evento se ejecuta una vez al día | ✅ |
| 180 | Programador con la aplicación cerrada (servicio de Windows) | ⏳ |

## 19. Ampliación 0.4: diseños, Camelot y Separador
| # | Función | Estado |
|---|---|---|
| 181 | Diseños de color seleccionables (⚙ → Diseño y Preferencias; seis desde la 0.5) | ✅ |
| 182 | Barra superior con modos MEZCLA / BIBLIOTECA / SEPARADOR y menú ⚙ | ✅ |
| 183 | Tonalidad en código Camelot + nombre en español (`8A · La menor`) | ✅ |
| 184 | Notación de tonalidad elegible (Camelot, musical, español) | ✅ |
| 185 | Detector de tonalidad HPCP con perfil Temperley y reanálisis automático | ✅ |
| 186 | Instalación del motor de separación (Demucs) desde fuentes oficiales | ✅ |
| 187 | Separar una canción arrastrándola, desde un archivo o desde la biblioteca | ✅ |
| 188 | Cola de separación con progreso, sin bloquear la mezcla | ✅ |
| 189 | 6 pistas (voz, batería, bajo, guitarra, piano, otros) o 4 pistas | ✅ |
| 190 | Reproductor de stems: ondas, M/S, volumen, medidores, salto, auriculares | ✅ |
| 191 | Llevar un stem, la acapella, el instrumental o el original a un plato | ✅ |
| 192 | Exportar a WAV un stem o la mezcla de stems que suena | ✅ |
| 193 | Lista de canciones separadas: abrir, carpeta, separar de nuevo, eliminar | 🟡 |

## 20. Ampliación 0.5: diseño nuevo y navegador tipo VirtualDJ
Comprobadas con `rdj_tests` (carpetas y carátulas), `--autotest` (pasos «Navegador») y una prueba de interfaz con ratón y teclado simulados en Wine (clics, doble clic, arrastrar, Ctrl+clic, Supr, buscar). Falta repasarlas con el ratón en Windows.

| # | Función | Estado |
|---|---|---|
| 194 | Diseño «Ramiro» por defecto: azul noche, plato A turquesa y B coral, paneles planos | ✅ |
| 195 | Fuente Barlow Semi Condensed (OFL) para títulos, BPM, tiempos y botones | ✅ |
| 196 | Iconos vectoriales propios (no dependen de las fuentes del sistema) | ✅ |
| 197 | Carátulas de ID3 (APIC/PIC), FLAC (PICTURE), OGG (METADATA_BLOCK_PICTURE) y cover/folder/front.jpg, en segundo plano | ✅ |
| 198 | Cabecera del plato: carátula, título, favorito, artista, tiempos, BPM grande, tonalidad en insignias y etiquetas de estado | ✅ |
| 199 | Plato giratorio con BPM y pitch en el centro y aro de progreso | ✅ |
| 200 | Transporte CUE · PLAY · SYNC · ▾ · hot cues 1-4 · ⋯ (los hot cues se ocultan si no caben) | ✅ |
| 201 | Pads por pestañas: CUES 1-8, LOOP, SALTO (±1/4/8/16), REJILLA, TONO, EFECTO | ✅ |
| 202 | Ondas del color de cada plato (con núcleo más claro) o por bandas, a elegir | ✅ |
| 203 | Barra superior: medidor master, piloto LIM, volumen master, auriculares, grabar y ⚙ | ✅ |
| 204 | Modo BIBLIOTECA: navegador a pantalla completa con un resumen de cada plato | ✅ |
| 205 | Árbol «Este equipo»: Escritorio, Música, Descargas, Documentos, Vídeos y unidades, desplegable | ✅ |
| 206 | Abrir cualquier carpeta: sus pistas aparecen y entran en la colección; opción de leer subcarpetas | ✅ |
| 207 | Carpetas favoritas con estrella (escaneo completo) | ✅ |
| 208 | Colección, Favoritos, Historial por días y Grabaciones en el árbol | ✅ |
| 209 | Sugerencias (como las *Ideas* de VirtualDJ): BPM ±8 % y tonalidad compatible con lo que suena | ✅ |
| 210 | Listas en el árbol: crear, renombrar, duplicar, exportar M3U8, vaciar, eliminar; soltar pistas encima | ✅ |
| 211 | Lista central con carátula, título, artista, remix, BPM, tonalidad, duración, reproducciones, fecha y archivo | ✅ |
| 212 | Columnas ordenables, ocultables y movibles, que se recuerdan | 🟡 |
| 213 | Selección múltiple (Ctrl, Mayús, recuadro) y arrastre de varias pistas a la vez | ✅ |
| 214 | Reordenar listas arrastrando o con ↑ ↓; Supr quita; Intro carga | ✅ |
| 215 | Buscar en la vista o en toda la colección | ✅ |
| 216 | Pistas cargadas marcadas con el color del plato; las ya sonadas hoy, atenuadas con ✓ | ✅ |
| 217 | Sidelist (lista contigua) con pestañas abajo: Sidelist, Automix y Estadísticas | ✅ |
| 218 | Panel de detalles: carátula, tonalidad, Camelot, onda, datos y compatibilidad armónica | ✅ |
| 219 | Paneles ocultables y con ancho ajustable; se ocultan solos si la ventana es estrecha | ✅ |
| 220 | Menú de pista: cargar, Sidelist, Automix, listas, favoritos, separar, ir a su carpeta, Explorador | ✅ |
| 221 | Favoritos con el corazón del plato y desde el navegador | ✅ |

## 20. Rack de hardware virtual (0.9, primera fase)
| # | Función | Estado |
|---|---|---|
| 226 | Estación de efectos: 4 ranuras reordenables, 9 efectos, intensidad, mezcla, tiempos con el BPM | ✅ |
| 227 | Macro para subidas y transiciones | ✅ |
| 228 | Inserción y envío/retorno; fuente A, B o mezcla por módulo | ✅ |
| 229 | Pad XY con dos destinos, vuelta suave y HOLD | ✅ (ratón) / 🟡 (táctil sin probar) |
| 230 | Aislador de tres bandas con corte total y cruces ajustables | ✅ |
| 231 | Loop station: 1–8 compases cuantizados, capas, deshacer y WAV | ✅ |
| 232 | EFECTOS OFF con fundido y colas | ✅ |
| 233 | Presets del rack (5 incluidos y propios) | ✅ |
| 234 | MIDI del rack (Aprender, también en mandos con mapeo de Mixxx) | ✅ (sin probar con mando físico) |
| 235 | Efectos por stems, grabación de movimientos | ⏳ pendiente |

## 21. Ampliación 0.10: SYNC preciso e instrumentos
| # | Función | Estado |
|---|---|---|
| 236 | Rejilla automática a la muestra (< 1 ms) y primer tiempo de compás por bombo, caja y acentos | ✅ (8/9 en el banco de pruebas) |
| 237 | Detección de tempo doble (drum & bass) | ✅ |
| 238 | Re-medición automática de las rejillas antiguas al cargar (cues pegados incluidos) | ✅ |
| 239 | Caja de ritmos: 8 voces sintetizadas, 8 patrones de 16/32 pasos, acento, swing, 9 estilos | ✅ |
| 240 | Sintetizador de bajos acid con secuenciador, acento, slide y tónica del plato | ✅ |
| 241 | Sampler de 16 pads: disparo, mantener, loop en el pulso, archivos propios y 8 sonidos incluidos | ✅ |
| 242 | Reloj de instrumentos ligado al plato (o tempo propio), arranque en el compás | ✅ |
| 243 | Instrumentos como fuente de los módulos del rack | ✅ |
| 244 | Efectos nuevos: eco de cinta, reverb larga, bitcrusher, repetidor | ✅ |
| 245 | MIDI de los instrumentos (tocar, patrón, 16 pads) con luces | ✅ (sin probar con mando físico) |

## 22. Ampliación 0.11: efectos nuevos
| # | Función | Estado |
|---|---|---|
| 246 | Riser de ruido sincronizado (1-8 compases) | ✅ |
| 247 | Spiral: eco que sube de tono | ✅ |
| 248 | Sidechain (bombeo) en la rejilla | ✅ |
| 249 | Shimmer (reverb con octava) | ✅ |
| 250 | Vinilo lo-fi y teléfono / radio | ✅ |
| 251 | Chorus, trémolo y auto-pan | ✅ |
| 252 | Freeze y slicer (8 patrones, en la rejilla) | ✅ |
| 253 | Los once también en la pestaña FX de cada plato | ✅ |
| 254 | Ruleta de color del mezclador: FILTER, SPACE, NOISE, CRUSH | ✅ |
| 255 | Desplegable de efectos por familias, con descripción | ✅ |
| 256 | Presets del rack con los efectos nuevos (5) | ✅ |
| 257 | Botón QUITAR FX: cuántos efectos y filtros quedan puestos, cuáles, y quitarlos de un clic (también por MIDI) | ✅ (0.11.1) |
| 258 | Botón de efectos en cada plato: qué efecto tiene puesto (siempre a la vista), quitar solo los de ese plato, punto en las pestañas FX y VINILO (también por MIDI) | ✅ (0.11.3) |
