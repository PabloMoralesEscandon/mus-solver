# Entrenamiento y archivos de estrategia

[Volver al README](../README.md) · [Reglas](rules.md) · [Tabla de continuación](continuation-table.md)

Los ejemplos se ejecutan desde la raíz, después de `make solver`. Los números
son presupuestos ilustrativos para aprender a usar el programa; no representan
resultados medidos ni un nivel de juego garantizado.

## Mano aislada

`ordago` es la variante normal. Un checkpoint guarda las estrategias
acumuladas, los regrets —valores que orientan el aprendizaje—, los contadores
y el estado aleatorio. Un TXT exportado contiene probabilidades para consulta;
no permite reanudar el entrenamiento.

Para cuatro jugadores por parejas usa `ordago-four` en los mismos comandos.
Entrena una sola estrategia para la mano, sin tabla de continuación ni marcador
inicial. Admite `--threads`, guardado, reanudación y exportación como `ordago`.
Sus TXT se consultan mediante la API C; el bot admite únicamente dos jugadores.

```sh
build/release/mus-solver --format ordago \
    --iterations 100 --seed 7 --report-every 50 \
    --save-compressed generica.cfr.zst --export-strategy generica.txt

build/release/mus-solver --format ordago \
    --load-compressed generica.cfr.zst --iterations 100 \
    --save-compressed generica-ampliada.cfr.zst

build/release/mus-solver --format ordago \
    --load-compressed generica-ampliada.cfr.zst \
    --export-strategy generica-ampliada.txt
```

`--iterations N` añade N iteraciones al checkpoint cargado. Al reanudar se
restaura el estado aleatorio; no se admite `--seed` para reemplazarlo mientras
se añaden iteraciones. `--load` y `--save` usan el formato binario sin comprimir;
`--load-compressed` y `--save-compressed` usan Zstandard. Indica la opción
correspondiente al contenido del archivo, no solo a su extensión.

Las exportaciones TXT requieren una ruta nueva y contienen solo los nodos
visitados, ordenados por clave. Sus etiquetas describen mano, posición, fase,
historial abstraído y probabilidades. El [bot](adapter.md) los consulta en disco.

### Estrategias históricas

Para seguir usando un checkpoint del antiguo `ordago`:

```sh
build/release/mus-solver --format ordago-deprecated \
    --load-compressed antigua.cfr.zst --export-strategy antigua.txt

build/release/mus-solver --format ordago-deprecated \
    --load-compressed antigua.cfr.zst --iterations 100 \
    --save-compressed antigua-ampliada.cfr.zst
```

El antiguo comando `--format ordago` se sustituye por `--format ordago-deprecated`.
Los archivos conservan su esquema v9 y todo el aprendizaje acumulado. Los
checkpoints que antes se cargaban con `--format ordago-public` se cargan ahora
con `--format ordago`; el nombre anterior sigue siendo un alias válido.
**Renombrar el comando no exige reentrenar ni convertir el checkpoint.**

## Tablas con marcador

Usa `ordago-score` para dos jugadores y `ordago-score-four` para parejas.
Los ejemplos siguientes usan dos jugadores. `--target` fija el objetivo entre
1 y 40; por defecto es 40. `A:B` siempre se orienta desde quien empieza de mano.

```sh
build/release/mus-solver --format ordago-score \
    --score 39:39 --iterations 100 --evaluation-samples 1000 --seed 7 \
    --continuation tabla
```

El directorio padre debe existir; el programa crea el directorio final.
`--iterations` se aplica **a cada celda pendiente**. Las diagonales de suma
mayor se resuelven antes que las de suma menor y se publican completas. La
[guía de continuación](continuation-table.md) explica esta dependencia.

| Selección | Cobertura |
|---|---|
| `--score A:B` | Todas las diagonales desde la suma máxima hasta A+B. |
| `--score-region A:B` | Celdas con `(x >= A y y >= B) o (x >= B y y >= A)`. |
| Sin selección | Desde 0:0; a 40, la tabla completa tiene 1.600 celdas. |

Una región incluye las continuaciones necesarias al sumar tantos y cambiar
la mano. Sin `--score`, crear una región comienza desde A:B. Los límites se
guardan en la tabla; al reanudar se pueden omitir. Pedir otros límites o un
marcador fuera de la región produce un error. Para cambiar la cobertura, usa
otro directorio.

```sh
build/release/mus-solver --format ordago-score \
    --score-region 35:35 --iterations 100 --evaluation-samples 1000 --seed 7 \
    --continuation tabla-region
```

### Hilos y memoria

| Modo | `--threads N` | `--threads-per-cell M` |
|---|---|---|
| Mano aislada o un checkpoint cargado | N hilos comparten una estrategia. | No se admite. |
| Coordinador de tablas | Hasta N celdas de una diagonal a la vez. | Hasta M hilos comparten la estrategia de cada celda. |

Ambas opciones valen 1 por defecto. Las iteraciones se reparten, no se
multiplican por el número de hilos. Más celdas simultáneas requieren más
almacenes de estrategias; más hilos por celda añaden estados y espacio de
trabajo. Ajusta la concurrencia a la memoria disponible.

Un hilo por estrategia permite reproducir el entrenamiento y su reanudación.
Cambiar solo la cantidad de celdas simultáneas mantiene las semillas y los
resultados de cada celda. Con varios hilos dentro de una estrategia, el orden
de actualizaciones depende del planificador: la semilla no garantiza archivos
idénticos ni equivalencia entre una ejecución continua y una reanudada.

`--reserve-information-sets N` reserva capacidad del almacén, por celda si
corresponde. Es una reserva de memoria, no un límite de nodos ni de trabajo.
`--report-every N` informa del progreso; en tablas, `score-report` muestra el
avance dentro de cada celda y `score-cell` su finalización.

### Guardado y recuperación

| Archivo en `--continuation` | Uso |
|---|---|
| `values.txt` | Valores publicados, cobertura, esquemas y estadísticas por celda. |
| `score-AA-BB.cfr.zst` | Checkpoint de cada marcador. |
| `recovery.txt` | Permite reutilizar celdas guardadas de una diagonal incompleta. |
| `training.lock` | Metadatos del escritor; su existencia no significa que siga activo. |
| `extension.txt` | Origen y parámetros de una ampliación, cuando la hay. |

Repite el comando para completar el trabajo pendiente. Las celdas publicadas
conservan sus valores e iteraciones; **reanudar una tabla no añade iteraciones
a las celdas ya publicadas**. Se pueden usar presupuestos distintos al continuar
hacia diagonales anteriores.

El bloqueo real se mantiene sobre el directorio y el sistema lo libera al
terminar el proceso, incluso tras una señal. No borres `training.lock` para
reanudar. El protocolo requiere un sistema de archivos local con `flock` sobre
directorios; no coordina escritores entre equipos mediante almacenamiento de red.
Mantén el directorio en su ubicación y evita modificar sus archivos durante el uso.

La recuperación valida parámetros y checkpoints antes de reutilizar una celda.
Las entradas incompletas o incompatibles se recalculan. Cubre interrupciones
del proceso; no garantiza conservar las últimas escrituras tras un corte de
energía. Las estrategias publicadas no se sobrescriben al reanudar.

### Añadir iteraciones a toda una tabla

Usa `--extend-continuation` con un destino distinto:

```sh
build/release/mus-solver --format ordago-score --score 39:39 \
    --extend-continuation tabla --continuation tabla-ampliada \
    --iterations 100 --evaluation-samples 1000
```

El origen debe contener todos los marcadores solicitados y permanecer
inmutable. Se restauran los checkpoints, se añaden iteraciones y se recalculan
las continuaciones por diagonal. No se admite `--seed` ni `--load` en este modo.
El aprendizaje previo se conserva: las iteraciones acumuladas no equivalen a
haber entrenado desde cero bajo los valores nuevos de continuación.

Para reanudar una ampliación parcial, repite el mismo origen, destino y
parámetros. Para ampliar otra vez, usa el resultado como origen y otro destino.

### Añadir iteraciones a una sola celda

```sh
build/release/mus-solver --format ordago-score --score 39:39 \
    --continuation tabla --load-compressed tabla/score-39-39.cfr.zst \
    --iterations 100 --threads 2 \
    --save-compressed celda-ampliada.cfr.zst --export-strategy celda-ampliada.txt
```

Las salidas deben quedar fuera de `--continuation`. Este modo conserva la
tabla de valores y **no publica un valor nuevo**: otras celdas ya entrenadas
pueden depender del valor anterior. Mantén el checkpoint junto con su tabla
compatible. Para evaluar o exportar el resultado, cárgalo explícitamente.

### Inicializar desde una estrategia genérica

En dos jugadores se puede usar un checkpoint de `ordago` como punto
de partida, mediante `--preload-ordago` o `--preload-ordago-compressed`:

```sh
build/release/mus-solver --format ordago-score --score 39:39 \
    --preload-ordago-compressed generica.cfr.zst --preload-weight 1 \
    --iterations 100 --evaluation-samples 1000 --seed 7 \
    --continuation tabla-precargada
```

La opción conserva su nombre por compatibilidad y también admite
`ordago-deprecated`. No acepta TXT ni estrategias de cuatro jugadores.
La precarga inicializa los nodos al crearlos y conserva los ya entrenados.
Comparte un único almacén genérico en memoria entre las celdas activas.

La política transferida se filtra al menú legal y se mezcla con un 5 % de
política uniforme. `--preload-weight` fija su peso inicial, por defecto 1.
No se copian los regrets antiguos, sus unidades de utilidad ni el RNG.
Con la genérica pública la correspondencia de observaciones es exacta; con la
histórica se combinan candidatos sin consultar cartas rivales, una aproximación.
Ninguna precarga garantiza aprender más rápido ni obtener una política mejor.

Repite las opciones al reanudar para inicializar también los nodos nuevos.
El checkpoint resultante se puede usar sin la genérica; en ese caso, los nodos
nuevos empiezan uniformes. Los contadores de precarga informan de cobertura,
no de calidad de juego. La evaluación y el TXT no añaden una consulta implícita
a la genérica para nodos ausentes.

## Evaluar y exportar

| Variante | Alcance de `--evaluate` |
|---|---|
| `ordago`, `ordago-deprecated` | Diagnóstico de dos repartos fijos en juego y punto, con ambas manos fijadas. No es explotabilidad desde la raíz. |
| `ordago-four` | Diferencia de tantos por muestreo de manos completas con la estrategia media, sin continuación. |
| `ordago-score`, `ordago-score-four` | Valor por muestreo de la estrategia media con continuaciones fijas. |
| `single-bet`, `raises-to-eight` | Evaluación exacta de sus árboles: valor, mejores respuestas, NashConv y explotabilidad. |

El diagnóstico de mano aislada informa `scope=bounded-final-lances` y
`root_bound=none`. No constituye una cota global ni restablece garantías de
convergencia. Para medir el bot con sus traducciones y respaldos, usa la
[evaluación de partidas](match-evaluation.md).

Para evaluar una estrategia de parejas sin marcador:

```sh
build/release/mus-solver --format ordago-four \
    --load-compressed cuatro.cfr.zst --evaluate --evaluation-samples 1000
```

La salida identifica `scope=single-hand-sampled` y `utility=points`. Informa el
valor de los cuatro jugadores, con igual valor entre compañeros y signo opuesto
entre parejas. `standard_error` se expresa en tantos. No informa probabilidad
de victoria, mejores respuestas ni explotabilidad. Las opciones de muestreo
adaptativo también se admiten con `--evaluate`. Sin checkpoint se evalúa una
política uniforme; los nodos ausentes de una estrategia también usan esa política.

Para evaluar y exportar una celda publicada:

```sh
build/release/mus-solver --format ordago-score --score 39:39 \
    --continuation tabla --evaluate --evaluation-samples 1000 --seed 7 \
    --export-strategy score-39-39.txt
```

Repite la exportación para cada marcador publicado si vas a usar una tabla
con el bot. Cada TXT debe llamarse `score-AA-BB.txt` y corresponder a la tabla
que se entregará al adaptador.

Las evaluaciones de tablas usan un flujo aleatorio separado, no entrenan y
usan una distribución uniforme en nodos ausentes. `estimate=fresh` distingue
la nueva medición de `stored_value`. `win_probability = (value + 1) / 2` es
una estimación con esa política y sus continuaciones, no una tasa medida del bot.

### Incertidumbre de muestreo

`standard_error` es `sqrt(varianza_muestral / muestras)`;
`win_probability_se` es su mitad. Con una sola muestra o datos antiguos sin
varianza se informa como desconocido. Solo mide ruido local con política y
continuaciones fijas; no mide error frente a la política óptima ni la
incertidumbre propagada desde otras celdas.

Por defecto se evalúan exactamente `--evaluation-samples` manos. Para detener
la evaluación al alcanzar un error estimado, añade juntas
`--evaluation-min-samples N` y `--evaluation-target-se X`; entonces
`--evaluation-samples` es el máximo. Esta parada adaptativa es una regla
práctica, no una garantía estadística, y puede sesgar la estimación. Usa un
presupuesto fijo para comparaciones que necesiten el mismo número de muestras.

## Compatibilidad de archivos

Los nombres de CLI no cambian los esquemas guardados. El cargador comprueba
variante, límite de muses y, para tablas, objetivo, marcador y huella de las
continuaciones. Conserva los archivos de una misma tabla juntos.

`ordago-four` usa `mus.four-partnership-single-hand.ordago/v1-muses-1`
(o `muses-2`). Sus checkpoints no se intercambian con `ordago-score-four`
ni con las variantes de dos jugadores.

| Tabla | Esquema |
|---|---|
| Dos jugadores | `mus.ordago-score/v4` |
| Dos jugadores, región | `mus.ordago-score-region/v1` |
| Cuatro jugadores | `mus.ordago-score-four/v1` |
| Cuatro jugadores, región | `mus.ordago-score-four-region/v1` |

Las tablas completas y las regionales no intercambian checkpoints, incluso
con una región 0:0. Las versiones de dos jugadores anteriores a v4 requieren
reentrenar. En cuatro jugadores, las reglas actuales son
`mus.four-partnership-score/v3-muses-1` o `v3-muses-2`; archivos de reglas v1/v2
requieren la versión correspondiente del solver o entrenamiento nuevo.

`values.txt` puede incluir `statistics=sample-variance-v1`. El lector actual
acepta tablas anteriores sin estadísticas; lectores anteriores a esa extensión
no leen las tablas que la incluyen. Las estadísticas no cambian la identidad
de las reglas ni los checkpoints.
