# Solver simplificado de mus para dos jugadores

Solver en C17 de una única mano de mus para dos jugadores. Entrena una
estrategia mediante external-sampling MCCFR, guarda checkpoints reanudables y
calcula valor, mejores respuestas y explotabilidad exactos bajo demanda.

## Formatos de reglas

- El jugador 0 siempre es mano y el jugador 1 es postre.
- Se usa la baraja española de 40 cartas, con ochos reyes y ocho ases según las
  equivalencias del mus: tres = rey y dos = as.
- Se juegan en orden grande, chica, pares y juego o punto.
- Los formatos históricos no tienen fase de mus ni descartes. El formato
  `ordago` descrito más abajo sí los incorpora.
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
- La utilidad del jugador 0 es sus tantos menos los del jugador 1; la utilidad
  contraria es su opuesto.

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

La compilación release usa LTO y requiere la biblioteca de desarrollo de
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

`--iterations` siempre indica el total global: cada bloque se distribuye de la
forma más uniforme posible entre los hilos. Cada hilo mantiene el `Trainer`, el
estado raíz y el flujo aleatorio independientes que exige C-CFR, y todos
actualizan un único `InfoStore` concurrente. El hilo principal también entrena,
por lo que `--threads 8` crea siete hilos adicionales. Antes de emitir un
informe, guardar, evaluar o exportar, el programa espera a que finalice el
bloque completo. El valor predeterminado, `--threads 1`, conserva el recorrido
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

Estos formatos no incluyen partidas completas a 40 tantos, rotación de la
mano, parejas, señas, consulta de una jugada aislada ni una interfaz para jugar
contra la estrategia. `ordago` representa el órdago aceptado como el final de
la mano por 40 tantos; no modela el marcador previo de una partida.
