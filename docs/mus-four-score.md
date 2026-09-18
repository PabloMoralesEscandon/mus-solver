# Órdago: cuatro jugadores

[Volver al README](../README.md) · [Entrenamiento](training.md) · [Reglas comunes](rules.md)

`ordago-four` juega una mano aislada y `ordago-score-four` añade el marcador
y la continuación de la partida. Ambas son para cuatro jugadores por
parejas: 0 y 2 contra 1 y 3. Cada jugador decide con sus cartas y la información
pública; no conoce las cartas del compañero. El bot y el evaluador de partidas
incluidos en el repositorio admiten únicamente dos jugadores.

`ordago-four-simplified` y `ordago-score-four-simplified` conservan estas
reglas y flujos de trabajo, pero solo permiten incrementos de 2, 4 y 10,
estrictamente crecientes. No ofrecen 7 ni añaden un límite de acciones;
mantienen el órdago. Las propuestas de ambos compañeros se comparan con la
oferta rival, no entre sí. Consulta las [reglas simplificadas](rules.md#variantes--simplified).

## Mano aislada sin marcador

`ordago-four` es el equivalente de `ordago` para parejas. Empieza sin tantos
previos, cuenta todos los lances y limita el total de cada pareja a 40. La
utilidad es la diferencia de tantos: 40:1 vale +39 para los jugadores 0 y 2,
y −39 para 1 y 3. Alcanzar 40 no termina anticipadamente la mano. Un órdago
querido sí la termina y sustituye todos los cobros por 40:0 o 0:40.

```sh
make solver
build/release/mus-solver --format ordago-four \
    --iterations 100 --seed 7 --save-compressed cuatro.cfr.zst \
    --export-strategy cuatro.txt --evaluate --evaluation-samples 1000
```

Admite reanudación y varios hilos como `ordago`. No admite `--score`, `--target`
ni las opciones de tablas de continuación. La evaluación estima la diferencia
de tantos con la estrategia media, sin calcular explotabilidad. Sus checkpoints
tienen un esquema propio y no se cargan como `ordago-score-four`.

## Reglas y marcador

El jugador 0 es mano. Los turnos siguen el orden 0, 1, 2, 3. El marcador
`A:B` corresponde a los equipos 0 y 1, respectivamente. Ambos miembros de
un equipo reciben la misma utilidad; la utilidad del equipo contrario tiene
el signo opuesto. No hay cuatro marcadores independientes.

Se mantienen grande, chica, pares y juego o punto; las equivalencias de la
baraja de 40 cartas; las aperturas de 2, 4, 7, 10 u órdago; las subidas
aditivas de 2, 4 o 10; el límite de cinco envites numéricos y la prohibición
de que una pareja repita el mismo incremento en un lance, aunque lo haya usado
el otro compañero. La pareja rival puede usar ese incremento una vez. La cota numérica
conserva la regla de la versión score: mayor marcador público, más envites
aceptados pendientes, más envite activo y nuevo incremento, debe ser menor
que 40. En la mano aislada, el marcador público contiene únicamente los cobros
de esa mano. En `ordago-score-four`, `--target` cambia el objetivo de victoria,
como en dos jugadores.

Los cuatro deben dar mus para descartar. Cada uno elige un subconjunto no
vacío de su mano canónica. Se conservan las máscaras equivalentes de cartas
iguales y los recuentos públicos de descarte. El reparto y los robos respetan
las existencias de cada clase de carta. La compilación admite uno o dos muses.
Si el segundo mus agota el mazo, se barajan los descartes para continuar los
robos, como en el simulador. Hasta ese momento los descartes quedan fuera del
mazo; no se puede robar una carta que siga en la mano de otro jugador.

En pares solo actúan quienes tienen pares; en juego, quienes tienen juego.
Si solo una pareja cumple el requisito, el lance no se negocia. La mejor
mano de cada lance determina la pareja ganadora; un empate favorece al
jugador más próximo a mano. La pareja ganadora suma los valores intrínsecos
de pares o juego de sus dos miembros.

Ante un envite, responden los dos miembros elegibles de la pareja en orden de
turno. La primera respuesta es pública y queda pendiente hasta que responde
el compañero, incluso si el primero quiere, sube o propone órdago. Se ejecuta
la mayor de las dos respuestas: no quiero < quiero < subida numérica < órdago.
Entre subidas numéricas gana la de mayor incremento. Por ejemplo, si 0 quiere
y 2 propone órdago, la pareja lanza órdago.

Al resolver la respuesta, se conservan públicamente las dos propuestas y sus
autores, incluida la propuesta que no se ejecutó. Esta memoria se sustituye
cuando se resuelve otra respuesta de pareja y se borra al empezar otro lance.
La primera propuesta pendiente de un nuevo envite no borra la respuesta anterior.
Si solo hay un miembro elegible, se conserva su única propuesta y su autor.
La abstracción recuerda solo esta última respuesta completa: no conserva todo
el historial de propuestas ni ofrece memoria perfecta.

Ambos responden al mismo envite activo; una propuesta pendiente no aumenta
todavía la apuesta ni consume un incremento. Solo la respuesta ejecutada
consume el incremento para toda su pareja y cuenta para el límite de
cinco envites numéricos. Si las respuestas empatan, prevalece la del primero.
El estado reversible conserva quién ejecutó cada incremento, pero la observación
y la clave de estrategia solo guardan los incrementos usados por cada pareja.
El apostador actual y los autores de la última respuesta del lance sí se conservan.
Se restablecen los incrementos al empezar cada lance.
Si solo un miembro de la pareja es elegible para el lance, su respuesta se ejecuta
inmediatamente. Las aperturas se deciden por turno.
Las funciones de comparación de manos y tanteo de `Simulador-Mus` se usan como
referencia independiente; su negociador no implementa esta regla de respuestas.

Los dejes y las cantidades cubiertas rechazadas se cobran inmediatamente.
Los envites aceptados y los tantos intrínsecos esperan al recuento final,
en orden de lances. En `ordago-score-four`, un órdago querido termina la partida
antes de ese recuento y el marcador se comprueba después de cada cobro.
Si la mano termina sin
ganador de partida, la siguiente mano pasa al equipo contrario: desde el
equipo que era mano, la continuación es `-V[B][A]` con los tantos actualizados.

## Entrenamiento con marcador

```sh
make solver
build/release/mus-solver --format ordago-score-four \
    --score 39:39 --iterations 100 --evaluation-samples 1000 --seed 7 \
    --continuation tabla-cuatro
```

La tabla tiene dos dimensiones, una por equipo, y se entrena por diagonales.
Se aplican las opciones de la [guía de entrenamiento](training.md): regiones,
hilos, recuperación, ampliación a otro directorio y evaluación por muestras.
Las iteraciones son por celda; `--threads` distribuye celdas y
`--threads-per-cell` distribuye el trabajo de cada estrategia.

Para reanudar una celda y exportar sus probabilidades:

```sh
build/release/mus-solver --format ordago-score-four --score 39:39 \
    --continuation tabla-cuatro \
    --load-compressed tabla-cuatro/score-39-39.cfr.zst \
    --iterations 100 --save-compressed cuatro-ampliada.cfr.zst \
    --export-strategy cuatro-ampliada.txt
```

No se admite precargar estrategias de dos jugadores. Los esquemas de tablas,
reglas y checkpoints son específicos de parejas; consulta la
[compatibilidad de archivos](training.md#compatibilidad-de-archivos).

La evaluación estima el valor de la política con continuaciones fijas.
Entrenar cuatro políticas privadas con utilidad compartida no hereda las
garantías de equilibrio de CFR para dos jugadores. La abstracción también
pierde recuerdo perfecto. El evaluador exacto de C-CFR para dos jugadores
rechaza esta variante.

## API C

[mus_four.h](../include/mus_solver/mus_four.h) expone el estado reversible,
las observaciones, las claves y los menús. La observación conserva cartas
propias, descartes públicos, fuerza por equipo, declaraciones, cobros,
pendientes, límites de apuestas y las respuestas del lance activo descritas arriba.
`used_team_wager_action_masks[2]` agrupa los incrementos por pareja; el estado
interno mantiene `used_wager_action_masks[4]` por asiento. La exportación usa
`usados_pareja=A/B`, en orden 0+2 y 1+3.
La exportación muestra la última respuesta como, por ejemplo,
`ultima_respuesta=0:querer/2:ordago`.

Para una mano aislada, inicializa `MusFourGameState` con
`mus_four_game_state_init` y usa `mus_four_game_descriptor()` con C-CFR.
`mus_four_game_observe`, `mus_four_information_set_encode`,
`mus_four_information_set_decode` y `mus_four_information_set_legal_actions`
permiten consultar la estrategia. `mus_four_strategy_write_text` la exporta.
El campo `score_target` del modelo es cero y `continuation` es `NULL`.
`mus_four_evaluate_with_options` usa los tipos compartidos
`MusScoreEvaluationOptions` y `MusScoreEvaluation`, pero devuelve valores y
errores en tantos. Las funciones `mus_four_score_*` siguen siendo específicas
de la variante con marcador; usa el codec que corresponda al checkpoint.

Para usar [mus_score.h](../include/mus_solver/mus_score.h):

1. Inicializa `MusScoreTable` y asigna `table.four_player = true`.
2. Crea la celda con `mus_score_game_init`.
3. Reserva `mus_score_game_state_size(&game)` bytes.
4. Inicializa la memoria con `mus_score_game_state_init_generic`.
5. Usa `game.game` y ese `GameState` con C-CFR.

`mus_score_game_matches_any` reconoce ambas variantes, pero no permite
convertir cualquier estado a `MusGameState`. El predicado
`mus_score_game_matches`, `mus_score_game_state_init` y
`mus_score_terminal_utility` son específicos de dos jugadores. Para obtener
utilidades genéricas usa `cfr_game_terminal_utility` con el descriptor y estado
correspondientes. Conserva la tabla y el descriptor vivos durante su uso.

Al ampliar tablas mediante la API, configura
`MusScoreExtension.load_cell_generic` con un `MusScoreLoadCellGeneric` y deja
`load_cell = NULL`. El callback genérico recibe `GameState*` y admite ambas
variantes. El callback histórico `load_cell` recibe `MusGameState*`, tiene
prioridad en dos jugadores y se rechaza para cuatro antes de ejecutar la carga.
