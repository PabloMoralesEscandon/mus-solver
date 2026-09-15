# Mus Solver

Solver de mus en C17 para dos jugadores y cuatro jugadores por parejas.
Entrena estrategias con external-sampling Monte Carlo CFR (MCCFR), permite
reanudar el entrenamiento y exporta las probabilidades de cada decisión.
Incluye un bot y un evaluador de partidas para dos jugadores.

**Para entrenar una mano aislada, usa `ordago`: es la variante normal
y predeterminada.** Usa únicamente las cartas propias y la información pública
al decidir. `ordago-deprecated` conserva el modelo histórico para poder seguir
cargando, consultando y ampliando las estrategias ya entrenadas con él.

## Instalación

Necesitas un compilador C17, Make, Git, Zstandard y, para el bot, json-c y
`pkg-config`. Las pruebas requieren Python 3. El CLI de tablas con marcador
utiliza funciones POSIX y bloqueos de directorio del sistema de archivos local.

En Debian o Ubuntu:

```sh
sudo apt install build-essential git pkg-config libzstd-dev libjson-c-dev python3
git clone --recurse-submodules https://github.com/PabloMoralesEscandon/mus-solver.git
cd mus-solver
make
```

Si ya tienes el repositorio, ejecuta `git submodule update --init --recursive`
antes de compilar.

| Comando | Resultado en `build/release/` |
|---|---|
| `make` | Biblioteca y los tres ejecutables. |
| `make solver` | `mus-solver` y `libmus_solver.a`; no requiere json-c. |
| `make bot` | `mus-bot`, para consultar decisiones por JSON. |
| `make match-eval` | `mus-match-eval`, para comparar políticas en partidas. |

## Elegir variante

| `--format` | Jugadores | Uso |
|---|---|---|
| **`ordago`** | 2 | **Normal:** mano aislada con mus, descartes, envites y órdago; información pública y utilidad en tantos. |
| `ordago-score` | 2 | Con marcador: optimiza el valor de ganar la partida mediante una tabla de continuación. |
| `ordago-score-four` | 4, por parejas | Con marcador: cada jugador decide con sus propias cartas. |
| `ordago-deprecated` | 2 | **Obsoleta:** compatibilidad con estrategias históricas de órdago. |
| `single-bet` | 2 | Modelo reducido: pasar o envidar 2, sin mus ni descartes. |
| `raises-to-eight` | 2 | Modelo reducido: subidas 2 → 4 → 8, sin mus ni descartes. |

### Comandos anteriores y estrategias guardadas

**El cambio de nombres no requiere reentrenar ni convertir archivos.**

| Comando anterior | Comando actual | Entrenamiento guardado |
|---|---|---|
| `--format ordago-public` | `--format ordago` | Se carga, reanuda y exporta directamente. |
| `--format ordago` | `--format ordago-deprecated` | Se carga, reanuda y exporta directamente. |
| Sin `--format` (antes elegía `single-bet`) | `--format single-bet` | Conserva el modelo reducido anterior. |

Ahora omitir `--format` selecciona `ordago`. `ordago-public` sigue aceptándose
como alias de la variante normal. Las API C y los esquemas de los archivos
se conservan; el formato se valida al cargar cada checkpoint.

`ordago-deprecated` se mantiene para aprovechar las estrategias existentes.
Su modelo utiliza resultados aún ocultos en sus decisiones y límites de envite;
los entrenamientos nuevos deben usar `ordago` o una variante con marcador.

## Primer entrenamiento

Desde la raíz del repositorio:

```sh
build/release/mus-solver --format ordago \
    --iterations 100 --seed 7 \
    --save-compressed generica.cfr.zst --export-strategy generica.txt
```

Es un ejemplo para comprobar el flujo de trabajo, no un presupuesto que
garantice una estrategia fuerte. El checkpoint permite reanudar; el TXT sirve
para consultar las probabilidades y alimentar al bot.

```sh
build/release/mus-solver --format ordago \
    --load-compressed generica.cfr.zst --iterations 100 \
    --save-compressed generica-ampliada.cfr.zst
```

Para empezar con una tabla pequeña de partidas a 40 tantos:

```sh
build/release/mus-solver --format ordago-score \
    --score 39:39 --iterations 100 --evaluation-samples 1000 --seed 7 \
    --continuation tabla
```

Las iteraciones de una tabla se aplican **a cada celda pendiente**. Entrenar
desde `0:0` a 40 puede requerir 1.600 estrategias; empieza por un marcador final
o una región. La [guía de entrenamiento](docs/training.md) explica la cobertura,
los hilos, la recuperación y la ampliación de tablas.

## Usar una estrategia

El bot recibe una observación JSON por línea y devuelve una acción legal con
sus probabilidades. El motor que lo integra conserva las reglas y el tanteo.

```sh
build/release/mus-bot --strategy generica.txt --seed 7
```

Consulta el [protocolo y ejemplo de petición](docs/adapter.md#petición).
Para comparar dos estrategias en partidas completas:

```sh
build/release/mus-match-eval \
    --strategy-a generica.txt --strategy-b otra.txt \
    --matches 1000 --seed 7 > partidas.json
```

El bot y el evaluador de partidas admiten dos jugadores y un mus. Las
estrategias de cuatro jugadores se entrenan y consultan mediante la biblioteca C.

## Documentación

| Guía | Contenido |
|---|---|
| [Reglas y variantes](docs/rules.md) | Envites, tanteo, abstracciones y compatibilidad histórica. |
| [Entrenamiento](docs/training.md) | Guardar, reanudar, exportar, entrenar tablas y configurar recursos. |
| [Tabla de continuación](docs/continuation-table.md) | Cómo se usa el marcador para valorar el resto de la partida. |
| [Bot e integración](docs/adapter.md) | Protocolo JSON, contabilidad del motor, políticas híbridas y API. |
| [Evaluación de partidas](docs/match-evaluation.md) | Comparar políticas y entender las métricas y su alcance. |
| [Cuatro jugadores](docs/mus-four-score.md) | Reglas de parejas, entrenamiento y API específica. |
| [Desarrollo](docs/development.md) | Compilación, pruebas y puntos de entrada de la biblioteca. |

Cada ejecutable incluye ayuda con `--help`.

## Alcance

Se utiliza la baraja española de 40 cartas con ocho reyes y ocho ases. Las
apuestas y la memoria de los jugadores están abstraídas para reducir el árbol:
no se representa todo el mus sin restricciones. La abstracción pierde recuerdo
perfecto y el entrenamiento no garantiza converger a un equilibrio.

`--evaluate` en `ordago` es un diagnóstico de dos repartos fijos en los
lances finales, con ambas manos fijadas; **no mide la explotabilidad del juego
completo**. Las variantes con marcador estiman valores por muestreo. Las
victorias de `mus-match-eval` corresponden a las políticas, reglas y rival
seleccionados. No se incluyen señas ni una interfaz gráfica de partida.

## Licencia

Copyright 2026 Pablo Morales Escandon. Publicado bajo [Apache-2.0](LICENSE).
Las dependencias incluidas como submódulos se mantienen en sus repositorios:
[C-CFR](https://github.com/PabloMoralesEscandon/C-CFR), también con
[Apache-2.0](C-CFR/LICENSE), y
[Simulador-Mus](https://github.com/PabloMoralesEscandon/Simulador-Mus).
