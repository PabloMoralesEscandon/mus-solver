# Desarrollo y biblioteca C

[Volver al README](../README.md)

## Compilar

```sh
git submodule update --init --recursive
make
make debug
```

Release usa `-O3` y LTO. `make solver`, `make bot` y `make match-eval` construyen
componentes por separado. `libmus_solver.a` contiene el modelo y sus adaptadores;
el evaluador de partidas enlaza el motor de `Simulador-Mus` de forma explícita.

Para compilar con dos muses sin sustituir la configuración habitual:

```sh
make BUILD_DIR=build/two-muses CFR_BUILD_DIR=build/two-muses MUS_MAX_MUSES=2 solver
```

El núcleo admite uno o dos muses; el bot y el evaluador de partidas requieren
uno. El build registra compilador y flags e invalida los objetos afectados
cuando cambia la configuración. No compiles configuraciones distintas a la vez
en los mismos `BUILD_DIR` o `CFR_BUILD_DIR`.

`make clean` elimina `BUILD_DIR`; `make clean-all` elimina también la caché de
C-CFR seleccionada. Después de actualizar C-CFR con cambios en su lista de
fuentes, usa `make clean-all` para retirar miembros antiguos de su biblioteca.
Un cambio de herramienta bajo el mismo nombre o de variables como `CPATH`
también puede requerir limpieza o directorios nuevos.

## Pruebas

```sh
make test
make test-sanitize
make test-tsan
make dependencies
```

| Comando | Alcance |
|---|---|
| `make test-core` | Modelos, claves, acciones, tanteo, API y CLI del solver. |
| `make test` | Añade persistencia, recuperación, estadísticas, bot y partidas. |
| `make test-sanitize` | AddressSanitizer y UndefinedBehaviorSanitizer. |
| `make test-tsan` | ThreadSanitizer para acceso y entrenamiento concurrentes. |
| `make dependencies` | Baterías independientes de ambos submódulos. |

La configuración alternativa se prueba con:

```sh
make BUILD_DIR=build/two-muses CFR_BUILD_DIR=build/two-muses MUS_MAX_MUSES=2 test-core
```

Las comprobaciones de configuración, dependencias incrementales y componentes
están en `tests/test_build_config.py`, `tests/test_build_incremental.py` y
`tests/test_build_components.py`; se ejecutan con Python 3 y usan copias temporales.
Los sanitizers requieren un compilador y un entorno que los admitan.

## API pública

| Encabezado | Contenido |
|---|---|
| [mus_game.h](../include/mus_solver/mus_game.h) | Descriptores, estado reversible, acciones y claves de dos jugadores. |
| [mus_four.h](../include/mus_solver/mus_four.h) | Modelo y observaciones de cuatro jugadores. |
| [mus_training.h](../include/mus_solver/mus_training.h) | Entrenamiento compartido. |
| [mus_score.h](../include/mus_solver/mus_score.h) | Tablas, celdas, continuaciones y precarga. |
| [mus_strategy.h](../include/mus_solver/mus_strategy.h) | Exportación de estrategias. |
| [mus_evaluation.h](../include/mus_solver/mus_evaluation.h) | Evaluación exacta y diagnósticos acotados. |
| [mus_adapter.h](../include/mus_solver/mus_adapter.h) | Bot e integración con un motor externo. |

Para la variante normal usa `mus_ordago_public_game_state_init` y
`mus_ordago_public_game_descriptor`. Los nombres `mus_ordago_*` sin `public`
identifican el formato histórico o utilidades compartidas; no se han renombrado
para conservar compatibilidad. `mus_game_*`, `mus_evaluation_*` y
`mus_strategy_write_text` siguen representando el modelo reducido `single-bet`.

C-CFR consume el descriptor `Game` y un estado reversible. Conserva el
contexto, la tabla y el estado vivos mientras el entrenador los use. Cada
almacén de una variante con marcador pertenece a una única celda. La
[integración del bot](adapter.md#biblioteca-c) y la [API de cuatro jugadores](mus-four-score.md#api-c)
describen sus contratos específicos.

## Contribuir

Incluye una descripción del cambio y la validación pertinente. Si cambia una
interfaz pública, actualiza su guía y los ejemplos. Añade una prueba cuando
cambien reglas, compatibilidad o comportamiento observable.

Antes de publicar una rama, comprueba que los commits referenciados por los
submódulos se pueden descargar. Los cambios en C-CFR o Simulador-Mus pertenecen
a sus repositorios y se incorporan aquí actualizando la referencia.
