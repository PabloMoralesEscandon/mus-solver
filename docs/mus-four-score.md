# Mus con marcador para cuatro jugadores

El formato `ordago-score-four` extiende el modelo de `ordago-score` a cuatro
jugadores por parejas. Los jugadores 0 y 2 forman el equipo 0; 1 y 3 forman
el equipo 1. Cada jugador decide con sus propias cartas y la información
pública. Las cartas del compañero no forman parte de su observación.

## Reglas y marcador

El jugador 0 es mano. Los turnos siguen el orden 0, 1, 2, 3. El marcador
`A:B` corresponde a los equipos 0 y 1, respectivamente. Ambos miembros de
un equipo reciben la misma utilidad; la utilidad del equipo contrario tiene
el signo opuesto. No hay cuatro marcadores independientes.

Se mantienen grande, chica, pares y juego o punto; las equivalencias de la
baraja de 40 cartas; las aperturas de 2, 4, 7, 10 u órdago; las subidas
aditivas de 2, 4 o 10; el límite de cinco envites numéricos y la prohibición
de que un jugador repita el mismo incremento en un lance. La cota numérica
conserva la regla de la versión score: mayor marcador público, más envites
aceptados pendientes, más envite activo y nuevo incremento, debe ser menor
que 40. `--target` cambia el objetivo de victoria, como en la versión anterior.

Los cuatro deben dar mus para descartar. Cada uno elige un subconjunto no
vacío de su mano canónica. Se conservan las máscaras equivalentes de cartas
iguales y los recuentos públicos de descarte. El reparto y los robos respetan
las existencias de cada clase de carta. La compilación admite uno o dos muses.
Si el segundo mus agota el mazo, se barajan los descartes para continuar los
robos, como en el simulador. Hasta ese momento los descartes quedan fuera del
mazo; no se puede robar una carta que siga en la mano de otro jugador.

En pares solo actúan quienes tienen pares; en juego, quienes tienen juego.
Si solo una pareja cumple el requisito, el lance no se negocia. La mejor
mano de cada lance determina la pareja ganadora; un empate favorece al
jugador más próximo a mano. La pareja ganadora suma los valores intrínsecos
de pares o juego de sus dos miembros.

Ante un envite, un rechazo individual permite responder al otro miembro
elegible de la pareja. El rechazo del equipo se produce cuando rechazan todos
sus miembros elegibles. Querer o subir obliga a la pareja inmediatamente.
Esta negociación permite que el compañero quiera después de un rechazo.
El negociador actual de `Simulador-Mus` resuelve el rechazo con la primera
respuesta; sus funciones de comparación de manos y tanteo se usan como
referencia independiente para las pruebas del nuevo solver.

Los dejes y las cantidades cubiertas rechazadas se cobran inmediatamente.
Los envites aceptados y los tantos intrínsecos esperan al recuento final,
en orden de lances. Un órdago querido termina la partida antes de ese recuento.
El marcador se comprueba después de cada cobro. Si la mano termina sin
ganador de partida, la siguiente mano pasa al equipo contrario: desde el
equipo que era mano, la continuación es `-V[B][A]` con los tantos actualizados.

## Entrenar, guardar y reanudar

Una prueba corta a 40 tantos:

```sh
make -j4 solver
build/release/mus-solver --format ordago-score-four \
    --score 39:39 --iterations 10 --evaluation-samples 100 \
    --seed 7 --continuation entrenamiento-cuatro
```

Para entrenar una región de marcadores y repartir el trabajo:

```sh
build/release/mus-solver --format ordago-score-four \
    --score-region 35:35 --iterations 1000 --evaluation-samples 10000 \
    --threads 4 --threads-per-cell 2 --seed 7 \
    --continuation entrenamiento-cuatro-region
```

La tabla sigue teniendo dos dimensiones. Las diagonales superiores se entrenan
antes que las inferiores; cada celda tiene su propio almacén de estrategias.
`--threads` distribuye celdas de una diagonal y `--threads-per-cell` distribuye
las iteraciones de cada celda. Las iteraciones indicadas son por celda.
Una ejecución con varios hilos dentro de una celda depende del orden de las
actualizaciones y no es reproducible bit a bit.

Se conservan la recuperación de celdas ya guardadas, los bloqueos de proceso,
la extensión a otro directorio, las regiones cerradas, las estadísticas de
muestreo y las opciones de evaluación adaptativa. Repetir un entrenamiento
completa las celdas pendientes. Para añadir iteraciones a las ya publicadas,
use `--extend-continuation` con un directorio de destino distinto, como en
`ordago-score`.

Un checkpoint de una celda se puede cargar, continuar y exportar:

```sh
build/release/mus-solver --format ordago-score-four --score 39:39 \
    --continuation entrenamiento-cuatro \
    --load-compressed entrenamiento-cuatro/score-39-39.cfr.zst \
    --iterations 10 --save-compressed cuatro-39-39.cfr.zst \
    --export-strategy cuatro-39-39.txt
```

Los esquemas de tabla son `mus.ordago-score-four/v1` y
`mus.ordago-score-four-region/v1`. Los checkpoints incluyen la variante,
el número de muses, el objetivo, el marcador y la identidad de la continuación.
Se rechazan tablas, checkpoints y extensiones de otra variante. Las estrategias
de dos jugadores tampoco sirven como precarga de cuatro jugadores.
Los comandos y checkpoints históricos siguen usando sus esquemas anteriores.
`single-bet` sigue siendo el formato predeterminado.

## C-CFR y API

C-CFR recorre los cuatro jugadores estratégicos. En cada recorrido calcula
los regrets del jugador objetivo a partir de su utilidad de equipo. El
compañero sigue siendo otro actor con información privada. El cálculo de
estrategia media para más de dos jugadores usa un recorrido adicional con
muestreo e importancia corregida; los juegos existentes mantienen su recorrido.

Entrenar cuatro políticas descentralizadas con utilidad compartida no convierte
el problema en un juego de dos jugadores con acceso conjunto a las cartas.
Las garantías de convergencia de CFR para dos jugadores no se extienden
automáticamente a esta variante. Además, se conservan abstracciones de memoria
de la versión score. La evaluación por muestras mide el valor de la política
con la continuación dada; no certifica equilibrio ni explotabilidad global.
El evaluador exacto de C-CFR con resultados para dos jugadores rechaza juegos
con más jugadores.

[`mus_four.h`](../include/mus_solver/mus_four.h) expone el estado reversible,
la observación pública, la codificación y decodificación de claves y los menús
de acciones. La abstracción mantiene las cartas propias exactas durante mus
y descartes, y las categorías por lance de score durante los envites. También
conserva los descartes públicos, la fuerza anterior por equipo, las declaraciones
actuales, los marcadores públicos y los límites de apuestas. Las claves se
codifican sin colisiones mediante rangos de estados alcanzables.

Para usar la infraestructura de marcador:

1. Inicialice `MusScoreTable` y asigne `table.four_player = true`.
2. Cree la celda con `mus_score_game_init`.
3. Reserve `mus_score_game_state_size(&game)` bytes.
4. Inicialice esa memoria con `mus_score_game_state_init_generic`.
5. Use el descriptor `game.game` y el estado con C-CFR.

La API histórica `mus_score_game_matches` identifica exclusivamente el
adaptador score de dos jugadores. Un cliente que utiliza ese predicado antes
de acceder a un `MusGameState` puede conservar su código. El nuevo predicado
`mus_score_game_matches_any` admite ambas variantes, pero no selecciona la
representación del estado ni permite convertirlo directamente a `MusGameState`.
Use las funciones genéricas anteriores o el predicado específico de la variante.

`mus_score_game_state_init(MusGameState*, ...)` conserva su estado de dos
jugadores y rechaza el formato de cuatro. `mus_score_terminal_utility` también
requiere un `MusGameState` de dos jugadores; no detecta otras representaciones
de estado. Para obtener utilidades con un estado genérico, llame a
`cfr_game_terminal_utility(&game.game, state, player, &utility)`.

Para ampliar tablas, `MusScoreExtension.load_cell` conserva el callback
histórico `MusScoreLoadCell`, que recibe exclusivamente `MusGameState*` de dos
jugadores. Los callbacks existentes no necesitan cambios. Un cargador compatible
con cuatro jugadores debe utilizar `MusScoreLoadCellGeneric`, que recibe
`GameState*`, y asignarlo al nuevo campo `load_cell_generic`. Ese cargador también
sirve para tablas de dos jugadores. Para utilizarlo, asigne `NULL` a `load_cell`.
En las tablas de dos jugadores, un `load_cell` no nulo conserva la prioridad y
el campo nuevo no se lee; los clientes antiguos que asignan sus campos por
separado tampoco necesitan cambios.
El entrenamiento rechaza un cargador histórico para una tabla de cuatro jugadores
antes de invocar callbacks, recuperar celdas o modificar la tabla.

El bot y el evaluador de partidas existentes siguen siendo consumidores de dos
jugadores; el bot rechaza expresamente tablas de cuatro jugadores.

## Verificación

```sh
make test
make BUILD_DIR=build/two-muses CFR_BUILD_DIR=build/two-muses \
    MUS_MAX_MUSES=2 test-core
make test-sanitize
make dependencies
```

Las pruebas nuevas contrastan 2.400 secuencias de acciones con las API de tanteo
del simulador para objetivos 3, 7, 15 y 40. Cubren envites numéricos aceptados y
rechazados, subidas con apuesta anterior cubierta, tantos propios de pares,
juego y punto tras un rechazo, órdagos ganados por los asientos 2 y 3, desempates
y orden de cobro. También recorren manos completas, comprueban las probabilidades
del reparto, la igualdad de
utilidades dentro del equipo, el signo de continuación, la ausencia de cartas
ajenas en las observaciones, las claves y menús y la restauración al deshacer.
Incluyen rechazos de un solo miembro, aceptación del compañero, prioridad
del órdago y del recuento a `39:39`. Una prueba adicional recorre las 330
manos posibles, 66.600 observaciones de envites y las 755 combinaciones
alcanzables de incrementos usados, con ambos límites de mus.
Las pruebas del CLI cubren las tablas,
regiones, hilos, exportación, recuperación, extensiones y reanudación exacta
con un hilo. Las pruebas de API conservan el contrato de los callbacks y estados
históricos, verifican cargadores genéricos en ambas variantes y rechazan un
cargador incompatible antes de invocarlo. C-CFR añade sus propias regresiones
de entrenamiento multijugador.
