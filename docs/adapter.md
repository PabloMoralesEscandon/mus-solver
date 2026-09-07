# Adaptador para partidas de dos jugadores

`mus-bot` utiliza un checkpoint `ordago` como política de referencia. Recibe
observaciones de un motor de partida y devuelve una distribución y una acción.
El motor conserva el estado real, determina las acciones legales, resuelve los
lances y actualiza el marcador. El adaptador no necesita las cartas del rival.

## Compilar y ejecutar

Se necesitan un compilador C17, Zstandard, `pkg-config` y la biblioteca de
desarrollo de **json-c**. La biblioteca C del adaptador no depende de json-c;
solo lo utiliza el ejecutable JSON. Las pruebas de CLI necesitan Python 3 y el
comando `zstd`.

```sh
git submodule update --init --recursive
make
build/release/mus-bot --strategy mus-ordago-100m.txt --seed 17
```

**El modo recomendado es `--strategy` con el exportador de texto ordenado.**
El archivo se abre en lectura y cada consulta busca sus claves mediante saltos
por posición en disco. El orden de las claves actúa como índice: no hay que
recorrer previamente los 33 GB ni construir una tabla en RAM. Se leen las filas
necesarias y se conservan como máximo 256 resultados en caché, unos 40 KiB. No
se modifica el archivo ni se realiza entrenamiento.

El archivo debe proceder de `mus_ordago_strategy_write_text` o del CLI del
solver con `--format ordago --export-strategy`, tener claves en orden creciente
y permanecer inmutable mientras esté abierto. Se comprueban la cabecera, las
filas consultadas, sus acciones y probabilidades. El texto no tiene checksum;
no se hace una validación completa del archivo al abrirlo. Una fila no puede
superar 4096 bytes, cota suficiente para el exportador actual.

Los modos anteriores `--load-compressed` y `--load` se conservan para checkpoints
binarios y comparaciones. **Esos modos sí cargan el almacén completo de C-CFR en
RAM**, incluidos los regrets; no son el modo de consulta ligera.

Enviar un objeto JSON por línea a la entrada estándar. Cada línea produce un
objeto JSON en la salida estándar, que se vacía después de cada respuesta.
El límite es 1 MiB por línea. Los errores de una petición no terminan el
proceso. Los errores de carga se escriben en stderr y terminan con código 1;
los argumentos incorrectos terminan con código 2. `--help` describe el comando.

El adaptador corresponde al esquema `v9-muses-1` y debe compilarse con el límite
predeterminado de un mus. Las partidas pueden alternar la mano: normalizar todas
las posiciones, marcadores e historiales a **0 = mano, 1 = postre** en cada mano.
No se mantienen identidades de jugadores entre peticiones.

## Petición

Ejemplo de la secuencia `paso → +2 → +2 → +2`, con mano como bot:

```json
{"id":"grande-3","player":0,"phase":"grande","hand":[1,0,0,0,0,0,0,3],"mus_stop_player":0,"history":[{"player":0,"type":"pass"},{"player":1,"type":"raise","amount":2},{"player":0,"type":"raise","amount":2},{"player":1,"type":"raise","amount":2}],"match_scores":[12,18],"legal_actions":[{"type":"reject"},{"type":"accept"},{"type":"raise","amount":2},{"type":"raise","amount":4},{"type":"raise","amount":10},{"type":"ordago"}]}
```

`legal_actions` debe describir las posibilidades reales. El ejemplo enumera un
subconjunto de cantidades; un motor con envites enteros libres debe enumerar
los incrementos que permita, no solo las cantidades del entrenamiento. El bot
proyecta sus cantidades base sobre esa lista, incluidos importes intermedios o
repetidos. El historial admite importes que no pertenezcan a la lista actual.

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

## Respuesta y diagnóstico

`ok: true` contiene `action` y `probabilities`, una lista de acciones legales
con `probability`. Se combinan las cantidades que terminen en la misma acción.
El muestreo utiliza una semilla local: mismas peticiones en el mismo orden y
misma semilla producen las mismas acciones. Un error no avanza ese generador.

`diagnostic` contiene:

- `method`: `exact`, `interpolated`, `nearest`, `fallback` o `mus_limit`.
- `sources`: hasta dos claves del checkpoint, sus pesos, distancias y cantidades
  abstractas `total`, `covered`, `prior_total` y `settled_score`. Las claves se
  envían como texto decimal para conservar precisión en clientes JavaScript.
- `real_total` y `real_covered`: total numérico real y deje al retirarse. Ante un
  órdago, `real_total` conserva el total numérico previo; no convierte el órdago
  en una apuesta de 40.
- `score_proxy`: indica que los tanteos de lances anteriores no son totalmente
  públicos. `exact` significa que se encontró la clave consultada; con esta
  marca, su componente de tanteo sigue siendo una aproximación.
- `lookup_count`: número de consultas al almacén, como máximo 64.
- `ordago_factor`: multiplicador aplicado por el marcador.
- `adjustments`: acción abstracta (identificador C), acción real resultante y
  masa aportada por cada fuente, antes de la corrección de marcador y de la
  normalización final.
- `reason`: `no_trained_candidate`, `one_mus_completed` o null.

`ok: false` contiene `error`, por ejemplo `invalid_json`, `invalid_observation`,
`line_too_long` o `invalid_strategy_data`. Un dato corrupto no se presenta como
una respuesta prudente. En el modo binario, los nodos sin masa acumulada de estrategia se tratan como
ausencia de datos. El exportador de texto escribe una distribución uniforme
para esos nodos y no conserva su masa acumulada. El lector de texto utiliza las
probabilidades publicadas: no puede distinguir ese caso de una distribución
uniforme entrenada. Por ello pueden variar el respaldo y las decisiones de
esos estados entre ambos modos.

## Heurísticas

1. Una clave representable y con datos conserva la estrategia media antes de
   los ajustes de acciones y marcador.
2. Si solo la última cantidad queda entre dos incrementos legales del modelo,
   interpolar las probabilidades linealmente. Fuera del intervalo se utiliza
   el extremo. Si solo uno de los dos nodos tiene datos, se utiliza ese nodo.
3. Una repetición prohibida, una disminución del incremento o una secuencia
   demasiado larga activa la búsqueda de estados próximos. Se conserva lance,
   posición, mano abstracta, mus, fuerza anterior y clase de respuesta. La
   distancia es `|Δtotal| + |Δdeje| + |Δacumulado| + |Δtanteo|`. Se desempata por
   diferencia en número de apuestas y bits de incrementos usados, después por
   menor número de apuestas y clave. La búsqueda no impone una distancia máxima.
   Los pesos son inversos a la distancia; una distancia cero absorbe el peso
   frente a otra positiva, y dos distancias cero reciben la mitad cada una.
4. Se recalcula cada consulta desde la observación real. La traducción no cierra
   lances ni altera cantidades, aunque cambie el historial abstracto utilizado.
5. Los incrementos del bot se proyectan sobre cantidades legales que no excedan
   `max(40−marcador_mano, 40−marcador_postre)−total_actual`. Se escoge la cantidad
   más próxima; los empates favorecen la menor. Sin subida útil, su masa pasa a
   querer o pasar. Otras acciones no disponibles trasladan su masa al respaldo
   prudente. El órdago se mantiene separado.
6. Multiplicar la probabilidad de lanzar órdago o de querer uno recibido por
   `clamp((distancia_bot+8)/(distancia_rival+8), 0.5, 2)` y normalizar. No se crea
   masa donde antes era cero. Desactivar con `--no-score-adjustment`.
7. Después de un mus completado, cortar siempre. No se soporta comenzar una
   consulta después de dos o más muses.
8. Sin datos entre las candidatas consultadas, pasar, no querer, cortar mus o
   escoger la máscara canónica legal de menor valor.

No se infiere la fuerza del rival ni se optimiza la probabilidad de ganar una
partida. El filtro de cantidades ignora tantos pendientes; el proxy de tanteo
ignora ganadores desconocidos. La consulta de estados cercanos puede perder
información sobre la insistencia del rival. Estas aproximaciones son parte del
contrato, no una garantía de equilibrio ni de mejora sobre otro traductor.

## Biblioteca C

La API está en `include/mus_solver/mus_adapter.h`:

- `mus_adapter_open_text`: abrir un exportador de texto ordenado para consultas
  en disco. Es la API recomendada para jugar con memoria acotada.
- `mus_adapter_load`: cargar un checkpoint binario completo en RAM; se conserva
  para compatibilidad y comparación.
- `mus_adapter_create`: utilizar un `InfoStore` prestado, útil para integración
  y pruebas. El llamador conserva el almacén vivo y no lo modifica durante las
  consultas. Debe pertenecer al mismo esquema `ordago`.
- `mus_adapter_decide`: recibir un `MusAdapterObservation` y producir un
  `MusAdapterDecision`. No modifica la observación ni el checkpoint.
- `mus_adapter_destroy`: liberar el adaptador y su almacén, si lo posee.

Las llamadas de un adaptador son secuenciales. Sus observaciones prestan los
arrays `history` y `legal_actions` durante la llamada; no se retienen punteros.
Los errores conservan el resultado del llamador y el estado aleatorio.
`mus_adapter_default_config` proporciona los valores del CLI; el llamador C
puede ajustar el desplazamiento y las cotas del factor de marcador.

`mus_ordago_game_information_set_set_hand` construye la abstracción desde las
cartas propias. `mus_ordago_game_information_set_encode` complementa al
decodificador existente sin cambiar las claves ni el esquema del entrenamiento.

## Validación y medición

```sh
make test
make test-sanitize
python3 tests/benchmark_adapter.py build/release/mus-bot mus-ordago-100m.txt --text --deals 300
```

La batería incluye un checkpoint pequeño con distribuciones conocidas,
interpolación, historial repetido, límites numéricos, marcador, respaldo,
compatibilidad binaria y errores del protocolo. Comprueba también la ida y
vuelta del codificador con las 330 manos y las situaciones de las pruebas del
solver existente.

El benchmark abre una vez, mide consultas sin contar aciertos de su caché y
prueba ambos asientos, lances, mus y descartes. Compara la mezcla con traducir
hacia arriba y reutilizar la última respuesta válida. La comparación comienza
en un historial fijo de grande con tres +2 y utiliza rivales que quieren o
repiten +2/+3 antes de querer. Los tres métodos comparten repartos y semillas.
Se puntúa solo ese lance, con límite de 40 y órdagos siempre queridos por los
rivales programados. El resultado no es una tasa de victorias de partidas
completas ni una medida de explotabilidad.

### Medición anterior con el checkpoint binario de 100 millones

En este entorno, el 7 de septiembre de 2026, con `--deals 300`:

- Carga y primera consulta: 43,94 s; pico de memoria del proceso: 14,13 GiB.
- 697 consultas distintas: 419 exactas, 268 por proximidad, 9 interpoladas y
  1 respaldo. Esta cobertura corresponde a esas peticiones, no al juego completo.
- Latencia posterior a la carga, incluyendo JSON y comunicación local:
  mediana 0,061 ms y máximo 1,974 ms. Se excluyen aciertos de la caché del benchmark.

Tantos medios del bot por lance en los repartos programados:

| Método | Rival quiere | Rival repite +2 | Rival repite +3 |
|---|---:|---:|---:|
| Mezcla de estados próximos | -1,40 | -1,55 | -1,47 |
| Redondeo hacia arriba | -1,05 | -1,41 | -1,41 |
| Última respuesta válida | 0,00 | +2,02 | +2,36 |

La mezcla rindió peor en este diagnóstico. Los historiales iniciales son
forzados, los rivales siguen guiones y siempre quieren los órdagos: esta prueba
no establece qué traductor ganará más partidas contra humanos. La implementación
mantiene la mezcla acordada, y expone las fuentes y ajustes para facilitar
comparaciones posteriores. No se afirma una mejora de calidad estratégica.

### Consulta directa del texto

La batería compara las probabilidades, las acciones muestreadas y los
resultados del adaptador entre un checkpoint pequeño y su exportación de texto.
Incluye las 330 manos, ambos asientos, descartes, envites repetidos, claves
inexistentes y filas de longitudes distintas para comprobar los saltos en disco.
Las políticas con masa positiva coinciden exactamente en esas pruebas.

Con `mus-ordago-100m.txt` (33 GB) y `--text --deals 300`, en este entorno:

- 702 consultas distintas; pico y memoria residente del bot: **2296 KiB
  (2,24 MiB)**, medidos en `/proc/PID/status` después de las consultas.
- Apertura y primera respuesta: **1,35 ms**.
- Consultas posteriores: mediana **0,053 ms**, máximo **3,25 ms**.
- Se conserva una caché fija de 256 resultados. No se asigna memoria en
  proporción al tamaño del texto y no se crea un archivo de índice adicional.

Son tiempos de una ejecución local con caché del sistema operativo disponible;
el almacenamiento y el estado de esa caché afectan a la latencia. El campo
`child_peak_rss_kib` del benchmark puede incluir la memoria del lanzamiento desde
Python. Para medir este bot ligero, usar `bot_peak_rss_kib`, leído de su propio
proceso. Las diferencias de cobertura respecto al binario pueden incluir nodos
sin masa acumulada que el exportador representa como uniformes, tal como se
explica en el contrato de respuesta.
