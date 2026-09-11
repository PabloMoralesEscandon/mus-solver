# Aplicación de la revisión conjunta

Inicio: 2026-09-11. Base: `299915b45d8e4c02099fe194dac2308c7d4a3434`.
Fuente: `ASTRA_CLAUDE_REVISION_CONJUNTA_2026-09-11.md` en la raíz del workspace.
Run de Orca: `run_7b18a487f130`.

## Método y límite de rendimiento

Cada cambio se implementa, se comprueba, se revisa con Claude mediante una tarea
de Orca y se mide antes del commit. El coordinador resuelve las observaciones de
la revisión. El núcleo de C-CFR queda fuera del alcance.

Los ejecutables originales se conservan en `build/review-baseline/release`.
Cada candidato usa su propio directorio de construcción. Las comparaciones
alternan el orden de ejecución y usan tres repeticiones, semilla 7 y las mismas
cargas. Se compara con el padre del cambio y con la base inicial cuando procede.
Los cambios que no afectan una ruta se documentan como tales, sin atribuirles una
mejora por fluctuaciones de tiempo. Las mediciones se ejecutan sin compilaciones,
pruebas o benchmarks propios simultáneos.

Una mediana de tiempo al menos un 20 % mayor, o un pico de RSS al menos un 25 %
mayor, activa una comprobación adicional de cinco pares en la carga afectada.
Si se confirma una regresión grave atribuible al cambio, se conserva un parche,
se retira el candidato del árbol de trabajo y se espera la decisión del usuario.
Los tiempos muy cortos se amplían antes de juzgar diferencias relativas.
Una alteración del número de nodos o de la semántica se explica por separado;
la corrección funcional no exime del límite de rendimiento.

La [referencia inicial](review-performance/baseline.json) recoge compilador,
configuración, comandos y medidas. Los JSON por cambio guardan sus comparaciones.
Para cambios que preservan el algoritmo se comprueban también los checkpoints,
valores y contadores deterministas. La evaluación de la política media y el
rendimiento del bot desplegado son métricas diferentes.

## Estado por bloque

| Bloque | Trabajo | Estado |
|---|---|---|
| J01 | Modelo genérico sin información oculta y compatibilidad histórica | Pendiente |
| J02 | Recuento diferido en el simulador de dos/cuatro jugadores | Pendiente |
| J03 | Reintento tras fallo al guardar una diagonal | Completado |
| J04 | Rechazar TXT truncado a su cabecera | Pendiente |
| J05 | Excluir candidatos de precarga incompatibles con los dejes | Pendiente |
| J06 | Usar operaciones de confianza en la evaluación exacta local | Pendiente |
| J07 | Mezclar bits en la tabla de mejor respuesta | Pendiente |
| J08 | Reutilizar celdas terminadas de una diagonal interrumpida | Pendiente |
| J09 | Región opcional de continuaciones cerrada por incrementos y giro | Pendiente |
| J10 | Incertidumbre y presupuesto de evaluación | Pendiente |
| J11 | Precisar el alcance del diagnóstico de repartos fijos | Pendiente |
| J12 | Evaluar partidas con la política desplegada del bot | Pendiente |
| J13 | Conservar disponibilidad de política en TXT | Pendiente |
| J14 | Rechazar explícitamente el adaptador incompatible con dos muses | Pendiente |
| J15 | Rechazar fases de azar/terminal en la clave rápida | Pendiente |
| J16 | Gramática explícita para opciones enteras sin signo | Pendiente |
| J17 | Bloqueo ligado al proceso y contrato de durabilidad | Pendiente |
| J18 | Reducir el coste de codificación y generación de candidatos | Pendiente |
| J19 | Distribución y memoria de trabajadores por diagonal | Pendiente |
| J20 | Invalidar objetos al cambiar la configuración | Pendiente |
| J21 | Evitar enlaces sin cambios conservando dependencias reales | Pendiente |
| J22 | Dependencias, portabilidad y separación del simulador | Pendiente |
| J23 | Matriz de pruebas, observabilidad y contratos menores | Pendiente |

## J03: reintento de publicación

La publicación temporal conserva las celdas anteriores en el búfer de la
diagonal. Si `save_diagonal` falla, se restauran esas celdas; las diagonales
guardadas antes permanecen intactas. Un reintento vuelve a entrenar y guardar la
diagonal fallida. Los callbacks deben admitir reemplazar sus salidas anteriores.
La recuperación desde un proceso nuevo corresponde a J08.

La prueba nueva provocó doce fallos antes de corregir el código. Cubre uno y dos
trabajadores, metadatos de celdas no listas, conservación de la diagonal superior,
reintento efectivo, ausencia de repetición tras éxito y equivalencia final con un
entrenamiento sin fallo. La batería `make -j4 test` pasó después de la corrección.

[Rendimiento](review-performance/J03.json): 45 celdas desde 35:35, 1.000
iteraciones y 10.000 muestras por celda. Archivos idénticos en todas las
ejecuciones. Con un trabajador: 3,8302 → 3,8598 s (+0,77 %). En tres pares con
cuatro trabajadores: 2,2100 → 2,1856 s (−1,10 %), RSS 88.288 → 88.364 KiB
(+0,09 %). Sin regresión grave; estas pequeñas diferencias no demuestran una
aceleración. No cambia la ruta de entrenamiento de los otros tres formatos.

Revisión Orca: tarea `task_435ad0808077`, dispatch `ctx_f0247b89d0bb`.
Claude: **APPROVE**, mensaje `msg_56e843db31f9`. Se corrigieron los detalles
de sangría, redacción del contrato e identificador del dispatch indicados por
el revisor. La prueba adicional de ampliación con fallo queda como sugerencia
opcional; la restauración no depende de la modalidad de entrenamiento.
