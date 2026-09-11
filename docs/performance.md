# Rendimiento de `ordago-score`

Las optimizaciones conservan las acciones legales y su orden, las claves de
información, la abstracción `mus.ordago-score/v4`, el muestreo MCCFR y los formatos
de persistencia. No se reducen las iteraciones ni las muestras de evaluación.

El tanteo actualiza los cobros de cada lance y resuelve los contratos pendientes
al final. La validación conserva el cálculo completo como referencia
independiente. Los descartes canónicos y las condiciones comunes de las apuestas
se calculan una vez por menú. La evaluación restaura el estado activo sin borrar
todo el historial de undo en cada muestra.

C-CFR conserva una caché de nodos durante cada llamada de entrenamiento,
reutiliza divisiones de la normalización de regrets y evita escrituras
innecesarias en las consultas y en las acciones muestreadas. Estos cambios están
en el submódulo `C-CFR`, además de los cambios del repositorio principal.

## Medición de una celda

```sh
make benchmark-score
python3 tests/benchmark_score.py build/release/benchmark-score
```

El programa mide entrenamiento y evaluación por separado. Usa una tabla
inmutable de continuaciones sintéticas `V[a][b] = (a-b)/40` para poder medir
`39:39`, `35:18`, `20:20` y `0:0` sin entrenar las 1.600 celdas. Estos valores
definen una carga reproducible; no representan una política resuelta.

La reserva explícita se incluye en el tiempo de entrenamiento. La inicialización
del juego y la serialización para verificar el checkpoint quedan fuera. Cada
resultado incluye el número de infosets, nodos visitados, valor evaluado y hash
del checkpoint completo. La comparación exige igualdad exacta de esos datos.

```sh
python3 tests/benchmark_score.py build/release/benchmark-score \
    --baseline /ruta/al/benchmark-anterior --repeats 3 --seed 7
```

`--quick` reduce las iteraciones para comprobar equivalencia con rapidez.
`--samples`, `--seed` y `--reserve` permiten cambiar la carga de forma explícita.
Para fijar un procesador en Linux se puede anteponer `taskset -c 0`. Deben
evitarse compilaciones u otros benchmarks simultáneos al medir rendimiento.

Para construir el mismo programa contra una copia anterior del proyecto:

```sh
make -C /ruta/al/proyecto-anterior
cc -std=c17 -O2 -flto -pthread \
    -I/ruta/al/proyecto-anterior/include \
    -I/ruta/al/proyecto-anterior/C-CFR/include \
    tests/benchmark_score.c \
    /ruta/al/proyecto-anterior/build/release/libmus_solver.a \
    /ruta/al/proyecto-anterior/C-CFR/build/release/libcfr.a \
    -lzstd -o /tmp/benchmark-score-anterior
```

La copia anterior debe contener sus versiones originales de ambos submódulos.
Si cambia el límite de muses, debe usarse el mismo valor al compilar el programa
de medición y las bibliotecas.

## Medición de diagonales completas

```sh
make
python3 tests/benchmark_score_table.py build/release/mus-solver \
    --baseline /ruta/al/mus-solver-anterior --threads 1 4 --repeats 3
```

La carga predeterminada entrena las 45 celdas con suma al menos 70, hasta
`35:35`, a objetivo 40. Ejecuta 1.000 iteraciones y 10.000 muestras por celda con
semilla 7. Cada ejecución crea un directorio temporal nuevo y lo elimina al
terminar. El tiempo incluye entrenamiento, evaluación y guardado comprimido.
Las iteraciones por segundo son las 45.000 iteraciones divididas por ese tiempo.
La comprobación SHA-256 de todos los archivos queda fuera del tiempo medido y
exige igualdad entre todas las variantes.

Si la referencia es anterior a las estadísticas de evaluación, añada
`--ignore-sampling-statistics`: compara los checkpoints completos y los seis
campos originales de cada valor, omitiendo solo el sufijo y las columnas de
varianza. La salida usa `policy_files_and_values_equal` y
`sampling_statistics_checked=false`; con la opción omitida también se exige
igualdad de las estadísticas guardadas.

`--threads N` aplica paralelismo entre celdas de una misma diagonal. Cada celda
conserva su propio almacén y semilla. Las llamadas para guardar estrategias
ocurren en el hilo del llamador y en orden de marcador. Solo se publican los
valores cuando termina la diagonal. La reanudación de un checkpoint individual
admite `--threads N` para compartir su única estrategia.

En el coordinador, `--threads-per-cell M` comparte el almacén CFR de cada celda
entre hasta M hilos. `--threads 1 --threads-per-cell 8` entrena una estrategia
cada vez con hasta ocho hilos. `--threads 2 --threads-per-cell 4` puede entrenar
dos estrategias con hasta ocho hilos en total. Las iteraciones se reparten y
los contadores se agregan antes de guardar. Este modo conserva la abstracción,
pero el orden concurrente de las actualizaciones no es reproducible bit a bit.
Las comparaciones de igualdad de los benchmarks anteriores usan un hilo por
estrategia.

La memoria aumenta con el número de celdas simultáneas. La reserva de infosets
también se aplica por celda. Las diagonales estrechas y los grupos con una celda
mucho más costosa limitan la aceleración; aumentar los hilos no garantiza una
mejora proporcional.

## Resultados anteriores: un hilo por celda

Estas mediciones corresponden al commit `26e492b`, anterior al modo de varios
hilos sobre una misma estrategia. Las mediciones de rendimiento y memoria de
esta versión usan un hilo por celda; no miden `--threads-per-cell`.

Medianas de tres ejecuciones en un AMD Ryzen AI 7 350, GCC 16.1.1, frente al
commit `e69fdc0` con sus submódulos originales. La versión anterior usa `-O2` y
LTO; la optimizada usa `-O3` y LTO. Se alternó el orden de las variantes y no se
ejecutaron otras compilaciones ni benchmarks durante esta medición.

Tabla completa de la carga `35:35` descrita arriba, incluyendo evaluación y
persistencia:

| Versión | Hilos | Tiempo | Iteraciones/s del proceso | Mejora |
| --- | ---: | ---: | ---: | ---: |
| Anterior | 1 | 4,116 s | 10.932 | — |
| Optimizada | 1 | 3,709 s | 12.134 | 1,11× |
| Optimizada | 4 | 2,209 s | 20.368 | 1,86× |
| Optimizada | 8 | 1,900 s | 23.687 | 2,17× |

Las 46 salidas de cada ejecución —tabla y 45 checkpoints— tienen los mismos
hashes SHA-256. Estas cifras describen esta carga y esta máquina; la distribución
de marcadores, el crecimiento de los almacenes y la memoria disponible cambian
el rendimiento.

Entrenamiento de celdas individuales con continuaciones sintéticas, un hilo
fijado a CPU 0 y semilla 7. Las muestras de evaluación y el guardado del
checkpoint quedan fuera de estas tasas:

| Marcador | Iteraciones | Antes, iteraciones/s | Después, iteraciones/s | Mejora |
| --- | ---: | ---: | ---: | ---: |
| 39:39 | 100.000 | 102.151 | 157.628 | 1,54× |
| 35:18 | 10.000 | 3.077 | 3.267 | 1,06× |
| 20:20 | 1.000 | 273 | 303 | 1,11× |
| 0:0 | 300 | 74,07 | 80,31 | 1,08× |

La evaluación de 100.000 muestras mejoró entre 1,19× y 1,34× en esas cuatro
celdas. Los valores, hashes de checkpoints, infosets y nodos visitados coinciden
en todas las comparaciones. Los resultados completos están en
[performance-results.json](performance-results.json).

## Memoria medida

Se midió el pico de memoria residente de cada proceso con `wait4` y
`ru_maxrss` en Linux. Cada variante se ejecutó dos veces en procesos nuevos,
alternando el orden. Los rangos muestran los dos picos, en MiB.

Para las mismas 45 celdas de la carga `35:35`, incluyendo evaluación y guardado:

| Versión | Hilos | Pico de RAM |
| --- | ---: | ---: |
| Anterior | 1 | 36,88–37,04 MiB |
| Optimizada | 1 | 37,18–37,29 MiB |
| Optimizada | 4 | 84,31–86,29 MiB |
| Optimizada | 8 | 132,87–135,24 MiB |

Para `0:0`, con 300 iteraciones, 100.000 muestras y continuaciones sintéticas,
la versión anterior alcanzó 1.065,44–1.065,53 MiB y la optimizada
1.065,19–1.065,84 MiB, ambas con un hilo. Esta medida incluye la serialización
del checkpoint usada por el benchmark para comprobar equivalencia.

La memoria por celda queda prácticamente igual: se conservan los mismos nodos
y acumuladores CFR. Reducir cálculos y escrituras no reduce su almacenamiento.
El paralelismo aumenta el pico porque mantiene varios almacenes simultáneos.
En la carga de 45 celdas, ocho hilos requieren aproximadamente 3,6 veces la RAM
de la ejecución anterior con un hilo. Esta proporción no es un límite para
otros marcadores o presupuestos de entrenamiento.

## Hilos sobre un mismo marcador: 2026-09-08

Se ha medido la implementación nueva con 1, 2, 4, 8 y 14 hilos que comparten
un único almacén. Son medianas de tres procesos nuevos por configuración,
ejecutados de uno en uno y alternando el orden de los hilos entre repeticiones.
La máquina es el mismo Ryzen AI 7 350: 8 núcleos físicos y 16 procesadores
lógicos, todos disponibles para cada ejecución. La compilación usa GCC 16.1.1,
`-O3 -flto` y el límite predeterminado de un mus.

Se mantienen la semilla 7, la abstracción y el total de iteraciones por marcador:
500.000 en `39:39`, 10.000 en `35:18`, 1.000 en `20:20` y 300 en `0:0`.
Las continuaciones son las mismas sintéticas e inmutables del benchmark de una
celda. Se mide solo entrenamiento, incluida la preparación concurrente y la
creación y espera de los hilos. No se reserva capacidad explícita, ni se evalúa
o guarda la estrategia. Las compilaciones y otras suites de pruebas terminaron
antes de iniciar la medición.

Iteraciones por segundo:

| Marcador | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 14 hilos |
| --- | ---: | ---: | ---: | ---: | ---: |
| 39:39 | 221.609 | 385.338 | 438.346 | 720.285 | 930.293 |
| 35:18 | 3.152 | 4.901 | 7.654 | 11.965 | 15.635 |
| 20:20 | 302,0 | 420,7 | 538,1 | 703,7 | 797,5 |
| 0:0 | 80,0 | 114,9 | 141,2 | 176,1 | 187,6 |

Aceleración respecto a un hilo, calculada como tiempo mediano con un hilo
dividido entre tiempo mediano con N hilos:

| Marcador | 2 hilos | 4 hilos | 8 hilos | 14 hilos |
| --- | ---: | ---: | ---: | ---: |
| 39:39 | 1,74× | 1,98× | 3,25× | 4,20× |
| 35:18 | 1,56× | 2,43× | 3,80× | 4,96× |
| 20:20 | 1,39× | 1,78× | 2,33× | 2,64× |
| 0:0 | 1,44× | 1,76× | 2,20× | 2,35× |

En `0:0`, pasar de 8 a 14 hilos añade un 6,5% de iteraciones por segundo. En
`20:20` añade un 13,3%; en los otros dos marcadores, aproximadamente un 30%.
Son tasas de recorrido, no una medida de convergencia a la misma calidad de
estrategia. Las semillas auxiliares y las actualizaciones concurrentes cambian
los nodos visitados y el crecimiento del almacén. Por ejemplo, `0:0` visita una
mediana de 88.843 nodos por iteración con un hilo y 77.370 con catorce. Los
tiempos de sus tres ejecuciones con catorce hilos van de 1,33 a 1,79 segundos;
esa variación limita la precisión de la comparación entre ocho y catorce.

Pico de RAM residente después del entrenamiento, mediana en MiB:

| Marcador | 1 hilo | 2 hilos | 4 hilos | 8 hilos | 14 hilos |
| --- | ---: | ---: | ---: | ---: | ---: |
| 35:18 | 132 | 135 | 138 | 145 | 155 |
| 20:20 | 614 | 608 | 640 | 694 | 737 |
| 0:0 | 1.007 | 944 | 1.037 | 1.142 | 1.342 |

La memoria incluye un almacén compartido y los espacios de trabajo de sus
hilos. Puede bajar en alguna ejecución porque cambia la cantidad de infosets;
no significa que añadir hilos reduzca por sí mismo la memoria. En `0:0`, la
mediana con catorce hilos es un 33% mayor que con uno. Estas cifras no incluyen
evaluación ni serialización, a diferencia de la medición de memoria anterior.

Para repetir la medición:

```sh
make benchmark-score-threads
python3 tests/benchmark_score_threads.py build/release/benchmark-score-threads \
    --threads 1 2 4 8 14 --repeats 3 --seed 7 \
    --output /tmp/performance-shared-threads-results.json
```

Las 60 ejecuciones, sus nodos e infosets, los rangos y las medianas están en
[performance-shared-threads-results.json](performance-shared-threads-results.json).
El programa también permite medir una configuración individual con
`build/release/benchmark-score-threads A B ITERACIONES HILOS [SEMILLA]`.

## Verificación

El 2026-09-08 se han compilado y ejecutado las tres suites siguientes con
`-j2`, incluido el soporte nuevo de hilos por estrategia. Todas han terminado
correctamente, sin errores de ASan/UBSan ni avisos de ThreadSanitizer.

```sh
make test
make test-sanitize
make test-tsan
```

Las pruebas cubren los 330 multiconjuntos de cartas para los descartes, recorridos
con validación de cada hijo y undo en todos los marcadores a objetivos 3, 5 y 40,
y la igualdad byte a byte de tablas y checkpoints con distintos números de
celdas simultáneas y un hilo por estrategia. También cubren precarga,
reanudación, orden de callbacks y fallo antes de publicar una diagonal. Para
una estrategia compartida, comprueban el reparto de iteraciones, los contadores
acumulados, la cancelación, los bloques incompletos, el exceso de hilos y la
persistencia de checkpoints. Las pruebas de C-CFR comprueban la caché con 2, 8 y 64
acciones, los menús incompatibles y la recuperación ante fallos de asignación.
