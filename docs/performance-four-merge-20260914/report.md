# Integración y rendimiento de cuatro jugadores

Se combinan los commits de C-CFR `cfaf1cf` y `1823426` en `1122857`.
El resultado conserva las tres optimizaciones de MCCFR: consulta de snapshots
por clave antes de acceder al almacén compartido, publicación de versiones
sin operaciones atómicas de lectura-modificación-escritura y espera sobre el
nodo que causa un conflicto. La pasada de estrategia media de cuatro jugadores
reutiliza los snapshots de la pasada de regrets.

El adaptador de Mus recalcula solamente el resumen de la mano que cambia al
repartir, robar o descartar. Al deshacer compara esa mano con la del estado
anterior y actualiza su resumen si ha cambiado. Se conservan las reglas,
las claves y el formato de checkpoints. El benchmark admite
`ordago-score-four`, además de los dos formatos anteriores.

## Mediciones

Cada muestra arranca con un almacén vacío y continuaciones sintéticas fijas.
Se mide una celda de entrenamiento, incluidos la preparación del almacén y
el arranque y cierre de los hilos; se excluyen la evaluación y la escritura
de checkpoints. Semilla 7, un mus y objetivo 40. Siete repeticiones por caso,
con orden rotativo de las tres versiones y afinidad de CPU fija.

- **Entrante:** C-CFR `1823426` y el adaptador de cuatro jugadores sin cambios.
- **Merge:** C-CFR `1122857` y ese mismo adaptador.
- **Final:** C-CFR `1122857` y la actualización de una sola mano.

Todas las versiones usan los mismos objetos del solver salvo `mus_four.o`,
y se compilan con `-O3 -flto -pthread` y `-DNDEBUG` en C-CFR.

Equipo: AMD Ryzen AI 7 350 w/ Radeon 860M. Compilador: cc (GCC) 16.1.1 20260625.

Medianas en iteraciones por segundo (más es mejor):

| Marcador | Iteraciones | Hilos | Entrante | Merge | Final |
|---|---:|---:|---:|---:|---:|
| 35:35 | 3000 | 1 | 6623.8 | 6638.0 | 6980.2 |
| 35:35 | 3000 | 4 | 16004.6 | 17262.4 | 16479.8 |
| 0:0 | 300 | 1 | 234.1 | 234.2 | 234.9 |
| 0:0 | 300 | 4 | 377.3 | 408.6 | 398.9 |

La actualización de una sola mano mejora un 5,2 % la mediana de un hilo en
35:35 respecto al merge sin ese cambio; en 0:0 la diferencia es del 0,3 %.
No se observa una mejora adicional consistente con cuatro hilos: sus
medianas finales son inferiores a las del merge sin ese cambio, dentro de
unas muestras con dispersión apreciable. La ejecución concurrente también
cambia el orden de aprendizaje y el número de nodos visitados. Estos casos
no permiten afirmar una aceleración uniforme ni extrapolar el resultado a
una tabla completa o a entrenamientos largos.

Las muestras completas, incluidos tiempos de CPU, memoria y nodos visitados,
están en [results.csv](results.csv). El entorno está en
[environment.json](environment.json).

Para ejecutar el benchmark final:

```sh
make -j4 benchmark-training
build/release/benchmark-training ordago-score-four 3000 1 7 35 35
build/release/benchmark-training ordago-score-four 3000 4 7 35 35
build/release/benchmark-training ordago-score-four 300 1 7 0 0
build/release/benchmark-training ordago-score-four 300 4 7 0 0
```

## Validación

Se comparan checkpoints completos de las tres versiones tras 200 iteraciones
con un hilo, con semillas 7 y 99, en `ordago-score-four`, `ordago-score` y
`ordago-public`. En cada caso los tres archivos son idénticos byte a byte.
Los formatos con marcador usan 35:35 y la misma continuación sintética.
Las huellas están en [checkpoints.sha256](checkpoints.sha256).

C-CFR añade dos regresiones que simulan una actualización compartida entre
las pasadas: la estrategia media debe conservar el snapshot original; si
cambia el orden de las acciones de la misma clave, el recorrido debe fallar
sin publicar sus deltas ni cambiar RNG o salidas.

Comprobaciones ejecutadas:

- C-CFR: `make test`, `test-asan`, `test-ubsan` y `test-tsan`.
- Solver: `make test` con un mus y `test-core` con dos muses.
- Solver: `test-core` con AddressSanitizer y UndefinedBehaviorSanitizer.
- Compilación release con LTO; contratos de configuración, dependencias
  incrementales y componentes.
- Las propiedades de cuatro jugadores verifican, entre otros casos, 1200
  manos aleatorias con restauración exacta del modelo y de los cuatro
  resúmenes tras deshacer cada acción.

La batería del solver se ejecuta en una copia del resultado del merge con
`Simulador-Mus` en el commit fijado por el proyecto, `2fda65e`. La copia de
trabajo original ya tenía ese submódulo en `cf52bbb`, donde `make test`
falla por conversiones de signo en `src/mus_sim.c:418`. Ese cambio local y
los borrados previos de informes de rendimiento quedan fuera de este merge.
