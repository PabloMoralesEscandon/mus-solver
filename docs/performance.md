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

`--threads N` aplica paralelismo entre celdas de una misma diagonal. Cada celda
conserva su propio almacén y semilla. Las llamadas para guardar estrategias
ocurren en el hilo del llamador y en orden de marcador. Solo se publican los
valores cuando termina la diagonal. La reanudación de un checkpoint individual
sigue usando un hilo.

La memoria aumenta con el número de celdas simultáneas. La reserva de infosets
también se aplica por celda. Las diagonales estrechas y los grupos con una celda
mucho más costosa limitan la aceleración; aumentar los hilos no garantiza una
mejora proporcional.

## Resultados medidos

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

## Verificación

```sh
make test
make test-sanitize
make test-tsan
```

Las pruebas cubren los 330 multiconjuntos de cartas para los descartes, recorridos
con validación de cada hijo y undo en todos los marcadores a objetivos 3, 5 y 40,
y la igualdad byte a byte de tablas y checkpoints con distintos números de
hilos. También cubren precarga, reanudación, orden de callbacks y fallo antes de
publicar una diagonal. Las pruebas de C-CFR comprueban la caché con 2, 8 y 64
acciones, los menús incompatibles y la recuperación ante fallos de asignación.
