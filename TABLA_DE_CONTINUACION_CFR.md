# Diseño de una tabla de continuación de marcador para CFR

## Objetivo

Un solver de una mano aislada suele utilizar como utilidad la diferencia de
tantos obtenidos en esa mano. Esa utilidad no representa bien el objetivo real
de una partida a 40:

- ganar tres tantos con `35–18` no vale lo mismo que ganarlos con `0–0`;
- conceder tres tantos al rival con `18–35` puede ser decisivo;
- un órdago aceptado decide la partida, mientras que un envite ordinario solo
  conduce a otro marcador y otra mano.

La solución separa el problema en dos niveles:

1. **Nivel interior:** CFR resuelve una mano completa para un marcador inicial.
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
V[a][b] = valor esperado de la partida para el equipo 0
          al comenzar una mano con marcador a–b
```

Existen `40 × 40 = 1.600` marcadores no terminales antes de aprovechar
simetrías o eliminar estados inalcanzables desde una posición concreta.

La tabla de valores no sustituye a las estrategias. El sistema completo debe
guardar dos resultados:

- `V[a][b]`: valor escalar empleado para entrenar marcadores anteriores;
- `S[a][b]`: estrategia CFR que se utilizará al jugar desde `a–b`.

Guardar solamente `V` permite calcular valores anteriores, pero no jugar las
manos posteriores de una partida real.

## Escala de utilidad y suma cero

La representación más clara es una utilidad firmada y normalizada:

```text
equipo 0 gana la partida:   +1
equipo 0 pierde la partida: -1
```

Entonces:

```text
V[a][b] = P(equipo 0 gana | a, b)
        - P(equipo 1 gana | a, b)
```

`V` queda acotado en `[-1, 1]`. Si no existen empates:

```text
P(equipo 0 gana | a, b) = (V[a][b] + 1) / 2
```

También puede utilizarse `+40/-40`: contiene la misma información multiplicada
por 40. La escala `+1/-1` facilita la interpretación y mantiene pequeños los
arrepentimientos.

Todas las hojas deben ser de suma cero:

```text
U0 = z
U1 = -z
```

Si una evaluación aproximada produce dos valores ligeramente incompatibles,
pueden proyectarse a suma cero:

```text
z = (estimación_equipo_0 - estimación_equipo_1) / 2
```

## Cómo se valora el final de una mano

Al alcanzar una hoja se aplica primero todo el tanteo, en el orden establecido
por las reglas. Después se distingue entre dos casos.

### La partida ha terminado

Si un equipo alcanza 40, no se consulta la tabla:

```text
ganador == equipo 0  → U0 = +1, U1 = -1
ganador == equipo 1  → U0 = -1, U1 = +1
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

La convención de orientación debe decidirse antes de construir la tabla. Una
opción compacta es que el índice 0 represente siempre al equipo que ocupa la
posición de mano al comenzar el siguiente reparto.

Si la mano pasa al equipo contrario, el marcador se intercambia. Desde la
perspectiva del equipo 0 de la mano que acaba de terminar:

```text
U0 = -V[b'][a']
U1 =  V[b'][a']
```

El intercambio orienta la posición desde la perspectiva de la nueva mano. El
cambio de signo devuelve el resultado a la perspectiva anterior.

Si toda la rotación no puede absorberse intercambiando equipos, la solución
general es añadir la posición de mano como dimensión:

```text
V[a][b][posición_de_mano]
```

Esto es necesario cuando la abstracción distingue posiciones dentro de una
pareja. No debe suponerse una simetría que el árbol o los conjuntos de
información no respeten.

## Ejemplo práctico: marcador 35–18

Supongamos que A es el equipo 0 actual y que la mano pasa al adversario después
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

Si una variante permite terminar una mano sin conceder puntos, aparece un
ciclo hacia el mismo marcador. En ese caso hace falta un punto fijo exterior o
incorporar la repetición al juego; no debe leerse una entrada provisional.

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
dependen de diagonales posteriores. Publicar el lote al terminar hace que el
resultado no dependa del orden ni del paralelismo.

Durante el entrenamiento de una celda, `V` debe permanecer inmutable. Cambiar
las continuaciones mientras CFR actualiza arrepentimientos convertiría el
juego en un objetivo móvil.

## Qué valor guardar después de CFR

`V[a][b]` debe representar la política que se utilizará:

1. entrenar CFR;
2. extraer la estrategia media;
3. evaluarla con la misma tabla de continuación;
4. guardar el valor de esa evaluación.

La utilidad de la última iteración es demasiado ruidosa. Promediar utilidades
de entrenamiento sirve como estimador rápido, pero no siempre coincide con el
valor de la estrategia media.

Como todas las hojas están en `[-1, 1]`, cualquier entrada fuera de ese rango
indica un error de signo, orientación o tanteo.

## Integración con CFR

### Contexto de cada celda

Cada entrenamiento necesita un contexto inmutable con:

- marcador al comenzar la mano;
- objetivo de la partida;
- orientación de equipos y mano;
- referencia de solo lectura a `V`;
- reglas y abstracción utilizadas.

El estado mutable conserva los tantos resueltos durante la mano. El marcador
absoluto se obtiene sumándolos al inicial y limitándolo al objetivo.

### Función de utilidad terminal

La función terminal deja de devolver la diferencia de tantos de la mano. Debe:

1. resolver el tanteo completo;
2. comprobar si alguien ganó la partida;
3. devolver `+1/-1` si terminó;
4. calcular el marcador siguiente si continúa;
5. rotar o canonizar los roles;
6. consultar `V`;
7. devolver valores exactamente opuestos.

Este es el único punto en el que la programación dinámica entra en CFR. El
muestreo, el cálculo de arrepentimientos y la estrategia media no necesitan
conocer el significado de la tabla.

### Conjuntos de información

Con un almacén CFR separado por marcador, el marcador inicial es constante y
no necesita aparecer en cada clave. Sí debe codificarse cualquier tanteo
público producido dentro de la mano que cambie acciones legales o decisiones
posteriores.

Con un único almacén para varios marcadores, el marcador y la posición de mano
deben formar parte de la clave. De otro modo se mezclarían políticas que deben
apostar de forma distinta, como `0–0` y `39–38`.

Todos los estados que compartan una clave CFR deben ofrecer exactamente las
mismas acciones y en el mismo orden.

### Acciones dependientes del marcador

Si el máximo de un envite depende de los tantos restantes, el marcador
absoluto debe estar disponible al generar acciones. Dos historiales de envite
idénticos pueden tener menús distintos cuando parten de marcadores diferentes.

Toda información pública que determine el menú legal debe formar parte del
estado y, cuando corresponda, del conjunto de información.

### Persistencia y uso durante una partida

Por cada celda deben guardarse:

- valor de continuación;
- estrategia media o checkpoint;
- iteraciones y criterio de parada;
- versión de reglas y abstracción;
- convención de orientación y escala de utilidad.

Al comenzar una mano se selecciona `S[a][b]`. Al terminarla se actualiza el
marcador, se rota la posición y se selecciona la celda siguiente.

## Coste y optimizaciones

Resolver 1.600 celdas con un entrenamiento completo puede ser caro. Las
optimizaciones más seguras son:

- entrenar en paralelo las celdas de una diagonal;
- calcular solo el cierre de dependencias de los marcadores necesarios;
- reutilizar la estructura de infosets de una celda cercana sin mezclar sus
  acumuladores finales;
- guardar estrategias por separado y cargarlas bajo demanda;
- asignar más iteraciones a celdas cuyo valor sea inestable;
- mantener utilidades normalizadas.

La legalidad cambia cerca de 40, por lo que cualquier reutilización debe
validar de nuevo el menú de acciones. Los errores de las diagonales altas se
propagan hacia las anteriores; conviene resolverlas bien antes de invertir
muchas iteraciones en `0–0`.

## Invariantes y pruebas

1. Para toda hoja, `U0 + U1 = 0`.
2. Todos los valores pertenecen a `[-1, 1]`.
3. Alcanzar 40 devuelve `+1/-1` sin consultar `V`.
4. Un órdago aceptado termina la partida sin recompensas adicionales.
5. Un órdago rechazado aplica solo el tanteo cubierto y continúa si procede.
6. Una prueba explícita verifica intercambio de marcador y cambio de signo.
7. Ninguna celda consulta una continuación sin inicializar.
8. Una misma clave CFR siempre tiene el mismo menú de acciones.
9. Una transición sin puntos se detecta como ciclo.
10. Una partida reducida a tres o cinco tantos coincide con una resolución
    exhaustiva de la partida completa.

Como comprobación de cordura, aumentar el marcador propio no debería reducir
sistemáticamente el valor, y aumentar el contrario no debería mejorarlo.
Pequeñas infracciones pueden ser ruido de CFR; desviaciones grandes suelen
señalar un error de orientación o signo.

## Plan recomendado

1. Fijar la orientación y la utilidad terminal `+1/-1`.
2. Separar el tanteo de la función que asigna utilidad CFR.
3. Añadir la consulta de continuación a las hojas no terminales.
4. Validar signos y rotación con una tabla artificial conocida.
5. Construir el coordinador de diagonales, de 78 a 0.
6. Guardar por separado `V[a][b]` y `S[a][b]`.
7. Validar primero una partida con objetivo pequeño mediante resolución total.
8. Entrenar después la tabla a 40 y medir la propagación del error.

El resultado mantiene el árbol CFR limitado a una mano, pero hace que pasar,
envidar, aceptar, rechazar u ordenar un órdago se valore por su efecto real
sobre la probabilidad de ganar la partida completa.
