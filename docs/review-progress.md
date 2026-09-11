# Aplicación de la revisión conjunta

Inicio: 2026-09-11. Base: `299915b45d8e4c02099fe194dac2308c7d4a3434`.
Fuente: `ASTRA_CLAUDE_REVISION_CONJUNTA_2026-09-11.md` en la raíz del workspace.
Run de Orca: `run_7b18a487f130`.

## Método y límite de rendimiento

Cada cambio se implementa, se comprueba, se revisa con Claude mediante una tarea
de Orca y se mide antes del commit. El coordinador resuelve las observaciones de
la revisión. El núcleo de C-CFR queda fuera del alcance.

Los ejecutables originales se conservan en `build/review-baseline/release`.
Cada candidato usa su propio directorio de construcción. Las comparaciones
alternan el orden de ejecución y usan tres repeticiones, semilla 7 y las mismas
cargas. Se compara con el padre del cambio y con la base inicial cuando procede.
Los cambios que no afectan una ruta se documentan como tales, sin atribuirles una
mejora por fluctuaciones de tiempo. Las mediciones se ejecutan sin compilaciones,
pruebas o benchmarks propios simultáneos.

Una mediana de tiempo al menos un 20 % mayor, o un pico de RSS al menos un 25 %
mayor, activa una comprobación adicional de cinco pares en la carga afectada.
Si se confirma una regresión grave atribuible al cambio, se conserva un parche,
se retira el candidato del árbol de trabajo y se espera la decisión del usuario.
Los tiempos muy cortos se amplían antes de juzgar diferencias relativas.
Una alteración del número de nodos o de la semántica se explica por separado;
la corrección funcional no exime del límite de rendimiento.

La [referencia inicial](review-performance/baseline.json) recoge compilador,
configuración, comandos y medidas. Los JSON por cambio guardan sus comparaciones.
Para cambios que preservan el algoritmo se comprueban también los checkpoints,
valores y contadores deterministas. La evaluación de la política media y el
rendimiento del bot desplegado son métricas diferentes.

## Estado por bloque

| Bloque | Trabajo | Estado |
|---|---|---|
| J01 | Modelo genérico sin información oculta y compatibilidad histórica | Pendiente |
| J02 | Recuento diferido en el simulador de dos/cuatro jugadores | Completado |
| J03 | Reintento tras fallo al guardar una diagonal | Completado |
| J04 | Rechazar TXT truncado a su cabecera | Completado |
| J05 | Excluir candidatos de precarga incompatibles con los dejes | Completado |
| J06 | Usar operaciones de confianza en la evaluación exacta local | Completado |
| J07 | Mezclar bits en la tabla de mejor respuesta | Pendiente |
| J08 | Reutilizar celdas terminadas de una diagonal interrumpida | Pendiente |
| J09 | Región opcional de continuaciones cerrada por incrementos y giro | Pendiente |
| J10 | Incertidumbre y presupuesto de evaluación | Pendiente |
| J11 | Precisar el alcance del diagnóstico de repartos fijos | Pendiente |
| J12 | Evaluar partidas con la política desplegada del bot | Pendiente |
| J13 | Conservar disponibilidad de política en TXT | Pendiente |
| J14 | Rechazar explícitamente el adaptador incompatible con dos muses | Completado |
| J15 | Rechazar fases de azar/terminal en la clave rápida | Completado |
| J16 | Gramática explícita para opciones enteras sin signo | Completado |
| J17 | Bloqueo ligado al proceso y contrato de durabilidad | Pendiente |
| J18 | Reducir el coste de codificación y generación de candidatos | Pendiente |
| J19 | Distribución y memoria de trabajadores por diagonal | Pendiente |
| J20 | Invalidar objetos al cambiar la configuración | Pendiente |
| J21 | Evitar enlaces sin cambios conservando dependencias reales | Pendiente |
| J22 | Dependencias, portabilidad y separación del simulador | Pendiente |
| J23 | Matriz de pruebas, observabilidad y contratos menores | Pendiente |

## J03: reintento de publicación

La publicación temporal conserva las celdas anteriores en el búfer de la
diagonal. Si `save_diagonal` falla, se restauran esas celdas; las diagonales
guardadas antes permanecen intactas. Un reintento vuelve a entrenar y guardar la
diagonal fallida. Los callbacks deben admitir reemplazar sus salidas anteriores.
La recuperación desde un proceso nuevo corresponde a J08.

La prueba nueva provocó doce fallos antes de corregir el código. Cubre uno y dos
trabajadores, metadatos de celdas no listas, conservación de la diagonal superior,
reintento efectivo, ausencia de repetición tras éxito y equivalencia final con un
entrenamiento sin fallo. La batería `make -j4 test` pasó después de la corrección.

[Rendimiento](review-performance/J03.json): 45 celdas desde 35:35, 1.000
iteraciones y 10.000 muestras por celda. Archivos idénticos en todas las
ejecuciones. Con un trabajador: 3,8302 → 3,8598 s (+0,77 %). En tres pares con
cuatro trabajadores: 2,2100 → 2,1856 s (−1,10 %), RSS 88.288 → 88.364 KiB
(+0,09 %). Sin regresión grave; estas pequeñas diferencias no demuestran una
aceleración. No cambia la ruta de entrenamiento de los otros tres formatos.

Revisión Orca: tarea `task_435ad0808077`, dispatch `ctx_f0247b89d0bb`.
Claude: **APPROVE**, mensaje `msg_56e843db31f9`. Se corrigieron los detalles
de sangría, redacción del contrato e identificador del dispatch indicados por
el revisor. La prueba adicional de ampliación con fallo queda como sugerencia
opcional; la restauración no depende de la modalidad de entrenamiento.

## J04: cabeceras TXT y pérdida de todas las filas

Se comprueban variante, iteraciones, recuento de nodos, límites numéricos y final
de cabecera para estrategias genéricas y por marcador. Un recuento positivo
exige filas; un recuento cero exige que no las haya. La apertura conserva su
coste independiente del número de filas. El truncamiento de un archivo no vacío
por filas completas requiere un contrato adicional de integridad; queda
documentado y se coordinará con la versión de TXT de J13.

La prueba de cabecera truncada falló contra el lector anterior. `make -j4 test`
pasa con la corrección, incluidas las pruebas genéricas e híbridas. Se admite un
almacén explícitamente vacío y se conserva su respaldo por ausencia de datos.

El SHA-256 del ejecutable `mus-solver` es idéntico al de J03:
`62ad47f341a6d44f140d44492dd3a0bbe32742ef8843aab5c45966664ab615c4`.
No cambia el entrenamiento. Se mide por separado la apertura de TXT.
[Apertura](review-performance/J04.json): tres pares de 200.000 aperturas por
ejecución. Genérica: 0,3027 → 0,2911 s (−3,82 %); celda: 0,2984 → 0,3021 s
(+1,23 %). RSS sin aumento observable. Son archivos con caché caliente y procesos
pequeños; no se infiere una mejora estable ni una medida de partidas completas.
No se activa el límite de regresión.

Revisión Orca: tarea `task_7b4ac7c8d699`, dispatch `ctx_a6eb86a6a5dc`.
Claude: **APPROVE**, mensaje `msg_14473ef1c4fa`. Se añadieron los casos sugeridos
de variante desconocida y texto sobrante, se dio nombre propio al fixture vacío
y se documentó el contrato en la cabecera interna. Las pruebas de CLI del bot
se repitieron tras estos ajustes.

## J05: máximos históricos compatibles con los pagos

El máximo histórico se acota por el total posible menos el menor de los pagos
ya recibidos por cada jugador. Los puntos públicamente cobrados por el rival
no pueden atribuirse al otro jugador al distribuir los contratos pendientes.

La regresión construye un deje de un punto para cada jugador antes de pares.
Solo puede existir un máximo histórico de uno. Se carga una política correcta
con masa 1 y otra incompatible, favorable al órdago, con masa 99. La prueba
comprueba la lista de candidatos, la capacidad mínima del búfer y la política
final de la precarga. Falló cuatro comprobaciones antes de la corrección.

Pasan `make -j4 test` y las baterías de núcleo y rutas rápidas compiladas con
`MUS_MAX_MUSES=2` en `build/review-j05-two`. Se comparan rendimiento y resultados
sin precarga, y el tiempo/memoria de entrenar tablas con precarga. En este
último caso puede cambiar la política porque se elimina información incompatible.
[Rendimiento](review-performance/J05.json): sin precarga se conservan exactamente
los checkpoints, valores y contadores de los cuatro marcadores de referencia.
Con precarga, la mediana para 45 celdas pasa de 5,8819 a 5,8406 s (−0,70 %), y el
RSS de 153.052 a 153.264 KiB (+0,14 %). Cambian 31 checkpoints y `values.txt`;
cada versión es determinista en sus tres ejecuciones. No se activa el límite de
regresión. Los archivos diferentes no demuestran por sí mismos mayor tasa de
victoria del bot.

Claude: **APPROVE**, tarea `task_c253677971f6`, dispatch `ctx_e1ceb4a7d0e0`,
mensaje `msg_c373e2326959`. Se añadió su caso opcional de pagos a ambos jugadores
más un contrato de pares pendiente, que exige el intervalo completo 3..6.
Las pruebas del núcleo se repitieron con uno y dos muses tras añadirlo.

## J15: contrato de la clave rápida

La especialización de `ordago`, también utilizada por `ordago-score`, exige una
fase de decisión antes de calcular la clave. En reparto, reposición o terminal
devuelve error y conserva la salida. Se mantienen las comprobaciones ligeras de
fase; no se repite la validación completa del estado en cada consulta.

La prueba recorre todos los tipos de fase de los cuatro formatos con estados
producidos por acciones públicas legales. Compara estado de retorno y clave
entre API pública y rápida, y verifica que los errores conservan la salida.
Falló contra la implementación anterior. Se valida con uno y dos muses.
Pasan `make -j4 test` y las pruebas de núcleo/rutas rápidas con dos muses.
Revisión Orca: tarea `task_b1d65ad7be49`, dispatch `ctx_a82dff9f1f47`.
Claude: **APPROVE**, mensaje `msg_d20705d3566d`. Se mantienen los clasificadores
de fase compartidos; no se añade otro helper para una condición que en la ruta
pública también depende del formato.

[Rendimiento](review-performance/J15.json): medianas de tiempo genérico −0,24 %
en single-bet, +1,68 % en raises-to-eight y −0,01 % en ordago; el mayor aumento
de RSS es +1,56 %. En las cuatro cargas de score no aparece aumento de tiempo
de entrenamiento; se conservan los checkpoints, valores y contadores.
No se activa el límite de regresión. Estas diferencias pequeñas no prueban una
aceleración causada por la comprobación añadida.

## J16: enteros sin signo del CLI

La gramática requiere uno o más dígitos ASCII antes de convertir. Así no se
aceptan signos ocultos tras espacios ni signos positivos. Se conservan los
límites, los ceros iniciales, la semilla cero y las cantidades de trabajo
estrictamente positivas. También se aplica a los componentes de `--score`.

Las pruebas usan `--help` para validar entradas sin iniciar trabajos enormes.
Cubren las ocho opciones que comparten los parsers, signos, blancos, caracteres
no numéricos, vacío, desbordamiento y límites válidos de semilla. La primera
reproducción falló con `--iterations ' -1'` antes de corregir el parser.
Al inspeccionar el parser del bot se encontró el mismo defecto en `--seed`.
Se exige un dígito inicial y consumo completo de la cadena, preservando el
intervalo 0..UINT64_MAX. La prueba adicional también falló antes de corregirlo.
La modificación del bot no cambia el ejecutable del solver ya medido.
Revisión Orca: tarea `task_578c0096685d`, dispatch `ctx_e4da62aa3d94`.
Esa primera aprobación cubrió el solver. La revisión ampliada
`task_1601fdec441a` / `ctx_b6ce7b5e9855` pidió cerrar stdin en las pruebas de
semillas válidas del bot, para evitar bloqueos desde una terminal interactiva.
Se corrigió con `input=b''` y se comprobó que la batería termina manteniendo
abierta su propia entrada. Claude dio **APPROVE** en la verificación final
`task_ae173bc4919b` / `ctx_e73ec63937c9`, mensaje `msg_e754d3b4cf0b`.
También se incorporaron los casos sugeridos de cero en tamaños y límites de
marcador. Pasan la batería completa y las pruebas finales de ambos CLIs.

[Rendimiento](review-performance/J16.json): tiempo genérico −0,57 % / −0,35 % /
+1,58 % para single-bet / raises-to-eight / ordago, con contadores iguales.
La tabla score mantiene archivos idénticos y pasa de 3,8793 a 3,7095 s (−4,38 %).
En el diagnóstico del bot ampliado a 3.000 repartos: +5,35 % de tiempo total,
+12,76 % de latencia mediana y −0,17 % de RSS. Se conservan cobertura y valores
del diagnóstico. Se registra el aumento observado, que no alcanza el umbral de
regresión grave; no se atribuye una mejora o empeoramiento de política a estos
tiempos. El diagnóstico no mide partidas completas.

## J14: configuración de muses del adaptador

El protocolo actual del adaptador exige un máximo de un mus. La creación y las
aperturas rechazan explícitamente otro límite, sin cargar archivos ni publicar
un objeto. El CLI devuelve `unsupported_mus_limit`. El límite por defecto no
cambia y el núcleo del solver continúa compilando y funcionando con dos muses.

La prueba de configuración se añade a `make test` y se ejecuta también en una
compilación con dos muses. Comprueba creación, apertura binaria/comprimida y TXT,
incluida la conservación del puntero de salida. Falló contra la implementación
anterior en la creación del adaptador con dos muses.
Pasan la batería completa, las pruebas de núcleo y rutas rápidas con dos muses,
y la prueba de configuración en ambas compilaciones. El CLI rechaza aperturas
TXT, binarias y comprimidas con dos muses, y mantiene disponible `--help`.

Claude: **APPROVE**, tarea `task_df61da36b9f6`, dispatch `ctx_1163be5818ea`,
mensaje `msg_9c9255063346`. Se refuerza la prueba con un puntero centinela no nulo,
se documenta su conservación y se añade `-lm` al enlace de la prueba. El mensaje
del CLI conserva una explicación legible junto al identificador del error.

[Rendimiento](review-performance/J14.json): ambos ejecutables release de la
configuración por defecto son idénticos byte a byte a J16. Esta modificación
no cambia el algoritmo ni su coste en la configuración admitida.

## J02: recuento diferido en el simulador

Se negocian los cuatro lances antes de cobrar los contratos y tantos intrínsecos.
Los dejes se siguen pagando al cerrar su negociación. Un deje decisivo o un
órdago querido resuelve la partida antes de los pendientes. Las estrategias
solo observan el marcador público; el recuento posterior conserva el orden
habitual de los lances. La implementación no modifica los comparadores de
manos ni las funciones aisladas de puntuación.

La prueba nueva fija repartos físicos al cortar mus y calcula los resultados
esperados a mano. Cubre dos/cuatro jugadores, juego/punto, contratos queridos,
lances en paso, órdago y deje decisivos en chica desde 39:39, intrínsecos de
pares/juego/punto rechazados, y el marcador observado por cada callback.
Antes del arreglo fallaban 139 comprobaciones; después pasan las 659.
Pasan la batería optimizada del simulador y la batería completa del solver.
También pasa la batería del simulador con AddressSanitizer y UndefinedBehaviorSanitizer.

[Rendimiento](review-performance/J02.json): tres pares alternos en ocho cargas
con dos/cuatro jugadores, política pasiva/con envites y rondas/partidas completas.
Cada ejecución recorre dos millones de rondas o 200.000 partidas desde 0:0.
Las medianas de tiempo varían entre −2,34 % y +6,60 %; RSS entre −1,29 % y +1,30 %.
No se activa el límite de regresión. Coinciden los recuentos de rondas y los
resultados de todas las rondas aisladas y partidas pasivas. Cambian los resultados
de partidas con envites: los dejes decisivos preceden ahora a los pagos pendientes.
Esto no mide fuerza de juego. `mus_sim.c` no se enlaza en el solver, por lo que
no cambia el entrenamiento CFR. Fuente reproducible: `tests/benchmark_simulator.c`.
Claude: **APPROVE**, tarea `task_79e0b4eb4570`, dispatch `ctx_5192988d40ac`,
mensaje `msg_485667c4912c`. Se añade su caso sugerido: el apostador con medias
cobra sus intrínsecos cuando el rival con duples rechaza. La prueba final pasa
740 comprobaciones, tanto optimizada como con sanitizers. Se documenta también
que los intrínsecos de lances rechazados pertenecen al equipo apostador.
El commit del submódulo precede a la actualización del gitlink raíz: `c83774156560461ac4888573be3877cad982d478`.

## J06: operaciones de confianza en el evaluador exacto

Tras validar la raíz, el evaluador enlaza una copia local del descriptor con
su tabla de operaciones de confianza, si existe. La selección se hace una vez;
los wrappers conservan sus comprobaciones de argumentos. La alternativa es la
tabla pública completa cuando no hay tabla de confianza; los dos descriptores
actuales proporcionan enumeración de azar. El descriptor original no se modifica. La cota
de profundidad de mejor respuesta se determina antes de sustituir el descriptor.
No se cambian las pasadas ni la fórmula de evaluación.

Pasa la batería completa. Las comparaciones existentes con el evaluador general
verifican perfil, mejores respuestas y explotabilidad en ambos formatos. La
prueba nueva verifica argumentos inválidos, rechazo de una raíz corrupta y
conservación del estado y la salida. El benchmark reproduce el evaluador completo
con todas sus pasadas y consultas a una estrategia entrenada, e identifica
expresamente el alcance de cada raíz.

Claude: **APPROVE**, tarea `task_9265c53eb2bc`, dispatch `ctx_b1b57b844ea7`,
mensaje `msg_708ce4b05527`. Se corrige la explicación de la alternativa pública,
se aclara que `visited_nodes` cuenta entrenamiento y se añade el objetivo
`benchmark-evaluation`. Una prueba independiente compara la tabla pública sin
`trusted_operations` con la ruta de confianza: métricas idénticas y conservación
de raíz y descriptor. El binario del benchmark sigue siendo idéntico tras estos
ajustes de comentarios/documentación/construcción.

[Rendimiento](review-performance/J06.json), tres pares por carga, con estrategia
entrenada durante 10.000 iteraciones y semilla 7:

| Formato y alcance | Mediana anterior | Mediana actual | Relación |
|---|---:|---:|---:|
| single-bet, raíz completa | 192,052 s | 68,159 s | 2,82× |
| single-bet, primera mano fijada, diez evaluaciones | 5,034 s | 0,579 s | 8,69× |
| raises-to-eight, primera mano fijada | 18,692 s | 3,402 s | 5,49× |

Las ocho métricas coinciden exactamente en todas las ejecuciones. Las raíces
condicionadas no miden explotabilidad global; no se midió la raíz completa de
raises-to-eight. Las ejecuciones antiguas de la raíz single-bet variaron entre
178,44 y 302,16 s; se conservan todos los datos y no se presenta la relación como
una garantía. El mayor incremento de RSS de evaluación es +2,13 %.

En entrenamiento genérico: +10,44 % / −0,55 % / −0,12 % de tiempo para
single-bet / raises-to-eight / ordago, con contadores iguales y RSS prácticamente
igual. La tabla score pasa de 3,7458 a 3,7477 s (+0,05 %) y mantiene todos sus
archivos idénticos. No se activa el límite de regresión.
