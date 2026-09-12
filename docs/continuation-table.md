# Tabla de continuación por marcador

`ordago-score` entrena estrategias para partidas de mus de dos jugadores.
Esta guía explica cómo utiliza el marcador para valorar cada mano. Los
[comandos de entrenamiento y reanudación](../README.md#variante-con-marcador-ordago-score)
están en el README; la [guía de integración](adapter-integration.md) describe
cómo consultar esas estrategias desde un motor de partida.

## Objetivo

Un solver de una mano aislada suele utilizar como utilidad la diferencia de
tantos obtenidos en esa mano. Esa utilidad no representa bien el objetivo real
de una partida a 40:

- ganar tres tantos con `35–18` no vale lo mismo que ganarlos con `0–0`;
- conceder tres tantos al rival con `18–35` puede ser decisivo;
- un órdago aceptado decide la partida, mientras que un envite ordinario solo
  conduce a otro marcador y otra mano.

El solver separa el problema en dos niveles:

1. **Nivel interior:** CFR entrena una mano completa para un marcador inicial.
2. **Nivel exterior:** una tabla guarda el valor de continuar la partida desde
   cada marcador posible.

Cada hoja no terminal de la mano recibe el valor ya calculado del marcador de
la mano siguiente. CFR sigue recorriendo solamente una mano, pero optimiza
indirectamente la probabilidad de ganar la partida completa.

```text
marcador inicial
       │
       ▼
  CFR de una mano ── acciones, mus, descartes y lances
       │
       ▼
resultado de la mano
       │
       ├── alguien alcanza 40 ──► utilidad terminal (+1 o -1)
       │
       └── nadie alcanza 40 ────► valor del marcador siguiente
                                      │
                                      ▼
                              actualización de CFR
```

## Qué guarda la tabla

Sea `T = 40` el objetivo. Los marcadores no terminales son:

```text
(a, b), con 0 ≤ a < T y 0 ≤ b < T
```

La tabla contiene un escalar por marcador:

```text
V[a][b] = estimación del valor de la partida para el jugador 0
          al comenzar una mano con marcador a–b
```

Existen `40 × 40 = 1.600` marcadores no terminales. Una tabla puede cubrirlos
todos o limitarse a una región suficiente para los marcadores solicitados.

La tabla de valores y las estrategias se guardan por separado:

- `V[a][b]`: valor escalar empleado para entrenar marcadores anteriores;
- `S[a][b]`: estrategia CFR que se utilizará al jugar desde `a–b`.

Guardar solamente `V` permite calcular valores anteriores, pero no jugar las
manos posteriores de una partida real.

## Escala de utilidad y suma cero

El modelo utiliza una utilidad firmada y normalizada:

```text
jugador 0 gana la partida:   +1
jugador 0 pierde la partida: -1
```

El valor que se estima es:

```text
V[a][b] = P(jugador 0 gana | a, b)
        - P(jugador 1 gana | a, b)
```

`V` queda acotado en `[-1, 1]`. La partida termina con un ganador, por lo que:

```text
P(jugador 0 gana | a, b) = (V[a][b] + 1) / 2
```

La conversión de un valor guardado produce una estimación condicionada por la
política evaluada y las continuaciones usadas. No mide por sí sola las victorias
del bot desplegado, que también aplica traducciones y respaldos.

Todas las hojas son de suma cero:

```text
U0 = z
U1 = -z
```

## Cómo se valora el final de una mano

Durante la negociación solo se cobran los dejes y los importes cubiertos de
apuestas rechazadas. Los envites queridos, los lances en paso y los valores
intrínsecos permanecen pendientes hasta el final de la mano. Entonces se cuentan
por orden —grande, chica, pares, juego o punto— y se detiene el cobro al alcanzar
el objetivo. Un órdago querido resuelve la partida inmediatamente, sin cobrar
antes los lances pendientes. Véanse los títulos IV.5 y V.1 del
[reglamento FEMUS](https://www.mustembleque.es/downloads/REGLAMENTO_FEMUS.29.pdf).

Después se distingue entre dos casos.

### La partida ha terminado

Si un jugador alcanza 40, no se consulta la tabla:

```text
ganador == jugador 0  → U0 = +1, U1 = -1
ganador == jugador 1  → U0 = -1, U1 = +1
```

Los tantos que excedan 40 no añaden utilidad. El objetivo es ganar la partida,
no maximizar la diferencia final.

### La partida continúa

Si el nuevo marcador es `(a', b')`, la hoja recibe el valor de continuación:

```text
U0 = valor_de_continuación(a', b')
U1 = -U0
```

No se suma además la diferencia de tantos de la mano. Hacerlo contaría dos
veces el mismo progreso: los tantos solo sirven para elegir el marcador
siguiente.

## Rotación de la mano y orientación

El índice 0 representa siempre al jugador que ocupa la posición de mano al
comenzar el reparto; el índice 1 representa al postre.

La mano pasa al contrario después de cada reparto y el marcador se intercambia.
Desde la perspectiva del jugador 0 de la mano que acaba de terminar:

```text
U0 = -V[b'][a']
U1 =  V[b'][a']
```

El intercambio orienta la posición desde la perspectiva de la nueva mano. El
cambio de signo devuelve el resultado a la perspectiva anterior.

## Ejemplo práctico: marcador 35–18

Supongamos que A es el jugador 0 actual y que la mano pasa al adversario después
del tanteo. Estos valores son solo ilustrativos:

```text
V[18][38] = -0,84
V[21][35] = -0,52
V[22][35] = -0,47
```

Si A gana tres tantos, el marcador pasa a `38–18`:

```text
U_A = -V[18][38] = +0,84
U_B = -0,84
```

La utilidad no es `+3`: es el valor de afrontar el resto de la partida desde
`38–18`.

Si B gana tres tantos, el marcador pasa a `35–21`:

```text
U_A = -V[21][35] = +0,52
U_B = -0,52
```

A sigue siendo favorito, pero perder esos tantos reduce su ventaja. Si A gana
cinco o más y alcanza 40, la utilidad es directamente `+1/-1`.

Un órdago aceptado también termina la partida con `+1/-1`, independientemente
del marcador anterior. Si se rechaza, se conceden solamente los tantos
cubiertos. Cuando nadie alcanza 40, se consulta la continuación del nuevo
marcador. Rechazar un órdago no equivale automáticamente a perder la partida.

## Por qué se calcula hacia atrás

Una mano terminada concede al menos un tanto. Toda transición no terminal
cumple:

```text
a' + b' > a + b
```

El grafo de marcadores es acíclico al ordenarlo por la suma de tantos. Se
resuelven primero los marcadores cercanos a 40:

```text
suma 78: 39–39
suma 77: 38–39, 39–38
...
suma 1:  0–1, 1–0
suma 0:  0–0
```

Al entrenar `20–12`, cualquier marcador alcanzable tras la mano tiene suma
mayor que 32 y ya dispone de valor.

## Algoritmo de entrenamiento

```text
crear V sin valores no terminales
crear almacenamiento de estrategias S

para suma desde 78 hasta 0:
    lote = todos los (a, b) válidos con a + b = suma

    para cada (a, b) del lote:
        crear el juego de una mano con marcador inicial (a, b)
        fijar V como tabla de continuación de solo lectura
        entrenar CFR
        extraer la estrategia media
        evaluar el valor de la estrategia media
        guardar provisionalmente valor y estrategia

    publicar simultáneamente los resultados del lote
```

Las celdas de una misma diagonal pueden entrenarse en paralelo porque solo
dependen de diagonales posteriores. Con las mismas semillas y un hilo por
estrategia, cambiar el número de celdas simultáneas conserva los resultados.
El entrenamiento con varios hilos sobre una misma estrategia no es reproducible
bit a bit, porque cambia el orden de las actualizaciones.

Durante el entrenamiento de una celda, `V` debe permanecer inmutable. Cambiar
las continuaciones mientras CFR actualiza arrepentimientos convertiría el
juego en un objetivo móvil.

## Evaluación del valor guardado

El solver calcula `V[a][b]` a partir de la estrategia media:

1. entrenar CFR;
2. extraer la estrategia media;
3. evaluarla con la misma tabla de continuación;
4. guardar el valor de esa evaluación.

La utilidad de la última iteración es demasiado ruidosa. Promediar utilidades
de entrenamiento sirve como estimador rápido, pero no siempre coincide con el
valor de la estrategia media.

La evaluación usa manos muestreadas y una política uniforme en los nodos sin
datos. La media y la varianza describen esa evaluación con continuaciones fijas.
El error de muestreo local no incluye los errores propagados desde otras celdas
ni el error de la política respecto a una estrategia óptima. La abstracción
pierde recuerdo perfecto, por lo que el entrenamiento no garantiza un equilibrio.

Como todas las hojas están en `[-1, 1]`, los valores guardados también deben
pertenecer a ese intervalo.

## Integración con CFR

### Contexto de cada celda

Cada celda tiene un contexto inmutable con:

- marcador al comenzar la mano;
- objetivo de la partida;
- orientación de jugadores como mano y postre;
- referencia de solo lectura a `V`;
- reglas y abstracción utilizadas.

El estado mutable separa los tantos cobrados de los contratos pendientes por
lance. Durante las decisiones, el marcador absoluto incorpora solo cobros ya
públicos. Los ganadores y valores ocultos de los contratos pendientes no se
resuelven ni se incluyen en las claves hasta que corresponde el tanteo final.
La abstracción de `ordago-score/v4` conserva en las claves el resumen de fuerza
por lance de `ordago` y la suma de envites queridos pendientes, sin separar sus
importes. Estados con repartos distintos de ese total pueden compartir política.
El modelo conserva los contratos exactos para resolver el tanteo en orden.

### Función de utilidad terminal

La utilidad terminal se calcula a partir del resultado de la mano:

1. resolver el tanteo completo;
2. comprobar si alguien ganó la partida;
3. devolver `+1/-1` si terminó;
4. calcular el marcador siguiente si continúa;
5. intercambiar mano y postre;
6. consultar `V`;
7. devolver valores exactamente opuestos.

Este es el único punto en el que la programación dinámica entra en CFR. El
muestreo, el cálculo de arrepentimientos y la estrategia media no necesitan
conocer el significado de la tabla.

### Conjuntos de información

Cada marcador tiene su propio almacén CFR. El marcador inicial es constante y
forma parte del esquema del archivo de estrategia, sin repetirse en cada clave.
Las claves incluyen los marcadores cobrados y los envites aceptados pendientes
durante la mano, además de las demás observaciones que utiliza la abstracción.

Todos los estados que compartan una clave CFR deben ofrecer exactamente las
mismas acciones y en el mismo orden.

### Acciones dependientes del marcador

El máximo de un envite depende de los tantos restantes. Por ello, dos historiales
de envite idénticos pueden tener menús distintos cuando parten de marcadores
diferentes.

En la abstracción actual, cada oferta numérica cumple:

```text
max(a + cobrado_0, b + cobrado_1) + queridos_pendientes + total_propuesto < 40
```

Los dejes e importes cubiertos ya cobrados no se vuelven a sumar como pendientes.
Los lances en paso y los valores de cartas esperan al tanteo final; no aportan
puntos calculados por anticipado a esta cota. El total propuesto incluye las
ofertas numéricas previas del lance actual. Los órdagos quedan fuera de la cota;
se mantienen los incrementos y el máximo de cinco ofertas numéricas por lance.
El límite de 40 es fijo incluso cuando se usa un objetivo de prueba reducido.

Toda información pública que determine el menú legal debe formar parte del
estado y, cuando corresponda, del conjunto de información.

### Persistencia y uso durante una partida

Por cada celda se guardan:

- valor de continuación;
- checkpoint con la estrategia y los acumuladores para reanudar;
- iteraciones, muestras y estadísticas de evaluación disponibles;
- versión de reglas y abstracción;
- convención de orientación y escala de utilidad.

Los valores están en `values.txt` y cada checkpoint en `score-AA-BB.cfr.zst`.
Para servir decisiones con memoria acotada, se exportan las estrategias a TXT.
El adaptador elige `S[a][b]` por el marcador al inicio de la mano y conserva esa
celda hasta terminarla. La mano siguiente usa el nuevo marcador, orientado de
nuevo como mano y postre. Los marcadores iniciales fuera de la tabla usan la
política genérica, según el [contrato del adaptador](adapter.md).

## Coste y configuración

Entrenar 1.600 celdas puede ser caro. El CLI permite ajustar el trabajo:

- `--score A:B` entrena las diagonales necesarias hasta ese marcador.
- `--score-region A:B` restringe la tabla a una región cerrada ante aumentos
  de tantos e intercambio de jugadores.
- `--threads N` entrena hasta N celdas de una diagonal en paralelo, con un
  almacén independiente por celda.
- `--threads-per-cell M` permite compartir el almacén de cada celda entre
  varios hilos.
- `--evaluation-samples` fija el presupuesto de evaluación. Las opciones
  `--evaluation-min-samples` y `--evaluation-target-se` permiten una parada
  adaptativa basada en el error local estimado.

La memoria crece con el número de almacenes simultáneos. Aumentar los hilos no
garantiza una aceleración proporcional. La [guía de rendimiento](performance.md)
incluye mediciones y comandos para comparar configuraciones.

Los errores de las diagonales altas se propagan hacia las anteriores. Una
evaluación con más muestras reduce su ruido local, pero no garantiza que la
estrategia entrenada sea mejor. Las celdas publicadas conservan sus valores al
reanudar; para cambiar su entrenamiento se utiliza una tabla de destino nueva.

## Propiedades y comprobaciones

1. Para toda hoja, `U0 + U1 = 0`.
2. Todos los valores pertenecen a `[-1, 1]`.
3. Alcanzar 40 devuelve `+1/-1` sin consultar `V`.
4. Un órdago aceptado termina la partida sin recompensas adicionales.
5. Un órdago rechazado aplica solo el tanteo cubierto y continúa si procede.
6. Una prueba explícita verifica intercambio de marcador y cambio de signo.
7. Ninguna celda consulta una continuación sin inicializar.
8. Una misma clave CFR siempre tiene el mismo menú de acciones.

Las pruebas incluyen un subjuego a tres tantos con cartas conocidas y acciones
restringidas. Comparan la continuación por tabla con el desarrollo directo del
mismo árbol para comprobar orientación y signo. Esta comparación no equivale a
resolver exhaustivamente la partida completa con información imperfecta.
