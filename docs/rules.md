# Reglas y variantes

[Volver al README](../README.md) · [Entrenamiento](training.md)

`ordago` es el modelo normal de mano aislada para dos jugadores; `ordago-four`
es su equivalente para cuatro jugadores por parejas. `ordago-score` y
`ordago-score-four` añaden el marcador y el valor de continuar la partida.
`ordago-deprecated` existe para conservar las estrategias históricas.

## Cartas, posiciones y mus

- Baraja española de 40 cartas: tres equivale a rey y dos equivale a as o pito.
- En dos jugadores, 0 siempre es mano y 1 es postre. Los empates favorecen a mano.
- Los lances siguen el orden grande, chica, pares y juego o punto.
- Los palos no influyen en las decisiones. El reparto respeta las cantidades
  reales de cada valor y los robos sin reposición.
- Cada jugador dice mus o no mus por turno. Para descartar deben aceptar todos.
  Cada descarte contiene al menos una carta; las máscaras equivalentes de
  cartas iguales se agrupan. Los bits siguen la mano ordenada por valor.
- La compilación permite uno o dos muses, con uno por defecto. El bot admite
  solo uno. Las particularidades de parejas están en [cuatro jugadores](mus-four-score.md).

## Envites

En las cuatro variantes principales sin `-simplified`, las aperturas numéricas
son 2, 4, 7 o 10. Ante un envite se puede no querer,
querer, subir 2, 4 o 10, o lanzar órdago. También se puede abrir con órdago.
Las cantidades son **incrementos**: `+2 → +2 → +4` deja 8 tantos en juego.

Cada respuesta numérica debe ser igual o mayor que el incremento anterior.
Cada jugador puede usar cada incremento solo una vez por lance, y hay como
máximo cinco envites numéricos, incluida la apertura. Después aún se puede
querer, no querer o lanzar órdago. Son restricciones de la abstracción del
solver; un motor externo puede ofrecer otras cantidades al bot.

### Variantes `-simplified`

`ordago-simplified`, `ordago-four-simplified`, `ordago-score-simplified` y
`ordago-score-four-simplified` reducen el menú a incrementos de 2, 4 y 10.
No existe el envite de 7, ni siquiera como apertura. Cada subida debe ser
**estrictamente mayor que el incremento anterior**, no que el total acumulado.
Tras abrir con 2 se permite subir 4 o 10; tras subir 4, solo 10; tras subir 10,
solo quedan querer, no querer u órdago. No se añade ningún límite de acciones:
la secuencia numérica más larga es naturalmente `2 → +4 → +10`, con total 16.
La restricción vuelve a empezar en cada lance.

En parejas, ambos compañeros responden a la misma oferta. Pueden proponer
la misma subida válida; solo la respuesta elegida al resolver la pareja
se convierte en un nuevo incremento. Se mantienen mus, descartes, órdago,
información, cobros, utilidad y cota pública de la variante de origen.

### Cota pública y rechazos

En los modelos públicos, toda propuesta numérica cumple:

```text
mayor marcador público + envites aceptados pendientes + total propuesto < 40
```

En una mano aislada el marcador público contiene solo los cobros de esa mano.
Con marcador incluye también los tantos iniciales. El total propuesto incluye
las apuestas previas del lance activo. Los dejes ya cobrados no vuelven a
sumarse como pendientes. Los lances al paso y los tantos intrínsecos tampoco
se anticipan. El órdago queda fuera de esta cota; la cota sigue siendo 40 al
usar un `--target` menor.

Rechazar una apertura concede una piedra. Rechazar una subida concede el
total anterior cubierto: tras `+2 → +2 → +4`, no querer concede 4. Si se lanza
órdago después de esos tres envites y se rechaza, se conceden 8.

## Cobro y utilidad

Los dejes se cobran durante la negociación. Los envites aceptados, los lances
al paso y los tantos intrínsecos de pares, juego o punto se cuentan al final,
en orden de lances. Los intrínsecos quedan pendientes también si se rechaza
el lance y corresponden al apostador en ese caso.

| Modelo | Final ordinario | Órdago querido |
|---|---|---|
| `ordago` y `ordago-four` | Cuenta todos los lances, limita cada total a 40 y usa la diferencia de tantos. | Termina la mano y sustituye todos los pagos por 40:0 o 0:40. |
| `ordago-score` y `ordago-score-four` | Detiene el cobro cuando se alcanza el objetivo; si la partida continúa, consulta su valor futuro. | Decide la partida inmediatamente, sin cobrar pendientes. |

Así, un resultado ordinario 40:1 vale +39 para mano en `ordago` o para la pareja
de mano en `ordago-four`. Alcanzar 40 en estas variantes no interrumpe los
lances pendientes; sigue siendo posible pasar o lanzar órdago.
En las variantes con marcador, ganar la partida vale +1 y perderla −1.
Desde 39:39, grande al paso sigue pendiente mientras se negocia chica: un
deje decisivo o un órdago querido en chica puede resolver antes la partida.

La [tabla de continuación](continuation-table.md) explica el cambio de mano y
la utilidad cuando nadie ha alcanzado el objetivo.

## Información y abstracciones

Mus y descartes conservan la composición exacta de las cartas propias.
Durante los envites se usa una categoría distinta por lance:

| Lance | Información de la mano |
|---|---|
| Grande | Número de reyes y siguiente carta más alta. |
| Chica | Número de pitos y siguiente carta más baja. |
| Pares | Tipo y valores de las parejas. |
| Juego | Uno de los ocho totales con juego. |
| Punto | Total entre 4 y 30. |

En dos jugadores, los lances anteriores se resumen por quién mostró fuerza:
ninguno, mano, postre o ambos. Envidar, subir y querer muestran fuerza. Se
conserva también quién tiene pares cuando ese lance no se negocia, los
descartes públicos y los datos necesarios para determinar el menú actual.

Las claves conservan los cobros públicos y el total de envites aceptados
pendientes, sin separar sus cantidades por lance. Grande 2/chica 4 y grande
4/chica 2 pueden compartir política; el estado conserva los importes exactos
para contarlos correctamente. Las claves nunca incluyen las cartas rivales
ni los ganadores ocultos de lances pendientes en los modelos públicos.

Esta reducción pierde recuerdo perfecto. Las garantías habituales de CFR no
certifican la convergencia de estas abstracciones; con cuatro jugadores se
añade la limitación de entrenar políticas privadas por separado.

## Compatibilidad histórica

**`ordago-deprecated` está obsoleta y se conserva para seguir usando las
estrategias entrenadas con ella.** Los comandos que usaban el antiguo modelo
`ordago` deben indicar ahora `ordago-deprecated`. Su contabilidad incorpora
resultados aún ocultos en las claves y
en los límites de envite. La variante normal corrige esa dependencia.

Se mantienen los lectores, las reglas históricas y los identificadores de
archivo: los checkpoints se cargan y se reanudan con el nombre nuevo sin
conversión ni reentrenamiento. `ordago-public` sigue siendo un alias del actual
`ordago`, que utiliza el modelo público.

| Variante | Esquema de mano aislada, con un mus cuando corresponde |
|---|---|
| `ordago` | `mus.heads-up-single-hand.ordago-public/v1-muses-1` |
| `ordago-four` | `mus.four-partnership-single-hand.ordago/v1-muses-1` |
| `ordago-simplified` | `mus.heads-up-single-hand.ordago-simplified/v1-muses-1` |
| `ordago-four-simplified` | `mus.four-partnership-single-hand.ordago-simplified/v1-muses-1` |
| `ordago-deprecated` | `mus.heads-up-single-hand.ordago-with-mus/v9-muses-1` |
| `single-bet` | `mus.heads-up-single-hand/v1` |
| `raises-to-eight` | `mus.heads-up-single-hand.raises-to-eight/v1` |

Los dos modelos reducidos no tienen mus ni descartes: `single-bet` solo abre
con 2 y permite querer o no querer; `raises-to-eight` añade subidas 2 → 4 → 8.
Ambos usan diferencia de tantos y sirven para estudiar árboles más pequeños.

Los checkpoints con dos muses usan el sufijo `muses-2` y no se mezclan con
los de uno. Los esquemas de tablas con marcador se describen en
[entrenamiento](training.md#compatibilidad-de-archivos).
