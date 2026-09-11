# Integrar el adaptador híbrido en un motor de mus

El motor conserva las reglas y el estado de la partida. `mus-bot` recibe una
observación pública, consulta una política y devuelve una acción legal junto
con sus probabilidades. El [contrato completo](adapter.md) describe los campos,
las acciones y las aproximaciones de los envites arbitrarios.

## Preparar los archivos y el proceso

Se necesitan tres recursos de la misma configuración de un mus:

1. La estrategia genérica `ordago-public/v1-muses-1` o la histórica
   `ordago/v9-muses-1`, exportada como TXT ordenado.
2. La tabla `continuacion/values.txt` de `ordago-score/v4` o
   `mus.ordago-score-region/v1`.
3. Los TXT `score-AA-BB.txt` de **todas las celdas publicadas** de esa tabla.

Una tabla creada con `--score-region A:B` solo exige los TXT de su región.
Los marcadores iniciales excluidos usan la política genérica. La tabla y cada
TXT deben compartir cobertura y esquema; no mezcle exports de una tabla
completa con los de una región.

La genérica pública usa los mismos campos `hand_start_scores` y
`pending_wager_total` para las manos cuyo marcador inicial no esté entrenado.
También los requiere si se ejecuta sin tabla. Calcula los cobros restando el
marcador inicial al actual y conserva los envites aceptados pendientes; no
necesita estimar qué jugador ganará cada lance al mostrar las cartas.

No se necesitan los checkpoints binarios para servir decisiones. Conserva la
tabla y los TXT de una misma ejecución. No edites ni sustituyas archivos
mientras el bot los tiene abiertos. Para actualizar el conjunto, inicia un
proceso nuevo con las rutas nuevas.

```sh
make
build/release/mus-bot \
  --strategy /datos/generica/mus-ordago-100m.txt \
  --score-table /datos/entrenamiento-50m/continuacion/values.txt \
  --score-strategies /datos/entrenamiento-50m/txt \
  --seed 17
```

Mantén el proceso abierto. Escribe un JSON seguido de salto de línea en stdin y
lee una respuesta JSON de stdout. Las peticiones se procesan en orden. Usa `id`
para relacionar cada respuesta con el estado al que pertenece; no apliques una
respuesta si el motor ya ha cambiado ese estado. stderr contiene errores de
arranque; `ok:false` corresponde a una petición rechazada y no avanza la semilla.

Las rutas son argumentos de configuración, no parte de las observaciones.
El bot comprueba la tabla y las cabeceras al arrancar, pero solo lee las filas
necesarias durante las consultas. Mantiene una caché genérica y una caché del
marcador activo; la memoria no crece con el tamaño total de los TXT.

## Guardar el marcador inicial de cada mano

Al comenzar una mano, guarda una copia del marcador como `hand_start_scores`.
Ese vector permanece fijo hasta terminar la mano, aunque se anoten dejes.
`match_scores` contiene siempre el marcador efectivamente cobrado en ese momento.

Todas las observaciones utilizan **0 = mano y 1 = postre**. Si las identidades
estables del motor son A y B, construye un mapeo nuevo al cambiar quién es mano.
Aplica el mismo mapeo a:

- El jugador que decide y todos los jugadores del historial.
- `hand_start_scores`, `match_scores` y `known_hand_scores`.
- Quién cortó el mus y los recuentos de descartes.
- Los resúmenes `previous_strengths`: intercambia 1 y 2, y 5 y 6; conserva 0,
  3 y 4 al intercambiar mano y postre.

Las cartas que se envían son únicamente las del jugador que decide. Para
responder, deshaz el mapeo de posición si tu motor lo necesita. Las máscaras de
descarte usan el orden canónico ascendente del solver, no la posición visual de
las cartas en la interfaz.

No hay memoria de una partida concreta dentro del adaptador. El host debe enviar
`hand_start_scores` en **cada petición** del modo híbrido. Esto también permite
intercalar consultas de partidas distintas sin confundir sus marcadores.

## Mantener dos acumulados de envites

El acumulado de la genérica y el del modelo con marcador no tienen el mismo
significado. Mantén ambos desde cero al iniciar cada mano:

| Evento al terminar un lance | `prior_wager_total`, para la genérica | `pending_wager_total`, para la política con marcador |
|---|---|---|
| Ambos pasan | No sumar | No sumar |
| Se acepta un envite numérico de total T | Sumar T | Sumar T mientras siga pendiente |
| Se rechaza y se cobra un deje o importe cubierto D | Sumar D | No sumar: ya está cobrado |
| Se rechaza un órdago | Sumar el importe D que concede el motor | No sumar |
| Se acepta un órdago | La partida termina; no enviar otra decisión | La partida termina |

No sumes a `pending_wager_total` los puntos de grande o chica al paso, los tantos
intrínsecos de pares o juego, ni la apuesta todavía abierta del lance actual.
Los contratos aceptados pendientes se mantienen separados del marcador cobrado
hasta que el motor los resuelve al final. Los dejes se incorporan a
`match_scores` inmediatamente. Si alguien llega a 40, termina la partida y no
solicites otra decisión.

`known_hand_scores` conserva los tantos públicamente resueltos de esta mano
para la ruta genérica. No uses ganadores obtenidos mirando cartas ocultas para
rellenarlo. `hand_scores_complete` conserva su significado anterior. En una
consulta con `ordago-score`, estos dos campos no sustituyen a los marcadores
reales ni intervienen como proxy en la clave.

Mantén `history` solo para el lance actual y reinícialo al cambiar de lance.
Cada `raise.amount` es un **incremento**, no el total. Incluye las acciones del
bot, los pasos y el órdago. No incluyas la acción que cerró el lance anterior.

## Ejemplos de peticiones

Grande, jugador 0 con dos reyes, siete y cuatro, sin mus, corta el propio mano:

```json
{"id":"mano-17-grande","player":0,"phase":"grande","hand":[0,1,0,0,1,0,0,2],"mus_count":0,"mus_stop_player":0,"hand_start_scores":[37,30],"match_scores":[37,30],"prior_wager_total":0,"pending_wager_total":0,"known_hand_scores":[0,0],"previous_strengths":[],"history":[],"legal_actions":[{"type":"pass"},{"type":"raise","amount":2},{"type":"ordago"}]}
```

El diagnóstico debe indicar `strategy:"ordago-score"`,
`strategy_scores:[37,30]`, `strategy_reason:"trained_start_score"` y
`ordago_factor:1`. Con acciones legales que coincidan con las del entrenamiento
y un nodo presente, las probabilidades son las publicadas en ese TXT.

Otra mano comienza en 30–30. En grande ocurre `mano +2 → postre +2 → mano +2 →
postre no quiere`, y el motor concede cuatro tantos a mano. En chica:

```json
{"id":"mano-18-chica","player":0,"phase":"chica","hand":[0,1,0,0,1,0,0,2],"mus_count":0,"mus_stop_player":0,"hand_start_scores":[30,30],"match_scores":[34,30],"prior_wager_total":4,"pending_wager_total":0,"known_hand_scores":[4,0],"hand_scores_complete":true,"previous_strengths":[3],"history":[],"legal_actions":[{"type":"pass"},{"type":"raise","amount":2},{"type":"raise","amount":4},{"type":"ordago"}]}
```

Se consulta **el archivo de 30–30**, con puntuaciones 34–30 y acumulado pendiente
cero dentro de su clave. Si grande hubiera terminado con un envite de cuatro
aceptado, el marcador cobrado seguiría siendo 30–30, `pending_wager_total`
valdría 4, `known_hand_scores` seguiría en `[0,0]` y `hand_scores_complete`
sería false. Son decisiones distintas aunque `prior_wager_total` valga 4 en ambas.

Para mus o descartes también se envían ambos marcadores. No hace falta
`pending_wager_total` en esas fases; si se envía, debe ser cero. Las cartas y
los recuentos públicos de descarte se actualizan según el protocolo general.

## Cuándo cambia de política

El adaptador busca `hand_start_scores` en `values.txt`:

- Celda publicada: usa ese TXT durante toda la mano, en todos los lances,
  mus y descartes. No añade el sesgo de órdago ni el límite heurístico de
  cantidades de la genérica.
- Celda no publicada: conserva el adaptador genérico, incluido su ajuste de
  marcador, salvo que el proceso se haya iniciado con `--no-score-adjustment`.
- Un nodo ausente dentro de una celda publicada no activa la genérica: se
  intenta la traducción documentada dentro de esa celda y, sin datos, el respaldo
  prudente. El diagnóstico identifica `method:"fallback"`.
- Un archivo ausente o corrupto es un error, no una señal para usar la genérica.

La ejecución de 50M hasta 30–30 cubre 190 celdas, con suma de puntuaciones ≥60.
Incluye 30–30, 39–21 y 21–39; excluye 29–30 y 39–20. La selección no está
codificada con estos valores y admite una tabla ampliada en el futuro.

Si una mano empieza fuera de la tabla y entra en esa zona por un deje, sigue
usando la genérica hasta acabar la mano. En la siguiente se selecciona la celda
correspondiente, después de normalizar el nuevo mano. Cambiar de TXT a mitad de
mano usaría un entrenamiento que parte de otro estado inicial.

`legal_actions` siempre procede del motor. Los historiales reales pueden tener
incrementos intermedios o repetidos que no existen en el entrenamiento. En esos
casos los diagnósticos `interpolated` o `nearest` describen una aproximación;
no deben presentarse como consultas exactas de una política entrenada. Dentro
de una celda, la aproximación nunca cambia sus puntuaciones públicas ni los
envites pendientes para buscar una respuesta.

## Integración directa con la biblioteca C

```c
MusAdapter *adapter = NULL;
MusAdapterConfig config = mus_adapter_default_config();
Status status = mus_adapter_open_text(generic_txt, &config, 17, &adapter);
if (status == CFR_STATUS_SUCCESS)
    status = mus_adapter_enable_score_text(adapter, values_txt, score_txt_directory);
if (status != CFR_STATUS_SUCCESS) {
    mus_adapter_destroy(adapter);
    return status;
}

/* Fill the remaining public observation fields and legal actions. */
MusAdapterObservation observation = {0};
observation.has_hand_start_scores = true;
observation.hand_start_scores[0] = 37;
observation.hand_start_scores[1] = 30;
observation.match_scores[0] = 37;
observation.match_scores[1] = 30;
observation.has_pending_wager_total = true;
observation.pending_wager_total = 0;
/* mus_adapter_decide(adapter, &observation, &decision); */
/* Release the adapter after the final query. */
mus_adapter_destroy(adapter);
```

Las llamadas a un adaptador son secuenciales. Recompila el consumidor C al
actualizar la biblioteca: las estructuras públicas tienen campos nuevos.
Los clientes del protocolo anterior pueden seguir usando el modo genérico sin
las opciones `--score-*`.

## Comprobaciones de integración

Ejecuta `make test`. Para comprobar tu motor, conserva una traza de observación
y diagnóstico que cubra el inicio de mano, un deje, un envite aceptado pendiente,
el cambio de mano y un marcador fuera de la tabla. Comprueba que:

- Nunca se envían cartas rivales ni se anticipan puntos todavía pendientes.
- El TXT elegido no cambia durante la mano.
- Dentro de la tabla, `ordago_factor` siempre es 1 y `score_proxy` es false.
- Fuera de la tabla, la misma observación conserva las probabilidades genéricas.
- Al alternar marcadores en un mismo proceso, no se reutilizan políticas de
  otra celda aunque compartan una clave numérica.
