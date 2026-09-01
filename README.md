# Solver simplificado de mus para dos jugadores

Solver en C17 de una única mano de mus para dos jugadores. Entrena una
estrategia mediante external-sampling MCCFR, guarda checkpoints reanudables y
calcula valor, mejores respuestas y explotabilidad exactos bajo demanda.

## Formatos de reglas

- El jugador 0 siempre es mano y el jugador 1 es postre.
- Se usa la baraja española de 40 cartas, con ochos reyes y ocho ases según las
  equivalencias del mus: tres = rey y dos = as.
- Se juegan en orden grande, chica, pares y juego o punto.
- No hay fase de mus ni descartes.
- El formato `single-bet` conserva la primera versión: en cada lance se puede
  pasar o envidar 2 y, ante el envite, solo se puede querer o no querer.
- El formato `raises-to-eight` añade respuestas y subidas en todos los lances.
  Quien abre puede pasar o envidar 2. Si pasa, postre también puede pasar o
  envidar 2. Ante un envite se puede no querer, querer o subir duplicando la
  cantidad: 2 → 4 → 8. Ante 8 ya no se puede volver a subir.
- Los jugadores observan todo el historial de apuestas anterior. Las
  declaraciones de pares y juego se hacen públicas al llegar a esos lances.
- Se aplica literalmente el tanteo de `Simulador-Mus`: el envite o deje se
  suma al valor intrínseco de pares, juego y punto.
- La utilidad del jugador 0 es sus tantos menos los del jugador 1; la utilidad
  contraria es su opuesto.

Al no querer el envite inicial se concede una piedra. Si se rechaza una subida
a 4 u 8, se concede el último envite ya cubierto: 2 o 4 piedras. Si se quiere,
el lance se resuelve por la cantidad final aceptada.

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
make test-sanitize
make dependencies
```

Para una compilación release con optimización entre unidades de traducción,
use `make lto`. Sus artefactos se escriben en `build/lto/release` y los de
`C-CFR` en `C-CFR/build/lto/release`, sin reemplazar la compilación normal.

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

`single-bet` es el formato predeterminado, por lo que los comandos y
checkpoints existentes conservan su comportamiento. Para entrenar con subidas:

```sh
build/release/mus-solver \
    --format raises-to-eight \
    --iterations 1000000 \
    --seed 0 \
    --save mus-raises-1m.cfr
```

Un informe normal no recorre el árbol completo:

```text
report iterations=100000 information_sets=... traversals=... visited_nodes=... seconds=...
```

Continuación exacta del mismo flujo aleatorio:

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
- `raises-to-eight`: `mus.heads-up-single-hand.raises-to-eight/v1`.

Al cargar un checkpoint de `raises-to-eight` hay que volver a indicar
`--format raises-to-eight`.

## Evaluación exacta

```sh
build/release/mus-solver --load mus-1m.cfr --evaluate
```

También se puede evaluar la estrategia uniforme inicial:

```sh
build/release/mus-solver --evaluate
```

La evaluación enumera exactamente todos los repartos e historiales y trata
como uniformes los conjuntos de información aún no visitados. Calcula:

- valor del perfil para ambos jugadores;
- mejor respuesta de cada jugador;
- mejora unilateral, NashConv y explotabilidad.

El evaluador acumula valores contrafactuales por profundidad y no materializa
el árbol completo en memoria. Aun así, la evaluación completa es una operación
costosa y por eso solo se ejecuta al indicar `--evaluate`.

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

Cada fila contiene la mano por clases, posición, lance, declaraciones
públicas, historial anterior y probabilidades con nombres de mus. Solo se
exportan los conjuntos visitados. El destino no se sobrescribe: debe ser una
ruta nueva.

## API

Los encabezados públicos bajo `include/mus_solver/` exponen:

- el formato original mediante el prefijo `mus_single_bet_`;
- el formato con subidas mediante el prefijo `mus_raises_to_eight_`;
- el adaptador `Game`, el estado reversible, el evaluador exacto incremental y
  el exportador etiquetado para cada formato.

Las funciones históricas `mus_game_*`, `mus_evaluation_*` y
`mus_strategy_write_text` siguen apuntando a `single-bet` para mantener la
compatibilidad de la primera versión.

`C-CFR` incorpora además `cfr_info_store_visit_sorted`, una visita de solo
lectura en orden de clave utilizada por el exportador.

## Fuera de alcance

Estos formatos no incluyen partidas a 40 tantos, rotación de la mano, mus,
descartes, cantidades distintas de 2/4/8, órdago, parejas, señas, consulta de
una jugada aislada ni una interfaz para jugar contra la estrategia.
