# Bot e integración

[Volver al README](../README.md) · [Entrenamiento y exportación](training.md) · [Evaluar partidas](match-evaluation.md)

`mus-bot` consulta estrategias de dos jugadores y devuelve acciones legales.
La genérica normal es `ordago`; también admite `ordago-deprecated`
para seguir utilizando estrategias históricas. El modo híbrido añade políticas
`ordago-score` para los marcadores entrenados. Requiere una compilación de
un mus; no admite estrategias de cuatro jugadores.

## Arranque y archivos

```sh
make bot
build/release/mus-bot --strategy generica.txt --seed 7
```

Para añadir una tabla con marcador:

```sh
build/release/mus-bot --strategy generica.txt \
    --score-table tabla/values.txt --score-strategies estrategias --seed 7
```

Las dos opciones `--score-*` se usan juntas. El directorio de estrategias debe
contener los TXT `score-AA-BB.txt` de **todas las celdas publicadas**, exportados
desde esa misma tabla. El bot valida las cabeceras, incluidas las iteraciones
y la identidad de la continuación. No mezcles exportaciones de tablas completas
y regionales. Un archivo ausente o incompatible produce un error de arranque.

`--strategy` consulta el TXT ordenado en disco con una caché acotada. El modo
híbrido mantiene la genérica y una sola celda abiertas; cambiar de celda
reinicia su caché. No necesita cargar todos los nodos en RAM. Las alternativas
`--load` y `--load-compressed` sí cargan el checkpoint completo en memoria.

Los TXT deben proceder del exportador del solver, con claves crecientes, y
permanecer inmutables mientras estén abiertos. La apertura comprueba las
cabeceras; las filas se validan al consultarlas. No es una validación completa
anticipada del archivo y el TXT no tiene checksum. Para actualizar estrategias,
arranca otro proceso con el nuevo conjunto de archivos.

Mantén el proceso abierto: escribe un objeto JSON por línea en stdin y lee
una respuesta JSON por línea en stdout. El máximo es 1 MiB por petición.
Los errores de petición no terminan el proceso ni avanzan el generador aleatorio.
Los errores de arranque se escriben en stderr: código 1 para carga y 2 para
argumentos incorrectos.

## Petición

Ejemplo para la genérica normal, antes de apostar en grande:

```json
{"id":"mano-1-grande","player":0,"phase":"grande","hand":[0,1,0,0,1,0,0,2],"mus_stop_player":0,"hand_start_scores":[12,18],"match_scores":[12,18],"pending_wager_total":0,"previous_strengths":[],"history":[],"legal_actions":[{"type":"pass"},{"type":"raise","amount":2},{"type":"raise","amount":4},{"type":"ordago"}]}
```

`legal_actions` procede del motor real y debe enumerar las acciones que este
permite; puede incluir incrementos intermedios o repetidos que no existen en
el entrenamiento. `id` permite asociar la respuesta a la observación enviada.
No apliques una respuesta si el motor ya ha cambiado ese estado.

| Campo | Significado y valor predeterminado |
|---|---|
| `id` | Identificador opcional, texto, entero o null; se devuelve en la respuesta. |
| `player` | Obligatorio: 0 para mano, 1 para postre. Debe ser quien decide. |
| `phase` | Obligatorio: `mus`, `discard`, `grande`, `chica`, `pares`, `juego` o `punto`. |
| `hand` | Obligatorio: ocho recuentos que suman cuatro cartas. Orden: pitos, cuatros, cincos, seises, sietes, sotas, caballos, reyes. Tres cuenta como rey; dos, como pito. |
| `mus_count` | Muses completados: 0 o 1; predeterminado 0. |
| `discard_counts` | Recuentos públicos `[mano, postre]` del primer mus; predeterminado `[0,0]`. Tras completarlo, ambos deben estar entre 1 y 4. Al decidir el descarte de postre, indicar ya el recuento de mano. |
| `mus_stop_player` | Quién cortó antes del primer mus: 0 o 1; predeterminado 0. Tras el primer mus se usa el fin automático del modelo entrenado. |
| `previous_strengths` | Resúmenes de lances anteriores; predeterminado `[]`. Debe haber 0/1/2/3 elementos en grande/chica/pares/juego o punto. |
| `prior_wager_total` | Suma de envites numéricos de lances anteriores; predeterminado 0. Sumar el total aceptado o el deje al rechazar; un lance pasado no suma envite. |
| `known_hand_scores` | Tantos públicamente resueltos **de esta mano**, limitados a 40 por jugador; predeterminado `[0,0]`. No incluir resultados calculados mirando cartas ocultas. |
| `hand_scores_complete` | Si todos los tantos de los lances anteriores ya son públicos; predeterminado false. No autoriza usar información oculta. |
| `match_scores` | Marcador público actual `[mano, postre]`, incluidos los tantos ya anotados; predeterminado `[0,0]`. Cada valor debe estar entre 0 y 39. |
| `hand_start_scores` | Marcador `[mano, postre]` al inicio de esta mano, entre 0 y 39. Obligatorio con tablas de marcador o una genérica `ordago`. No se cambia al cobrar un deje. Ningún valor puede superar el marcador actual; en mus, descartes y grande debe coincidir con él. |
| `pending_wager_total` | Suma de envites numéricos **aceptados y todavía pendientes** de los lances anteriores. Obligatorio al decidir apuestas con una estrategia de marcador o una genérica `ordago`. Excluye dejes cobrados, tantos de lances al paso, tantos intrínsecos y la oferta del lance actual. En mus, descartes y grande es cero. |
| `history` | Acciones públicas **del lance actual**, desde su inicio; predeterminado `[]`. Cada evento añade `player` a una acción. Conservar también las acciones del bot. |
| `legal_actions` | Obligatorio: lista no vacía de acciones autorizadas por el motor real. |

El resumen de fuerza es 0 = ninguno, 1 = mano, 2 = postre, 3 = ambos.
Envidar, subir y querer muestran fuerza. Para pares no negociados, el tercer
resumen puede ser 4 = nadie tiene pares, 5 = solo mano, 6 = solo postre.

Las acciones son objetos con `type`:

- `pass`, `accept`, `reject`, `ordago`, `mus`, `no_mus`: sin cantidad.
- `raise`: requiere `amount`, un incremento entero positivo, nunca el total.
- `discard`: requiere `mask`, un entero de 1 a 15. Los bits siguen las cuatro
  cartas en orden canónico ascendente. Para valores repetidos, utilizar la
  máscara canónica del solver (seleccionar primero las copias de menor índice).

Un historial abierto de apuestas contiene pasos iniciales, envites y
posiblemente un órdago final. Querer, no querer o dos pasos cierran el lance:
la siguiente petición debe usar la nueva fase y reiniciar `history`.
No hay un límite de cinco apuestas ni una prohibición de repetir incrementos en
el historial real. Los importes y sus sumas tienen un límite técnico de
`INT64_MAX / 8` para evitar desbordamientos.

La lista legal debe incluir la acción prudente correspondiente: pasar al
abrir, no querer ante una apuesta, no mus al decidir mus, o al menos un descarte
canónico. El adaptador rechaza listas incompatibles con la fase. No verifica
reglas de un motor concreto, como su mínimo de subida. No admite campos
adicionales, cartas del rival ni peticiones de fases terminales.

## Contabilidad del motor

La variante normal y las políticas con marcador requieren `hand_start_scores`
en cada petición y `pending_wager_total` al apostar. Los cobros de la mano se
obtienen restando `hand_start_scores` a `match_scores`; no se estiman ganadores
ocultos. `prior_wager_total`, `known_hand_scores` y `hand_scores_complete`
son campos de compatibilidad de `ordago-deprecated`; se pueden omitir con
la genérica normal y no sustituyen los campos públicos anteriores.

Al iniciar una mano:

1. Orienta jugadores, marcadores e historial como **0 = mano, 1 = postre**.
2. Copia el marcador a `hand_start_scores` y mantenlo fijo hasta la siguiente mano.
3. Inicializa `pending_wager_total` a cero.
4. Envía solo las cartas del jugador que decide y la información pública.

| Evento al cerrar un lance | Marcador actual | Pendientes | Acumulado histórico `prior_wager_total` |
|---|---|---|---|
| Ambos pasan | Sin cambio. | Sin cambio. | Sin cambio. |
| Se quiere un total numérico T | Sin cambio. | Sumar T. | Sumar T. |
| Se rechaza y el motor cobra D | Sumar D al beneficiario. | Sin cambio. | Sumar D. |
| Se quiere un órdago | Termina la partida. | No enviar más decisiones. | No enviar más decisiones. |

No sumes a los pendientes la oferta activa, tantos al paso ni intrínsecos.
Los dejes se cobran durante la negociación; el resto se cuenta al final.
Reinicia `history` al cambiar de lance y conserva dentro de él también las
acciones del bot. `raise.amount` siempre es un incremento.

Por ejemplo, desde 30:30, un deje de cuatro en grande deja
`hand_start_scores=[30,30]`, `match_scores=[34,30]` y
`pending_wager_total=0`. Si se aceptaron cuatro, el marcador sigue en 30:30 y
`pending_wager_total=4`. Son observaciones distintas.

Al cambiar de mano, reorienta también quién cortó mus, los descartes y la
fuerza previa: intercambia los códigos 1/2 y 5/6, y conserva 0/3/4. Convierte
las máscaras canónicas de descarte a las posiciones físicas de tu motor.
El adaptador no conserva la identidad de una partida entre peticiones.

## Selección de política y aproximaciones

La celda se elige por `hand_start_scores` y se mantiene durante toda la mano:

- Si está publicada, se usa su política en mus, descartes y todos los lances.
- Si no está publicada o queda fuera de la región, se usa la genérica.
- Un nodo ausente de una celda publicada no cambia la selección a la genérica.
- Un archivo corrupto produce un error; no se trata como ausencia de aprendizaje.

Para consultar envites ajenos al modelo, el adaptador interpola la última
cantidad o busca estados cercanos, con hasta 64 consultas. En las políticas
públicas conserva los cobros, los pendientes y la celda inicial: solo aproxima
el historial activo. Proyecta las probabilidades sobre `legal_actions`.
Sin datos, pasa, no quiere, corta mus o elige el menor descarte canónico legal.

Las genéricas aplican además un límite heurístico de cantidades y multiplican
la masa de órdago por `clamp((distancia_bot+8)/(distancia_rival+8), 0.5, 2)`.
`--no-score-adjustment` desactiva este multiplicador. Las políticas con marcador
no usan esos dos ajustes; su factor de órdago es siempre 1. Estas traducciones
y respaldos no son estrategias aprendidas ni garantías de equilibrio.

## Respuesta y diagnóstico

`ok:true` incluye `action`, una lista `probabilities` de acciones legales y
`diagnostic`. `ok:false` incluye un error como `invalid_json`,
`invalid_observation`, `line_too_long` o `invalid_strategy_data`.

| Campo de `diagnostic` | Significado |
|---|---|
| `method` | `exact`, `interpolated`, `nearest`, `fallback` o `mus_limit`. |
| `strategy` | Identificador del protocolo: `ordago`, `ordago-score` o `ordago-deprecated` para la histórica obsoleta. |
| `strategy_scores` | Marcador inicial de la celda, o null para una genérica. |
| `strategy_reason` | `score_table_disabled`, `untrained_start_score` o `trained_start_score`. |
| `sources` | Hasta dos claves de origen, como texto decimal, con pesos y distancias. |
| `real_total`, `real_covered` | Total numérico y tantos cubiertos del envite real. El órdago no se convierte en una apuesta numérica de 40. |
| `score_proxy` | Tanteo aproximado de la política histórica. Siempre false con las políticas públicas. |
| `lookup_count` | Número de consultas al almacén, hasta 64. |
| `ordago_factor` | Ajuste de órdago; 1 en celdas con marcador. |
| `adjustments` | Proyección de acciones abstractas sobre las legales y masa aportada. |
| `reason` | `no_trained_candidate`, `one_mus_completed` o null. |

`exact` indica que se encontró la clave consultada; en la política histórica
puede seguir habiendo un tanteo aproximado indicado por `score_proxy`.
Los identificadores del diagnóstico siguen los nombres actuales del CLI:
`strategy=ordago` identifica la genérica pública y `strategy=ordago-deprecated`
la histórica. Actualiza los consumidores del JSON que usaban los nombres
anteriores; los archivos de estrategia se leen sin cambios.

Mismas peticiones, orden, archivos y semilla producen las mismas acciones.
`--seed` admite enteros decimales sin signo ni espacios, incluido cero.
En TXT, los nodos con masa acumulada cero se exportan uniformes; el binario
los trata como ausencia de datos. Pueden cambiar los respaldos en esos nodos
según el tipo de archivo, aunque el resto de las probabilidades coincida.

## Biblioteca C

La referencia está en [mus_adapter.h](../include/mus_solver/mus_adapter.h).

| Función | Uso |
|---|---|
| `mus_adapter_open_text` | Abre un TXT para consultas en disco. |
| `mus_adapter_enable_score_text` | Añade una tabla y su directorio de TXT. |
| `mus_adapter_load` | Carga un checkpoint completo en RAM. |
| `mus_adapter_create_public` | Usa un `InfoStore` prestado de la variante normal. |
| `mus_adapter_create` | Usa un `InfoStore` prestado de `ordago-deprecated`. |
| `mus_adapter_decide` | Recibe la observación y produce la decisión. |
| `mus_adapter_destroy` | Libera el adaptador. |

Conserva los almacenes prestados vivos e inmutables. Las llamadas a una misma
instancia son secuenciales; `history` y `legal_actions` se prestan durante la
llamada. Los errores conservan el resultado del llamador y el estado aleatorio.
Usa `mus_adapter_default_config` para partir de los mismos ajustes que el CLI.

Para validar una integración, comprueba inicio de mano, deje, envite aceptado,
marcador fuera de cobertura y cambio de mano. La celda no debe cambiar dentro
de la mano y las observaciones nunca deben anticipar tantos de cartas ocultas.
