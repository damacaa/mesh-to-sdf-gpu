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

# 8. BIBLIOGRAFÍA

[1] J. T. Kajiya, «The rendering equation», en *Proceedings of the 13th Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH ’86)*, Association for Computing Machinery, Nueva York, Estados Unidos, 1986, pp. 143–150. doi: 10.1145/15922.15902.

[2] D. Wright, K. Narkowicz y P. Kelly, «Lumen: Real-Time Global Illumination in Unreal Engine 5», en *ACM SIGGRAPH 2022 Courses: Advances in Real-Time Rendering in Games*, 2022. Disponible en: `https://advances.realtimerendering.com/s2022/index.html`.

[3] J. C. Hart, «Sphere tracing: A geometric method for the antialiased ray tracing of implicit surfaces», *The Visual Computer*, vol. 12, n.º 10, pp. 527–545, 1996. doi: 10.1007/s003710050084.

[4] G. Rong y T.-S. Tan, «Jump flooding in GPU with applications to Voronoi diagram and distance transform», en *Proceedings of the 2006 Symposium on Interactive 3D Graphics and Games (I3D ’06)*, Association for Computing Machinery, Nueva York, Estados Unidos, 2006, pp. 109–116. doi: 10.1145/1111411.1111431.

[5] K. Peffers, T. Tuunanen, M. A. Rothenberger y S. Chatterjee, «A Design Science Research Methodology for Information Systems Research», *Journal of Management Information Systems*, vol. 24, n.º 3, pp. 45–77, 2007. doi: 10.2753/MIS0742-1222240302.
