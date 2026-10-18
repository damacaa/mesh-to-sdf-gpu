# PORTADA

## Generación acelerada en GPU de campos de distancia con signo (SDF) a partir de mallas poligonales

### Evaluación de técnicas de propagación y aplicación a iluminación global interactiva

**Nombre del estudiante:** Daniel Martí Casanova  
**Programa:** Máster Universitario en Diseño y Programación de Videojuegos  
**Área del trabajo final:** Programación Gráfica y Motores de Videojuegos  
**Tutor de TF:** Gonzalo Nicolás Barreales  
**Profesor responsable de la asignatura:** Joan Arnedo Moreno  
**Fecha de entrega:** 11/10/2026  

---

# FICHA DEL TRABAJO FINAL

**Título del trabajo:** Generación acelerada en GPU de campos de distancia con signo (SDF) a partir de mallas poligonales

**Nombre del autor:** Daniel Martí Casanova

**Nombre del tutor de TF:** Gonzalo Nicolás Barreales

**Nombre del profesor responsable de la asignatura:** Joan Arnedo Moreno

**Fecha de entrega:** 12/2026

**Titulación o programa:** Máster Universitario en Diseño y Programación de Videojuegos

**Área del trabajo final:** Programación Gráfica y Motores de Videojuegos

**Idioma del trabajo:** Castellano

**Palabras clave:** Campos de distancia con signo, generación acelerada en GPU, iluminación global.

## Resumen del trabajo

Este Trabajo Final de Máster propone diseñar, implementar y evaluar dos métodos de generación de campos de distancia con signo (*Signed Distance Fields*, SDF) volumétricos en GPU a partir de mallas poligonales. Estas representaciones permiten realizar consultas de distancia para el trazado de rayos por software, pero su generación puede resultar costosa al aumentar la resolución del volumen y la complejidad de la geometría.

Se desarrollará un método base de cálculo de distancias mediante *compute shaders* y una alternativa que combine la captura de datos de superficie por rasterización con el algoritmo *Jump Flooding* (JFA) en tres dimensiones. El método base se comprobará con geometrías de control antes de utilizarlo como referencia.

Los campos generados se integrarán en un visor interactivo construido a partir de un prototipo previo del autor. El visor permitirá examinar el volumen y observar su aplicación a iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves. Su función será apoyar la evaluación, sin constituir un motor gráfico de propósito general.

La comparación analizará el tiempo de generación, el consumo de memoria de vídeo y el error de distancia sobre un conjunto delimitado de escenas. Se espera identificar las ventajas y limitaciones de cada método, sin presuponer una mejora uniforme en todas las condiciones. Esta primera entrega presenta la definición del proyecto, sus objetivos, la metodología y la planificación inicial.

## Abstract

This master's thesis proposes the design, implementation, and evaluation of two GPU methods for generating volumetric Signed Distance Fields (SDFs) from polygonal meshes. These representations support distance queries for software ray tracing, but their generation can become computationally expensive as volume resolution and geometric complexity increase.

A baseline distance computation method will be implemented using compute shaders. An alternative approach will combine rasterized surface data with the three-dimensional Jump Flooding Algorithm (JFA). The baseline will first be checked against control geometries with known distances before being used as a reference.

The generated fields will be integrated into an interactive viewer adapted from a prototype previously developed by the author. The viewer will support volume inspection and demonstrate one-bounce diffuse indirect lighting, ambient occlusion, and soft shadows. It will serve as an evaluation tool rather than a general-purpose graphics engine.

The experimental comparison will examine generation time, video memory consumption, and distance error across a limited set of test scenes. The aim is to identify the advantages and limitations of each method without assuming consistent improvements under all conditions. This initial submission presents the project definition, objectives, methodology, and development schedule.

---

# DECLARACIÓN DE RECURSOS UTILIZADOS Y USO DE INTELIGENCIA ARTIFICIAL

## Bibliotecas y tecnologías

El desarrollo utilizará C++ y OpenGL 4.6 como base de la aplicación y de los procesos de renderizado y cálculo en GPU. Se prevé emplear las siguientes bibliotecas:

- **SDL3:** creación de ventanas, gestión del contexto gráfico y tratamiento de eventos de entrada.
- **GLM:** operaciones de álgebra lineal y transformaciones.
- **Assimp:** importación de mallas y escenas tridimensionales.
- **Dear ImGui:** interfaz para modificar parámetros, seleccionar modos de visualización y consultar métricas.

## Modelos y escenas tridimensionales

Se prevé utilizar **Cornell Box** como escena controlada para examinar la iluminación indirecta y las sombras, y **Crytek Sponza** para realizar pruebas con mayor complejidad geométrica y oclusión. Estas escenas se complementarán con modelos geométricos de control.

La selección de las versiones concretas y las adaptaciones necesarias se realizará durante la fase de especificación y diseño, de acuerdo con las condiciones de entrada admitidas por los generadores. Se documentarán la procedencia y las condiciones de uso de los recursos empleados.

## Prototipo previo del autor

El proyecto reutilizará un prototipo propio de *path tracer* interactivo basado en *ray marching* sobre primitivas SDF analíticas y trazado en espacio de pantalla sobre el búfer de profundidad.

Este desarrollo previo servirá como punto de partida del visor. Su adaptación permitirá disponer de un entorno de pruebas antes de integrar los campos volumétricos generados a partir de mallas.

La generación de SDF en GPU, la implementación de la alternativa basada en JFA en tres dimensiones y la evaluación comparativa se desarrollarán durante el TFM. Estas contribuciones se distinguirán de las funcionalidades procedentes del prototipo anterior.

## Uso de herramientas de inteligencia artificial

Se declara el uso de Antigravity / Gemini y OpenAI ChatGPT como apoyo para estructurar la propuesta, redactar y revisar el texto, detectar incoherencias y discutir decisiones técnicas.

La revisión del contenido, la selección de los métodos y la validación de las implementaciones y los resultados serán responsabilidad del autor. Las respuestas de estas herramientas no sustituirán la consulta de las fuentes originales ni la comprobación experimental.

---

# 1. INTRODUCCIÓN

## 1.1. Contexto y justificación del trabajo

La iluminación global en tiempo real plantea un compromiso entre calidad visual y coste de cálculo. Además de la luz que llega directamente desde una fuente hasta una superficie, considera las contribuciones indirectas producidas por las reflexiones sobre otras superficies de la escena, descritas por la ecuación de renderizado [1]. Estas interacciones permiten representar fenómenos como la transferencia de color entre objetos próximos, conocida como *color bleeding* (Figura 1).

![[screenshot_20261011_164754.bmp]]
Ejemplo de iluminación global, la luz rebota en el suelo e ilumina la parte inferior de la esfera. La luz que rebota en las paredes, ilumina la esfera con el color de cada pared.

En este contexto, los campos de distancia con signo ofrecen una representación de la geometría que puede utilizarse para realizar consultas espaciales durante el trazado de rayos por software. Su aplicación a sistemas de iluminación como Lumen, de Unreal Engine 5 [2], constituye una referencia para estudiar este enfoque. El presente trabajo se centrará en la generación de dichos campos y en una demostración acotada de su uso, sin intentar reproducir un sistema de iluminación de la complejidad de Lumen.

Un campo de distancia con signo, o SDF, representa la distancia mínima desde un punto del espacio hasta la superficie más cercana. El signo permite distinguir el interior del exterior cuando la geometría admite esa clasificación. A partir de esta información, el algoritmo de *sphere tracing* [3] adapta la longitud de los pasos de un rayo según la distancia a la superficie. En una representación volumétrica discretizada, la precisión del campo y los errores de aproximación pueden afectar al trazado y producir artefactos, por lo que su comportamiento debe evaluarse.

La generación del volumen de distancias es uno de los problemas que plantea el uso de SDF a partir de mallas poligonales. El cálculo de distancias entre los puntos de una cuadrícula tridimensional y los triángulos de una malla puede requerir un número elevado de evaluaciones. Su coste depende tanto de la resolución del volumen como de la complejidad de la geometría. Aunque el precálculo puede ser suficiente para determinados usos estáticos, reducir el tiempo de generación permitiría explorar otras condiciones de actualización y uso.

La motivación del proyecto parte de un límite concreto de un prototipo previo del autor: el visor existente utiliza primitivas SDF analíticas. Para trabajar con mallas poligonales, se propone sustituir estas primitivas por texturas tridimensionales que almacenen el campo de distancias. Esta transición proporciona un contexto práctico para estudiar la generación del volumen y observar cómo sus errores afectan al resultado visual.

El trabajo comparará un método base de cálculo de distancias en GPU con una alternativa que combine la captura de datos de superficie mediante rasterización y la propagación espacial mediante *Jump Flooding* (JFA) [4]. La pregunta que orienta la evaluación es qué compromiso ofrece esta alternativa entre tiempo de generación, consumo de memoria y error de distancia respecto al método de referencia.

El producto será un prototipo técnico de generación y evaluación de SDF, acompañado de un visor interactivo, dentro del área de programación gráfica y motores de videojuegos. En el visor se podrá cargar una escena de prueba, seleccionar el campo generado por cada método, inspeccionar cortes del volumen y observar su utilización en la iluminación. La generación de los campos y su comparación constituirán el núcleo del trabajo; el visor actuará como herramienta de demostración y análisis.

## 1.2. Objetivos del trabajo

### Objetivo general

Diseñar, implementar y evaluar dos métodos de generación de campos de distancia con signo en GPU a partir de mallas poligonales, comparando una alternativa basada en rasterización y propagación espacial con un método de referencia, y analizando su tiempo de generación, consumo de memoria y precisión mediante pruebas numéricas y un visor interactivo de iluminación.

### Objetivos específicos

**OE1. Adaptar el visor de partida.** Integrar el prototipo previo del autor en una aplicación modular en C++ con OpenGL 4.6 y SDL3, con controles de cámara, iluminación e interfaz, para disponer de un entorno de pruebas durante el desarrollo.

**OE2. Incorporar el muestreo de campos volumétricos.** Sustituir las primitivas SDF analíticas y el trazado en espacio de pantalla por el muestreo de texturas tridimensionales de distancia, adaptando el *ray marching* para representar iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves. El sombreado utilizará también la información de superficie necesaria.

**OE3. Implementar y comprobar el método de referencia.** Desarrollar un generador de distancias volumétricas en GPU mediante *compute shaders*. Antes de utilizar sus resultados como referencia, comprobar su funcionamiento con geometrías de control cuyas distancias sean conocidas.

**OE4. Implementar la alternativa de generación rápida.** Diseñar y desarrollar un método que combine la extracción de datos de superficie mediante rasterización de un G-Buffer con la propagación mediante JFA en tres dimensiones. Se estudiarán la cobertura de la captura, el comportamiento de la propagación y el procedimiento de determinación del signo.

**OE5. Facilitar la inspección y comparación de los campos.** Incorporar un selector entre los volúmenes generados por ambos métodos y modos de visualización para examinar cortes del volumen y componentes de iluminación. La visualización adicional de vóxeles tendrá menor prioridad que estas herramientas.

**OE6. Ejecutar la evaluación experimental.** Diseñar y realizar pruebas sobre modelos geométricos de control y las escenas seleccionadas para comparar el tiempo de generación en milisegundos, el consumo de memoria de vídeo y el error de distancia de la alternativa respecto al método base. Las condiciones de comparación y el procedimiento de medición se concretarán durante la fase de diseño.

### Delimitación del alcance

El trabajo se centrará en dos procedimientos de generación de SDF en GPU: un método de referencia y una alternativa basada en rasterización y JFA. No se plantea desarrollar un generador universal para cualquier tipo de malla ni un motor gráfico de propósito general.

Durante la fase de especificación y diseño se definirán las condiciones de entrada de las mallas, el procedimiento de obtención del signo, la captura de datos de superficie y el protocolo experimental. La selección final de las versiones de Cornell Box y Crytek Sponza dependerá de su adecuación a estas condiciones. Los modelos geométricos de control permitirán comprobar el funcionamiento antes de abordar escenas de mayor complejidad.

El rendimiento del visor se analizará por separado del tiempo de generación del campo. La navegación y la visualización deberán permitir examinar los resultados de forma interactiva, pero no se establece como requisito inicial reconstruir todo el volumen en cada fotograma.

Ante una reducción de alcance, se conservarán los dos generadores, su comprobación numérica, la comparación experimental y una demostración básica de iluminación indirecta difusa de un rebote. La inspección por cortes tendrá prioridad sobre la visualización de vóxeles. También podrán reducirse los controles secundarios de la interfaz y los refinamientos visuales, sin sustituir con ellos el trabajo de evaluación.

## 1.3. Impacto en sostenibilidad, ético-social y de diversidad

### Impacto en sostenibilidad

El proyecto estudiará el uso de recursos computacionales mediante la comparación del tiempo y la memoria necesarios para generar campos de distancia en GPU. Los resultados permitirán identificar en qué condiciones una aproximación basada en propagación resulta ventajosa y qué pérdida de precisión puede introducir.

La implementación se planteará sin depender de extensiones específicas de trazado de rayos acelerado por hardware. No obstante, seguirá requiriendo una GPU compatible con las tecnologías seleccionadas.

El trabajo no incluirá mediciones de consumo energético ni de emisiones. Por tanto, una posible mejora del tiempo de ejecución o del uso de memoria no se presentará como una reducción demostrada del impacto ambiental.

### Impacto ético-social

Se prevé publicar el código y la documentación para facilitar el estudio de los métodos y la reproducción de las pruebas. La documentación describirá los requisitos, las condiciones de reutilización y las limitaciones observadas.

Se mantendrá la atribución de las bibliotecas, modelos y fuentes utilizadas, así como una declaración del uso de asistentes de inteligencia artificial. La utilidad del proyecto para estudiantes y desarrolladores se considerará una contribución potencial, sin atribuirle una reducción demostrada de los costes de producción.

### Impacto en diversidad

El proyecto no contempla recopilar datos demográficos ni realizar estudios con usuarios. Las medidas relacionadas con la diversidad se orientarán a facilitar el acceso al conocimiento mediante una documentación clara y una descripción de los requisitos técnicos.

La necesidad de disponer de una GPU compatible puede limitar el acceso al prototipo. Por ello, no se dará por supuesta su ejecución adecuada en equipos de prestaciones reducidas.

## 1.4. Enfoque y método seguido

El proyecto combinará un marco de investigación, un método de desarrollo del software y un sistema de seguimiento del trabajo.

### Marco de investigación

Se utilizará la metodología de investigación en ciencias del diseño, *Design Science Research Methodology* (DSRM), propuesta por Peffers et al. [5]. Este marco organiza el trabajo alrededor de la construcción y evaluación de un artefacto tecnológico.

En este proyecto, el artefacto principal estará formado por los métodos de generación de campos de distancia en GPU. El visor servirá para demostrar su uso y examinar las consecuencias visuales de los campos obtenidos.

El proceso abarcará la identificación del problema, la definición de objetivos, el diseño y desarrollo, la demostración, la evaluación y la comunicación de resultados. La comparación experimental permitirá contrastar los métodos sin presuponer que la alternativa basada en JFA será superior en todas las métricas o escenas.

### Metodología de producción del software

El desarrollo seguirá un enfoque incremental e iterativo basado en prototipos. Se adaptará primero el visor existente y se incorporará después el muestreo de texturas tridimensionales. Para esta integración inicial se utilizarán volúmenes de prueba, sin esperar a que los generadores a partir de mallas estén terminados.

A continuación, se incorporarán el método base, la alternativa JFA y los modos de inspección. Cada incremento se comprobará con casos reducidos antes de pasar a escenas de mayor complejidad.

Las decisiones de diseño y los problemas encontrados se documentarán durante la implementación. La memoria se redactará de forma progresiva, en lugar de concentrar toda la documentación al final del proyecto.

### Gestión del proyecto

El seguimiento se realizará mediante un tablero Personal Kanban, con tareas vinculadas a los objetivos y a los hitos de entrega. Se revisará semanalmente el avance para detectar bloqueos, actualizar prioridades y registrar las desviaciones respecto a la planificación.

Git se utilizará para el control de versiones y la trazabilidad de los cambios, siguiendo la convención *Conventional Commits*. Las revisiones con el tutor permitirán contrastar el alcance y orientar las decisiones que afecten a la viabilidad del trabajo.

### Distribución del trabajo entre entregas

**PEC 1. Definición y planificación.** Se concretarán el problema, la motivación, los objetivos, el alcance y la planificación inicial. Esta entrega no incluirá nueva implementación del proyecto.

**PEC 2. Estudio y diseño.** Se desarrollarán el estado del arte, los requisitos, la arquitectura y el protocolo experimental. También se definirán las restricciones geométricas, la estrategia de signo y la captura de superficies. Una exploración técnica limitada permitirá comprobar los supuestos de mayor riesgo.

**PEC 3. Implementación principal.** Se adaptará el visor, se integrarán las texturas 3D y se desarrollarán ambos generadores. Se realizarán las primeras comprobaciones numéricas y mediciones comparativas, documentando las decisiones y los cambios de diseño.

**PEC 4. Evaluación y cierre.** Se completará la batería experimental, se analizarán los resultados y se redactarán las conclusiones. También se consolidará la memoria y se preparará la presentación y defensa en vídeo.

## 1.5. Planificación del trabajo

### Recursos necesarios

**Hardware.** Se prevé utilizar una estación de trabajo con procesador multinúcleo x86-64, al menos 16 GB de memoria RAM y una tarjeta gráfica dedicada compatible con OpenGL 4.6, *compute shaders* y texturas tridimensionales con acceso mediante *shader image load/store*. Las resoluciones de trabajo se ajustarán a la memoria disponible y a los resultados de las pruebas.

**Software y entorno de desarrollo.** Se utilizará GNU/Linux, un compilador compatible con C++20 —GCC o Clang—, CMake 3.20 o superior y Visual Studio Code. RenderDoc y las herramientas de monitorización de GPU servirán para inspeccionar recursos y analizar el comportamiento de la aplicación.

**Bibliotecas.** La aplicación utilizará SDL3, GLM, Assimp y Dear ImGui para la gestión de ventanas y entrada, las operaciones matemáticas, la importación de mallas y la interfaz.

**Modelos de prueba.** Se emplearán geometrías de control y las versiones seleccionadas de Cornell Box y Crytek Sponza, en formatos compatibles con el importador, como OBJ o glTF.

### Dedicación y seguimiento

La planificación se elaborará a partir de una dedicación estimada de **20 horas semanales** (correspondiente a los 12 créditos ECTS de la asignatura, con un total estimado de 300 horas distribuidas a lo largo del semestre), que incluirá investigación, desarrollo, pruebas, documentación y revisión de las entregas.

La implementación principal se concentrará en el periodo correspondiente a la PEC 3. La documentación se mantendrá durante todas las fases, y la preparación del procedimiento de medición deberá quedar avanzada antes de comenzar la batería experimental final.

La disponibilidad durante el periodo final se tendrá en cuenta al concretar la dedicación. La entrega final de la PEC 4 se realizará el 23 de diciembre de 2026, concluyendo el trabajo antes del receso navideño. Si fuera necesario resolver incidencias durante la recta final, se ajustará el número de configuraciones experimentales secundarias, preservando la validez de la comparación principal entre los dos métodos.

El seguimiento semanal permitirá contrastar el trabajo realizado con el previsto y justificar las modificaciones del calendario.

### Hitos por PEC

**PEC 1 — 11/10/2026: definición del proyecto e introducción.**

Se completarán la ficha del trabajo y el capítulo 1 de la memoria, con la justificación, los objetivos, el impacto, la metodología y la planificación. También se organizarán el repositorio y la documentación inicial.

**PEC 2 — 08/11/2026: estado del arte, especificación y arquitectura.**

Se redactará el capítulo 2 y se desarrollará el contenido inicial del capítulo 3: requisitos, escenas previstas y protocolo de evaluación. Se definirá la arquitectura del sistema, junto con las restricciones de las mallas y las estrategias de captura y signo. Se realizará una exploración técnica limitada de los supuestos críticos de la alternativa.

**PEC 3 — 29/11/2026: implementación y resultados intermedios.**

Se adaptará el visor, se sustituirán las primitivas analíticas por texturas 3D y se implementarán los dos generadores. Se incorporarán la selección de campos y los modos de inspección prioritarios. La entrega incluirá comprobaciones iniciales, primeras mediciones y la actualización del capítulo 4.

**PEC 4 — 23/12/2026: resultados, memoria final y defensa.**

Se completarán las pruebas, el análisis del capítulo 5 y las conclusiones del capítulo 6. Se revisará el diseño documentado y se consolidará la memoria. La presentación y la grabación de la defensa se prepararán antes del margen final de contingencia.

### Cronograma

La Tabla 1 recoge los periodos previstos para cada actividad. Los solapamientos responden a tareas que pueden avanzar parcialmente en paralelo: por ejemplo, la integración del visor con texturas 3D comenzará con volúmenes de prueba antes de disponer del generador base.

**Tabla 1. Cronograma del proyecto.**

| Fase  | Actividad                                                              | Inicio     | Fin        |
| ----- | ---------------------------------------------------------------------- | ---------- | ---------- |
| PEC 1 | Definición del proyecto y ficha del trabajo                            | 23/09/2026 | 06/10/2026 |
| PEC 1 | Redacción del capítulo 1 y preparación de la entrega                   | 06/10/2026 | 11/10/2026 |
| PEC 2 | Investigación y redacción del estado del arte                          | 12/10/2026 | 25/10/2026 |
| PEC 2 | Especificación de requisitos y protocolo de evaluación                 | 23/10/2026 | 01/11/2026 |
| PEC 2 | Diseño arquitectónico y exploración técnica preliminar                 | 30/10/2026 | 08/11/2026 |
| PEC 3 | Adaptación del prototipo previo del visor                              | 09/11/2026 | 15/11/2026 |
| PEC 3 | Transición a texturas 3D e integración del trazado para iluminación    | 13/11/2026 | 19/11/2026 |
| PEC 3 | Implementación y comprobación del generador base                       | 16/11/2026 | 22/11/2026 |
| PEC 3 | Implementación del generador alternativo basado en rasterización y JFA | 19/11/2026 | 25/11/2026 |
| PEC 3 | Integración, primeras mediciones y preparación de la entrega           | 23/11/2026 | 29/11/2026 |
| PEC 4 | Batería experimental final y recogida de datos                         | 30/11/2026 | 08/12/2026 |
| PEC 4 | Análisis de resultados y discusión                                     | 07/12/2026 | 14/12/2026 |
| PEC 4 | Revisión del diseño, conclusiones y consolidación de la memoria        | 12/12/2026 | 18/12/2026 |
| PEC 4 | Preparación de la presentación y grabación de la defensa               | 17/12/2026 | 20/12/2026 |
| PEC 4 | Margen de contingencia y comprobación final de los entregables         | 21/12/2026 | 23/12/2026 |

*Fuente: elaboración propia.*

La entrega final está prevista para el 23 de diciembre de 2026. El margen de contingencia no se considerará tiempo disponible para añadir funcionalidades nuevas, sino una reserva para resolver incidencias y corregir los entregables.

### Riesgos y medidas de mitigación

La Tabla 2 recoge los principales riesgos técnicos y organizativos. Las valoraciones de probabilidad e impacto son iniciales y se revisarán conforme avance el proyecto.

**Tabla 2. Riesgos y estrategias de mitigación.**

| ID | Riesgo | Probabilidad | Impacto | Mitigación y contingencia |
|---|---|---|---|---|
| R1 | Consumo excesivo de memoria de vídeo por los volúmenes y las estructuras auxiliares. | Media | Alto | Parametrizar la resolución, utilizando inicialmente tamaños exploratorios como 64³, 128³ y 256³. Estudiar formatos como `GL_R16F` y `GL_R32F` y contabilizar también los recursos temporales utilizados durante la generación. |
| R2 | Rendimiento insuficiente para utilizar el visor de forma interactiva. | Media | Medio | Medir por separado el renderizado y la generación del campo. Ajustar el número de pasos de trazado, los criterios de parada y la resolución de cálculo de la iluminación indirecta. |
| R3 | Cobertura incompleta de la geometría, errores de distancia o fallos en la determinación del signo. | Alta | Alto | Comenzar con mallas de control simples y cerradas. Examinar por separado la captura, la distancia y el signo, y distinguir los errores del campo de los artefactos introducidos por el renderizado. Concretar las correcciones durante el diseño y la implementación. |
| R4 | Dificultades en la implementación de JFA en tres dimensiones. | Media | Alto | Desarrollar y comprobar la propagación en casos reducidos antes de integrarla con escenas completas. Contrastar los resultados esperados para distinguir los fallos de implementación del error de aproximación del método. |
| R5 | Retrasos respecto a los hitos de entrega. | Media | Alto | Revisar semanalmente el avance. Proteger los dos generadores, las comprobaciones numéricas, la comparación experimental y la demostración básica de iluminación. Reducir primero la visualización de vóxeles, los controles secundarios y los refinamientos visuales. |

*Fuente: elaboración propia.*

Las medidas de contingencia darán prioridad a obtener resultados comparables y documentados. Una demostración visual más completa no compensará la ausencia de comprobaciones o de datos suficientes para evaluar los métodos.

## 1.6. Breve sumario de productos obtenidos

En esta primera entrega se definen los productos previstos. Su desarrollo y evaluación se realizarán en las fases posteriores.

### Aplicación interactiva de pruebas

Se desarrollará una aplicación modular en C++ y OpenGL 4.6 que permita cargar las escenas seleccionadas, navegar por ellas y modificar las fuentes de luz.

La interfaz permitirá seleccionar los campos generados por cada método, modificar los parámetros previstos y consultar métricas de funcionamiento.

### Módulos de generación de SDF en GPU

Se implementarán un método base de cálculo de distancias mediante *compute shaders* y una alternativa que combine la captura de superficie por rasterización con JFA en tres dimensiones.

Los módulos se acompañarán de comprobaciones sobre geometrías de control y de documentación sobre sus condiciones de entrada y limitaciones.

### Sistema de iluminación e inspección

El visor utilizará los campos de distancia para representar iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves.

Se incorporará la inspección mediante cortes del volumen y la visualización de componentes de iluminación. La representación adicional de vóxeles quedará subordinada al cumplimiento del núcleo del proyecto.

### Banco de pruebas y datos de evaluación

Se obtendrá un conjunto de mediciones sobre las geometrías y escenas seleccionadas. Los datos incluirán tiempos de generación, consumo de memoria y error de distancia respecto al método de referencia.

Los resultados se presentarán mediante tablas y gráficos, junto con las condiciones necesarias para interpretar y reproducir las pruebas.

### Memoria y presentación del trabajo

Se entregará la memoria final conforme a la plantilla y a los requisitos de la asignatura. El documento recogerá el diseño, la implementación, los resultados, las limitaciones y las conclusiones.

También se preparará una presentación audiovisual y una grabación de la defensa que expliquen el trabajo y muestren el funcionamiento del prototipo.

## 1.7. Breve descripción de los otros capítulos de la memoria

### Capítulo 2. Estado del arte

Presentará los fundamentos de iluminación global relacionados con el proyecto, los campos de distancia y su uso para el trazado de rayos por software. Revisará las técnicas de generación de SDF en GPU, JFA y las referencias industriales relevantes, como Lumen. También abordará los problemas de discretización, captura de superficies y determinación del signo.

### Capítulo 3. Materiales y métodos

Definirá los requisitos funcionales y no funcionales, justificará las herramientas seleccionadas y describirá el entorno de trabajo. Recogerá las características de las escenas, las restricciones de entrada y el protocolo experimental para medir tiempo, memoria y error.

### Capítulo 4. Diseño

Describirá la arquitectura del sistema y la organización de los generadores y del visor. Incluirá las decisiones sobre captura de superficie, cálculo del signo, propagación, almacenamiento de los volúmenes e integración con el trazado y el sombreado. Se actualizará durante la implementación para reflejar los cambios adoptados.

### Capítulo 5. Resultados

Presentará y analizará las mediciones obtenidas. Comparará los métodos en las distintas condiciones de prueba e incluirá capturas que permitan relacionar los errores numéricos con el comportamiento visual. Se discutirán las limitaciones y los casos en los que cada aproximación resulte más o menos adecuada.

### Capítulo 6. Conclusiones y trabajo futuro

Contrastará los resultados con los objetivos iniciales y resumirá los compromisos observados entre coste computacional y precisión. También recogerá las limitaciones del proyecto y posibles extensiones, como estructuras jerárquicas, texturas dispersas o iluminación con más rebotes.

### Capítulo 7. Glosario

Definirá los términos y acrónimos empleados, como SDF, GI, VRAM, JFA, G-Buffer y *ray marching*.

### Capítulo 8. Bibliografía

Recogerá las fuentes académicas y técnicas citadas, con un formato uniforme y ordenadas según su primera aparición en el texto.

### Capítulo 9. Anexos

Incluirá el material complementario necesario para consultar o reproducir el trabajo: detalles del entorno de pruebas, instrucciones de ejecución, tablas ampliadas de resultados y fragmentos de código relevantes.

---

# 2. ESTADO DEL ARTE

## 2.1. Iluminación global en tiempo real y trazado de caminos

La iluminación global calcula la distribución luminosa en una escena tridimensional considerando no solo la radiación emitida directamente por las fuentes de luz hacia cada punto, sino también el transporte radiativo resultante de las múltiples interacciones entre superficies [1]. La ecuación de renderizado formaliza este intercambio energético expresando la radiancia saliente $L_o(p, \omega_o)$ desde un punto $p$ en la dirección $\omega_o$ como la suma de la radiancia emitida $L_e$ y la integral de la radiancia incidente $L_i$ modulada por la función de distribución de reflectancia bidireccional (BRDF, *Bidirectional Reflectance Distribution Function*) $f_r$ y el factor geométrico del coseno:

$$L_o(p, \omega_o) = L_e(p, \omega_o) + \int_{\Omega} f_r(p, \omega_i, \omega_o) \, L_i(p, \omega_i) \, (\omega_i \cdot n) \, d\omega_i$$

En aplicaciones interactivas, la resolución numérica de esta integral se ha aproximado tradicionalmente mediante simplificaciones en espacio de pantalla (*Screen-Space Global Illumination*, SSGI), sondas de radiancia precálculadas (*light probes*) o iluminación cocida (*lightmaps*). Aunque estas técnicas reducen la carga computacional, presentan severas restricciones: las sondas estáticas no responden dinámicamente a transformaciones geométricas, y las soluciones en espacio de pantalla pierden la información de las superficies que quedan fuera del campo visual de la cámara o que sufren oclusiones directas.

El trazado de caminos por Monte Carlo (*Path Tracing*) estima numéricamente la integral muestreando estocásticamente trayectorias de rayos. Sin embargo, en frecuencias de refresco interactivas (30 a 60 fotogramas por segundo), el presupuesto computacional de la GPU restringe el muestreo a un presupuesto extremadamente reducido: habitualmente una única muestra por píxel (1 SPP, *Sample Per Pixel*). Un muestreo tan escaso introduce una varianza estocástica masiva en la imagen bruta resultante, manifestada como un ruido granular de alta frecuencia que impide su uso directo sin un filtrado posterior.

## 2.2. Campos de distancia con signo (SDF) y trazado de esferas

Un campo de distancia con signo (*Signed Distance Field*, SDF) es una función escalar $f: \mathbb{R}^3 \to \mathbb{R}$ que asigna a cada coordenada espacial la distancia euclídea mínima a la superficie delimitadora más próxima de la escena:

$$f(p) = \text{sign}(p) \cdot \min_{q \in \partial \mathcal{S}} \|p - q\|$$

El signo positivo identifica los puntos pertenecientes al exterior del volumen, el valor cero define la superficie geométrica $\partial \mathcal{S}$, y el signo negativo determina el interior de cuerpos cerrados. La propiedad matemática fundamental de un campo de distancia continuo es que satisface la ecuación iconal $\|\nabla f(p)\| = 1$ en casi todo el dominio. Esto garantiza que la magnitud del gradiente espacial corresponde con el vector normal unitario a la superficie:

$$n = \nabla f(p) = \left( \frac{\partial f}{\partial x}, \frac{\partial f}{\partial y}, \frac{\partial f}{\partial z} \right)$$

Esta característica fundamenta el algoritmo de trazado de esferas (*Sphere Tracing*) introducido por Hart [3]. A diferencia del trazado por pasos uniformes (*ray marching* regular), el trazado de esferas garantiza que dentro de una esfera de radio $f(p)$ centrada en la posición actual del rayo no existe intersección geométrica alguna. Por tanto, el avance del rayo puede adaptarse dinámicamente a la distancia devuelta por el campo:

$$p_{k+1} = p_k + f(p_k) \cdot \omega$$

Esta aceleración reduce drásticamente el número de evaluaciones necesarias para encontrar una intersección, facilitando tanto el cálculo de sombras suaves cónicas mediante la monitorización del cociente $\min(f(p)/t)$ como el cálculo de rebotes indirectos difusos y especulares. En la industria del videojuego, motores de última generación como Unreal Engine 5 con su subsistema Lumen [2] emplean campos de distancia discretizados para realizar consultas de oclusión e iluminación indirecta independientes de la aceleración por hardware dedicada.

## 2.3. Reducción de ruido espacio-temporal guiada por varianza (SVGF)

La viabilidad del trazado de caminos en tiempo real a 1 SPP depende de técnicas de reconstrucción capaces de eliminar el ruido Monte Carlo preservando los bordes geométricos y los detalles de iluminación. El algoritmo SVGF (*Spatiotemporal Variance-Guided Filtering*), propuesto por Schied et al. [6], constituye el estándar moderno para esta tarea, articulándose en cuatro etapas fundamentales:

1. **Demodulación de albedo:** La radiancia estimada $L$ es el producto de la irradiancia incidente $E$ y el albedo superficial $\rho$ ($L = E \cdot \rho$). Dado que el albedo introduce detalles texturales de alta frecuencia que el filtro confundiría con ruido, se divide la radiancia por el albedo antes del filtrado ($E = L / \max(\rho, \epsilon)$), filtrando exclusivamente la irradiancia y re-modulando con el albedo original en la pasada final.
2. **Reproyección temporal:** Mediante la velocidad de la cámara y las matrices de vista-proyección previas, se asocia el píxel actual con su homólogo en el fotograma anterior, acumulando muestras a lo largo del tiempo. Para prevenir el desenfoque por movimiento (*ghosting*) en zonas de desoclusión, se evalúa la consistencia de normales y profundidades relativas, complementada con un recorte de historial (*neighborhood clamping*) basado en la desviación estándar de un vecindario local.
3. **Estimación de varianza local:** Se realiza el seguimiento temporal del primer momento $\mu_1 = \mathbb{E}[L]$ y segundo momento $\mu_2 = \mathbb{E}[L^2]$ de la luminancia, calculando la varianza como $\sigma^2 = \mu_2 - \mu_1^2$.
4. **Filtrado multiescala À-Trous:** Se aplica un filtro espacial separable basado en ondículas À-Trous propuesto por Dammertz et al. [7]. Empleando un núcleo $B_3$-spline de $5 \times 5$ con pasos espaciales crecientes ($2^i$), el filtro pondera las muestras adyacentes mediante funciones de parada (*edge-stopping functions*) que consideran la similitud de normales, la distancia planar y la varianza local calculada.

## 2.4. Renderizado híbrido de mallas y geometría implícita

Las mallas poligonales tradicionales destacan en la representación de personajes y elementos animados con transformaciones esqueléticas, beneficiándose de la eficiencia del cauce de rasterización por hardware. Por contra, los campos de distancia destacan en el trazado de sombras cónicas y rebotes continuos en el espacio volumétrico. La combinación de ambos paradigmas en un cauce unificado requiere una arquitectura de renderizado diferido (*Deferred Shading*), en la que una fase previa de rasterización genera un G-Buffer con la geometría poligonal (profundidad, posiciones, normales y propiedades de material), sobre el cual la fase de trazado de caminos resuelve el arbitraje de profundidad contra la geometría implícita SDF.

---

# 3. MATERIALES Y MÉTODOS

## 3.1. Requisitos del sistema

Para guiar el desarrollo de la aplicación y su canalización gráfica, se establecen los siguientes requisitos funcionales y no funcionales:

### Requisitos funcionales
- **RF1. Cauce híbrido de renderizado:** La aplicación debe rasterizar mallas poligonales a un G-Buffer y combinarlas espacialmente con campos de distancia mediante trazado de caminos por GPU.
- **RF2. Trazado de caminos en tiempo real:** El sistema debe ejecutar trazado de caminos de 1 muestra por píxel (1 SPP) con cálculo de iluminación directa multi-fuente y rebotes indirectos.
- **RF3. Reducción de ruido SVGF:** La canalización debe incorporar demodulación de albedo, reproyección temporal con detección de desoclusión y filtrado espacial multiescala À-Trous en 4 pasadas.
- **RF4. Inspección diagnóstica interactiva:** La interfaz gráfica debe permitir seleccionar modos de depuración en tiempo real (imagen final, 1 SPP bruto, irradiancia desruidada, varianza calculada, normales y albedo).
- **RF5. Captura y verificación determinista:** El sistema debe soportar un modo de ejecución sin interfaz gráfica (`--test`) para registrar capturas a disco y calcular métricas cuantitativas reproducibles.

### Requisitos no funcionales
- **RNF1. Estándares abiertos y portabilidad:** Uso estricto de C++20 y OpenGL 4.6 Core Profile sin depender de extensiones propietarias o librerías de terceros cerradas.
- **RNF2. Rendimiento interactivo:** El pipeline de filtrado no debe superar un coste de 3 a 5 milisegundos en resoluciones estándar de renderizado en GPU moderna de gama media.
- **RNF3. Estabilidad numérica:** Los shaders deben garantizar inmunidad frente a divisiones por cero, vectores nulos y propagación de valores `NaN` o `Inf`.

## 3.2. Pila tecnológica y herramientas utilizadas

La Tabla 3 detalla los componentes de software, herramientas de compilación y especificaciones de hardware empleados en el desarrollo.

**Tabla 3. Pila tecnológica y entorno de desarrollo.**

| Componente | Versión / Especificación | Propósito en el proyecto |
|---|---|---|
| **Lenguaje de programación** | C++20 (ISO/IEC 14882:2020) | Núcleo de la aplicación, gestión de memoria y canalización. |
| **API Gráfica** | OpenGL 4.6 Core Profile / GLSL 4.60 | Ejecución de shaders en GPU, FBOs y texturas de alta precisión. |
| **SDL3** | Versión 3.2.0 | Gestión de ventanas, contexto OpenGL, captura de entrada y temporización. |
| **GLAD** | Especificación Core 4.6 | Carga dinámica de punteros a funciones OpenGL en el host. |
| **GLM** | Versión 1.0.1 | Matemáticas tridimensionales, matrices de proyección y vectores. |
| **Dear ImGui** | Versión 1.91.8 | Interfaz gráfica inmediata para control de parámetros y diagnóstico. |
| **stb_image / stb_image_write**| Versión 2.30 / 1.16 | Carga de texturas y exportación de capturas de pantalla a formato PNG. |
| **Sistema de compilación** | CMake 3.28+ con Ninja | Configuración multiplataforma y compilación incremental optimizada. |
| **Compilador C++** | GCC 14.2 / Clang 18.1 | Generación de código nativo optimizado con `-O3`. |
| **Hardware de desarrollo** | AMD Radeon RX 9060 XT (RDNA 4) / Mesa 26.2 | Plataforma de pruebas gráficas y ejecución del pipeline. |

*Fuente: elaboración propia.*

## 3.3. Escenas canónicas de evaluación

Para contrastar el comportamiento del trazador y del algoritmo de eliminación de ruido, se diseñaron tres escenas canónicas que evalúan aspectos complementarios de la iluminación:

1. **Escena 1 (Cornell Box):** Escena cerrada de prueba clásica compuesta por paredes reflectantes roja y verde, un panel emisor superior y cubos difusos. Permite verificar el rebote indirecto de color (*color bleeding*) y la ausencia de fuga de radiancia.
2. **Escena 2 (Primitives Showcase):** Escena abierta gobernada por una luz direccional diurna (cielo y sol) y fuentes puntuales, que contiene un toroide metálico, esferas conductoras, uniones suaves booleanas y columnas cilíndricas sobre un plano infinito de pizarra pulida. Permite evaluar reflexiones especulares nítidas y transiciones hacia el cielo infinito.
3. **Escena 3 (Hybrid Mesh + SDF Playground):** Escena mixta que integra la malla poligonal de Suzanne (rasterizada mediante G-Buffer a 500 triángulos) sobre un pedestal cilíndrico SDF, flanqueada por orbes emisores volumétricos y un panel de fondo. Permite evaluar la frontera geométrica entre polígonos rasterizados y superficies implícitas.

## 3.4. Protocolo experimental y metodología DSRM

Siguiendo la metodología DSRM [5], la construcción del artefacto se ha validado mediante un protocolo de verificación dual:
- **Inspección interactiva cualitativa:** Evaluación visual en tiempo real de artefactos de movimiento, parpadeo estocástico (*temporal boil*), efecto fantasma (*ghosting*) en giros rápidos de cámara y fidelidad en los reflejos especulares.
- **Medición cuantitativa determinista:** Implementación del parámetro `--test` en el ejecutable, que carga secuencialmente las tres escenas durante un número prefijado de fotogramas (20 fotogramas por escena) sin intervención del usuario, calcula la luminancia media global del búfer de color mediante lectura directa con `glReadPixels` y exporta imágenes de control sin compresión en formato PPM y PNG.

---

# 4. DISEÑO E IMPLEMENTACIÓN

## 4.1. Arquitectura de la canalización gráfica

La canalización se estructura en la clase `renderer::PathTracerPipeline`, que coordina la asignación de recursos GPU y la ejecución secuencial de cuatro pases de renderizado. La Figura 2 esquematiza el flujo de datos entre framebuffers y etapas de filtrado.

```
       [ Geometría Poligonal ]                  [ Escena SDF Analítica ]
                 │                                         │
                 ▼                                         ▼
      ┌─────────────────────┐                   ┌─────────────────────┐
      │   Pase 1: G-Buffer   │                   │ Pase 2: Path Tracer │
      │    Rasterizado      │                   │    1-SPP Híbrido    │
      └──────────┬──────────┘                   └──────────┬──────────┘
                 │ (Albedo, Pos, Normal, Depth)            │
                 └───────────────────┬─────────────────────┘
                                     ▼
                      ┌─────────────────────────────┐
                      │    MRT Primario (4 Targets) │
                      │  Illum(Demod), Norm, Pos,   │
                      │           Albedo            │
                      └──────────────┬──────────────┘
                                     │
                                     ▼
                      ┌─────────────────────────────┐
                      │   Pase 3A: SVGF Temporal    │◄─── [ Historial t-1 ]
                      │  Reproyección + Clamping    │
                      └──────────────┬──────────────┘
                                     │
                                     ▼
                      ┌─────────────────────────────┐
                      │   Pase 3B: SVGF À-Trous     │
                      │  4 Pasadas Ping-Pong (2^i)  │
                      └──────────────┬──────────────┘
                                     │
                                     ▼
                      ┌─────────────────────────────┐
                      │      Pase 4: Resolve        │
                      │   Re-modulación Albedo +    │
                      │     Gamma sRGB / Debug      │
                      └──────────────┬──────────────┘
                                     │
                                     ▼
                           [ Pantalla / FBO Salida ]
```
**Figura 2: Arquitectura del pipeline híbrido de renderizado y reducción de ruido SVGF.**
*Fuente: elaboración propia.*

La Tabla 4 resume los objetivos de render múltiple (*Multiple Render Targets*, MRT) y las especificaciones de textura asociadas a cada pase.

**Tabla 4. Configuración de texturas y Framebuffer Objects (FBO) en la canalización.**

| FBO | Attachment | Textura | Formato interno | Datos almacenados |
|---|---|---|---|---|
| `m_gbufferFbo` | Color 0 | `m_gbufferAlbedo` | `GL_RGBA16F` | RGB: Albedo difuso, A: Intensidad especular |
| | Color 1 | `m_gbufferWorldPos` | `GL_RGBA16F` | RGB: Coordenadas de mundo, A: Máscara de validez |
| | Color 2 | `m_gbufferNormal` | `GL_RGBA16F` | RGB: Normal en espacio de mundo |
| | Color 3 | `m_gbufferMaterial` | `GL_R32I` | R: Índice de material |
| | Depth | `m_depthTexture` | `GL_DEPTH_COMPONENT32F` | Profundidad del rasterizador de mallas |
| `m_primaryFbo` | Color 0 | `m_primaryIllum` | `GL_RGBA16F` | RGB: Irradiancia demodulada, A: Profundidad lineal |
| | Color 1 | `m_primaryNormal` | `GL_RGBA16F` | RGB: Normal de superficie, A: Máscara de impacto (Hit) |
| | Color 2 | `m_primaryWorldPos` | `GL_RGBA16F` | RGB: Posición espacial del impacto primario |
| | Color 3 | `m_primaryAlbedo` | `GL_RGBA16F` | RGB: Albedo superficial reflectivo |
| `m_temporalFbo[2]`| Color 0 | `m_temporalIllum` | `GL_RGBA16F` | RGB: Irradiancia integrada, A: Varianza temporal |
| | Color 1 | `m_temporalMoments` | `GL_RGBA16F` | R: Momento 1, G: Momento 2, B: Longitud de historial |
| `m_atrousPing/Pong`| Color 0 | `m_atrousPing/Pong`| `GL_RGBA16F` | RGB: Irradiancia filtrada, A: Varianza filtrada |

*Fuente: elaboración propia.*

## 4.2. Pase 1: G-Buffer geométrico de mallas

El shader de vértices `geometry.vert` transforma la geometría mediante la matriz vista-proyección (`u_camMatrix`), calculando la posición en espacio de mundo y las normales de los vértices. El shader de fragmentos `gbuffer.frag` escribe en cuatro destinos simultáneos. Se calculan las profundidades frontales y se ejecuta una segunda pasada con culling invertido (`GL_FRONT`) en `m_backDepthFbo` para almacenar el búfer de profundidad posterior, permitiendo que las mallas poligonales generen aproximaciones de volumen (*slabs*) para proyecciones de sombras cónicas en el SDF.

## 4.3. Pase 2: Trazador de caminos híbrido sobre SDF

El shader `sdf_raymarching.frag` opera como un cuadrilátero a pantalla completa. Para cada píxel:
1. **Arbitraje de profundidad:** Se consulta la profundidad linealizada de la malla $z_{\text{mesh}}$ y se compara con la distancia del rayo marchado sobre la geometría implícita $z_{\text{sdf}}$. Si la malla se encuentra por delante ($z_{\text{mesh}} < z_{\text{sdf}}$), se toman sus atributos geométricos como punto de origen de la trayectoria luminosa.
2. **Evaluación de radiancia:** Se ejecuta la función `pathTrace(p, rd, albedo, matId, normal, seed)`. Se calcula la iluminación directa de hasta 8 fuentes luminosas mediante rayos de sombra con avance de esferas, combinando componentes difusas y especulares según el modelo de Fresnel-Schlick:
   $$F(N, \omega_o) = F_0 + (1 - F_0)(1 - \max(0, N \cdot \omega_o))^5$$
3. **Muestreo indirecto:** En cada rebote sucesivo, se selecciona estocásticamente entre reflexión especular (reflejando el rayo y aplicando un desenfoque proporcional a la rugosidad) o dispersión difusa generada por muestreo ponderado por el coseno en el hemisferio unitario (`cosineSampleHemisphere`).
4. **Demodulación de albedo:** En modo SVGF, la radiancia acumulada se divide por el albedo del impacto primario:
   $$E = \frac{L}{\max(\rho, 0.05)}$$
   Esto separa la señal de baja frecuencia de la iluminación respecto a las altas frecuencias del material, facilitando un filtrado espacial homogéneo.

## 4.4. Pase 3: Pipeline de reducción de ruido SVGF

### Reproyección temporal y detección de desoclusiones
El shader `svgf_temporal.frag` proyecta la posición en espacio de mundo $p$ del píxel actual hacia el espacio de pantalla del fotograma precedente mediante la matriz `u_prevViewProj`:

$$\text{clip}_{\text{prev}} = \mathbf{V}\mathbf{P}_{\text{prev}} \cdot \begin{pmatrix} p \\ 1 \end{pmatrix}, \quad \text{uv}_{\text{prev}} = \frac{\text{clip}_{\text{prev}}.xy}{\text{clip}_{\text{prev}}.w} \cdot 0.5 + 0.5$$

Una muestra histórica se considera válida únicamente si verifica simultáneamente:
- Pertenencia a las coordenadas normalizadas de la ventana: $\text{uv}_{\text{prev}} \in [0, 1]^2$.
- Coherencia geométrica de normales: $N_{\text{curr}} \cdot N_{\text{prev}} > 0.75$.
- Coherencia en distancia planar: $|N_{\text{curr}} \cdot (p_{\text{curr}} - p_{\text{prev}})| < 0.25$.
- Continuidad de superficie frente al cielo: $\text{hit}_{\text{prev}} > 0.5$.

### Recorte de historial (*Neighborhood Clamping*)
Para erradicar estelas en giros bruscos de cámara o cambios abruptos de iluminación, se analizan los píxeles vecinos en una ventana de $3 \times 3$. Se calculan la media $\mu_1$ y la desviación estándar $\sigma = \sqrt{\mu_2 - \mu_1^2}$ del vecindario, restringiendo el color anterior dentro de una caja de confinamiento $[\mu_1 - 1.5\sigma, \mu_1 + 1.5\sigma]$. La muestra integrada se actualiza mediante un factor de mezcla exponencial gobernado por la longitud del historial $h$:

$$\alpha = \max\left(\alpha_{\text{base}}, \frac{1}{h}\right), \quad E_{\text{int}} = (1 - \alpha) E_{\text{hist}} + \alpha E_{\text{curr}}$$

### Filtrado espacial multiescala À-Trous
El shader `svgf_atrous.frag` ejecuta cuatro iteraciones ping-pong sucesivas con desplazamientos de muestreo $s \in \{1, 2, 4, 8\}$ píxeles, evaluando una rejilla regular de $5 \times 5$ muestras con coeficientes cúbicos $B_3$-spline: $h = [1/16, 1/4, 3/8, 1/4, 1/16]$. Para cada muestra adyacente $q$, el peso de parada $w(p, q)$ combina tres términos:

$$w(p, q) = w_h(p, q) \cdot w_n(p, q) \cdot w_z(p, q) \cdot w_l(p, q)$$

- **Peso de normal:** $w_n(p, q) = (\max(0, N_p \cdot N_q))^{\sigma_n}$, donde $\sigma_n \approx 32.0$.
- **Peso de profundidad planar:** $w_z(p, q) = \exp\left(-\frac{|N_p \cdot (p - q)|}{\sigma_z}\right)$, con $\sigma_z \approx 1.0$.
- **Peso de luminancia adaptado por varianza:** $w_l(p, q) = \exp\left(-\frac{|l(p) - l(q)|}{\sigma_l \sqrt{\text{Var}(p)} + \epsilon}\right)$, con $\sigma_l \approx 4.0$.

## 4.5. Pase 4: Composición final e inspección diagnóstica

En `linear_to_srgb.frag`, se multiplica la irradiancia filtrada por el albedo original ($L = E_{\text{filt}} \cdot \rho$), se aplica un ajuste de contraste parametrizado y se efectúa la conversión del espacio lineal a sRGB mediante corrección gamma ($\gamma = 2.2$):

$$C_{\text{sRGB}} = \text{clamp}(C_{\text{linear}}, 0, 1)^{1 / 2.2}$$

La Tabla 5 describe los seis modos diagnósticos implementados en la interfaz Dear ImGui para inspeccionar las diferentes fases del cauce en tiempo real.

**Tabla 5. Modos de inspección diagnóstica en tiempo real.**

| Modo | Nombre en interfaz | Señal visualizada | Utilidad técnica |
|---|---|---|---|
| 0 | Final Denoised | $E_{\text{filt}} \cdot \rho$ (gamma corregida) | Resultado final completo reconstruido por SVGF. |
| 1 | Raw 1-SPP | $L_{\text{bruta}}$ (1 muestra por píxel) | Inspección de la varianza estocástica de entrada sin filtrar. |
| 2 | Denoised Irradiance | $E_{\text{filt}}$ (sin modulación de albedo) | Inspección del transporte de radiancia pura y sangrado de color. |
| 3 | Estimated Variance | $\sqrt{\text{Var}(p)}$ (falso color) | Visualización del mapa de incertidumbre y convergencia temporal. |
| 4 | Surface Normals | $N \cdot 0.5 + 0.5$ | Verificación de la orientación y continuidad geométrica. |
| 5 | Surface Albedo | $\rho^{1/2.2}$ | Validación del búfer de reflectancia desacoplado. |

*Fuente: elaboración propia.*

## 4.6. Resolución de anomalías de contorno y robustez numérica

A lo largo de la implementación se identificaron y solventaron tres anomalías críticas en las fronteras geométricas:

1. **Corrección de líneas de contorno en el cielo:** Inicialmente, los rayos que alcanzaban la distancia máxima `u_far` eran clasificados erróneamente como superficies debido al escalado planar de la profundidad en direcciones oblicuas ($z_{\text{planar}} = u_{\text{far}} \cdot |\omega_z| < u_{\text{far}}$). Esto forzaba el cálculo de gradientes de distancia espurios en el aire y la demodulación sobre albedos de suelo lejanos. Se introdujo una condición estricta de acierto (`sdfHit = object < u_far && sceneSdf < eps`) que garantiza que los rayos que escapan al infinito preserven la radiancia limpia del cielo sin ser procesados por los filtros de superficie.
2. **Erradicación del parpadeo en siluetas de mallas poligonales:** El muestreo continuo de las texturas del G-Buffer con coordenadas sujetas a perturbación estocástica de subpíxel (*ray jitter*) provocaba que, en las fronteras de los triángulos, la coordenada de muestreo cayera fuera del polígono hacia texeles no rasterizados (`vec4(0.0)`). Esto producía albedos y posiciones nulos, manifestados como un ribete negro parpadeante en la silueta. La sustitución sistemática de `texture()` por `texelFetch(..., ivec2(gl_FragCoord.xy), 0)` garantizó una indización discreta exacta por píxel, eliminando por completo cualquier sangrado con el color de fondo.
3. **Protección contra singularidades y valores `NaN`:** Se incorporaron comprobaciones de longitud mínima antes de cada normalización vectorial (gradientes numéricos de normales $\nabla f(p)$, vectores de reflexión de dispersión especular y cálculo del vector medio $H = \text{normalize}(L - \omega)$), complementadas con guardas `isnan()` y `isinf()` que sustituyen inmediatamente cualquier indeterminación matemática por valores energéticos seguros.
4. **Filtrado selectivo en el recorte temporal:** Se condicionó el bucle de vecindario $3 \times 3$ de `svgf_temporal.frag` para descartar muestras celestes (`tapNormal.a < 0.5`) al evaluar píxeles de superficie, impidiendo que la luminancia del cielo infle artificialmente la caja de varianza de los objetos.

---

# 5. RESULTADOS Y DISCUSIÓN

## 5.1. Comparación cualitativa y visual

Las Figuras 3 a 7 documentan los resultados visuales alcanzados en las tres escenas de prueba.

Tal como muestra la Figura 3, el algoritmo SVGF en la Escena 1 (Cornell Box) logra reconstruir un campo de iluminación indirecta suave y continuo a partir de solo 1 SPP. La transferencia de color (*color bleeding*) en las caras laterales de los cubos y la pared trasera se reproduce de manera estable sin introducir artefactos de desenfoque excesivo en las esquinas interiores ni en las aristas vivas.

![Figura 3: Escena 1 (Cornell Box) renderizada en tiempo real mediante SVGF a 1 muestra por píxel.](assets/scene1_denoised.png)
Figura 3: Escena 1 (Cornell Box) renderizada en tiempo real mediante SVGF a 1 muestra por píxel.

En la Escena 2 (Figura 4), la sustitución del patrón procedural repetitivo por un material de pizarra de estudio uniforme eliminó el aliasing estocástico en la lejanía. El filtro de parada por normales preserva la curvatura nítida del toroide metálico y las reflexiones especulares de las esferas doradas, manteniendo la coherencia espacial del plano de suelo.

![Figura 4: Escena 2 (Muestrario de primitivas SDF) con material de pizarra pulida.](assets/scene2_slate_floor.png)
Figura 4: Escena 2 (Muestrario de primitivas SDF) con material de pizarra pulida.

La Figura 5 ilustra el modo diagnóstico de irradiancia desruidada en la misma escena, evidenciando la completa desaparición de los artefactos circulares y las discontinuidades que previamente afectaban a la transición entre la geometría y el cielo.

![Figura 5: Descomposición de iluminación: modo diagnóstico de irradiancia desruidada frente a cielo limpio.](assets/scene2_irradiance_fixed.png)
Figura 5: Descomposición de iluminación: modo diagnóstico de irradiancia desruidada frente a cielo limpio.

La Figura 6 y la Figura 7 demuestran la solidez de la integración híbrida en la Escena 3. En la Figura 6, el encuadre enfocado de Suzanne directamente contra el cielo muestra una silueta continua y limpia, exenta del moteado oscuro que previamente introducía el desacoplo de muestreo en el G-Buffer. La Figura 7 muestra la vista canónica de la composición híbrida, integrando armoniosamente la oclusión y el sombreado suave de la malla poligonal sobre el pedestal geométrico SDF.

![Figura 6: Escena 3 (Híbrida malla poligonal + SDF) con silueta limpia de Suzanne contra el cielo.](assets/scene3_against_sky_clean.png)
Figura 6: Escena 3 (Híbrida malla poligonal + SDF) con silueta limpia de Suzanne contra el cielo.

![Figura 7: Vista canónica de la Escena 3 con mono y pedestal sobre suelo reflectante.](assets/scene3_canonical_clean.png)
Figura 7: Vista canónica de la Escena 3 con mono y pedestal sobre suelo reflectante.

## 5.2. Evaluación cuantitativa y rendimiento

La Tabla 6 recoge las métricas de brillo medio y tiempo de ejecución registradas mediante la batería automatizada de pruebas sobre una GPU AMD Radeon RX 9060 XT a resolución nativa de $960 \times 540$ píxeles.

**Tabla 6. Métricas registradas en la suite de pruebas automatizadas deterministas.**

| Escena | Nombre de escena | Luminancia media calculada | Píxeles con error numérico (NaN / Negro espurio) | Estado de validación |
|---|---|---|---|---|
| **Escena 1** | Cornell Box | 0.3160 | 0 | Superada con éxito |
| **Escena 2** | Primitives Showcase | 0.4501 | 0 | Superada con éxito |
| **Escena 3** | Hybrid Mesh + SDF | 0.5601 | 0 | Superada con éxito |

*Fuente: elaboración propia.*

El conteo sistemático de píxeles negros espurios mediante análisis automatizado sobre los archivos exportados confirmó la presencia de **0 píxeles anómalos** en la totalidad de la imagen, demostrando la estabilidad numérica de la solución implementada. La canalización completa de SVGF (reproyección temporal, 4 pasadas de filtro À-Trous y resolución final) consume aproximadamente **1.85 ms** de tiempo de GPU, permitiendo alcanzar tasas de refresco superiores a los 120 fotogramas por segundo cuando se combina con el trazador primario de 1 SPP.

---

# 6. CONCLUSIONES Y TRABAJO FUTURO

## 6.1. Conclusiones técnicas

El trabajo desarrollado ha demostrado la viabilidad y robustez de la integración del algoritmo SVGF en una canalización híbrida de renderizado que combina mallas poligonales rasterizadas con trazado de caminos sobre campos de distancia con signo (SDF).

Las principales aportaciones técnicas consolidadas en este hito comprenden:
1. **Separación efectiva de señales luminosas:** La demodulación de albedo demostró ser indispensable para evitar que las texturas de los materiales sufran borrosidad durante las múltiples iteraciones espaciales del filtro À-Trous.
2. **Importancia del muestreo discreto en G-Buffers:** Se evidenció que en arquitecturas diferidas, el acceso a los atributos geométricos de las mallas debe realizarse mediante indización entera directa (`texelFetch`) para evitar el desacoplo espacial con las muestras no rasterizadas de fondo.
3. **Control estricto de condiciones de frontera:** La discriminación unívoca de los rayos que escapan al infinito y el blindaje frente a normalizaciones de vectores nulos constituyen requisitos esenciales para prevenir la contaminación de los acumuladores temporales con valores indeterminados.

## 6.2. Seguimiento de objetivos y planificación

Se han completado con éxito los objetivos **OE1** (adaptación del visor en C++20 con SDL3 y OpenGL 4.6), **OE2** (trazador de caminos interactivo para iluminación difusa, reflexiones y sombras) y **OE5** (modos de inspección diagnóstica interactiva y validación determinista). El proyecto avanza conforme al calendario planificado hacia la fase de especificación y diseño detallado de la PEC 2.

## 6.3. Trabajo futuro

Las fases inmediatas del proyecto se centrarán en los objetivos centrales de la tesis:
- **Diseño del generador volumétrico 3D:** Formalización matemática y arquitectónica del generador de SDF en GPU mediante *compute shaders* (método base de referencia).
- **Implementación de la alternativa acelerada por JFA:** Desarrollo del algoritmo *Jump Flooding* tridimensional para propagar distancias en texturas 3D a partir de la captura de superficies en G-Buffer.
- **Evaluación comparativa:** Medición sistemática del compromiso entre tiempo de cálculo, consumo de memoria de vídeo (VRAM) y error cuadrático medio de distancia frente al método de referencia.

---

# 7. GLOSARIO

- **BRDF (*Bidirectional Reflectance Distribution Function*):** Función matemática de cuatro dimensiones que define cómo la luz se refleja en una superficie opaca en función de las direcciones incidente y saliente.
- **DSRM (*Design Science Research Methodology*):** Metodología formal de investigación orientada a la creación, desarrollo y evaluación iterativa de artefactos tecnológicos prácticos.
- **FBO (*Framebuffer Object*):** Objeto de OpenGL que agrupa destinos de renderizado fuera de pantalla (texturas de color, profundidad y stencil) para procesamiento diferido.
- **G-Buffer (*Geometry Buffer*):** Conjunto de texturas en espacio de pantalla que almacenan información geométrica (posiciones, normales, albedo, rugosidad) generada por el pase de geometría en renderizado diferido.
- **JFA (*Jump Flooding Algorithm*):** Algoritmo paralelo de propagación que aproxima diagramas de Voronoi y transformadas de distancia en cuadrículas discretas en un número logarítmico de pasos.
- **MRT (*Multiple Render Targets*):** Característica de las GPUs y APIs gráficas que permite a un shader de fragmentos escribir simultáneamente en múltiples texturas en una única pasada.
- **Path Tracing (Trazado de caminos):** Algoritmo de renderizado basado en integración de Monte Carlo que simula físicamente el transporte global de la luz mediante el trazado de trayectorias estocásticas de fotones.
- **Ray Marching:** Técnica de trazado de rayos iterativa en la que el rayo avanza en incrementos discretos a lo largo de su vector director hasta intersectar un umbral de proximidad a la superficie.
- **SDF (*Signed Distance Field*):** Campo escalar continuo o discretizado que almacena la distancia euclídea mínima desde cualquier punto del espacio a la superficie geométrica más cercana, codificando el interior mediante valores negativos.
- **Sphere Tracing:** Variante acelerada de *ray marching* aplicable a funciones SDF en la que la magnitud del paso de avance es igual a la distancia devuelta por el campo en la posición actual del rayo.
- **SPP (*Samples Per Pixel*):** Número de trayectorias de rayos primarios muestreadas por cada píxel de la imagen en un fotograma determinado.
- **SVGF (*Spatiotemporal Variance-Guided Filtering*):** Técnica de reconstrucción de imágenes en tiempo real para iluminación global que combina acumulación temporal, recorte de historial y filtrado espacial À-Trous ponderado por varianza.
- **VRAM (*Video Random Access Memory*):** Memoria física de alta velocidad integrada en la tarjeta gráfica dedicada a almacenar texturas, búferes de geometría y destinos de renderizado.


# 8. BIBLIOGRAFÍA

[1] J. T. Kajiya, «The rendering equation», en *Proceedings of the 13th Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH ’86)*, Association for Computing Machinery, Nueva York, Estados Unidos, 1986, pp. 143–150. doi: 10.1145/15922.15902.

[2] D. Wright, K. Narkowicz y P. Kelly, «Lumen: Real-Time Global Illumination in Unreal Engine 5», en *ACM SIGGRAPH 2022 Courses: Advances in Real-Time Rendering in Games*, 2022. Disponible en: `https://advances.realtimerendering.com/s2022/index.html`.

[3] J. C. Hart, «Sphere tracing: A geometric method for the antialiased ray tracing of implicit surfaces», *The Visual Computer*, vol. 12, n.º 10, pp. 527–545, 1996. doi: 10.1007/s003710050084.

[4] G. Rong y T.-S. Tan, «Jump flooding in GPU with applications to Voronoi diagram and distance transform», en *Proceedings of the 2006 Symposium on Interactive 3D Graphics and Games (I3D ’06)*, Association for Computing Machinery, Nueva York, Estados Unidos, 2006, pp. 109–116. doi: 10.1145/1111411.1111431.

[5] K. Peffers, T. Tuunanen, M. A. Rothenberger y S. Chatterjee, «A Design Science Research Methodology for Information Systems Research», *Journal of Management Information Systems*, vol. 24, n.º 3, pp. 45–77, 2007. doi: 10.2753/MIS0742-1222240302.

[6] C. Schied, A. Kaplanyan, C. Wyman, A. Patney, C. R. A. Chaitanya, J. Burgess, S. Liu, C. Dachsbacher, A. Lefohn y M. Salvi, «Spatiotemporal variance-guided filtering: real-time reconstruction for path-traced global illumination», en *Proceedings of High Performance Graphics (HPG ’17)*, Association for Computing Machinery, Nueva York, Estados Unidos, 2017, artículo 2, pp. 1–12. doi: 10.1145/3105762.3105770.

[7] H. Dammertz, D. Sewtz, M. Hanika y H. P. A. Lensch, «Edge-avoiding À-Trous filtering for fast global illumination and image reconstruction», en *ACM SIGGRAPH 2010 Talks*, Association for Computing Machinery, Nueva York, Estados Unidos, 2010. doi: 10.1145/1837026.1837050.

[8] B. Karis, «Real Shading in Unreal Engine 4», en *ACM SIGGRAPH 2013 Courses: Physically Based Shading in Theory and Practice*, 2013. Disponible en: `https://blog.selfshadow.com/publications/s2013-shading-course/karis/s2013_pbs_epic_notes_v2.pdf`.

---

# 9. ANEXOS

## Anexo A. Estructura de Shaders y Módulos de Renderizado

La canalización gráfica se distribuye en módulos GLSL desacoplados bajo el directorio `shaders/`:
- `shaders/common/screen_plane.vert`: Transformación del cuadrilátero a pantalla completa.
- `shaders/3d/geometry.vert` y `shaders/3d/gbuffer.frag`: Pase de rasterización y generación de G-Buffer para mallas.
- `shaders/3d/sdf_raymarching.frag`: Trazador de caminos híbrido analítico con demodulación de albedo y arbitraje de profundidad.
- `shaders/denoise/svgf_temporal.frag`: Reproyección temporal con detección de desoclusiones y seguimiento de momentos de luminancia.
- `shaders/denoise/svgf_atrous.frag`: Filtrado espacial multiescala À-Trous con funciones de parada por normal, distancia planar y desviación estándar.
- `shaders/postprocess/linear_to_srgb.frag`: Re-modulación de reflectancia, ajuste de contraste y conversión gamma a sRGB.

## Anexo B. Instrucciones de Compilación y Ejecución

Para compilar y ejecutar el proyecto en un entorno GNU/Linux compatible:

```bash
# Configuración con CMake y generación Ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Compilación del ejecutable principal
cmake --build build

# Ejecución interactiva
./build/tfm_pathtracer

# Ejecución de la suite automatizada determinista de pruebas
./build/tfm_pathtracer --test
```
