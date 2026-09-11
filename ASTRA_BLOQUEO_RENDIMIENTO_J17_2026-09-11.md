# J17: pausa por el coste de confirmar las escrituras

Fecha: 11 de septiembre de 2026. Padre del candidato: `1f1dec7`.

**El arreglo J01 aceptado por el usuario permanece aplicado**, incluidos núcleo,
CLI, precarga y bot. También permanece el bloqueo de directorios de J17 que se
libera al terminar el proceso. La pausa afecta únicamente al candidato posterior
que añade sincronización explícita de archivos y directorios al guardar.

El candidato pasó las pruebas y la revisión de corrección de Claude por Orca.
Se retiró del código después de confirmar una penalización superior al límite
acordado. Está conservado en [este parche](docs/review-performance/J17-sync-blocked.patch).
No se ha hecho commit de su implementación. Se espera la decisión del usuario.

## Qué cambia

Antes, el CLI escribe, cierra y renombra el archivo. El sistema puede dejar
escrituras pendientes cuando el proceso ya informa de éxito. El candidato
solicita confirmar los datos con `fsync`, publica el nombre y confirma también
el directorio. Las tablas se publican después de sus checkpoints y el
manifiesto de ampliación se guarda mediante el mismo procedimiento.

Esto busca mejorar la persistencia ante caída del sistema o pérdida de energía.
Las pruebas verifican orden y errores inyectados, no cortes físicos de energía.
Un fallo posterior al renombrado puede dejar visible el archivo nuevo aunque
se devuelva error. Se corrigió también el reintento de creación de directorio
cuando había fallado la primera sincronización de su padre.

## Medidas

Btrfs del workspace, un trabajador en CPU0, semilla 7. `/tmp` es tmpfs y no se
usa para estas escrituras. Ejecutables independientes del padre y del candidato
final. Se alterna el orden, se confirman escrituras anteriores fuera del tramo
medido y no se ejecutan compilaciones ni otras pruebas propias simultáneamente.

| Trabajo completo medido | Padre | Candidato | Diferencia |
|---|---:|---:|---:|
| Público, 500 iteraciones y checkpoint raw de 804 MB | 9,412 s | 9,624 s | +2,26 % |
| Público, 500 iteraciones y checkpoint zstd de 140 MB | 10,433 s | 10,692 s | +2,49 % |
| 45 celdas: 1.000 iteraciones y 10.000 muestras por celda | 3,707 s | 3,783 s | +2,05 % |
| Ampliar esas 45 celdas en otras 1.000 iteraciones | 3,942 s | 4,107 s | +4,18 % |
| 100 tablas de 45 celdas: **una iteración y una muestra** por celda | 0,748 s | 11,839 s | +1.482,84 % |

Las cuatro primeras filas son medianas de tres pares. La última es la mediana
de **cinco pares adicionales de confirmación**, posteriores a otros tres pares.
Agrupar 100 tablas amplía el trabajo de cada proceso, de unos 7 ms, para evitar
juzgar una única ejecución diminuta. La diferencia confirmada es de segundos.

En la carga mínima, el candidato tarda 15,83 veces el tiempo anterior: unos
**2,46 ms extra por celda**, incluyendo la publicación de las tablas y el arranque
de los procesos. Ese coste domina cuando casi no se entrena ni evalúa. No se
puede trasladar ese multiplicador a entrenamientos largos: en la carga de
1.000 iteraciones y 10.000 muestras el aumento observado es del 2,05 %.

**El recorrido de CFR no cambia.** La biblioteca del solver y el bot conservan
sus SHA-256; todos los checkpoints y valores comparados son idénticos. En las
tandas mínimas se comprobaron los 46 archivos de cada una de las 1.600 tablas.
El aumento corresponde a confirmar las escrituras en disco. El RSS de las
cargas regulares varía entre −0,06 % y +0,12 %; en los procesos diminutos la
medida incluye el arranque y no permite precisar su consumo independiente.

## Decisión pendiente

Se aplica el límite de rendimiento solicitado por el usuario incluso después
del visto bueno funcional: candidato retirado y trabajo detenido. La aceptación
del coste de J01 no se interpreta como autorización para este nuevo coste.

Se puede aceptar esta sincronización por defecto, ofrecerla mediante una opción
explícita de guardado o posponerla. No se ha aplicado automáticamente ninguna
alternativa. La recuperación de celdas provisionales y los restantes bloques
siguen pendientes; su estado está en [el registro](docs/review-progress.md).

## Evidencia

- [Mediciones regulares, comandos, script y validación](docs/review-performance/J17-sync.json).
- [Tandas mínimas y cinco pares de confirmación](docs/review-performance/J17-sync-small.json).
- [Primera revisión de Claude](docs/review-performance/J17-sync-claude-initial.md).
- [Revisión final del código y las pruebas](docs/review-performance/J17-sync-claude-final.md).

La batería completa, ASan/UBSan de persistencia y las pruebas sobre Btrfs pasan.
La segunda revisión condicionó su aprobación a esas comprobaciones finales,
que terminaron correctamente. El código y las pruebas del candidato se
retiraron invirtiendo exactamente el parche conservado; no se retira J01.

Claude también [revisó las mediciones](docs/review-performance/J17-sync-performance-claude.md)
y confirmó la equivalencia del trabajo y la justificación de la pausa. Tarea
`task_f4143d7e3031`, dispatch `ctx_37121eb23608`, mensaje `msg_ac468a5f4e3f`.
Su reserva sobre un posible solapamiento se resuelve con la cronología de las
herramientas y del sistema de archivos: el log regular termina a las
19:07:39,735 y el log de la carga mínima se crea a las 19:07:49,914 (Madrid).
El script se había escrito antes, pero se ejecutó después. Estos datos están
conservados en el JSON de la carga mínima.
