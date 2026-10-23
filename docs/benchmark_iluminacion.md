# Medición reproducible de la calidad de iluminación

Se incorpora `tfm_lighting_benchmark` para comparar configuraciones del renderizador mediante secuencias de longitud fija. Se conserva la cámara estática durante cada experimento. El primer preset compara ruido blanco PCG y ruido azul STBN con dos valores de alfa temporal.

## Compilación y ejecución

Se ejecutan estos comandos desde la raíz del repositorio. Se requiere un contexto OpenGL 4.6, incluso con la ventana oculta.

```powershell
& 'C:/Program Files/CMake/bin/cmake.exe' -S . -B build -DTFM_BUILD_BENCHMARKS=ON -DTFM_BUILD_RENDER_TESTS=ON
& 'C:/Program Files/CMake/bin/cmake.exe' --build build --config Release --parallel 4
& 'C:/Program Files/CMake/bin/ctest.exe' --test-dir build -C Release --output-on-failure

./build/Release/tfm_lighting_benchmark.exe --config tests/benchmarks/blue_vs_white.json --output build/benchmarks/comparison_01
python tools/compare_lighting.py build/benchmarks/comparison_01
```

Se exige un directorio de salida nuevo para evitar sobrescrituras. Los resultados válidos contienen `manifest.json` con `status: "complete"`. Un proceso interrumpido puede dejar resultados parciales, que no se consideran válidos.

Se puede sustituir únicamente el número de fotogramas medidos sin editar el archivo:

```powershell
./build/Release/tfm_lighting_benchmark.exe --config tests/benchmarks/blue_vs_white.json --output build/benchmarks/comparison_02 --frames 512
python tools/compare_lighting.py build/benchmarks/comparison_02 --baseline white_alpha015
python tools/compare_lighting.py build/benchmarks/comparison_02 --stage raw
python tools/compare_lighting.py build/benchmarks/comparison_02 --stage temporal
```

La opción `--baseline` compara todos los casos con el caso indicado. Para aislar el efecto del muestreador, se comparan casos con los demás parámetros iguales. Un porcentaje negativo representa una reducción de la métrica.

## Configuración del experimento

Se utiliza JSON. Se rechazan claves desconocidas, valores fuera de rango, nombres duplicados y semillas repetidas. Las escenas se numeran desde 1, como en la interfaz del renderizador. No se importan cambios de cámara, luces o materiales realizados en otra instancia de la aplicación.

Tabla 1: Parámetros generales del experimento.

| Parámetro | Valor del primer preset | Significado |
| :--- | ---: | :--- |
| `scene` | 5 | Escena predefinida: habitación SDF con ventana. |
| `width`, `height` | 192, 108 | Resolución real de renderizado. |
| `frames` | 256 | Fotogramas medidos por caso y semilla; mínimo 2. |
| `warmup_frames` | 64 | Fotogramas previos excluidos de todas las métricas. |
| `reference_frames` | 2048 | Fotogramas PCG sin desruidado que se promedian; cantidad par. |
| `reference_seed` | 104729 | Semilla reservada para la referencia. |
| `seeds` | `[1, 17, 53]` | Repeticiones de cada caso. |
| `ray_bounces` | 3 | Valor del control de rebotes del renderizador, común a todos los casos. |
| `mask` | `"surfaces"` | Se excluye el cielo; `"all"` incluye todos los píxeles. |
| `roi` | Imagen completa | Región opcional `[x, y, width, height]`, desde la esquina superior izquierda. |

Se define cada configuración dentro de `cases`. El valor de `name` identifica el caso; admite letras, números, guiones y guiones bajos. Se heredan los valores siguientes cuando se omiten parámetros.

Tabla 2: Parámetros de cada caso.

| Parámetro | Valor predeterminado | Rango o valores |
| :--- | ---: | :--- |
| `noise` | `"white"` | `"white"`, `"blue"` |
| `temporal_alpha` | 0.15 | 0–1 |
| `max_history_frames` | 32 | 1–2048 |
| `atrous_iterations` | 4 | 0–5 |
| `phi_normal` | 32 | 0.01–10000 |
| `phi_depth` | 0.1 | 0.00001–10000 |
| `phi_luminance` | 4 | 0.00001–10000 |
| `demodulate_albedo` | `false` | Booleano |

El alfa efectivo cumple `max(temporal_alpha, 1/history_length)`. Con un historial máximo de 32 fotogramas, no baja de 0.03125. Si se aumenta el historial, también se debe revisar el calentamiento. Para medir el arranque del acumulador, se establece `warmup_frames: 0`.

Se limita `max_history_frames` a 2048 porque la textura de historial utiliza precisión de 16 bits. Ese formato deja de representar todos los enteros consecutivos a partir de ese punto.

Se conservan la escena, los materiales, la iluminación, el transporte y la cámara para todos los casos. Se fija el tiempo de simulación a cero. Se desactivan el antialiasing de las muestras y FXAA. Cada caso reinicia tanto el historial como la secuencia de muestras. Se alterna el orden de los casos entre repeticiones para reducir efectos sistemáticos sobre los tiempos.

Se requiere un experimento separado para cambiar la escena, la resolución o los rebotes, con una referencia nueva. La resolución reducida del preset permite una primera evaluación rápida. No permite extrapolar la calidad ni los tiempos a 960 × 540: el tamaño efectivo del filtro depende de la resolución.

## Señal medida y referencia

Se leen tres señales RGB lineales directamente de las texturas GPU:

1. `raw`: iluminación de un fotograma antes de SVGF.
2. `temporal`: resultado de la acumulación temporal, antes del filtrado espacial del fotograma actual.
3. `filtered`: resultado de la última iteración espacial, antes del contraste, gamma y FXAA.

Con cero iteraciones espaciales, `temporal` y `filtered` coinciden. El historial temporal del renderizador contiene la primera iteración espacial del fotograma anterior. Por ello, `temporal` no constituye una acumulación puramente temporal cuando se activa el filtrado espacial.

Si se activa la demodulación, se multiplica la señal por el albedo, igual que en la resolución final. Las métricas incluyen así cualquier diferencia causada por ese procedimiento. Se conserva también el comportamiento existente con albedos menores de 0.05; no se modifica para favorecer la comparación.

La referencia se calcula mediante el promedio en doble precisión de `reference_frames` imágenes `raw` PCG. Se emplea una semilla distinta de las utilizadas por los casos. No se usa el acumulador estático de la aplicación, porque incluye contraste y recorte antes de acumular.

Esta referencia aproxima la salida media del renderizador actual. Conserva sus aproximaciones de transporte, el formato de las texturas GPU y el límite de radiancia de 4 para supresión de valores extremos. No se considera una solución física exacta ni una referencia independiente de la implementación PCG.

Se divide la referencia en fotogramas pares e impares. Se registra el RMSE entre ambos promedios (`reference_split_rmse`). La mitad de ese valor (`reference_estimated_rmse`) estima el ruido de la referencia completa si las muestras son independientes y no sesgadas. Esta estimación no detecta errores compartidos de transporte ni demuestra la independencia del generador. Se aumenta `reference_frames` cuando las diferencias entre métodos son pequeñas respecto a esta incertidumbre.

## Métricas

Se define el error como imagen evaluada menos referencia. Se promedian únicamente los píxeles de la máscara, idéntica en todos los casos. Se utilizan los pesos de luminancia lineal `(0.2126, 0.7152, 0.0722)`. No se recorta la señal antes de medirla. Los valores RGB no finitos invalidan el experimento, incluso fuera de la máscara.

Tabla 3: Definición e interpretación de las métricas.

| Métrica | Cálculo | Interpretación |
| :--- | :--- | :--- |
| `rmse` | Raíz del error cuadrático medio de RGB, píxeles y fotogramas. | Menor error global frente a la referencia. |
| `mae` | Error absoluto medio de RGB, píxeles y fotogramas. | Menor sensibilidad a errores extremos que el RMSE. |
| `relative_rmse` | RMSE dividido por la raíz de la potencia media RGB de la referencia. | Error normalizado por la magnitud de la señal. |
| `psnr_db_peak4` | `10 log10(4² / MSE)`. | Mayor valor indica menos error; se declara explícitamente el pico 4. |
| `luminance_bias` | Media del error de luminancia con signo. | Positivo indica exceso de luz; negativo indica pérdida. |
| `frame_rmse_mean` | Media aritmética de los RMSE individuales. | Se distingue del RMSE agrupado de toda la secuencia. |
| `flicker_rms` | Raíz de la media de `(Y[t] - Y[t-1])²` sobre píxeles y transiciones medidas. | Variación entre fotogramas; no incluye la transición desde el calentamiento. |
| `temporal_std` | Raíz de la media espacial de las varianzas temporales por píxel. | Variación durante toda la secuencia; se usa varianza poblacional. |
| `mean_image_rmse` | RMSE de la imagen media de la secuencia frente a la referencia. | Error persistente, que puede incluir sesgo, suavizado y ruido residual. |
| `gpu_ms_mean`, `gpu_ms_p50`, `gpu_ms_p95` | Consulta GPU `GL_TIME_ELAPSED` alrededor de `render`. | Coste de los pases del renderizador, sin lectura de imágenes, métricas ni escritura. |

Se registra `null` para PSNR con error cero y para error relativo con referencia negra. En CSV, los valores no definidos se dejan vacíos. El primer fotograma tampoco tiene una diferencia temporal definida. La dispersión entre repeticiones usa la desviación estándar muestral; se deja sin definir con una única repetición. No se interpreta esta dispersión como intervalo de confianza.

La lectura sincronizada de las imágenes serializa el trabajo. Por tanto, los tiempos GPU no equivalen a los FPS interactivos. Se incluyen los pases de resolución y copia final, aunque las métricas se calculan antes de ellos. Se deben cerrar otras cargas GPU para comparaciones de rendimiento precisas.

## Archivos generados

- `config.json`: configuración completa con los valores predeterminados resueltos.
- `manifest.json`: estado, escena, cámara, GPU, versión OpenGL, máscara, incertidumbre de referencia y huellas FNV-1a de ejecutable, shaders y recursos.
- `frames.csv`: métricas por caso, semilla, fotograma y etapa; el tiempo GPU se repite en las tres etapas del mismo fotograma.
- `results.json`: métricas por repetición y agregados por caso y etapa.
- `results.partial.json`: punto de control provisional; no sustituye a los resultados completos.
- `summary.csv`: media y desviación entre repeticiones para cada métrica, caso y etapa.
- `reference.pfm`, `reference.png`: referencia lineal y vista previa.
- `mask.pfm`, `mask.png`: máscara efectiva.
- `<case>_seed<N>/`: imágenes final y media de cada etapa, en PFM y PNG.

Los PFM conservan RGB lineal con flotantes de 32 bits, orden de filas de abajo arriba y formato little-endian. Los PNG aplican únicamente la función sRGB y un recorte a `[0, 1]`, con exposición fija. No reproducen el contraste de la interfaz y no se usan para calcular las métricas.

Las huellas identifican los archivos empleados, pero no constituyen firmas criptográficas. Se comprueba que no cambian durante el experimento. La información de las escenas predefinidas forma parte del ejecutable registrado. Se conserva el registro de consola cuando se necesita diagnosticar un fallo de OpenGL.

## Interpretación de la comparación PCG–STBN

Se comparan primero los casos con el mismo alfa. Se examinan conjuntamente error, variación temporal e imágenes guardadas: una imagen excesivamente difuminada puede presentar poco parpadeo y conservar un error elevado.

El volumen STBN actual tiene 64 capas temporales y repite su secuencia cada 64 fotogramas con cámara fija. Las semillas seleccionan desplazamientos espaciales y fases temporales del mismo volumen; no generan volúmenes independientes. La media STBN de 256 fotogramas contiene cuatro ciclos, por lo que no equivale a 256 muestras temporales independientes. Esta limitación pertenece al método medido y se conserva en el experimento.

El preset cubre cuatro ciclos completos y tres desplazamientos. Se pueden usar más semillas, aunque algunas pueden seleccionar el mismo desplazamiento y fase por la periodicidad finita. La independencia estadística entre repeticiones no queda garantizada.

Esta primera versión mide escenas estáticas. No cuantifica arrastre, desoclusiones, respuesta a cambios de luz ni movimientos de cámara. Esas situaciones requieren trayectorias reproducibles y referencias por fotograma; no se infiere su calidad a partir de estas métricas estáticas.

## Verificación

Se incluyen pruebas CPU con señales sintéticas de error constante, sesgo y alternancia temporal. Se verifican también la máscara vacía y los valores no finitos. Las pruebas GPU comprueban la lectura lineal, el avance de las muestras, el reinicio reproducible y el cambio de semilla para ambos modos.

Cuando CMake detecta Python, se registra `lighting_benchmark_smoke` en CTest. Esta prueba ejecuta dos experimentos pequeños y exige igualdad exacta de las métricas de calidad y de los PFM. También verifica la configuración estricta, la cantidad de filas, la equivalencia temporal/filtrada sin iteraciones espaciales y la protección contra sobrescritura. Los tiempos GPU quedan excluidos de la comparación determinista.
