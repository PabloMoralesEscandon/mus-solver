# Solver simplificado de mus para dos jugadores

Solver en C17 de mus para dos jugadores. Entrena estrategias mediante
external-sampling MCCFR y guarda checkpoints reanudables. Los formatos
históricos resuelven una mano aislada; `ordago-score` incorpora el marcador de
la partida mediante una tabla de continuación. Las métricas disponibles
dependen del formato.

## Formatos de reglas

- El jugador 0 siempre es mano y el jugador 1 es postre.
- Se usa la baraja española de 40 cartas, con ochos reyes y ocho ases según las
  equivalencias del mus: tres = rey y dos = as.
- Se juegan en orden grande, chica, pares y juego o punto.
- Los formatos históricos no tienen fase de mus ni descartes. El formato
  `ordago` y su nueva variante `ordago-score` sí los incorporan.
- El formato `single-bet` conserva la primera versión: en cada lance se puede
  pasar o envidar 2 y, ante el envite, solo se puede querer o no querer.
- El formato `raises-to-eight` añade respuestas y subidas en todos los lances.
  Quien abre puede pasar o envidar 2. Si pasa, postre también puede pasar o
  envidar 2. Ante un envite se puede no querer, querer o subir duplicando la
  cantidad: 2 → 4 → 8. Ante 8 ya no se puede volver a subir.
- El formato `ordago` permite pasar o envidar 2, 4, 7, 10 u órdago en todos
  los lances. Los envites numéricos son incrementos: responder a 2 con otro 2
  deja 4 tantos en juego, y responder después con 4 deja 8. La apertura puede
  ser de 2, 4, 7 o 10. Las respuestas numéricas solo pueden ser de 2, 4 o 10 y
  deben ser iguales o mayores que el incremento anterior. Cada jugador puede
  usar cada incremento numérico una sola vez por lance. Por ejemplo, tras
  `+2 → +2`, el jugador que abrió ya no puede volver a usar `+2`, aunque el
  incremento anterior también sea 2; antes de responder, el otro jugador sí
  podía igualar con `+2` porque todavía no lo había usado. Por ello, tras
  `paso → +7`, el jugador 0 puede no querer, querer, lanzar órdago o subir 10.
  Cada lance admite como máximo cinco envites numéricos, incluida la apertura;
  después del quinto solo se puede no querer, querer o lanzar órdago. La cota
  numérica tiene en cuenta los tantos ya ganados por el jugador que va por
  delante, los envites acumulados de los lances anteriores, el envite activo y
  el nuevo incremento. La suma debe ser estrictamente menor que 40. Por
  ejemplo, si los jugadores han ganado 10 y 15 tantos y hay 20 de envites
  acumulados, se puede envidar 4 porque 15 + 20 + 4 = 39, pero no 7 ni 10. Al
  agotar el presupuesto ya no se puede hacer otro envite numérico, pero sí
  lanzar órdago, que sigue siendo una acción distinta.
- En `ordago`, antes de grande cada jugador dice, por orden, `mus` o `no mus`.
  Si ambos dicen mus, cada uno elige un subconjunto no vacío de sus cartas. Los
  bits recorren la mano en su orden canónico, de menor a mayor valor. Las copias
  del mismo valor son indistinguibles, por lo que sus máscaras equivalentes se
  agrupan: hay como máximo 15 acciones y solo 4 para cuatro cartas iguales. Las
  cartas descartadas quedan fuera del mazo y los robos se hacen sin reposición.
- El estado conserva el número de muses, las máscaras y los recuentos de
  descarte. Por defecto, tras un mus aceptado se pasa directamente a grande.
  `MUS_GAME_MAX_MUSES` permite compilar el solver con un límite de uno o dos
  muses; su valor predeterminado es uno.
- En los lances posteriores, cada lance completado se resume como ninguna
  muestra de fuerza, solo jugador 0, solo jugador 1 o ambos. Envidar, subir o
  querer cuenta como mostrar fuerza. No se conservan la secuencia ni las
  cantidades exactas de los lances anteriores. Si pares no se negocia, el
  resumen conserva además si no tenía pares nadie, solo mano o solo postre.
  También se conservan el mayor tanteo ya resuelto y el total numérico
  acumulado, porque ambos determinan qué envites siguen disponibles. No se
  conserva cuánto aportó individualmente cada lance.
  El acumulado suma el envite final cuando se quiere. Cuando se rechaza un
  envite numérico o un órdago, suma únicamente la cantidad cubierta: una piedra
  para una apertura y el último total cubierto para una subida.
  Mientras un lance está abierto, el conjunto de información conserva además
  el número de envites numéricos del lance y los incrementos ya usados por
  cada jugador para aplicar ambos límites. Las máscaras exportadas como
  `usados_j0` y `usados_j1` asignan los bits 0, 1, 2 y 3 a `+2`, `+4`, `+7` y
  `+10`, respectivamente; por ejemplo, el valor 11 representa `+2`, `+4` y
  `+10`.
- Se aplica literalmente el tanteo de `Simulador-Mus`: el envite o deje se
  suma al valor intrínseco de pares, juego y punto. En `ordago`, el tanteo total
  de cada jugador se limita a 40 para que una resolución ordinaria nunca supere
  el valor de un órdago querido.
- En los tres formatos históricos, la utilidad del jugador 0 es sus tantos
  menos los del jugador 1; la utilidad contraria es su opuesto.
- `ordago-score` parte de las reglas y abstracciones actuales de `ordago`, añade
  el marcador inicial y optimiza una utilidad de partida en `[-1, 1]`. Se
  selecciona como una cuarta opción; `single-bet` sigue siendo el valor
  predeterminado.

Al no querer el envite inicial se concede una piedra. Si se rechaza una subida
a 4 u 8, se concede el último envite ya cubierto: 2 o 4 piedras. Si se quiere,
el lance se resuelve por la cantidad final aceptada.

En `ordago` se aplica la misma regla general: rechazar una apertura concede una
piedra y rechazar una subida concede el total anterior ya cubierto. Por
ejemplo, tras envites de 2, 2 y 4 hay 8 tantos en juego; si después se lanza
un órdago y se rechaza, se conceden los 8 tantos cubiertos.
Si se quiere el órdago, la mano termina inmediatamente y el ganador del lance
actual recibe exactamente 40 tantos; no se suman lances ni valores intrínsecos
posteriores.

Los conjuntos de información de `ordago` usan una abstracción específica para
cada lance. Mus y descartes conservan la composición exacta de la mano. Grande
conserva el número de reyes y la siguiente carta más alta; chica, el número de
pitos y la siguiente carta más baja; pares, el tipo y los valores de las
parejas; juego, uno de sus ocho totales; y punto, el total entre 4 y 30. Esta
abstracción reduce mucho el número de estados, pero pierde recuerdo perfecto.
Por ello, CFR no garantiza que la estrategia media converja a un equilibrio y
las métricas acotadas descritas más abajo son diagnósticas, no una garantía.
La regla de usar cada incremento una sola vez por jugador introduce además una
asimetría posicional deliberada que no pertenece al mus sin abstraer. El mayor
envite numérico alcanzable desde un lance vacío es 30, mediante
`2 → 4 → 4 → 10 → 10`. Por estos cambios, los valores y la explotabilidad del
esquema `v9` no son comparables con los del esquema `v7`.

Los palos no afectan a ninguna decisión. El reparto agrupa las cartas en ocho
clases de valor, pero conserva exactamente sus multiplicidades y el muestreo
sin reemplazo. Hay 330 composiciones de mano y 104.820 repartos conjuntos.

## Compilar y probar

Inicialice primero los submódulos:

```sh
git submodule update --init --recursive
```

Después puede compilar la versión optimizada o ejecutar las pruebas:

```sh
make
make test
make test-tsan
make test-sanitize
make dependencies
```

La compilación release usa `-O3` y LTO y requiere la biblioteca de desarrollo de
Zstandard. `make lto` genera la misma configuración optimizada en
`build/lto/release` y `C-CFR/build/lto/release`, sin reemplazar los artefactos
normales.
`make test-tsan` comprueba con ThreadSanitizer tanto el acceso concurrente al
almacén de C-CFR como el entrenamiento paralelo del CLI.

Para volver a permitir dos muses en una compilación independiente:

```sh
make BUILD_DIR=build/two-muses MUS_MAX_MUSES=2
```

Los artefactos principales son:

- `build/release/mus-solver`
- `build/release/libmus_solver.a`

`make dependencies` ejecuta además las baterías independientes de `C-CFR` y
`Simulador-Mus`.

## Variante con marcador: `ordago-score`

Implementa el diseño de [TABLA_DE_CONTINUACION_CFR.md](TABLA_DE_CONTINUACION_CFR.md).
Cada celda `a–b` tiene su propio almacén CFR. El árbol conserva una sola mano,
con mus, descartes y los envites de `ordago`. El jugador 0 representa siempre
la mano y el jugador 1 el postre.

El modelo separa los cobros inmediatos de los contratos pendientes. Los envites
u órdagos rechazados pagan inmediatamente el deje o el total cubierto. Los
envites queridos, los lances en paso y los valores de pares/juego quedan para
el tanteo final, por orden: grande, chica, pares y juego o punto. Se detiene el
cobro cuando alguien alcanza el objetivo. Un órdago querido decide la partida
inmediatamente y descarta los tantos pendientes. Estas reglas siguen los
[títulos IV.5 y V.1 del reglamento FEMUS](https://www.mustembleque.es/downloads/REGLAMENTO_FEMUS.29.pdf).

Por ejemplo, a `39–39`, pasar ambos en grande permite continuar a chica y
resolver allí un órdago. Si toda la mano va en paso, grande se cuenta primero
al final. En cambio, un deje cobrado en chica puede decidir la partida antes
de que se cuente ese grande pendiente.

`paid_scores` conserva los cobros inmediatos; `settled_scores` incorpora además
el tanteo final cuando corresponde. `pending_lances` señala los contratos aún
pendientes, descritos en `outcomes` y sus importes, sin guardar sus ganadores
ni sus valores intrínsecos ocultos. El deshacer restaura los tres campos.

Las utilidades son `+1/-1` cuando termina la partida. Si continúa desde `a'–b'`,
la utilidad del jugador 0 es `-V[b'][a']`: el siguiente reparto pasa la mano al
contrario. No se añade la diferencia de tantos a ese valor.

En `ordago-score/v4`, cada oferta numérica debe cumplir:

```text
max(marcador_inicial_0 + tantos_cobrados_0,
    marcador_inicial_1 + tantos_cobrados_1)
+ total_de_envites_queridos_pendientes
+ total_propuesto_en_el_lance_actual < 40
```

Los dejes e importes cubiertos ya cobrados solo cuentan en el marcador; no se
suman otra vez como pendientes. Los lances en paso y los valores intrínsecos no
se anticipan. En una subida, el total propuesto incluye lo envidado antes en
ese mismo lance. El órdago sigue disponible y queda fuera de esta cota numérica.
Por ejemplo, partiendo de `29–30`, cobrar 1 el jugador 0 y 2 el jugador 1 deja
`30–32`. Si además hay 2 queridos en pares, la base es `32 + 2 = 34`: un total
de 6 queda excluido por la desigualdad estricta. Entre las aperturas disponibles,
se permiten 2 y 4; entre las respuestas a 2, subir otros 4 queda prohibido.
A `38–19`, `19–38` y `39–39` solo se puede abrir pasando o con órdago.

Se mantienen las restricciones por lance de la abstracción: incrementos
permitidos y no decrecientes, cada incremento una vez por jugador y máximo de
cinco envites numéricos. La cota acumulada de 40 también se usa con objetivos
reducidos. Es una restricción del árbol del solver. El formato histórico
`ordago` conserva su cota acumulada anterior.

Las claves vuelven al resumen de fuerza por lance de `ordago` y un único total
agregado de envites anteriores, que en el modo con marcador contiene solo los
queridos pendientes. No conservan sus importes separados por lance. Por tanto,
grande 2/chica 4 y grande 4/chica 2 comparten nodo si coinciden los demás datos.
El **modelo** conserva cada importe para cobrarlo correctamente al final; unir
las claves hace que esos estados usen la misma política pese a tener pagos
futuros distintos. Es la abstracción elegida para reducir los nodos.

Los conjuntos de información incluyen ambos marcadores **cobrados**, las cartas
propias abstraídas, el lance y turno, mus y descartes, fuerza anterior y envite
activo. No incluyen quién ganará un lance pendiente ni el valor oculto de los
pares del rival. El marcador inicial y el objetivo identifican el almacén de
cada celda. Las claves usan una codificación reversible dentro de `int64_t`;
los importes activos se reconstruyen con las máscaras de incrementos utilizados.

Prueba pequeña con una partida a cinco tantos:

```sh
build/release/mus-solver \
    --format ordago-score --target 5 --score 0:0 \
    --iterations 100 --evaluation-samples 10000 --seed 7 \
    --continuation marcador-5 \
    --evaluate --export-strategy marcador-5-0-0.txt
```

El directorio padre de `--continuation` debe existir. El solver crea el
directorio final si es necesario. Para una partida normal, el objetivo
predeterminado es 40:

```sh
build/release/mus-solver \
    --format ordago-score --score 35:18 \
    --iterations 1000 --evaluation-samples 10000 --seed 7 \
    --continuation marcador-40
```

`--iterations` es el número de iteraciones **por celda pendiente**.
El coordinador resuelve las diagonales desde `2 × (objetivo - 1)` hasta la suma
del marcador solicitado. Calcula todos los marcadores de esas diagonales;
`0:0` a 40 requiere 1.600 celdas. Esto puede costar mucho más que entrenar una
mano aislada. Se mantiene un almacén CFR de destino por celda simultánea y se libera después
de guardar cada estrategia. La tabla permanece inmutable durante el
entrenamiento y la evaluación de cada diagonal, y sus valores se publican
juntos al terminarla.

`--threads N` entrena hasta N celdas independientes de una misma diagonal en
paralelo; el valor predeterminado es 1. Con `--threads-per-cell 1`, cada celda
conserva su semilla, recorrido MCCFR y evaluación, por lo que cambiar solo
`--threads` no cambia las tablas ni los checkpoints. Las estrategias se guardan
en el mismo orden que en la ejecución
secuencial. La memoria puede crecer hasta N almacenes CFR; conviene ajustar los
hilos a la memoria disponible, especialmente lejos del objetivo. El paralelismo
está limitado por el número de celdas de cada diagonal.

```sh
./build/release/mus-solver --format ordago-score \
    --iterations 1000 --evaluation-samples 10000 --seed 7 \
    --threads 4 --continuation marcador-40
```

`--threads-per-cell M` permite que hasta M hilos entrenen **la misma estrategia**
dentro de cada celda; también vale 1 por defecto. Cada hilo tiene su estado,
generador aleatorio y espacio de recorrido, y todos actualizan el mismo almacén
CFR concurrente. Las iteraciones por celda se reparten entre esos hilos; no se
multiplican por M. Para entrenar una sola celda cada vez con ocho hilos:

```sh
./build/release/mus-solver --format ordago-score \
    --iterations 1000 --evaluation-samples 10000 --seed 7 \
    --threads 1 --threads-per-cell 8 --continuation marcador-40
```

Ambas opciones se pueden combinar: `--threads 2 --threads-per-cell 4` mantiene
hasta dos estrategias simultáneas, cada una con hasta cuatro hilos. Compartir
la estrategia evita un almacén completo por hilo, pero añade estados, espacios
de recorrido y las tablas hash del acceso concurrente. La memoria adicional
depende del tamaño del entrenamiento.

El entrenamiento de una estrategia con varios hilos usa el mismo MCCFR
concurrente que `ordago`. Conserva la abstracción y los formatos de checkpoint,
pero el orden de las actualizaciones depende del planificador: la misma semilla
no garantiza tablas ni checkpoints idénticos. Cada celda se evalúa y guarda
después de que terminen todos sus hilos. La tabla de continuación permanece fija
hasta publicar la diagonal completa.

Emite un informe `score-cell` por celda y un `score-table` para
el marcador solicitado. Al reanudar, `score-resume` muestra cuántas celdas ya
están publicadas, los mínimos y máximos de iteraciones y muestras, y cuántas
se entrenaron con ajustes diferentes de los solicitados. Se permiten
presupuestos diferentes por celda; las publicadas conservan sus valores.
El coordinador no utiliza `--report-every`, `--load` ni `--save`; su
persistencia se gestiona con `--continuation`. Cargar un checkpoint con
`--load` o `--load-compressed` selecciona la reanudación individual descrita
más abajo, sin recorrer las diagonales.
`--reserve-information-sets` permite reservar capacidad por celda; sin esa
opción el almacén crece según las necesidades.

El tanteo incremental, la generación de acciones y las cachés del recorrido
conservan la abstracción `ordago-score/v4`. La evaluación restaura solo el estado
activo entre muestras. El [benchmark de rendimiento](docs/performance.md)
permite medir iteraciones por segundo y comprobar que los resultados coinciden.

### Precarga desde `ordago` sin marcador

Puede reutilizarse la **estrategia media** de un checkpoint de `ordago` como
política inicial de cada celda pendiente:

```sh
build/release/mus-solver \
    --format ordago-score --target 5 --score 0:0 \
    --preload-ordago-compressed ordago.cfr.zst \
    --preload-weight 1 \
    --iterations 100 --evaluation-samples 10000 --seed 7 \
    --continuation marcador-5-precargado
```

Para checkpoints sin compresión se usa `--preload-ordago ordago.cfr`.
La carga valida el esquema de `ordago`, incluida la configuración de uno o dos
muses. No acepta exportaciones de texto, otros formatos de juego ni checkpoints
de ninguna versión de `ordago-score`.

La precarga del CLI inicializa cada nodo **cuando se crea**, en mus, descartes,
grande, chica, pares y juego/punto. Comparte una sola estrategia genérica en RAM
entre las celdas activas. No recorre ni copia todos sus nodos para cada marcador.

Para traducir una decisión, calcula por jugador `cobrado = marcador actual −
marcador inicial de la celda`. Reconstruye el acumulado antiguo sumando esos
cobros a los envites aceptados pendientes. Conserva la mano abstracta, jugador,
lance, historial de mus, fuerza previa e incrementos del envite activo.
El máximo de puntos calculados de la genérica incluía resultados todavía
ocultos: se enumeran sus valores posibles usando los cobros, los envites, los
puntos de grande/chica al paso y el intervalo de tantos de pares. No se consultan
las cartas rivales. Se suman las masas de estrategia de los candidatos existentes;
esto pondera sus políticas por el peso acumulado en el entrenamiento antiguo.
Es una aproximación a la política general, no una traducción exacta de todos los
estados ni una garantía de que mejore la convergencia.

Las probabilidades se emparejan por **acción** y se normalizan sobre el menú
legal del destino. La política inicial es `0,95 × política transferida +
0,05 × uniforme`. Si no hay candidatos con masa de estrategia, o toda la masa
corresponde a acciones prohibidas, el nodo empieza uniforme. Los contadores
`missing_source_nodes` e `imported_later_nodes` permiten comprobar la cobertura.

`--preload-weight` fija la masa inicial por nodo de estrategia media y de regrets
positivos **sintéticos**; por defecto vale 1. No copia regrets antiguos, unidades
de utilidad, iteraciones, RNG ni valores de continuación. El aprendizaje nuevo
optimiza la utilidad de ganar la partida. Los nodos existentes se conservan.

`--report-every 1000` permite seguir las iteraciones y la cobertura de la precarga
dentro de cada marcador mediante líneas `score-report`.

`score-preload-source` identifica el origen y sus parámetros; `score-preload`
informa por celda con `mode=lazy-all-lances`. Conserva el checkpoint genérico y
repite las opciones al reanudar para inicializar también los nodos que aparezcan
después. `values.txt` no guarda la configuración del prior y las celdas publicadas
no se vuelven a entrenar. Para rehacerlas con esta precarga, usa un directorio nuevo.

Los checkpoints y TXT contienen los nodos materializados, incluida su
inicialización y aprendizaje. Se pueden cargar sin la genérica, pero entonces
los nodos nuevos empiezan uniformes. La evaluación mantiene la política uniforme
para nodos ausentes y no materializa nodos; tampoco el TXT contiene un fallback
implícito a la genérica. La precarga no cambia el esquema `ordago-score/v4`.

La API de [mus_score.h](include/mus_solver/mus_score.h) ofrece
`mus_score_prior_attach()` para instalar la inicialización de todos los lances
en un almacén nuevo o restaurado. El origen permanece inmutable y el binding debe
vivir mientras se creen nodos. Los helpers anteriores
`mus_score_information_set_from_ordago()` y `mus_score_preload_ordago()` conservan
su operación limitada a mus, descartes y grande por compatibilidad. El coordinador
usa automáticamente el nuevo binding:

```c
MusScorePreloadOptions prior = {
    .source = &ordago_trainer, /* Cargado con cfr_checkpoint_read[_zstd]. */
    .weight = MUS_SCORE_PRELOAD_DEFAULT_WEIGHT,
    .uniform_mix = MUS_SCORE_PRELOAD_DEFAULT_UNIFORM_MIX,
};
MusScoreTrainingOptions training = {
    .iterations = 100,
    .evaluation_samples = 10000,
    .seed = 7,
    .preload = &prior,
};
Status status = mus_score_train(&table, 0, &training,
                                save_cell, save_diagonal, context);
```

### Ampliar una tabla ya entrenada

`--extend-continuation ORIGEN` carga los checkpoints de una tabla de marcadores
y añade `--iterations N` a **cada marcador** solicitado. Usa otro directorio como
destino. Por ejemplo, para pasar de 10M a 50M por marcador hasta 30–30:

```sh
build/release/mus-solver \
    --format ordago-score --target 40 --score 30:30 \
    --extend-continuation entrenamiento-10m/continuacion \
    --continuation entrenamiento-50m/continuacion \
    --iterations 40000000 --threads 1 --threads-per-cell 14 \
    --report-every 1000 --evaluation-samples 1000 \
    --preload-ordago-compressed ordago-100m.cfr.zst --preload-weight 1
```

El origen debe contener todos los marcadores solicitados, con el mismo objetivo
y esquema, y no tener un entrenamiento activo. El destino debe estar separado
del origen. No se admite `--seed` ni `--load`: cada marcador restaura su RNG.
Se conservan los regrets, la estrategia acumulada, los contadores y las semillas
originales. La genérica es opcional y solo inicializa nodos nuevos.

El coordinador valida cada checkpoint contra la tabla **antigua**. Después
asocia el entrenamiento a la tabla **nueva**: completa y evalúa una diagonal,
guarda sus checkpoints y publica sus valores antes de bajar a la siguiente.
Las claves y reglas del marcador no cambian. Los checkpoints de salida llevan
el hash de las nuevas continuaciones y se cargan con la nueva tabla. La carga
ordinaria mantiene la comprobación estricta de esquema.

Esta ampliación conserva aprendizaje de una aproximación anterior de los
valores de continuación. Un contador de 50M significa 10M anteriores más 40M
nuevos; no significa que los 50M se hayan ejecutado bajo los valores finales.

`extension.txt` guarda el origen canónico, su tabla y los parámetros de la
ampliación. Al reanudar, repite el mismo comando: las diagonales publicadas se
conservan y cada marcador pendiente vuelve a cargar su checkpoint del origen.
Los checkpoints de una diagonal incompleta pueden recalcularse. Para ampliar
otra vez desde 50M, utiliza esta nueva tabla como origen y otro destino.
Cambiar de origen, iteraciones añadidas, evaluación o precarga en una ampliación
parcial produce un error. El origen y sus checkpoints deben permanecer inmutables.
`score-report` muestra `iterations` acumuladas y `added_iterations` de esta ejecución.

### Persistencia y evaluación

El directorio contiene:

- `values.txt`: valores, objetivo, versión de reglas y abstracción, orientación,
  escala de utilidad, iteraciones, muestras de evaluación y semilla por celda.
- `score-AA-BB.cfr.zst`: checkpoint CFR de cada marcador, con la estrategia
  media y los acumuladores necesarios para reanudar el entrenamiento mediante
  la CLI o la API de C-CFR.
- `training.lock`: bloqueo con PID, nombre del equipo y fecha UTC de inicio.
  El error de bloqueo muestra esos datos. Si un proceso termina abruptamente,
  hay que comprobar en ese equipo que el proceso indicado ya no se ejecuta
  antes de borrar este archivo y reanudar.

Repetir el comando del coordinador con el mismo directorio conserva las diagonales terminadas
y completa las pendientes. Una diagonal interrumpida se vuelve a entrenar.
Las celdas ya publicadas no reciben iteraciones adicionales, porque sus
estrategias y valores son continuaciones fijas de las anteriores. Para cambiar
el entrenamiento completo se utiliza otro directorio. Los checkpoints tienen
un esquema distinto del de `ordago`, que incluye objetivo, marcador y una
huella de las continuaciones superiores. No se pueden intercambiar entre
marcadores, objetivos, versiones de mus o tablas distintas.

El esquema actual es `mus.ordago-score/v4`. **Las tablas y checkpoints de `v1`,
`v2` y `v3` no son compatibles y requieren un entrenamiento nuevo en otro directorio.**
`v3` usa otra abstracción de historial y otros menús; su incompatibilidad no
implica un error de tanteo. En cambio, `v1` y `v2` adelantaban el cobro de lances
pendientes y filtraban resultados de cartas ocultas en las claves. Sus estrategias,
utilidades y mediciones de
tamaño no validan el modelo corregido. Los artefactos antiguos pueden conservarse
para diagnóstico. Los formatos históricos mantienen sus esquemas y comportamiento;
no representan esta corrección del tanteo ni de la información disponible.
Los checkpoints de una diagonal interrumpida que no figuren en `values.txt`
se ignoran; se reemplazan cuando se vuelve a entrenar esa celda. Tanto los
checkpoints como la tabla se publican mediante archivo temporal y `rename()`;
las estrategias de celdas ya publicadas no se sobrescriben al reanudar.

### Añadir iteraciones a un checkpoint con marcador

`--load-compressed` carga el estado completo de un único marcador: regrets,
estrategia acumulada, contador, estadísticas y estado aleatorio. En este modo,
`--iterations N` añade **N iteraciones a ese checkpoint**, sin entrenar otras
celdas. Por ejemplo, un checkpoint
de 100.000 iteraciones pasa a 200.000 con:

```sh
build/release/mus-solver \
    --format ordago-score --target 40 --score 30:30 \
    --continuation marcador-40 \
    --load-compressed marcador-40/score-30-30.cfr.zst \
    --iterations 100000 --report-every 1000 \
    --save-compressed score-30-30-200k.cfr.zst \
    --export-strategy score-30-30-200k.txt
```

También admite `--load` y `--save` para checkpoints sin comprimir. El nuevo
checkpoint debe guardarse con `--save` o `--save-compressed`. Los archivos de
salida deben quedar fuera del directorio `--continuation`, para conservar sus
checkpoints publicados y su tabla. Las exportaciones TXT no se sobrescriben.
Para seguir desde el resultado, se utiliza ese nuevo archivo con `--load` o
`--load-compressed`, según su formato.
No se permite `--seed` al añadir iteraciones: se restaura el flujo aleatorio
guardado. Se admite `--preload-ordago` o `--preload-ordago-compressed` junto con
entrenamiento: inicializa únicamente nodos nuevos y conserva el aprendizaje
restaurado. Usa de nuevo el mismo origen y `--preload-weight` para mantener la
configuración. Estas opciones no se admiten para una evaluación o exportación
sin entrenamiento.

Al reanudar un checkpoint, `--threads N` reparte las nuevas iteraciones entre
hilos que comparten **esa única estrategia**, igual que en `ordago`. Por ejemplo:

```sh
build/release/mus-solver \
    --format ordago-score --target 40 --score 30:30 \
    --continuation marcador-40 \
    --load-compressed marcador-40/score-30-30.cfr.zst \
    --iterations 100000 --threads 8 --report-every 1000 \
    --save-compressed score-30-30-parallel.cfr.zst
```

`--threads-per-cell` se reserva para el coordinador de tablas y no se admite
con `--load` o `--load-compressed`. Los informes esperan a que todos los hilos
alcancen el final del bloque; después de unirlos se guardan los contadores
globales y el flujo aleatorio del hilo principal. Los flujos auxiliares se
derivan de este al reanudar. Dividir una ejecución paralela entre procesos no
garantiza el mismo resultado que ejecutarla sin interrupciones. Un hilo mantiene
la reanudación exacta del recorrido secuencial.

Los informes de `--report-every` muestran el contador total, incluidas las
iteraciones anteriores. El informe `score-checkpoint` separa
`initial_iterations`, `added_iterations` y `total_iterations`.

La continuación se lee de `values.txt` y permanece fija durante esta ejecución.
Solo se necesitan los valores de las diagonales superiores; no se cargan sus
estrategias. La celda seleccionada puede no estar publicada todavía. El esquema
del checkpoint debe coincidir con el marcador, objetivo, versión y valores de
continuación. Un cambio en esos valores impide una reanudación exacta.
Conserve la tabla compatible junto con el checkpoint.

Este modo **no publica un valor nuevo en `values.txt`**: modificarlo afectaría
a las estrategias que ya usaron ese marcador como continuación. El checkpoint
ampliado se evalúa o exporta pasando explícitamente `--load[-compressed]`, sin
`--iterations`. `--evaluate` usa las muestras de `--evaluation-samples`, con
semilla 0 por defecto o la indicada por `--seed` en esa evaluación separada.

Al comenzar otra mano puede seleccionarse su estrategia guardada:

```sh
build/release/mus-solver \
    --format ordago-score --target 5 --score 2:1 \
    --continuation marcador-5 \
    --evaluate --evaluation-samples 10000 \
    --export-strategy marcador-5-2-1.txt
```

La evaluación recorre manos completas muestreadas con la **estrategia media**,
con política uniforme en los conjuntos no visitados. Usa un flujo aleatorio
separado y no modifica los arrepentimientos. El valor guardado es una estimación
Monte Carlo de esa política, no la utilidad de la última iteración ni una
medida de explotabilidad. `win_probability = (value + 1) / 2`.

`--evaluate` produce una evaluación nueva, indicada con `estimate=fresh`, y
muestra su semilla, el `stored_value` de la tabla y las muestras utilizadas
para estimarlo (`stored_samples`). Ambos valores pueden diferir por muestreo.
Repetir la evaluación con la misma semilla, el mismo número de muestras y la
misma estrategia reproduce la evaluación nueva; no exige que coincida con la
estimación guardada durante el entrenamiento.

El error de
muestreo de las continuaciones se propaga hacia marcadores anteriores. Sigue
aplicándose la limitación de recuerdo imperfecto de la abstracción de `ordago`;
el entrenamiento no garantiza un equilibrio.

La API está en `include/mus_solver/mus_score.h`: permite leer la tabla, crear
un `MusScoreGame` para el marcador actual, inicializar el estado y cargar el
checkpoint correspondiente mediante C-CFR. Al terminar la mano, si nadie ha
alcanzado el objetivo, se suman los tantos resueltos al marcador inicial y se
intercambian los jugadores antes de cargar la siguiente celda. El descriptor,
la tabla y el estado deben permanecer vivos mientras el entrenador los use;
cada almacén pertenece a un único marcador.

Las pruebas comprueban signos, suma cero, órdagos, fin anticipado, tanteo de
lances sin negociación, acciones legales, aplicación y deshacer, persistencia
y publicación por diagonales. Un subjuego a tres tantos, con reparto fijo,
cartas conocidas, sin mus, apertura de dos u órdago y sin subidas numéricas,
compara la continuación por tabla con el
desarrollo directo del mismo árbol hasta el final de la partida. Esta prueba
valida la continuación, la orientación y el signo; comparte las reglas del
juego y no es un oráculo independiente de esas reglas. Otras pruebas fijan
explícitamente los menús de apuestas y los resultados de tanteo a tres y cinco
tantos, incluidos los lances sin negociación y los empates a favor de mano.
Hay regresiones explícitas de cobros inmediatos frente a pendientes, órdago
posterior a un grande en paso a `39–39`, prioridad del tanteo final y ausencia
de filtraciones al variar solo cartas ocultas. Se recorre también todo el árbol
de envites de grande a distintos marcadores para comprobar las claves y menús.
Otras regresiones comprueban la unión de las historias 2+4 y 4+2, su cobro
diferenciado, el límite estricto acumulado y la precarga de menús reducidos.
Las pruebas de entrenamiento y CLI cubren además mus,
descartes y la selección de marcadores a 40. Estas comprobaciones no equivalen
a resolver exhaustivamente el juego completo de información imperfecta.

## Entrenamiento

Entrenamiento nuevo, reproducible y con informes periódicos:

```sh
build/release/mus-solver \
    --iterations 1000000 \
    --seed 0 \
    --report-every 100000 \
    --save mus-1m.cfr
```

Para repartir esas mismas iteraciones entre varios hilos:

```sh
build/release/mus-solver \
    --iterations 1000000 \
    --threads 8 \
    --seed 0 \
    --report-every 100000 \
    --save mus-1m-parallel.cfr
```

En los tres formatos históricos, `--iterations` indica el total global: cada bloque se distribuye de la
forma más uniforme posible entre los hilos. Cada hilo mantiene el `Trainer`, el
estado raíz y el flujo aleatorio independientes que exige C-CFR, y todos
actualizan un único `InfoStore` concurrente. El hilo principal también entrena,
por lo que `--threads 8` crea siete hilos adicionales. Antes de emitir un
informe, guardar, evaluar o exportar, el programa espera a que finalice el
bloque completo. Los hilos y sus espacios de trabajo se conservan entre
informes durante toda la ejecución; cada informe mantiene su barrera de
sincronización. El valor predeterminado, `--threads 1`, conserva el recorrido
secuencial y reproducible de las versiones anteriores.

Los entrenamientos nuevos de `ordago` reservan capacidad para reducir las
ampliaciones del almacén. La estimación conserva 12.600 infosets por iteración
durante las primeras 1.000 iteraciones y añade 1.200 por iteración posterior,
hasta un máximo de 35,4 millones de infosets reservados. Es una heurística,
no un límite de entrenamiento. Los checkpoints cargados no usan esta reserva
automática. `--reserve-information-sets N` sustituye la estimación; conviene
ajustarlo si el tamaño de la carga o la memoria disponible son distintos.

Con varios hilos, una misma semilla determina los flujos aleatorios iniciales,
pero el orden de las actualizaciones depende del planificador y el resultado no
es reproducible bit a bit. Los checkpoints paralelos guardan correctamente el
almacén y los contadores globales y se pueden reanudar. El formato de checkpoint
de C-CFR contiene un solo flujo aleatorio; al reanudar, los flujos auxiliares se
derivan de él de nuevo. Por tanto, dividir una ejecución paralela entre procesos
no produce necesariamente el mismo fichero que ejecutarla sin interrupciones.

`single-bet` es el formato predeterminado, por lo que los comandos y
checkpoints existentes conservan su comportamiento. Para entrenar con subidas:

```sh
build/release/mus-solver \
    --format raises-to-eight \
    --iterations 1000000 \
    --seed 0 \
    --save mus-raises-1m.cfr
```

Para entrenar con mus, descartes y órdago:

```sh
build/release/mus-solver \
    --format ordago \
    --iterations 1000000 \
    --seed 0 \
    --save mus-ordago-1m.cfr
```

Un informe normal no recorre el árbol completo:

```text
report iterations=100000 threads=8 information_sets=... traversals=... visited_nodes=... seconds=...
```

Continuación exacta del mismo flujo aleatorio en el modo secuencial
predeterminado:

```sh
build/release/mus-solver \
    --load mus-1m.cfr \
    --iterations 500000 \
    --save mus-1.5m.cfr
```

El checkpoint binario contiene la estrategia acumulada, los regrets, las
estadísticas, la semilla interna y el número de iteraciones. Los identificadores
de esquema son independientes e incompatibles entre sí:

- `single-bet`: `mus.heads-up-single-hand/v1`;
- `raises-to-eight`: `mus.heads-up-single-hand.raises-to-eight/v1`;
- `ordago`: `mus.heads-up-single-hand.ordago-with-mus/v9-muses-1` por defecto.

La compilación con dos muses usa el sufijo `v9-muses-2`, por lo que sus
checkpoints no se mezclan con los del límite predeterminado.

Al cargar un checkpoint de `raises-to-eight` hay que volver a indicar
`--format raises-to-eight`.
Lo mismo se aplica a `ordago`: hay que indicar `--format ordago`.

Los checkpoints pueden comprimirse en streaming con Zstandard nivel 1. Es un
modo explícito: `--save` y `--load` conservan el formato binario original.

```sh
build/release/mus-solver \
    --format ordago \
    --iterations 1000000 \
    --save-compressed mus-ordago-1m.cfr.zst

build/release/mus-solver \
    --format ordago \
    --load-compressed mus-ordago-1m.cfr.zst \
    --iterations 500000 \
    --save-compressed mus-ordago-1.5m.cfr.zst
```

## Evaluación exacta

```sh
build/release/mus-solver --load mus-1m.cfr --evaluate
```

También se puede evaluar la estrategia uniforme inicial:

```sh
build/release/mus-solver --evaluate
```

La evaluación de `single-bet` y `raises-to-eight` enumera sus repartos e
historiales y trata como uniformes los conjuntos de información aún no
visitados. Calcula:

- valor del perfil para ambos jugadores;
- mejor respuesta de cada jugador;
- mejora unilateral, NashConv y explotabilidad.

Los formatos históricos acumulan valores contrafactuales por profundidad sin
materializar el árbol completo. La evaluación de `ordago` desde la raíz no es
viable con el evaluador general por su árbol de mus, descartes y envites
aditivos. Por eso, `--format ordago --evaluate` calcula un diagnóstico acotado:
promedia las métricas exactas de dos repartos fijos en el último lance, uno de
juego y otro de punto. La salida lo identifica con
`scope=bounded-final-lances`. Esta señal permite comparar checkpoints, pero no
mide la explotabilidad desde la raíz ni restablece la garantía de convergencia.
La API permite además evaluar cualquier subestado acotado mediante
`mus_ordago_evaluation_metrics_from_state`.

### Referencia reproducible

Una ejecución de referencia en este entorno, el 31 de agosto de 2026, produjo:

| Estrategia | Iteraciones | Valor J0 | Explotabilidad |
|---|---:|---:|---:|
| Uniforme | 0 | 0,6079451 | 1,5693584 |
| MCCFR, semilla 0 | 1.000.000 | 0,0755868 | 0,0455666 |

El entrenamiento de un millón de iteraciones tardó 24,45 segundos y su
evaluación exacta 79,41 segundos en esa medición. Son referencias, no garantías
de rendimiento para otros equipos o compiladores.

## Exportar la estrategia

```sh
build/release/mus-solver \
    --load mus-1m.cfr \
    --export-strategy mus-1m.txt
```

Cada fila contiene la abstracción de mano correspondiente, posición, lance,
resumen de fuerza anterior y probabilidades con nombres de mus. Solo se
exportan los conjuntos visitados. El destino no se sobrescribe: debe ser una
ruta nueva.

## API

Los encabezados públicos bajo `include/mus_solver/` exponen:

- el formato original mediante el prefijo `mus_single_bet_`;
- el formato con subidas mediante el prefijo `mus_raises_to_eight_`;
- el formato con mus y órdago mediante el prefijo `mus_ordago_`;
- el adaptador `Game`, el estado reversible y el exportador etiquetado para
  cada formato;
- el evaluador exacto incremental para los formatos históricos y la evaluación
  de subestados acotados de `ordago`.

Las funciones históricas `mus_game_*`, `mus_evaluation_*` y
`mus_strategy_write_text` siguen apuntando a `single-bet` para mantener la
compatibilidad de la primera versión.

`C-CFR` incorpora además `cfr_info_store_visit_sorted`, una visita de solo
lectura en orden de clave utilizada por el exportador.

## Fuera de alcance

Los formatos históricos no incluyen partidas completas a 40 tantos ni rotación
de la mano. `ordago-score` incorpora el marcador y la continuación con cambio
de mano descritos arriba. No se incluyen parejas, señas ni un motor de partida
interactivo. La consulta de decisiones y su integración con un motor externo
se describen en el adaptador siguiente.
`ordago` representa el órdago aceptado como el final de la mano por 40 tantos;
no modela el marcador previo de una partida.

## Adaptador para jugar contra humanos

El ejecutable `build/release/mus-bot` y la API `mus_adapter_*` utilizan un
checkpoint `ordago` para responder a observaciones de partidas reales de dos
jugadores. Admiten envites arbitrarios y repetidos mediante interpolación y
mezcla de estados próximos, con ajustes sencillos por marcador y un máximo de
un mus. El motor externo conserva las reglas, las acciones legales y el tanteo.
El adaptador utiliza estrategias `ordago`; no carga las políticas ni las tablas
de continuación de `ordago-score`.

El CLI requiere la biblioteca de desarrollo de json-c y `pkg-config`, además
de las dependencias del solver. Con `--strategy` consulta las filas necesarias
del exportador de texto ordenado directamente en disco, sin cargar toda la
estrategia en RAM. Recibe una observación JSON por línea:

```sh
build/release/mus-bot --strategy mus-ordago-100m.txt --seed 17
```

El [contrato del adaptador](docs/adapter.md) documenta el protocolo, la API,
las aproximaciones, los diagnósticos y el benchmark reproducible.
