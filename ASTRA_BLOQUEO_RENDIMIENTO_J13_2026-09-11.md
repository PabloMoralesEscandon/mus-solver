# J13 retirado por el límite de rendimiento

Fecha: 2026-09-11. Base: `f723699` (J12). **El cambio J13 no está aplicado.**
Se conserva el [parche completo](docs/review-performance/J13-blocked.patch) y
las [mediciones reproducibles](docs/review-performance/J13-blocked.json).

## Qué corregía

El binario reconoce cuándo un nodo nunca acumuló datos y busca otra respuesta.
El TXT actual pierde esa información: publica uniforme y el bot puede tomarla
como una estrategia entrenada. La misma política guardada en ambos formatos
puede producir decisiones distintas.

El candidato exportaba TXT v2 con `available=0|1`, de modo que los nodos sin
masa activasen el mismo respaldo que el binario. Conservaba el lector v1 para
archivos anteriores. Añadía un pie de finalización, comprobable leyendo como
máximo 128 bytes, para detectar pérdidas del final incluso de filas completas.
No alteraba C-CFR ni el simulador ni el esquema de checkpoints.

Claude lo aprobó: tarea `task_28ef84a839e1`, dispatch `ctx_2f85833c6973`, mensaje
`msg_37ab196ec5ac`; [revisión](docs/review-performance/J13-claude.md).
Pasan la batería completa, el núcleo y CLI con uno/dos muses, y las pruebas de
núcleo, adaptador, disponibilidad y partidas con ASan/UBSan. Se comprueban nodos
vacíos, ausentes, uniformes entrenados y no uniformes, tres backends, selector
híbrido, corrupción y equivalencia de partidas entre binario y TXT v2.
Se verificaron además las nuevas marcas y el pie en `single-bet` y
`raises-to-eight`. Los TXT antiguos conservaban sus limitaciones anteriores.

## Coste medido, alrededor del límite

Se compararon los ejecutables anteriores y candidatos con políticas congeladas
de 500 iteraciones, semilla 7, CPU0, sin otras compilaciones/pruebas propias.
El rival de control `three` pide un mus y usa incrementos de tres. No se afirma
convergencia ni fuerza competitiva de esas políticas.

La primera comparación de 10.000 partidas desde 0:0 con `ordago-public`
dio **+19,67 %**, muy cerca del límite de 20 %. Se hicieron cinco pares
alternados adicionales. La regla automática de tres pares no se activó;
repetir fue una decisión adicional por la proximidad al límite, no una
consecuencia de haber medido ya más del 20 %.

El efecto está **alrededor del 20 %**, no claramente separado de ese umbral:
la razón de medianas de los cinco pares adicionales da +21,29 %; la de las
ocho parejas juntas, +20,03 %; y la mediana de las razones por pareja, +19,32 %.
Una pareja tuvo tiempos anormalmente altos (2,4248 s en el anterior y 1,8412 s
en el candidato); se conserva en los datos. Son los máximos de cada grupo en
la confirmación. La decisión conservadora es retirar el candidato y consultar
al usuario, sin presentar 21,29 % como una penalización exacta universal.


| Medida, medianas de los cinco pares | TXT v1 anterior | TXT v2 corregido | Cambio |
|---|---:|---:|---:|
| Tiempo total de 10.000 partidas | 1,3423 s | 1,6281 s | **+21,29 %** |
| Solo juego, sin cargar ni emitir JSON | 1,3406 s | 1,6262 s | **+21,31 %** |
| Pico de RSS del proceso | 2.888 KiB | 2.920 KiB | +1,11 % |

Es el coste de completar esas partidas. **No es una pérdida del 21 % en el
entrenamiento ni una medición de igual número de consultas.** Al cambiar las
respuestas, las partidas también cambian: se juegan 19.094 → 20.777 manos y
179.746 → 193.713 decisiones totales. En la política A, los respaldos pasan de
19.385 a 29.509; los exactos bajan de 42.566 a 40.792.

Los datos son compatibles con dos costes: resolver más consultas sin datos
válidos y jugar más manos tras las nuevas decisiones. Esta medición conjunta
no separa causalmente cada coste ni mide el tiempo de una consulta aislada.
No implica explorar más ramas durante el entrenamiento CFR.

El control que conserva el **mismo TXT v1** en ambos ejecutables no muestra
pérdida: −0,88 % con la genérica pública y −0,55 % con la histórica. La genérica
histórica usando el TXT v2 mostró +11,61 % por 1.000 partidas, en tres pares.
Estas son muestras de una carga concreta, no una penalización universal.

## Entrenamiento y estado

El entrenamiento público de 500 iteraciones, incluida la escritura del
checkpoint, dio 9,4700 → 9,3421 s (−1,35 %). La tabla de 45 celdas desde 35:35,
con 1.000 iteraciones y 10.000 muestras por celda, dio 3,5475 → 3,6569 s
(+3,09 %). Son tres pares alternados; checkpoints y valores son idénticos.
Ninguno activa el límite. No se atribuye una mejora a esas fluctuaciones.

Se ha retirado J13 ante un coste en torno al límite: la corrección funcional
y la aprobación de Claude no eximen de consultar al usuario en este caso. El parche se comprobó con `git apply
--check` contra `f723699`. Los builds candidatos y datos de ensayo se conservan
como referencias; el código del repositorio vuelve al estado anterior a J13.
La medición adicional del coste de exportación queda pendiente al detenerse
por este límite; no se presenta como realizada.

J12 permanece aplicado en `f723699`: añade el evaluador de partidas reales y
no modifica los binarios de entrenamiento o del bot anteriores. J01 mantiene
la corrección cuyo coste ya aceptó el usuario. La sincronización de J17 sigue
pendiente de su decisión anterior.

Quedan J13, J18, J19, J20, J21, J22 y J23, además de la parte pendiente de J17.
Se espera la decisión del usuario antes de restaurar J13 o continuar con los
siguientes cambios. No se ha hecho commit del código candidato.

Claude verificó también este informe: tarea `task_186ec2467f47`, dispatch
`ctx_5d9b7195b430`, mensaje `msg_14a555c99e52`. Se incorporaron sus dos
correcciones sobre el carácter adicional de la confirmación y la variación
entre estimadores, incluida la pareja anormal. Los datos no se modificaron;
`decision` y `patch_sha256` se añadieron al JSON después de medir. La
[revisión de las cifras](docs/review-performance/J13-claude-gate.md) conserva
su cálculo independiente.
