# ASTRA — Bloqueo de J01 por rendimiento

11 de septiembre de 2026. Rama `PabloMoralesEscandon/mejoras`.
Base del candidato: `83c0a28`.

**Actualización: el usuario ha aceptado el coste y ha pedido conservar J01.**
El núcleo/CLI se recupera con las observaciones incorporadas, pruebas
completas y una nueva aprobación de Claude (`msg_a35a82f8cf47`). El resto de este informe conserva las mediciones y el historial
de la pausa; las referencias a «retirado» describen esa decisión inicial.
El [registro de trabajo](docs/review-progress.md) refleja el estado actual.

**J01 se ha retirado del código y no tiene commit.** La confirmación de cinco
pares supera ampliamente el límite de rendimiento acordado. Se conserva el
[parche del candidato](docs/review-performance/J01-blocked.patch) y se espera
la decisión del usuario antes de continuar. Los nueve bloques anteriores siguen
aplicados, cada uno con revisión de Claude mediante Orca y commit.

## Resultado que activa la pausa

Se compara el formato histórico `ordago` del padre con el candidato
`ordago-public`: 500 iteraciones, semilla 7, un hilo, afinidad al procesador
lógico 0, mismo compilador y biblioteca C-CFR. Se alterna el orden de ejecución.
No se ejecutaron compilaciones, pruebas ni otros benchmarks propios durante
las mediciones. Los tiempos incluyen el proceso de entrenamiento sin exportar
checkpoints. Se guardan también los tiempos internos y contadores del solver.

Tres pares iniciales dieron +194,57 % de tiempo y +124,58 % de RSS.
Los cinco pares adicionales confirman:

| Medida | `ordago` histórico | `ordago-public` candidato | Cambio |
|---|---:|---:|---:|
| Mediana de tiempo de proceso | 1,6681 s | 4,9279 s | **+195,42 %** |
| Mediana del tiempo interno de entrenamiento | 1,6460 s | 4,8934 s | +197,29 % |
| Mediana del pico de RSS | 596.724 KiB | 1.342.896 KiB | **+125,04 %** |
| Nodos visitados por ejecución | 20.174.542 | 42.630.116 | +111,31 % |
| Conjuntos de información | 3.492.885 | 11.018.683 | +215,46 % |

El límite establecido era +20 % de tiempo o +25 % de RSS, confirmado con cinco
pares adicionales. El nuevo modelo recorre más nodos y crea más conjuntos de
información. Cambia la semántica del juego y las claves: el mismo número de
iteraciones no demuestra la misma calidad de política. No se midió aquí una
mejora en tasa de victoria. La corrección funcional sigue sujeta al límite de
rendimiento solicitado.

En los tres formatos históricos, al ejecutar el candidato con sus respectivos
formatos anteriores, las medianas de tiempo variaron +5,76 % (`single-bet`),
+2,22 % (`raises-to-eight`) y −0,36 % (`ordago`). Sus contadores deterministas
se conservaron. Esas diferencias no alcanzan el límite ni prueban una mejora.
El coste grave aparece al entrenar el modelo público nuevo.

Los [datos completos](docs/review-performance/J01.json) contienen los 34 procesos,
los comandos, salidas, contadores, afinidad, resúmenes y hashes de ejecutables.
Las ocho repeticiones de cada modelo en la comparación afectada conservan sus
respectivos contadores. No se extrapolan aceleraciones acumuladas entre lotes
de mediciones de cambios anteriores.

## Alcance del candidato conservado

Se añade un formato explícito `ordago-public`, con esquema incompatible, para
reentrenar una mano aislada usando pagos públicos y contratos pendientes.
El objetivo de partida y el modo contable son conceptos separados. Los dejes
se pagan durante la negociación; los envites aceptados y los tantos intrínsecos
se cuentan al final. Se cuentan todos los lances, limitando a 40 cada total:
40:1 da utilidad +39 para mano. Un órdago aceptado sustituye los pagos por
40:0 o 0:40 y termina inmediatamente.

Se conservan el comportamiento y esquema histórico v9, y la identidad y
contabilidad de las tablas v4. El bloque incluye núcleo, codec, entrenamiento
de uno/varios hilos, CLI, reanudación binaria/comprimida, exportación y diagnóstico
de lances finales con ambas manos fijadas. La integración de la política nueva
en el adaptador y la precarga estaba prevista en bloques posteriores y sigue
pendiente. El problema original de información oculta del formato histórico
**sigue sin corregirse en el código aplicado**, puesto que este candidato se
ha retirado.

## Comprobaciones funcionales antes de retirar

Pasaron `make -j4 test`, el núcleo/rutas rápidas/pruebas nuevas con dos muses,
y esas tres baterías con AddressSanitizer y UndefinedBehaviorSanitizer.

- 47.720 comparaciones entre manos ocultas con la misma observación pública
  con un mus; 89.824 con dos. Se incluyen ambos jugadores y todas las fases
  de decisión, muses, descartes, declaraciones y varias historias de envite.
- Reproducciones de ambos informes originales, claves históricas fijadas como
  referencia y contraste de claves con v4 a 0:0.
- Mil recorridos aleatorios: se contrastan todos los hijos públicos y rápidos
  de los estados recorridos, validación contable completa y deshacer exacto.
- Utilidad calculada manualmente, recuento después de llegar a 40 y sustitución
  de pagos anteriores por un órdago aceptado.
- Entrenamiento, carga, reanudación y exportación del esquema nuevo, rechazo
  de checkpoints incompatibles en ambos sentidos y entrenamiento concurrente.

Las pruebas detectaron dos errores del candidato durante su desarrollo y
quedaron incorporadas como regresiones. Un órdago aceptado podía añadir
pendientes de lances posteriores; se detiene ahora ese avance. El dominio del
codec también debía permitir llegar a 40 antes del último lance: aceptar 27 y
12 y rechazar un órdago sin cubrir lleva el presupuesto público a 40; cobrar
dejes de 17, 22 y 1 alcanza 40 pagados antes de punto. El codec público admite
pagos 0..40, mientras v4 conserva pagos por debajo del objetivo.

La comparación completa byte a byte de checkpoints históricos y de tablas v4
no llegó a ejecutarse para J01: se detuvo el trabajo tras confirmar el límite.
Las pruebas existentes y los contadores de entrenamiento sí se comprobaron.

## Segunda opinión de Claude

Revisión de diseño por Orca: `task_01b746042ef5`, dispatch `ctx_08a071c1b808`,
mensaje `msg_35f0b13bf441`. Se incorporó su propuesta de modo contable y esquema
explícitos. Su hipótesis sobre el límite del codec se corrigió con los dos
contraejemplos anteriores; no se trató como una condición válida sin probarla.

Revisión de implementación: `task_664c5e0eef1e`, dispatch `ctx_37db6f8f21b1`.
**APPROVE para núcleo/CLI**, mensaje `msg_77f6aee17f98`.
Claude revisó la copia exacta conservada tras retirar el candidato y confirmó
la separación de modos, la semántica contable y la compatibilidad histórica.
También confirmó que su límite inicial del codec era incorrecto.
Su [informe completo](docs/review-performance/J01-claude-review.md) queda guardado.
La aprobación funcional no anula la pausa por rendimiento.

Se conservan para la siguiente intervención sus mejoras no bloqueantes: mover
el mensaje de éxito del script CLI al final; ampliar los grupos de invariancia
con subidas y rechazos de órdago; rechazar explícitamente estados construidos
con un órdago aceptado y fase no terminal; reforzar las referencias históricas
con un resumen de claves/menús; y aclarar el origen público de los campos de
fuerza. Son propuestas razonables, pero no se modifica el candidato retirado
mientras esté pendiente la decisión de rendimiento. Las subidas y órdagos sí
están presentes en otras pruebas; faltan en esos grupos específicos de
comparación entre manos ocultas.

## Estado de los cambios ya aplicados

| Bloque | Cambio | Commit principal |
|---|---|---|
| J03 | Restaurar la diagonal tras fallar su publicación | `1bc7b63` |
| J04 | Rechazar estrategias TXT truncadas a la cabecera | `9fad60d` |
| J05 | Acotar candidatos de precarga con los pagos de ambos jugadores | `4aac902` |
| J15 | Rechazar fases de azar/terminal en la clave rápida | `812ab0e` |
| J16 | Validar la gramática de enteros sin signo en ambos CLIs | `ccf1367` |
| J14 | Rechazar el adaptador con un límite incompatible de muses | `f8831eb` |
| J02 | Recuento diferido en el simulador de dos/cuatro jugadores | `d7bfe25` |
| J06 | Usar operaciones de confianza en la evaluación exacta local | `b898c5b` |
| J07 | Mezclar los bits altos en la tabla de mejor respuesta | `83c0a28` |

J02 incluye el commit `c837741` del submódulo del simulador. C-CFR no se ha
modificado. El [registro de trabajo](docs/review-progress.md) detalla para cada
bloque las pruebas, revisiones de Claude, decisiones y mediciones.

Siguen pendientes J01, J08, J09, J10, J11, J12, J13, J17, J18, J19, J20, J21,
J22 y J23. No se han aplicado otros cambios tras confirmar esta regresión.

## Material conservado y decisión pendiente

El parche técnico conserva código, pruebas y documentación del candidato y
excluye el registro de progreso, que refleja la retirada. Pasa `git apply
--check docs/review-performance/J01-blocked.patch` sobre el árbol actual.
Su SHA-256 es `6d5e79911faae67eee4705a43f991fc744240e9c6dbe58475d43fe416b60562c`.
Los ejecutables del candidato quedan aislados en `build/review-j01`; el árbol
de fuentes y los ejecutables de depuración habituales vuelven al código de
`83c0a28`. El informe, las mediciones y el parche quedan sin commit mientras se
espera la decisión del usuario.

La siguiente decisión es investigar cómo reducir el coste de J01, aceptar
explícitamente ese coste para continuar con su integración, o dejar J01 pendiente
y continuar con los demás bloques. Recomiendo investigar primero el coste de
J01. No se inicia ninguna de esas opciones hasta recibir respuesta.

## Aclaración del aumento de ramas, tras la pregunta del usuario

Eliminar una distinción oculta fusiona conjuntos de información si se conserva
el resto de la codificación y el árbol. J01 también cambia los límites de envite
y sustituye el máximo de tanteo por los pagos públicos de ambos jugadores. La
explicación inicial del aumento fue incompleta: estos cambios tienen efectos
distintos y no se había aislado su contribución al coste total.

Una comprobación adicional, sin modificar el código aplicado, fija dos pitos
y dos reyes para mano, dos cuatros y dos sietes para postre, no mus, grande +10
querido y chica +10 querido. Antes de pares:

| Medida | Histórico | Candidato público |
|---|---|---|
| Tanteo interno | 20:0 | 0:0, pendientes de resolver |
| Pagos públicos | 0:0 | 0:0 |
| Acumulado de envites aceptados | 20 | 20 |
| Términos del límite numérico, antes del nuevo envite | 20 + 20 | 0 + 20 |
| Acciones al abrir pares | Pasar, órdago | Pasar, +2, +4, +7, +10, órdago |
| Apariciones de estados en todas las continuaciones hasta terminal | 33 | 12.489 |

El histórico utiliza el máximo de los puntos liquidados con cartas ocultas y
además el acumulado previo: en esta historia cuenta los mismos 20 aceptados en
ambos términos, cerrando los envites numéricos. El candidato permite más acciones
y sus continuaciones. El recuento de esta tabla enumera todas las acciones con
el mismo reparto; no usa entrenamiento, política aprendida ni aleatoriedad, y
no es el contador de infosets únicos.

Al sustituir únicamente la mano rival por un pito y tres reyes, las claves
históricas son distintas (511864500 / 511854660), mientras el candidato usa la
misma clave 20985617940 y el mismo menú. Por tanto, la fusión de esa observación
sí ocurre a la vez que se amplía el árbol de acciones.

Además, conservar por separado los pagos públicos de ambos jugadores puede
separar historiales que un único máximo agrupaba. MCCFR comparte la estrategia
por infoset; no elimina por ello todos los recorridos de estados concretos que
lo alcanzan. Los 3,49/11,02 millones de la medición original son infosets
almacenados durante el entrenamiento, y los 20,17/42,63 millones son visitas
acumuladas, no un censo completo del árbol de juego.

La prueba y su salida se conservan en `branch_counterexample` del
[JSON de J01](docs/review-performance/J01.json). Demuestra una causa concreta
de expansión; todavía no cuantifica qué parte del aumento global corresponde
a menús, distinciones de la clave pública y cambios en las trayectorias de
entrenamiento. No demuestra que el sobrecoste completo sea inevitable.
