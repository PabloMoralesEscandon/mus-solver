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
| J02 | Recuento diferido en el simulador de dos/cuatro jugadores | Pendiente |
| J03 | Reintento tras fallo al guardar una diagonal | Completado |
| J04 | Rechazar TXT truncado a su cabecera | Completado |
| J05 | Excluir candidatos de precarga incompatibles con los dejes | Completado |
| J06 | Usar operaciones de confianza en la evaluación exacta local | Pendiente |
| J07 | Mezclar bits en la tabla de mejor respuesta | Pendiente |
| J08 | Reutilizar celdas terminadas de una diagonal interrumpida | Pendiente |
| J09 | Región opcional de continuaciones cerrada por incrementos y giro | Pendiente |
| J10 | Incertidumbre y presupuesto de evaluación | Pendiente |
| J11 | Precisar el alcance del diagnóstico de repartos fijos | Pendiente |
| J12 | Evaluar partidas con la política desplegada del bot | Pendiente |
| J13 | Conservar disponibilidad de política en TXT | Pendiente |
| J14 | Rechazar explícitamente el adaptador incompatible con dos muses | Pendiente |
| J15 | Rechazar fases de azar/terminal en la clave rápida | Completado |
| J16 | Gramática explícita para opciones enteras sin signo | Pendiente |
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
