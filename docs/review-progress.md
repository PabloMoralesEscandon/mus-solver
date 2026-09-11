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
| J01 | Modelo genérico sin información oculta y compatibilidad histórica | Completado: núcleo/CLI, precarga y bot; coste del modelo aceptado |
| J02 | Recuento diferido en el simulador de dos/cuatro jugadores | Completado |
| J03 | Reintento tras fallo al guardar una diagonal | Completado |
| J04 | Rechazar TXT truncado a su cabecera | Completado |
| J05 | Excluir candidatos de precarga incompatibles con los dejes | Completado |
| J06 | Usar operaciones de confianza en la evaluación exacta local | Completado |
| J07 | Mezclar bits en la tabla de mejor respuesta | Completado |
| J08 | Reutilizar celdas terminadas de una diagonal interrumpida | Completado |
| J09 | Región opcional de continuaciones cerrada por incrementos y giro | Completado |
| J10 | Incertidumbre y presupuesto de evaluación | Completado; integración terminal ensayada sin activar |
| J11 | Precisar el alcance del diagnóstico de repartos fijos | Completado |
| J12 | Evaluar partidas con la política desplegada del bot | Completado |
| J13 | Conservar disponibilidad de política en TXT | No aplicado por decisión del usuario; se conserva el parche y la medición |
| J14 | Rechazar explícitamente el adaptador incompatible con dos muses | Completado |
| J15 | Rechazar fases de azar/terminal en la clave rápida | Completado |
| J16 | Gramática explícita para opciones enteras sin signo | Completado |
| J17 | Bloqueo ligado al proceso y contrato de durabilidad | Bloqueo completado; sincronización retirada por rendimiento, a la espera del usuario |
| J18 | Reducir el coste de codificación y generación de candidatos | Completado |
| J19 | Distribución y memoria de trabajadores por diagonal | Candidato retirado: límite de RSS confirmado; a la espera del usuario |
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

## J07: dispersión de la tabla de mejor respuesta

La función interna mezcla los bits altos y bajos antes de aplicar la máscara
de capacidad. Se conserva la clave original en cada entrada y no se modifica
la serialización de estrategias. La misma función se usa en búsquedas y al
crecer la tabla. Este cambio se mide frente a J06, de forma separada.

Antes del cambio, las 330 manos reales al abrir grande usaban una única posición
inicial, con 54.615 sondeos de inserción y media 165,5, en ambos formatos.
Después: 279 posiciones iniciales y 429 sondeos en single-bet; 286 posiciones y
403 sondeos en raises-to-eight. Se usa el inicializador explícito de cada formato.
Pasa la batería completa y coinciden exactamente las ocho métricas de evaluación
en todas las comparaciones.

Claude: **APPROVE**, tarea `task_8511b2de7ee9`, dispatch `ctx_b4a9bc1b0192`,
mensaje `msg_4522afde531f`. La prueba de dispersión cubre las claves iniciales;
la aceptación de rendimiento utiliza el evaluador completo.

[Rendimiento](review-performance/J07.json): tras observar variaciones entre tandas,
se confirma con tres pares por carga fijando la afinidad al núcleo lógico 0.
Medianas: +0,93 % en el subproblema single-bet, −14,34 % en el subproblema
raises-to-eight y −1,02 % en la raíz completa single-bet. El mayor aumento de
RSS es +0,27 %. No hay regresión grave. La dispersión mejora mucho, pero no se
observa una aceleración general de single-bet.

Limitación importante de la comparación acumulada: el mismo binario de J06,
verificado mediante SHA256, pasó de una mediana de 68,159 s en su tanda anterior
a unos 18 s al reutilizarlo aquí. Es una variación entre ejecuciones ajena al
cambio de hash; no se multiplican los cocientes de J06 y J07 ni se atribuye esa
variación a una optimización. Se conservan las dos tandas de J07, con y sin
afinidad fijada, y se registrará la afinidad en las siguientes comparaciones.

## J01: bloqueo y aceptación del coste

El candidato de núcleo/CLI `ordago-public` pasó la batería completa, las pruebas
con uno/dos muses y el núcleo con ASan/UBSan. Se retiró inicialmente del código
al confirmar el límite de rendimiento indicado por el usuario. Después de
consultar el alcance y las cifras, el usuario aceptó expresamente ese coste:
«si es un error serio habrá que aplicarlo. Deja el cambio». Se recupera J01.
El adaptador y la precarga del esquema nuevo siguen pendientes, junto con los
otros bloques aún no aplicados.

En cinco pares adicionales con semilla 7, 500 iteraciones, un hilo y afinidad
CPU0, el tiempo de proceso pasa de 1,6681 a 4,9279 s (+195,42 %), y el RSS de
596.724 a 1.342.896 KiB (+125,04 %). El nuevo modelo visita 42.630.116 nodos y
crea 11.018.683 infosets, frente a 20.174.542 y 3.492.885 en el histórico.
La semántica del modelo cambia; igualar iteraciones no iguala calidad de política.
Se supera el límite aunque el cambio corrija un defecto de información.

[Mediciones completas](review-performance/J01.json),
[parche retirado](review-performance/J01-blocked.patch) e
[informe del bloqueo](../ASTRA_BLOQUEO_RENDIMIENTO_J01_2026-09-11.md).
El parche se comprobó antes de retirarlo y después contra HEAD `83c0a28`.
La revisión de implementación de Claude corresponde a `task_664c5e0eef1e`,
`ctx_37db6f8f21b1`: **APPROVE para núcleo/CLI**, mensaje `msg_77f6aee17f98`.
El [informe de Claude](review-performance/J01-claude-review.md) y sus mejoras
no bloqueantes quedaron conservados durante la pausa. Tras aceptar el coste,
se mueve el mensaje de éxito al final del CLI, se añaden subidas y rechazos de
órdagos a los grupos de invariancia y se rechazan estados que continúan tras
aceptar un órdago. La regresión de validación falló antes de añadir la guarda.
También se fijan resúmenes de claves, menús y utilidades de 256 recorridos por
formato histórico, calculados con el código de `83c0a28` y uno/dos muses.

La aceptación cubre el coste medido del nuevo modelo frente a v9. Los siguientes
cambios conservan el límite original: esta excepción no autoriza regresiones
adicionales. La revisión final de Claude es **APPROVE**, tarea `task_70823959bab2`, dispatch
`ctx_278271202084`, mensaje `msg_a35a82f8cf47`.
[Informe final](review-performance/J01-final-claude-review.md).

La batería completa, los resúmenes históricos, las pruebas con dos muses y
ASan/UBSan pasan. Las comparaciones entre manos ocultas alcanzan 75.900 con un
mus y 141.358 con dos. Los checkpoints históricos conservan sus bytes y el
checkpoint público coincide con el candidato aceptado. En seis ejecuciones
v4, sus 45 checkpoints y `values.txt` también coinciden byte a byte.

Medición final, tres pares por carga con CPU0: single-bet +3,33 %, raises +2,50 %,
ordago histórico +2,09 % y score +1,48 % frente al padre. El formato público,
comparado con el candidato cuyo coste se aceptó, varía +4,56 % en tiempo y
+0,04 % en RSS, con nodos e infosets idénticos. No se activa otra regresión grave.
No se atribuye esa variación pequeña a la guarda de validación, que no se ejecuta
en cada nodo de entrenamiento. Se conserva la limitación entre tandas descrita
en J07. Las cifras finales y los comandos que generaron las referencias de los
resúmenes están en `reintegration` del JSON de J01.

## J01: precarga de la política pública

La precarga identifica los esquemas genéricos histórico y público. Con el
público traduce todos los lances mediante los cobros por jugador: suma el
marcador inicial para importar a la celda y lo resta para consultar la genérica.
No enumera posibles resultados ocultos. Descarta respuestas incompatibles con
el presupuesto de la celda y conserva la masa por identidad de acción. Las
rutas históricas mantienen sus reglas anteriores. Se admite origen binario
normal o Zstandard, incluida la reanudación de checkpoints con marcador.

Validación: comparación de las claves del modelo de mano aislada con las de
partidas reales en 800 recorridos, cuatro combinaciones de objetivo y marcador,
ambos jugadores, mus, descartes y todos los lances. Se comprueban pagos a ambos
jugadores, envites pendientes, respuestas fuera del presupuesto, cobros hasta
40, resultados conservados ante errores, masa transferida, nodos existentes y
orígenes ausentes o incompatibles. Las pruebas del modelo pasan con uno y dos
muses y con ASan/UBSan. Se corrigió un error de tamaño en el registro de fases
de la prueba nueva antes de finalizar la validación.

La batería completa `make -j4 test` pasa. El CLI también pasa con dos muses.
Las pruebas de CLI comparan los nueve
checkpoints de una tabla de objetivo 3 precargada desde los dos tipos de archivo,
con distintos números de trabajadores, y su reanudación; conservan el origen.

[Rendimiento y script reproducible](review-performance/J01-preload.json): tres
pares alternados, CPU0, semilla 7, sin otras pruebas ni compilaciones propias.
El padre es `eb6bd55`. Entrenamiento público de 500 iteraciones: 5,7932 → 5,4969 s
(−5,11 %), RSS −0,12 %, mismos contadores. Tabla de 45 celdas, 1.000 iteraciones y
10.000 muestras por celda: 3,7056 → 3,6293 s sin prior (−2,06 %), y
5,0457 → 4,9461 s con prior histórico (−1,97 %); RSS sin cambios. Los 46 archivos
coinciden byte a byte entre versiones en cada carga. Estas variaciones pequeñas
no demuestran una aceleración atribuible al cambio.

La ruta nueva con prior público no tenía implementación anterior. Su medida
absoluta con un origen de 20 iteraciones es 4,9314 s y 109.444 KiB de RSS.
Los orígenes históricos y públicos de esa prueba pesan 13.416.467 y 41.923.809
bytes, respectivamente. Cargar la política pública incorpora el almacén mayor
del modelo cuyo coste fue aceptado; no se presenta como una regresión adicional
de las rutas existentes ni como una comparación de políticas equivalentes.

Revisión inicial: `task_5e837ddbd6bf`, dispatch `ctx_9d72c32d40f7`,
`msg_d42307be8547`: pidió corregir el comando de generación del archivo
comprimido de la prueba. La corrección y la repetición de las comprobaciones
recibieron **APPROVE** en `task_265194a233be`, dispatch `ctx_3071ca949edd`,
mensaje `msg_3c5d5ae78461`. [Informe final](review-performance/J01-preload-claude.md).
La identidad persistida del prior queda para J08/J17; la documentación ya
advierte que `values.txt` no conserva esa configuración. El adaptador sigue
pendiente y conserva el límite original para cualquier regresión adicional.

## J01: conexión de la política pública con el bot

El adaptador admite `ordago-public` en checkpoints normales, comprimidos y
TXT, incluidos los cambios entre genérica y tablas de marcador. Los almacenes
prestados se identifican explícitamente con `mus_adapter_create_public()`;
la API anterior mantiene su interpretación histórica. Los cobros públicos se
calculan como marcador actual menos marcador inicial. Las consultas de apuestas
requieren el acumulado pendiente explícito. Ambos cobros y ese acumulado se
mantienen fijos incluso al buscar un envite parecido. El diagnóstico identifica
`ordago-public` y no marca `score_proxy` en esa ruta.

Las celdas entrenadas siguen teniendo prioridad y conservan sus reglas de
consulta y su caché propia. La genérica pública mantiene el ajuste heurístico
por marcador y la proyección de cantidades de la genérica; se documenta esta
limitación. No se alteran las reglas ni el entrenamiento de ningún modelo.

Validación: batería completa; pruebas C de ambos jugadores y todos los lances,
mus y descartes; identidad de claves y probabilidades; interpolación, proximidad,
respaldo sin cambiar cobros ni pendientes; errores que conservan la decisión y
el RNG. Las pruebas JSON comparan los tres formatos de archivo, cambios de celda
y vuelta a genérica, errores de entrada, corrupción, límite de mus y conservación
de archivos de origen. Pasan normalmente y con ASan/UBSan. La configuración de
dos muses rechaza también el nuevo constructor. Se añadió la prueba sugerida
por Claude para el ajuste de órdago público y su ausencia en celdas entrenadas.

[Rendimiento y script](review-performance/J01-adapter.json): el ejecutable del
solver es idéntico byte a byte al padre `f62ad27` (SHA256
`cbf3e6d106d4a0bb11ceac5f272dde2657e67baba06424dd1efab8b7929df424`).
Este bloque no añade coste al algoritmo de entrenamiento. Se amplió el sondeo
inicial a 64.000 consultas JSON por ejecución, tres pares alternados, CPU0:

| Ruta existente | Antes | Después | Tiempo | Pico RSS |
|---|---:|---:|---:|---:|
| Genérica histórica, binario | 1,6909 s | 1,6908 s | −0,01 % | +1,80 % |
| Genérica histórica, TXT | 2,9185 s | 2,9512 s | +1,12 % | −1,18 % |
| Híbrido, TXT | 2,3238 s | 2,3330 s | +0,40 % | −1,05 % |

Las respuestas JSON son idénticas byte a byte entre padre y candidato.
La latencia media por consulta varía +0,55 %, +0,94 % y +0,46 %, respectivamente.
No se activa el límite de regresión. En la ruta pública nueva, las 64.000
consultas tardan 1,3921 s en binario y 1,4001 s en TXT: medias de 0,01579 y
0,01590 ms por consulta, RSS 2.312 y 2.340 KiB con estos archivos pequeños.
Son cifras absolutas de una carga repetida; no representan latencia de lectura
fría de archivos enormes ni una comparación de calidad entre políticas.

Claude aprobó la implementación en `task_0bef531f41f9`, dispatch
`ctx_6d053a440928`, mensaje `msg_03c322e96c32`. La comprobación de la prueba
adicional y la medición ampliada recibió **APPROVE** en `task_b8aeb48d0680`,
dispatch `ctx_798be744111e`, mensaje `msg_3e7ffcc8daaa`.
[Revisión de implementación](review-performance/J01-adapter-claude.md) y
[comprobación final](review-performance/J01-adapter-claude-final.md).
Se conserva `score_table_disabled` como motivo de ruta porque `strategy` ya
identifica la política pública. No se añade una inferencia de esquema a los
almacenes prestados: deben usar el constructor explícito documentado.

## J17: bloqueo ligado al proceso

Los coordinadores bloquean el directorio de continuación mediante `flock` y
mantienen su descriptor abierto hasta terminar. El bloqueo es exclusivo al
entrenar y compartido al evaluar, exportar, reanudar un checkpoint individual
o usar la tabla como origen de una ampliación. El sistema lo libera al morir
el proceso, sin manejadores de señales. `training.lock` conserva únicamente
los metadatos del último escritor; no determina la propiedad del bloqueo.

La primera revisión detectó que crear un archivo en un origen antiguo
impedía usar tablas archivadas en solo lectura. Se cambió el objeto bloqueado
al directorio: ningún lector crea o modifica archivos en el origen. No se usa
una excepción que deje una tabla sin protección. La documentación delimita el
contrato a sistemas locales que admitan este bloqueo cooperativo. No se deben
mezclar coordinadores antiguos ni mover el directorio durante su uso.

Validación final: `make -j4 test`, pruebas de procesos normales y ASan/UBSan.
Se comprueban exclusión entre escritores, conservación del archivo de metadatos
ante un intento denegado, liberación y reanudación tras SIGTERM/SIGINT/SIGKILL,
metadatos huérfanos, lectores compartidos, exclusión del escritor durante una
ampliación y conservación exacta del origen. Una tabla sin `training.lock` y
con directorio 0555 se puede ampliar sin añadir ni modificar archivos. Las
rutas de lectura rechazan a un escritor activo y los errores liberan todos los
descriptores. La espera de las pruebas lee primero el PID publicado para evitar
competir con la adquisición inicial; los hijos restauran la disposición de SIGINT.

Revisión inicial: `task_0c76291401dd`, dispatch `ctx_38f5104e593b`, mensaje
`msg_06c73a6f218a`, **REQUEST_CHANGES** por el origen antiguo de solo lectura.
Revisión del diseño final: `task_30a9ed61396b`, dispatch `ctx_c7e9ed872af1`,
mensaje `msg_d048409a4812`, **APPROVE**.
[Revisión inicial](review-performance/J17-lock-claude-initial.md) y
[revisión final](review-performance/J17-lock-claude-final.md).

[Rendimiento final y script](review-performance/J17-lock.json): tres pares
alternados, CPU0, padre `0373fc1`. Público, 500 iteraciones: 5,6404 → 5,3596 s
(−4,98 %), RSS −0,11 %, mismos contadores. Tabla de 45 celdas con 1.000 iteraciones
y 10.000 muestras por celda: 3,7880 → 3,8372 s (+1,30 %), RSS +0,01 %.
Ampliar esas celdas en 1.000 iteraciones: 3,9366 → 3,9248 s (−0,30 %),
RSS −0,16 %. Los 46 archivos de política y valores son idénticos entre versiones;
los orígenes quedan intactos, sin archivos adicionales. La biblioteca del solver
y el bot conservan su SHA256. No se activa una regresión grave ni se atribuyen
las pequeñas variaciones a una aceleración del código.

Este bloque resuelve el bloqueo huérfano y la exclusión entre procesos.
La sincronización de archivos/directorios ante pérdida de energía sigue
pendiente; la recuperación de celdas no publicadas corresponde a J08.

## J17: sincronización de checkpoints y tablas — candidato retirado

**Estado:** no aplicado. Se conserva en [parche](review-performance/J17-sync-blocked.patch)
tras confirmar el límite de rendimiento. La descripción siguiente corresponde
al candidato probado, no al código vigente. J01 y el bloqueo de J17 permanecen
aplicados.

El CLI del candidato confirma los datos de cada temporal con `fflush` y `fsync`, cierra el
archivo, publica su nombre con `rename` y sincroniza el directorio padre.
`values.txt` se publica después de sus checkpoints. `extension.txt` utiliza
el mismo protocolo bajo el bloqueo exclusivo. Al iniciar una escritura se
sincroniza también el padre de la continuación, incluso si ya existe: así un
reintento cubre una creación anterior cuya sincronización falló.

Los errores anteriores al renombrado conservan el archivo publicado. Un error
de sincronización posterior al renombrado devuelve error, pero puede dejar
visible el nuevo archivo completo; no se promete deshacer esa publicación.
El contrato abarca los checkpoints genéricos raw/zstd y las tablas. Excluye
exportaciones TXT y metadatos informativos del bloqueo. Abrir y sincronizar
los directorios requiere permisos de lectura sobre ellos.

La batería completa, las pruebas de persistencia con ASan/UBSan y la ejecución
de estas últimas sobre Btrfs pasan. Un ejecutable exclusivo de pruebas registra
`fsync`/`rename` mediante wrappers de enlace; el binario de producción llama al
sistema directamente. Se verifican orden, conservación del archivo anterior,
fallos de checkpoint raw/zstd, tabla y manifiesto, temporal de manifiesto
abandonado, creación fallida y reintento, rutas relativas y con espacios/barra
final, y error posterior al renombrado con recarga del archivo nuevo. Estas
pruebas no simulan un corte físico de energía.

Claude aprobó la primera revisión, tarea `task_00bc6da58ad8`, dispatch
`ctx_a6ee1542551d`, mensaje `msg_600e30aaeda4`. La revisión final aprueba el
reintento y los casos adicionales, condicionada a las comprobaciones finales
que ya han pasado: tarea `task_e14b8a955c0d`, dispatch `ctx_232559198829`,
mensaje `msg_99a22b87ba83`. Se añadió la nota solicitada sobre permisos.

[Rendimiento del candidato final](review-performance/J17-sync.json): tres
pares alternados sobre Btrfs del workspace; `/tmp` es tmpfs y no sirve para
medir este coste de disco. Se comparan ejecutables contra `1f1dec7`, CPU0,
semilla 7, sin compilaciones ni pruebas propias simultáneas. Antes de cada
proceso se confirman fuera del intervalo sus escrituras previas pendientes.
Entrenar 500 iteraciones públicas y guardar 804.253.137 bytes raw pasa de
9,4116 a 9,6240 s (+2,26 %); guardando 139.523.816 bytes comprimidos,
10,4330 a 10,6923 s (+2,49 %). La tabla de 45 celdas, 1.000 iteraciones y
10.000 muestras por celda pasa de 3,7072 a 3,7832 s (+2,05 %); su ampliación,
de 3,9420 a 4,1067 s (+4,18 %). RSS entre −0,06 % y +0,12 %.
Todos los checkpoints y valores coinciden exactamente, los manifiestos
coinciden al normalizar el origen y ninguna ampliación modifica su fuente.
Biblioteca y bot mantienen sus SHA-256. Estas cargas no activan el límite;
se mide además por separado el caso de muchas celdas pequeñas.

La [carga mínima](review-performance/J17-sync-small.json) sí confirma el límite.
Cada tanda crea 100 tablas de 45 celdas, con una iteración y una muestra por
celda; se agrupan ejecuciones para medir segundos de trabajo total. Tras los
tres pares iniciales se ejecutan otros cinco pares alternados: medianas
0,7479 → 11,8387 s (+1.482,84 %, 15,83 veces el tiempo). Equivale a unos
2,46 ms adicionales por celda, incluyendo publicación de tabla y arranque.
Los 46 archivos de cada una de las 1.600 tablas comparadas coinciden exactamente.
La biblioteca de CFR/mus no cambia: es un coste de confirmación de escrituras,
no más nodos ni un algoritmo distinto. La carga deliberadamente mínima acentúa
esa proporción y no representa el coste de un entrenamiento largo.

Se conserva el candidato, se retiran sus cambios de producción y pruebas
y se espera la decisión del usuario antes de continuar. La excepción de
rendimiento concedida para J01 no se extiende a este cambio. Posibles decisiones:
aceptar la sincronización por defecto, ofrecerla como opción explícita o
posponerla; ninguna de estas alternativas se ha aplicado automáticamente.

Claude confirmó también las mediciones y la pausa: tarea `task_f4143d7e3031`,
dispatch `ctx_37121eb23608`, mensaje `msg_ac468a5f4e3f`,
[revisión](review-performance/J17-sync-performance-claude.md). Su reserva sobre
un posible solapamiento confundía la creación del script con su ejecución:
el último log regular es de 19:07:39,735 y el nuevo log mínimo se crea a las
19:07:49,914. Las herramientas también confirman ese orden serial.
[Informe de la pausa](../ASTRA_BLOQUEO_RENDIMIENTO_J17_2026-09-11.md).

## J08: recuperación de celdas terminadas

Tras la autorización para continuar, se mantiene pendiente el candidato de
sincronización J17 y se implementa la recuperación ante interrupciones del
proceso. El CLI añade una entrada con checksum a `recovery.txt` después de
guardar cada checkpoint y vacía el buffer del registro. Una línea incompleta
no impide leer las posteriores. La caché de registros se carga una vez y es
inmutable durante el entrenamiento; los trabajadores pueden consultarla a
la vez y solo el coordinador escribe el archivo.

Para recuperar se exige coincidencia del esquema, valor y metadatos de la
celda, parámetros de entrenamiento/evaluación, contenido del prior y contenido,
iteraciones y semilla inicial de la celda de origen en ampliaciones. Se verifica
la huella del checkpoint y se carga con el lector de C-CFR para comprobar
formato, checksum, esquema y contador. Las huellas FNV detectan cambios
accidentales, no autentican archivos. No se añade `fsync` ni se promete
persistencia ante una caída del sistema.

La API incorpora un callback opcional de recuperación. Un resultado válido
omite entrenamiento, evaluación y guardado del checkpoint; la diagonal se
publica completa. `NOT_FOUND` solicita entrenamiento ordinario y los demás
errores detienen la operación. El núcleo rechaza los resultados de callbacks
que incumplan el contrato de contadores, semilla o valor. Se conserva la
restauración de J03 ante fallo al publicar la diagonal.

Las pruebas cubren reintentos con uno y dos trabajadores, checkpoint y registro
incompletos, checksum inválido, cambios de hilos y reserva, prior modificado
con el mismo tamaño/fecha y CRC32 válido, ampliaciones sin modificar el origen
y SIGKILL después de guardar una celda. Se comprueban igualdad de resultados
y conservación del contenido y fecha del checkpoint recuperado. Pasan la
batería completa, núcleo/rutas rápidas con dos muses, núcleo con ASan/UBSan y
las pruebas finales de recuperación normales y con sanitizadores.

La primera prueba SIGKILL usaba 20.000 iteraciones por celda y agotaba su plazo
en debug; se redujo a 500 y se evitó el buffering de Python al esperar la salida.
Las pruebas de recuperación finalizaron correctamente con esa carga.

Primera revisión de Claude: **APPROVE**, tarea `task_743f471e0df6`, dispatch
`ctx_d18f7188c8ef`, mensaje `msg_71c4fc9d38e0`. Se corrigió una frase antigua
del README que todavía afirmaba que todos los checkpoints no publicados se
ignoraban. Se conserva la comprobación conservadora de `--threads`, aunque
variar solo los trabajadores entre celdas no cambie los resultados.

La revisión final de identidad y pruebas dio **APPROVE**: tarea
`task_9a37095bb6aa`, dispatch `ctx_eb1c997813af`, mensaje `msg_e1254c38f2ab`.
Se encontró después que una línea con NUL podía hacer descartar la siguiente
entrada válida. El lector acotado ahora consume exactamente una línea física
y rechaza NUL, exceso de longitud y cola incompleta sin perder la siguiente.
Las regresiones de NUL y 8.192 bytes pasan en la batería completa y con
ASan/UBSan. Claude aprobó el ajuste, condicionado a las verificaciones finales:
tarea `task_e21303055acf`, dispatch `ctx_cdd1868e91f1`, mensaje
`msg_b4987b3af72c`. Los logs finales de pruebas y compilación corresponden
a la revisión con ese lector; no se cambia el núcleo entre estas revisiones.

[Rendimiento final](review-performance/J08.json), tres pares alternados en
Btrfs del workspace y CPU0: tabla de 45 celdas con 1.000 iteraciones y 10.000
muestras por celda, 3,7077 → 3,7514 s (+1,18 %), RSS +0,03 %; ampliación,
4,0473 → 4,1353 s (+2,18 %), RSS +0,24 %. Todos los checkpoints y valores
comparados son idénticos y los orígenes de ampliación quedan intactos.
La [carga mínima](review-performance/J08-small.json), 100 tablas de 45 celdas
con una iteración y una muestra por celda, pasa de 0,7378 a 0,7589 s (+2,87 %).
No se activa el límite de regresión. Las cifras incluyen la lectura adicional
para calcular la huella y la escritura del registro.

Al [recuperar ocho celdas de una diagonal de nueve](review-performance/J08-resume.json),
la mediana pasa de 1,1190 a 0,2801 s (−74,97 %) y el RSS baja un 9,63 %.
Los archivos finales son iguales; el candidato conserva las ocho fechas de
modificación y el padre vuelve a escribir esos ocho checkpoints. La mejora
corresponde a esta interrupción concreta, no a todo entrenamiento. El registro
ocupa 21.310 bytes para las 44 celdas guardadas del ejemplo; crece cuando se
vuelven a escribir celdas y no se compacta automáticamente.

Los scripts y las mediciones completas se conservan en sus JSON. Se ejecutaron
con el binario final `build/review-j08-final2/release/mus-solver`, después de
acabar compilaciones y pruebas y sin otras cargas propias simultáneas.

La batería completa de recuperación por CLI pasa también con dos muses. En esa
configuración, la prueba de interrupción usa 100 iteraciones por celda en lugar
de 500: con 500, la reanudación agotaba el plazo de 30 s de la prueba. Se
mantienen SIGKILL, recuperación efectiva e igualdad de contenido y fecha.
Las condiciones de validación de las revisiones quedan satisfechas.


## J09: regiones de marcadores opcionales

`--score-region A:B` limita la cobertura a `(x >= A && y >= B)` o
`(x >= B && y >= A)`. La unión conserva las continuaciones cuando aumentan
los tantos y cambia la mano. La selección de diagonales, validación,
ampliación, recuperación y bot respetan esa cobertura. Los límites ordenados
se guardan antes de entrenar la primera celda y se infieren al reanudar.
Unos límites explícitos distintos o un marcador excluido producen un error.

El formato restringido `mus.ordago-score-region/v1` tiene identidad propia
para tabla, checkpoints y TXT. La ruta sin la opción mantiene v4 y sus
archivos exactos. Una ampliación exige solo las celdas solicitadas del destino,
con el origen inmutable y bloqueado para lectura durante toda la operación.
El bot usa la política genérica para marcadores iniciales excluidos.

Las pruebas comprueban cierre, orientación, recuentos, valores y contenido
CFR idéntico salvo esquema y su checksum; cubren regiones simétricas y
asimétricas, uno/dos trabajadores, lectura estricta, continuación individual,
ampliaciones completas/restringidas, recuperación y pérdida de la primera
celda. El bot real selecciona tablas de 25 celdas y vuelve a la genérica fuera
de cobertura. Pasan el núcleo, CLI y ASan/UBSan; el núcleo y CLI pasan también
con dos muses. Los logs finales están en `/tmp/mus-review-j09-final-*.log`.
La batería completa `make -j4 test` pasa en la revisión final, después de las
mediciones; se cumple la última condición de verificación de Claude.

Claude aprobó el código inicialmente y pidió documentación: tarea
`task_2454d21e60a5`, dispatch `ctx_2b9cba16b8fd`, mensaje `msg_2d47aa4037a5`.
Se documentaron los contratos y se añadió la publicación inicial de la
cabecera vacía que sugirió. Revisión final **APPROVE**: tarea
`task_e128e3a9ddc0`, dispatch `ctx_f48fdfc7aaea`, mensaje `msg_f8206f066685`.
Los informes completos se conservan junto a las mediciones.

[Rendimiento](review-performance/J09.json), tres pares alternados en CPU0,
Btrfs, 1.000 iteraciones y 10.000 muestras por celda:

| Carga desde 35:35 | Padre | Candidato | Cambio de tiempo |
|---|---:|---:|---:|
| Tabla habitual, 45 celdas | 3,7847 s | 3,9206 s | +3,59 % |
| Ampliación habitual, 45 celdas | 4,0748 s | 3,9984 s | −1,87 % |
| Región opcional, 45 → 25 celdas | 3,8098 s | 2,2086 s | −42,03 % |
| Ampliación con región, 45 → 25 celdas | 4,1210 s | 2,3134 s | −43,86 % |

Los cambios de RSS quedan entre −0,14 % y +0,21 %. En la ruta habitual todos
los checkpoints y valores son idénticos. Con región coinciden los valores y
metadatos de las 25 celdas comunes; sus esquemas son distintos deliberadamente.
Todos los archivos de los orígenes se conservan.
La [carga mínima](review-performance/J09-small.json), 100 tablas de 45 celdas
con una iteración y una muestra, pasa de 0,7717 a 0,7820 s (+1,33 %), sin
cambio de RSS ni diferencias en las políticas guardadas. No se activa ningún
límite de regresión. Las medidas son de estas cargas; la reducción de celdas
no garantiza el mismo ahorro en otros marcadores.


## J10: varianza local y presupuesto adaptativo

Se acumula la varianza con Welford y se informa el error estándar de la media
`SE = sqrt(varianza_muestral / muestras)`. En la escala `(V+1)/2` el error es
`SE/2`. Una muestra o una celda antigua sin estadísticas produce `unknown`.
La media del modo fijo conserva exactamente la operación y las muestras
anteriores. Se añade una API con mínimo, máximo y objetivo opcional de error;
el CLI exige juntas `--evaluation-min-samples` y `--evaluation-target-se`.
El máximo sigue siendo `--evaluation-samples`. Se guarda el recuento real y
se informa si se alcanzó el objetivo estimado o el límite de muestras.

El error es local y condicionado a la política y continuaciones actuales.
No incorpora la incertidumbre de las celdas superiores ni el error de la
política frente a una óptima. La parada adaptativa es una regla práctica:
un mínimo pequeño puede observar varianza cero por casualidad y parar en
función de las muestras puede sesgar tanto la media como su error estimado.
Se mantienen presupuestos fijos como opción predeterminada. El README explica
cómo dedicar más muestras a las diagonales altas antes de bajar a otras.
No se impone una semilla común entre celdas: sin un acoplamiento definido,
compartir semilla no demuestra menor varianza de sus diferencias.

La cabecera versionada `statistics=sample-variance-v1` añade disponibilidad y
varianza a cada fila de `values.txt`. El lector conserva archivos antiguos y
filas mixtas; el nuevo formato exige lectores actualizados. Los checkpoints,
TXT y reglas conservan sus esquemas porque las estadísticas no entran en la
huella de continuaciones. Los registros `mus-score-recovery/v2` incorporan
varianza, recuento real y ajustes adaptativos; los registros provisionales
anteriores se recalculan, sin tocar celdas publicadas. La ampliación conserva
los ajustes en su manifiesto y rechaza cambios aunque den el mismo recuento.
La dependencia `-lm` es explícita para `sqrt`, adelantando ese punto de J22.

Pruebas: identidad analítica de la varianza para utilidades ±1; parada exacta
respecto al prefijo anterior; máximo, mínimo, una muestra, parámetros
inválidos y salidas conservadas ante error; persistencia antigua/mixta,
cabeceras y filas inválidas, esquemas estables, evaluación individual,
ampliación y recuperación con ajustes distintos. Pasan la batería completa,
núcleo y hot paths con dos muses, y CLI de estadísticas, región y recuperación
con ambas configuraciones. ASan/UBSan pasa en núcleo, estadísticas, región
y recuperación. La recuperación instrumentada agotó 30 s al coincidir con las
pruebas de dos muses; pasó sin cambios al ejecutarla sola. No se amplió el plazo
ni se redujo la carga para resolverlo. Logs `/tmp/mus-review-j10-*.log`.

Claude: **APPROVE**, tarea `task_9b6661d82207`, dispatch `ctx_f62fedf1274b`,
mensaje `msg_75c1d5d7bbf4`. La herramienta de benchmark ofrece ahora
`--ignore-sampling-statistics` para comparar con referencias antiguas sin
confundir la identidad de las medias con la del archivo ampliado; el modo
predeterminado conserva la comparación completa. Sus dos modos se verificaron.
Claude aprobó ese ajuste: tarea `task_0963c8b6e952`, dispatch `ctx_72f419d0af46`,
mensaje `msg_3d268fe5d8fc`. Se aclaró además el texto del error del modo normalizado.

[Rendimiento](review-performance/J10.json), tres pares alternados, CPU0,
Btrfs, un trabajador, 1.000 iteraciones y 10.000 muestras por celda:

| Carga | Padre | Candidato | Tiempo | RSS |
|---|---:|---:|---:|---:|
| 45 celdas desde 35:35 | 3,8415 s | 3,7762 s | −1,70 % | +1,26 % |
| Ampliación de esas 45 celdas | 4,1051 s | 4,0771 s | −0,68 % | +1,39 % |
| Región 35:35, 25 celdas | 2,1635 s | 2,0737 s | −4,15 % | +0,85 % |

No hay regresión grave; las fluctuaciones no demuestran que calcular varianza
acelere el entrenamiento. Checkpoints idénticos y las seis columnas originales
de valores idénticas en todos los pares. Los orígenes permanecen intactos.
La [carga mínima](review-performance/J10-small.json), 100 tablas de 45 celdas
con una iteración y una muestra, pasa de 0,7849 a 0,7777 s (−0,92 %), RSS igual.
También conserva todos los archivos de políticas y valores.

Un ejemplo adaptativo con mínimo 1.000, máximo 10.000 y objetivo SE 0,03 usa
25.632 muestras en las 25 celdas, frente a 250.000: 2,1586 → 1,8827 s
(−12,78 %), RSS +2,02 %. Es un intercambio entre tiempo y precisión, no una
aceleración con resultados iguales. La diferencia absoluta media entre valores
es 0,02982 y la máxima 0,083; también pueden cambiar las políticas de celdas
inferiores porque reciben otras continuaciones. Ninguna medida activa el límite.

### Ensayo de integración exacta parcial

El [programa exploratorio](review-performance/J10-terminal-trial.json), fuera
de producción, integra respuestas de juego/punto con como máximo dos acciones
cuando todos sus sucesores son terminales. Aplica y deshace cada acción y suma
las utilidades ponderadas por la política; el resto mantiene el muestreo.
Se midieron dos políticas fijas, 10.000 manos por cada una de 32 semillas,
y tres pares alternados. Los pares repiten esas mismas semillas; hay 32
réplicas estadísticas, no 96. Se excluye la carga del checkpoint del tiempo.

| Marcador | Varianza muestral media | Diferencia de tiempo |
|---|---:|---|
| 35:35 | −8,46 % | No resuelta frente a las fluctuaciones |
| 39:39 | −0,063 % | No resuelta frente a las fluctuaciones |

En 35:35 se integraron 67.515 de 320.000 manos; en 39:39 solo 421. Los tiempos
de 0,22–0,39 s tienen rangos solapados: sus pequeñas diferencias de medianas
no permiten atribuir un coste ni ahorro al cambio. El producto varianza × tiempo
y la varianza entre las 32 medias se conservan como datos exploratorios en el
JSON, sin inferir una mejora de eficiencia ni estabilidad entre semillas.
Al omitir algunos
sorteos cambian las trayectorias de las semillas; no se espera identidad exacta.
El ensayo apoya una mejora local en un caso, pero no una mejora general de las
tablas. Se conserva el muestreador ordinario y no se introduce una modalidad
general de integración con esta evidencia limitada. El código, los comandos
y los resultados completos quedan guardados para reproducir el ensayo.

Claude aprobó esa conclusión limitada y pidió distinguir las medianas de una
diferencia de tiempo demostrada; se ajustó esta presentación. Tarea
`task_13c14dc4c95f`, dispatch `ctx_4f7ed5302862`, mensaje `msg_6e570dd2d06c`.


## J11: alcance explícito del diagnóstico acotado

La salida de `ordago` y `ordago-public` conserva `bounded-final-lances` y
sus métricas, y añade `deals=2 opponent_cards=fixed best_responses=per-deal
root_bound=none`. Se aclara en README y API que ambas manos están fijadas,
con mus cortado y pases previos en grande/chica. Se optimiza cada respuesta
por separado en juego y punto y después se promedian las métricas. No es la
explotabilidad desde la raíz ni una cota superior o inferior demostrada.

Se documenta cómo tendría que ampliarse el diagnóstico: ponderar manos
rivales compatibles y acumular sus valores en los infosets compartidos antes
de escoger acciones. Promediar respuestas independientes a más repartos no
resuelve esa incertidumbre. Esa ampliación era consejo condicionado en J11,
no una modificación exigida del evaluador actual. Los consumidores del CLI
deben leer campos por nombre, porque `exploitability` deja de ser el último.

Claude: **APPROVE**, tarea `task_a61c88621805`, dispatch `ctx_8805e77269bb`,
mensaje `msg_dfdd8ad27541`. Se incorporaron sus aclaraciones de historial fijo
y lectura de campos. El cálculo y las reglas permanecen intactos.

Pasa la batería existente de CLI. La comprobación adicional exige los cuatro
campos nuevos y todas las métricas anteriores idénticas para ambos órdagos,
con uno y dos muses. Los archivos release `libmus_solver.a` y `mus-bot` son
idénticos byte a byte al padre; esto comprueba que entrenamiento, evaluación
numérica y decisiones del bot conservan su implementación compilada.

[Rendimiento](review-performance/J11.json): tres pares alternados en CPU0,
1.000 invocaciones del diagnóstico por tanda para amplificar la salida añadida.
`ordago`: 0,3998 → 0,4008 s (+0,25 %); `ordago-public`: 0,4035 → 0,4068 s
(+0,82 %), con el mismo RSS. No se activa el límite y estas variaciones no
constituyen una pérdida relevante. No se atribuye ninguna mejora al algoritmo.

## J12: partidas con la política realmente desplegada

Se añade `mus-match-eval`, documentado en [match-evaluation.md](match-evaluation.md).
Ejecuta partidas completas en el simulador de dos jugadores corregido en J02.
Cada jugador usa una instancia real de `MusAdapter`, con su selector de celda,
interpolación, vecinos, traducción, respaldo y muestreo. Admite políticas
`ordago`/`ordago-public` binarias, comprimidas o TXT, y el selector híbrido por
marcador. Incluye dos rivales de control, comparación entre políticas y una
traza pública opcional que exige un archivo nuevo.

La observación contiene solo cartas propias y datos públicos, orientados a
mano/postre en cada mano. Se conservan los dejes cobrados y contratos pendientes
por separado; se registran declaraciones de pares y descartes. El menú del
experimento contiene totales numéricos 2–40, validados por el simulador, con
incrementos ajenos a la abstracción entrenada. Permite un mus; rechaza el build
de dos muses como el bot. Alterna la primera mano y falla al alcanzar el límite
de manos, sin publicar una tasa que omita partidas incompletas.

La salida informa de victorias, manos y cobertura por método, fase, marcador
inicial, acción y backend. Las distribuciones del JSON son marginales; el CSV
conserva conjuntamente los datos públicos de cada decisión. El valor del perfil
medio para la tabla sigue siendo otra medición. No se presenta esta frecuencia
de victorias como explotabilidad ni como prueba de fuerza con entrenamiento
mínimo. El muestreo de cartas y el de decisiones son flujos separados; comparar
políticas distintas no garantiza repetir las mismas cartas.

Claude: **APPROVE**, tarea `task_f9649e40ccdc`, dispatch `ctx_670ca1778437`,
mensaje `msg_be2f75bf6bb4`; [informe](review-performance/J12-claude.md).
Se añaden sus comprobaciones de `hand_scores_complete` y las tres declaraciones
de pares omitidos. `prior_wager_total` tras rechazos ya se comprobaba en los
casos de deje. Se mantiene la advertencia sobre muestras pequeñas, sin añadir
un error estándar binomial ingenuo que daría cero tras observar cero victorias.
La repetición emparejada de repartos queda como extensión opcional: no forma
parte de la acción J12 y necesita controlar también mus y descartes.

Pasan la batería completa, las pruebas del simulador, las nuevas pruebas de
anfitrión y CLI con ASan/UBSan y el rechazo explícito de dos muses. Los guiones
comprueban veinte tantos pendientes, dejes numéricos y de órdago, órdago querido,
descartes, conservación de resultados ante errores, límites e identidad exacta
con el simulador pasivo independiente. Los CLI prueban los tres backends,
selección híbrida, determinismo, cobertura y conservación de archivos fuente.

### Rendimiento

Los ejecutables release `mus-solver`, `mus-bot` y `libmus_solver.a` son idénticos
byte a byte al padre `d80d01b`: este bloque no altera el código compilado que
entrena o decide en el bot. El motor adicional se enlaza solo con la herramienta
nueva. El único ajuste del simulador hace explícitas conversiones a `uint64_t`
que ya ocurrían en `combinaciones`, para cumplir las flags estrictas del root.

[Medición reproducible](review-performance/J12.json): tres repeticiones de
1.000 partidas en CPU0, semilla 7, sin otras compilaciones/pruebas propias.
Políticas genéricas congeladas de 500 iteraciones; celda 39:39 de 500 iteraciones
y 1.000 muestras. Son cargas de funcionamiento, no estrategias convergidas.
La herramienta nueva no tiene un ejecutable padre equivalente: se mide su
coste absoluto y no se atribuye una aceleración al algoritmo.

| Política A / rival / salida | Tiempo total mediano | Solo juego | RSS máximo mediano |
|---|---:|---:|---:|
| Histórica binaria / three / 0:0 | 1,097 s | 0,159 s | 476,4 MiB |
| Histórica TXT / three / 0:0 | 1,013 s | 1,011 s | 2,77 MiB |
| Pública binaria / three / 0:0 | 3,158 s | 0,0247 s | 1.278,1 MiB |
| Pública TXT / three / 0:0 | 0,141 s | 0,140 s | 2,74 MiB |
| Dos públicas binarias / 35:35 | 6,278 s | 0,0140 s | 2.559,2 MiB |
| Híbrida TXT / three / 39:39 | 0,0123 s | 0,0110 s | 3,05 MiB |

El total incluye carga, partida y salida; el tiempo interno excluye carga y
JSON final. La RAM de binarios grandes corresponde a las políticas cargadas,
incluidas dos instancias en la comparación. El TXT consulta bajo demanda.
Los casos genéricos ejercitan exactos, interpolados, vecinos, respaldo y límite
de mus. TXT v1 y binario pueden recorrer partidas distintas por la pérdida de
masa cero pendiente de J13; sus diferencias no son comparaciones de igual
política efectiva ni de igual trabajo. Los tiempos internos más breves sirven
como referencia local, sin inferir diferencias finas.

El primer ensayo de RSS se descartó: el lanzador Python había leído TXT completos
y contaminaba el máximo heredado. Las cifras finales usan hashes en streaming
y un lanzador C pequeño, guardado en el JSON, que obtiene el RSS del hijo con
`wait4`. No se mezclan esas muestras. No hay una regresión que active el límite
sobre el entrenamiento o el bot existentes, cuyos archivos son idénticos.

Claude comprobó también los hashes y las seis medianas: tarea `task_c44afa5d78b9`,
dispatch `ctx_915ee5e34ff2`, mensaje `msg_d97b1df75d77`. Aprobó con la condición
de explicar el posprocesado del JSON, ya incorporada: se sustituyeron las
salidas completas repetidas por su SHA256, sin cambiar ninguna muestra ni
resumen. El script incorporado reproduce ese formato compacto; el ejecutado
guardaba las salidas completas. El cast del simulador quedó en `2fda65e`.


## J13: candidato retirado por rendimiento

El TXT v2 conservaba disponibilidad de política y añadía un pie de finalización,
con lectura compatible de v1. Claude aprobó la implementación, y pasaron las
pruebas de los cinco formatos, uno/dos muses y sanitizadores. La comparación de
partidas encontró +19,67 % con la genérica pública; se confirmaron cinco pares
adicionales por su proximidad al límite: **+21,29 %**, 1,3423 → 1,6281 s para
10.000 partidas. Cambian las decisiones y la duración de las partidas; no es
una pérdida del entrenamiento ni de una consulta de trabajo idéntico.

El entrenamiento público y la tabla conservaron checkpoints/valores, con
−1,35 % y +3,09 % de tiempo. Usar el mismo TXT v1 en ambos ejecutables tampoco
mostró pérdida. La explicación y los límites de la medición están en el
[informe de bloqueo](../ASTRA_BLOQUEO_RENDIMIENTO_J13_2026-09-11.md), con
[datos](review-performance/J13-blocked.json) y [parche](review-performance/J13-blocked.patch).
El candidato se retiró y el parche pasa `git apply --check` contra `f723699`.
El usuario rechazó aplicar J13 y autorizó continuar con los siguientes bloques.
Se conserva el comportamiento TXT de J12. La aprobación de la implementación es la tarea
`task_28ef84a839e1`, dispatch `ctx_2f85833c6973`, mensaje `msg_37ab196ec5ac`.

Claude verificó las medidas: tarea `task_186ec2467f47`, dispatch
`ctx_5d9b7195b430`, mensaje `msg_14a555c99e52`. Se precisó que la regla automática
no se activó con +19,67 %: los cinco pares adicionales fueron una decisión
por proximidad al límite. El efecto ronda el 20 %; con las ocho parejas la
razón de medianas da +20,03 %, y la mediana de razones por pareja, +19,32 %.
Se conserva una pareja con tiempos anormalmente altos (2,4248 s y 1,8412 s).
El candidato queda retirado por prudencia ante ese coste fronterizo; no se
presenta el +21,29 % de la confirmación como una penalización exacta universal.


## J18: codificación directa y vecinos por distancia

El codificador obtiene el código de la mano a partir de los campos observados,
sin probar sucesivamente todas las manos. Conserva una decodificación y la
comparación de campos para validar la entrada, además de la validación final
de la clave. Los resúmenes no requieren conservar las cartas originales; se
mantienen los campos ignorados y los errores anteriores.

La búsqueda histórica recorre los pares de acumulado/tanteo por distancia
creciente dentro de cada plantilla. Termina una distancia completa antes de
parar, para conservar todos los desempates de la lista de hasta 64 candidatos.
Las variantes pública y con marcador siguen fijando los pagos y contratos
pendientes. No cambia el presupuesto de lecturas, las decisiones, los esquemas
ni el código de C-CFR. No se ha aplicado J13.

Pasan la batería completa, núcleo/CLI de uno y dos muses, la guarda del bot con
dos muses y ASan/UBSan en las rutas modificadas. Una prueba compara los 64
candidatos contra la enumeración exhaustiva en 1.350 casos de los tres formatos
con órdago, cinco lances, empates, límites y apuestas grandes. Las 330 manos y
sus resúmenes conservan las claves, incluidos campos inactivos con valores
obsoletos. Una comparación adicional de **1.113.900 llamadas** entre padre y
candidato conserva estado devuelto y clave, incluidas entradas inválidas.
El código y el resultado de ese ensayo se conservan con las medidas.

[Rendimiento reproducible](review-performance/J18.json): tres parejas alternadas,
CPU0, semilla 7, mismos datos y compilación sin LTO en ambos casos. Los
checkpoints, valores de continuación y
resultados completos de partidas coinciden entre padre y candidato.

| Carga | Padre | J18 | Cambio de tiempo |
|---|---:|---:|---:|
| Entrenamiento público, 500 iteraciones y guardado | 9,6052 s | 9,8536 s | +2,59 % |
| Tabla desde 35:35, 1.000 iteraciones y 10.000 muestras/celda | 3,7336 s | 3,7344 s | +0,02 % |
| 1.000 partidas, TXT histórico frente a `three` | 1,0266 s | 0,9280 s | −9,60 % |
| 10.000 partidas, TXT público frente a `three` | 1,4050 s | 1,2873 s | −8,37 % |

Las partidas usan las políticas congeladas de 500 iteraciones de J12; no se
atribuye fuerza competitiva ni convergencia a esos ejemplos. El tiempo incluye
carga y salida; el tiempo interno de juego baja un 9,61 % y un 8,42 %.
El RSS mediano cambia +0,002 % en entrenamiento, +0,17 % en tabla, +4,13 % en
partidas históricas y +1,29 % en públicas. Ninguna carga activa el límite.
La pequeña variación de entrenamiento no demuestra una alteración del coste
por nodo; esta optimización actúa en codificadores externos y búsqueda del bot.

Además se miden **76 casos sintéticos**: consultas exactas, interpoladas y sin
datos en cada lance, más exactas/ausentes en mus y descarte, con estrategias
históricas/públicas en memoria y TXT. Cada carga se calibra con el padre y se
fija para ambos. Las variaciones medianas por caso van de −91,92 % a +12,42 %;
se conservan todas. Empeoran 26 de las 76, con un máximo de +12,42 % en
una consulta exacta de punto con TXT histórico. Son políticas pequeñas, cuatro
reyes (cuatro pitos en punto), y no representan la mezcla de una partida ni
el coste de cargar un checkpoint grande.

El perfil instrumentado separa generación, codificación y consulta/validación
de política; esta última incluye la validación de acciones y probabilidades,
no solo E/S. Sirve para atribuir trabajo, no para calcular aceleraciones: los
relojes añaden coste. En los casos históricos ausentes de chica, pares, juego
y punto se pasa de 149 a 67 codificaciones por decisión, manteniendo 64
lecturas. Por ejemplo, en pares con almacén en memoria, la consulta completa
sin instrumentar baja un 91,92 %; con TXT, un 89,90 %. El efecto menor en
partidas refleja su mezcla de consultas y otros costes.

Se estudió también indexar plantillas. El perfil permitió reducir generación
y codificación sin añadir ese índice; no se incorpora una optimización extra
sin medirla. La búsqueda pública/con marcador ya evita el recorrido de
acumulados y tanteos. La equivalencia de esta última está cubierta por las
pruebas; no se presentan las consultas genéricas como mediciones aisladas de
cada celda híbrida.

Claude aprobó la implementación: tarea `task_0bb2fdb88071`, dispatch
`ctx_c00dc37077cd`, mensaje `msg_bcb0be5a7daa`;
[revisión](review-performance/J18-claude.md). Se documentó el oráculo anterior
y se añadieron casos inválidos explícitos para los tres codificadores.

Claude verificó las cifras finales: tarea `task_02edb655881f`, dispatch
`ctx_46ba406d166b`, mensaje `msg_1797a4c03a18`;
[revisión de medidas](review-performance/J18-claude-final.md). Aprobó sin bloqueos.
Se incorporaron sus precisiones sobre las 26 consultas que empeoran y el
resultado del codificador padre, idéntico al candidato.


## J19: candidato retirado por memoria

El candidato reutiliza cada espacio de trabajador después de guardar su celda,
manteniendo orden de callbacks, un máximo de N tablas vivas y publicación de
la diagonal completa. La primera celda permanece en el llamador; los reemplazos
pueden entrenarse mientras se guarda otra. No modifica C-CFR. Pasan núcleo,
coordinador, recuperación, dos muses, ASan/UBSan y TSan en las rutas modificadas.
Claude revisó la implementación y pidió proteger los mensajes compuestos de
consola y explicitar la concurrencia de callbacks; ambas correcciones están en
el parche retirado y la prueba de salida pasó con los sanitizadores.

En tres pares, las 120 celdas desde 32:32 con cuatro trabajadores tardan
14,0327 → 10,1897 s (−27,39 %), con RSS de 263,35 → 322,52 MiB (+22,47 %).
Los checkpoints y valores son idénticos. El control serial da +4,95 % de tiempo
y +0,15 % de RSS. Las tablas pequeñas activaron el límite de memoria: en cinco
pares adicionales de lotes de 100 tablas, el pico del lote pasa de 6,11 a
8,64 MiB (**+41,52 %, +2,54 MiB**), mientras el tiempo baja un 16,10 %.
El pico del lote es el máximo de sus procesos consecutivos; la mediana de
los 500 procesos individuales por versión sube un 15,41 %. Se explicita esta
diferencia para no presentar el +41,52 % como el aumento típico de cada tabla.

J19 queda retirado, sin commit de implementación, a la espera del usuario.
J18 continúa aplicado y J13 descartado. El [informe de bloqueo](../ASTRA_BLOQUEO_RENDIMIENTO_J19_2026-09-12.md)
conserva el [parche](review-performance/J19-blocked.patch), las
[mediciones](review-performance/J19-blocked.json), sus límites y la revisión.

Claude verificó el parche corregido y el límite: tarea `task_d0649b43c015`,
dispatch `ctx_9e48de53ea50`, mensaje `msg_98714cae4f5a`;
[revisión final](review-performance/J19-claude-gate.md). Aprobó la retirada.
El informe muestra también la dispersión de memoria en la carga normal:
una ejecución del candidato alcanzó 353,7 MiB, aunque su mediana fue 322,52 MiB.
