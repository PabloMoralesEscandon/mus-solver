# Tabla de continuación

[Volver al README](../README.md) · [Comandos de entrenamiento](training.md)

`ordago-score` y `ordago-score-four` usan el marcador para valorar el resto de
la partida. Ganar tres tantos desde 35:18 no tiene el mismo efecto que hacerlo
desde 0:0; maximizar la diferencia de tantos de una mano no representa por sí
solo el objetivo de ganar la partida.

## Una estrategia por marcador

Cada celda A:B tiene una estrategia y un valor `V[A][B]`. La estrategia decide
cómo jugar esa mano; el valor estima el resultado de continuar la partida
desde ese marcador. A 40 existen 1.600 marcadores no terminales, aunque se
puede entrenar solo una región suficiente.

El índice 0 corresponde a mano y el 1 a postre en dos jugadores; en parejas,
a los equipos 0+2 y 1+3. El valor se expresa desde la posición o equipo de mano.
Las cartas del compañero no se comparten entre sus políticas.

| Resultado | Utilidad de quien comenzó de mano |
|---|---|
| Gana la partida | +1 |
| Pierde la partida | −1 |
| La mano termina en A':B' y la partida continúa | `-V[B'][A']` |

El intercambio de índices refleja el cambio de mano. El signo menos devuelve
el valor a la perspectiva anterior. No se añade la diferencia de tantos:
esos tantos ya determinan la celda siguiente. El rival recibe la utilidad opuesta.

Por ejemplo, desde 35:18, ganar tres tantos lleva a 38:18. Como la mano pasa
al contrario, la utilidad de continuación es `-V[18][38]`. Si se ganan cinco
y se alcanza 40, la utilidad es directamente +1.

## Recuento de tantos

Los dejes se cobran inmediatamente; los envites queridos, lances al paso y
tantos intrínsecos quedan pendientes hasta el recuento final. Se cuentan en
orden de lances y se detienen al alcanzar el objetivo. Un órdago querido o un
deje decisivo termina la partida antes de cobrar los pendientes.

Durante las decisiones se conocen solo los cobros públicos y los contratos
pendientes. Las claves no incorporan ganadores ni valores de cartas ocultas.
Las [reglas y abstracciones](rules.md) detallan qué información se conserva.

## Por qué se entrena hacia atrás

Una mano terminada concede tantos, por lo que una continuación no terminal
siempre aumenta la suma del marcador. Esto permite resolver primero las
celdas cercanas al objetivo:

```text
39:39 → 39:38 y 38:39 → siguientes diagonales → 0:0
```

Para cada diagonal, el coordinador:

1. Entrena una mano por celda con los valores superiores fijos.
2. Evalúa la estrategia media de cada celda por muestreo.
3. Guarda sus checkpoints y publica los valores de la diagonal completa.
4. Continúa hacia marcadores de suma menor.

Las celdas de una diagonal pueden entrenarse en paralelo porque no dependen
entre sí. Durante cada entrenamiento la tabla de continuación permanece
inmutable. Cambiar una celda publicada afecta al problema que resolvieron las
anteriores; por eso la [ampliación de tablas](training.md#añadir-iteraciones-a-toda-una-tabla)
recalcula hacia atrás en otro directorio.

## Interpretar los valores

`V` está entre −1 y +1. La conversión `(V + 1) / 2` expresa una estimación de
probabilidad de victoria con la política y continuaciones evaluadas.

El muestreo usa la estrategia media y una política uniforme en los nodos sin
datos. Su error estándar mide incertidumbre local; no incluye los errores de
las continuaciones ni demuestra cercanía a una política óptima. La abstracción
pierde recuerdo perfecto, y cuatro políticas privadas por parejas tampoco
heredan automáticamente las garantías de CFR para dos jugadores.

## Archivos y uso en una partida

`values.txt` guarda valores y metadatos; `score-AA-BB.cfr.zst` guarda la
estrategia y los acumuladores de entrenamiento. Los valores bastan para
entrenar celdas anteriores; para jugar hacen falta también las estrategias.

El [bot](adapter.md) consulta las estrategias exportadas a TXT. Elige una
celda por el marcador al inicio de la mano y la conserva durante todo el
reparto, aunque se cobren dejes. Al empezar otra mano, normaliza de nuevo las
posiciones y selecciona la siguiente celda. Fuera de la cobertura utiliza la
genérica `ordago`, o una histórica si así se ha configurado.
