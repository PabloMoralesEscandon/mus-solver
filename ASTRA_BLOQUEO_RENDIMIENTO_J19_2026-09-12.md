# J19 retirado por el límite de memoria

Fecha: 2026-09-12. Base: `142e203` (J18). **J19 no está aplicado.**
J13 sigue descartado por decisión del usuario. J18 permanece aplicado.
Se conservan el [parche](docs/review-performance/J19-blocked.patch) y las
[mediciones](docs/review-performance/J19-blocked.json). El parche pasa
`git apply --check` contra la base.

## Qué cambiaba

El coordinador actual espera a que termine toda una tanda de celdas antes de
guardarlas y empezar la siguiente. El candidato guarda cada celda en orden
y reutiliza su espacio para otra de la misma diagonal, mientras las restantes
siguen trabajando. Permite solapar el guardado con otros entrenamientos.

Conserva el límite de N tablas vivas, incluidos resultados pendientes de guardar,
los callbacks en el hilo del llamador y el orden por marcador. La primera celda
sigue calculándose en el llamador, junto con sus compañeros; los reemplazos usan
hilos. Las continuaciones permanecen inmutables hasta publicar toda la diagonal.
Ante un fallo deja de asignar celdas y espera a todos los trabajadores antes de
liberar sus almacenes. Conserva la recuperación y el rollback de publicación.

Se añadió protección de la línea `score-cell`, porque sus varias llamadas a
stdio podían mezclarse con los mensajes de otros trabajadores. El contrato
explicita la concurrencia de guardado con progreso, recuperación y carga de
otras celdas. No se modificó C-CFR ni el simulador. Todas estas modificaciones
están retiradas junto con J19 y conservadas en el parche.

## Resultado medido

Se comparan compilaciones sin LTO, semilla 7 y un hilo por estrategia. El caso
serial usa CPU0; los casos de cuatro trabajadores usan CPU0–3. No se ejecutan
otras compilaciones, pruebas o mediciones propias simultáneas. Los archivos de
política y valores coinciden en todos los ensayos: no se ahorra trabajo de
entrenamiento ni se cambian sus resultados.

| Carga | Tiempo padre → J19 | Cambio de tiempo | RSS padre → J19 | Cambio RSS |
|---|---:|---:|---:|---:|
| 45 celdas desde 35:35, un trabajador, 1.000 iteraciones y 10.000 muestras/celda | 3,4890 → 3,6619 s | +4,95 % | 38,06 → 38,12 MiB | +0,15 % |
| 120 celdas desde 32:32, cuatro trabajadores, mismo trabajo por celda | 14,0327 → 10,1897 s | −27,39 % | 263,35 → 322,52 MiB | +22,47 % |
| Lotes de 100 tablas de 45 celdas, cuatro trabajadores, 1 iteración y 1 muestra/celda; tres pares | 1,0538 → 0,8639 s | −18,02 % | 6,09 → 8,76 MiB | +43,81 % |
| Misma carga pequeña; cinco pares adicionales | 1,0301 → 0,8642 s | −16,10 % | 6,11 → 8,64 MiB | **+41,52 %** |

Las cifras son razones entre medianas de repeticiones alternadas. En un lote,
cada tabla se ejecuta en un proceso nuevo: el tiempo es la suma de los tiempos
de esos 100 procesos y el RSS del lote es el mayor pico de sus procesos. No
hay 100 tablas entrenándose a la vez. El lanzador C mide el RSS del hijo mediante
`wait4`, sin heredar el máximo acumulado por el analizador Python. Se conservan
también las 100 muestras individuales de cada lote.

El límite de comprobación es +20 % de tiempo o +25 % de RSS. La carga pequeña
lo superó en los tres primeros pares; se hicieron cinco adicionales y se
confirmó. Por eso se retira el candidato, aunque **el tiempo mejora** en los
casos paralelos.

El aumento absoluto del pico en esa confirmación es pequeño: **2,54 MiB**
(6.252 → 8.848 KiB). No equivale a una gran penalización de tiempo ni a que todas
las tablas necesiten un 41,52 % más de RAM. Entre los 500 procesos individuales
por versión de la confirmación, la mediana de sus picos es 5.852 → 6.754 KiB
(+15,41 %); la regla se ha aplicado al máximo de cada lote. En la carga de 120
celdas, el aumento mediano es mayor en términos absolutos: **59,17 MiB**, pero
su +22,47 % no activa por sí solo el límite del 25 %. Hay variación entre
ejecuciones: una de las tres del candidato alcanzó 353,7 MiB, un 34,30 % más
que su padre emparejado. La mediana no garantiza que cada ejecución quede
por debajo del límite.

No se oculta el cambio de memoria detrás de la mejora de tiempo. Limitar el
número de tablas vivas no limita exactamente sus bytes ni los buffers auxiliares.
El nuevo orden cambia qué entrenamientos y guardados coinciden. No se ha aislado
cuánto de la subida corresponde a tablas, compresión o gestión del heap; no se
atribuye a una causa concreta sin esa medición.

## Perfil y comprobaciones

Un perfil separado registra inicio/fin de cada celda y de su guardado. Para las
120 celdas, el intervalo instrumentado baja de 13,6870 a 9,8828 s. El tiempo
acumulado en guardados pasa de 7,4898 a 8,1622 s, mientras se solapan otros
entrenamientos; no se acelera la escritura individual. La mediana de espera de
una celda terminada hasta comenzar su guardado baja de 76,95 a 43,07 ms.
Las sumas de entrenamiento y espera se solapan entre celdas y **no se suman al
tiempo total**. Los relojes y mensajes del perfil añaden coste; las comparaciones
de la tabla anterior usan ejecutables sin esa instrumentación.

El perfil aislado también dio más RSS: 263,46 → 374,52 MiB (+42,16 %). Es una
sola ejecución por versión y no sustituye las medianas sin instrumentación.
Se conserva para mostrar la variabilidad y el aumento observado. El perfil
padre procede de la sesión preliminar, antes de modificar el coordinador; el
del candidato se hizo después de validarlo. No fueron una pareja alternada
en la misma sesión.

Pasaron la batería completa antes del ajuste de arranque, y después de ese
ajuste el núcleo, la prueba del coordinador y la recuperación de celdas.
Núcleo/coordinador pasaron también con dos muses, ASan/UBSan y ThreadSanitizer.
Las pruebas comprueban avance sin barrera de tanda, orden de guardado,
checkpoint idéntico al serial cuando falla la creación de hilos, límite de
asignaciones tras fallo y ausencia de publicación de una diagonal incompleta.
La prueba nueva de salida pasó en debug, ASan/UBSan y TSan, con tres tablas
nuevas y una extensión, cuatro trabajadores y un informe por iteración.

Claude revisó el coordinador: tarea `task_020c0e9cfe8b`, dispatch
`ctx_c5f63dfd1e89`, mensaje `msg_6e4edfb87378`;
[revisión inicial](docs/review-performance/J19-claude-initial.md).
Pidió proteger la salida de consola y precisar los callbacks concurrentes;
ambas correcciones están en el parche retirado. Su nota sobre el llamador se
refiere a una versión intermedia: la final vuelve a calcular la primera celda
en él. Durante un guardado quedan como máximo N−1 celdas entrenándose, porque
la que se guarda ocupa un espacio. Puede haber N trabajadores más el llamador
esperando, pero no N celdas entrenándose a la vez que se guarda otra adicional.

## Estado

El árbol de código vuelve a J18 (`142e203`). No se ha hecho commit del código
candidato de J19. Se espera la decisión del usuario sobre este intercambio
entre tiempo y memoria antes de restaurarlo o continuar con J20–J23.
La sincronización de J17 conserva su bloqueo anterior por rendimiento.


Claude confirmó el parche corregido, las cifras y la retirada: tarea
`task_d0649b43c015`, dispatch `ctx_9e48de53ea50`, mensaje `msg_98714cae4f5a`;
[revisión final](docs/review-performance/J19-claude-gate.md). Aprobó sin
correcciones obligatorias. Se incorporaron sus precisiones sobre la dispersión
de RSS de la carga normal y el origen preliminar del perfil padre.
