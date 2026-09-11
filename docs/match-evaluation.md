# Evaluar la política desplegada en partidas completas

`mus-match-eval` juega partidas a 40 tantos con el simulador de dos jugadores
y el mismo `mus_adapter_decide` que usa `mus-bot`. Incluye selección de celda,
interpolación, vecinos, traducción a acciones legales, respaldo y muestreo de
la decisión final. Los archivos de políticas se leen sin entrenarlos.

```sh
make
build/release/mus-match-eval \
    --strategy-a public.txt --opponent three \
    --matches 1000 --seed 7 --trace decisiones.csv > partidas.json
```

El ejemplo compara la política A con un rival de control, no con un jugador
óptimo. `--opponent passive` (predeterminado) corta mus, pasa y rechaza envites.
`three` pide un mus, descarta una carta, abre/sube de tres en tres mientras el total
previo sea menor que nueve y acepta a partir de ahí o ante un órdago. Sirve también para
probar respuestas a incrementos ajenos a la abstracción del solver.

Para comparar dos políticas:

```sh
build/release/mus-match-eval \
    --strategy-a anterior.txt --strategy-b nueva.txt \
    --matches 1000 --seed 7 > comparacion.json
```

Cada política tiene su propia instancia del adaptador y su generador de
acciones. La mano inicial alterna entre A y B y cambia normalmente tras cada
reparto. El simulador baraja con su propio flujo `rand()`, separado del
muestreo del adaptador. La semilla hace reproducible la ejecución con el mismo
binario, biblioteca C, archivos y opciones; no se promete el mismo reparto
entre plataformas ni un acoplamiento de cartas entre políticas diferentes.

## Entradas y cobertura

Para cada lado puede usar exactamente una de estas opciones:

- `--strategy-a FILE` / `--strategy-b FILE`: TXT ordenado.
- `--load-a FILE` / `--load-b FILE`: checkpoint sin comprimir.
- `--load-compressed-a FILE` / `--load-compressed-b FILE`: checkpoint Zstandard.

Se admiten las genéricas `ordago` y `ordago-public` de un mus. Para el selector
híbrido, añada ambas rutas de ese lado:

```sh
build/release/mus-match-eval \
    --strategy-a public.txt \
    --score-table-a continuacion/values.txt \
    --score-strategies-a continuacion \
    --opponent three --score 35:35 --matches 1000 --seed 7 > tramo-final.json
```

La política genérica sirve los marcadores iniciales excluidos o no publicados;
la tabla sirve las celdas listas. En una celda publicada, un nodo ausente usa
el respaldo del adaptador para esa celda. La evaluación conserva ese
comportamiento. El TXT v1 y el binario pueden diferir al consultar nodos con
masa cero; el evaluador reproduce el comportamiento del backend elegido.

`--score A:B` fija el marcador inicial de las **políticas A y B**, por defecto
0:0. Un experimento desde 35:35 mide partidas desde ese tramo, no victorias
desde 0:0. `--no-score-adjustment` desactiva en ambos adaptadores la heurística
de órdago de las genéricas, igual que en `mus-bot`.

## Reglas y observaciones

El anfitrión usa `simularRondaMusConEstrategias`, con los cobros diferidos del
simulador corregido. Los dejes se cobran durante la negociación; los envites
queridos, lances al paso y valores intrínsecos se cuentan al final. El órdago
querido o un deje decisivo terminan antes de cobrar los pendientes.

El entorno permite un mus y descartes no vacíos. Ofrece pasar o responder,
órdago y todos los totales numéricos enteros de 2 a 40 que admite la
negociación activa, incluidos aumentos de un tanto. Cada acción se valida en
una copia del envite del simulador. Este es el menú finito del experimento;
no se limita a los incrementos 2/4/7/10 del árbol entrenado.

La observación de cada jugador contiene solo sus cartas y datos públicos:
marcador inicial, dejes cobrados, contratos numéricos pendientes, historial
actual, fuerza mostrada, declaración de pares y recuento de descartes. El
anfitrión convierte la máscara canónica de descarte a las posiciones físicas
de sus cartas. Al cambiar la mano, reorienta los marcadores y jugadores a
mano/postre. No añade ganadores ni tantos intrínsecos obtenidos de cartas
ocultas a las consultas del bot.

## Resultados e interpretación

La salida JSON tiene `scope=deployed-adapter-matches`, partidas completadas,
victorias de A/B, frecuencia observada de victoria de A, manos, decisiones y
el tiempo del juego (excluye cargar las políticas y emitir el JSON final).
La cobertura cuenta exactos, interpolados, vecinos, respaldo y límite de mus,
por política y por cada una de estas dimensiones:

- Fase de la decisión.
- Marcador al inicio de la mano, orientado como mano/postre.
- Tipo de acción seleccionada.

Son distribuciones separadas, no su producto cartesiano. `strategy_counts`
separa genérica histórica, genérica pública, celda entrenada y rival de control.
El CSV opcional conserva conjuntamente fase, marcador, método y acción de cada
decisión; no contiene cartas. El archivo de traza debe ser nuevo: se rechaza
una ruta existente para conservar las políticas y otros archivos.

Las victorias miden estas políticas, rival, menú, marcador de salida y
semillas. Con pocas partidas la frecuencia puede variar mucho. No es una
medida de explotabilidad ni sustituye el valor del perfil medio utilizado por
la tabla: ese evaluador puede usar otro respaldo para nodos ausentes y no
incluye necesariamente las heurísticas del despliegue.

`--max-hands N` limita cada partida (1.000 por defecto). Si una partida no
termina dentro de ese límite, el comando falla y no publica una tasa de
victorias que omita la partida incompleta. El CSV puede quedar parcial tras
un error. No modifique los archivos de estrategias mientras el evaluador los
mantiene abiertos. Los checkpoints binarios de A y B se cargan por separado;
los TXT se consultan bajo demanda y evitan duplicar almacenes completos.
