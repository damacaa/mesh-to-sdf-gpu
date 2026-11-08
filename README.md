# GPU-Accelerated Signed Distance Field (SDF) Generation from Polygonal Meshes

This repository contains the source code and academic documentation for the Master's Thesis (*Trabajo Fin de Máster* - TFM) at the Universitat Oberta de Catalunya (UOC).

- **Author**: Daniel Martí Casanova (`damaca`)
- **Tutor/a de TF**: Gonzalo Nicolás Barreales
- **Profesor/a Responsable de la Asignatura (PRA)**: Joan Arnedo Moreno
- **Program**: Máster Universitario en Diseño y Programación de Videojuegos (UOC)
- **PEC 2 Demo Video**: [https://youtu.be/ffXoGX69jPI](https://youtu.be/ffXoGX69jPI)

## Project Overview

This project evaluates two GPU methods that generate volumetric Signed Distance Fields (SDFs) from polygonal meshes. It compares a baseline point-to-triangle compute shader method against an alternative method that combines rasterized surface data with the 3D Jump Flooding Algorithm (JFA).

An interactive OpenGL 4.6 path tracer viewer validates the generated 3D distance textures. The viewer uses sphere tracing at 1 sample per pixel (1 SPP), Spatiotemporal Blue Noise (STBN) masks, and Spatiotemporal Variance-Guided Filtering (SVGF) to evaluate one-bounce diffuse indirect lighting, ambient occlusion, and soft shadows.

## Repository Structure

- `src/` and `include/`: C++20 source files and header files for the application and rendering pipeline.
- `shaders/`: GLSL 4.60 fragment, vertex, and compute shaders (`sdf_raymarching.frag`, `generate_3d_sdf.comp`, `svgf_temporal.frag`, `svgf_atrous.frag`, `linear_to_srgb.frag`).
- `assets/`: Textures and Spatiotemporal Blue Noise (STBN) masks.
- `third-party/`: Bundled dependencies (SDL3, GLAD, GLM, Dear ImGui, stb, nlohmann/json).
- `tests/`: Automated GPU regression tests and lighting quality benchmarks.
- `docs/`: Academic thesis document in LaTeX (`docs/memoria.tex` and modular chapters in `docs/capitulos/`).

## System Requirements

- **Operating System**: Linux (x86-64) or Windows (x86-64).
- **GPU**: Dedicated graphics card with OpenGL 4.6 Core Profile support.
- **Compiler**: C++20 compatible compiler (GCC 13+, Clang 16+, or MSVC 2022).
- **Build System**: CMake 3.20 or newer (Ninja is recommended).
- **Git**: Git with submodule support (`third-party/SDL`).

## Build Instructions

1. Clone the repository and initialize the submodules:
   ```bash
   git clone --recursive https://github.com/damacaa/mesh-to-sdf-gpu.git
   cd mesh-to-sdf-gpu
   ```
   If you already cloned the repository without `--recursive`, run:
   ```bash
   git submodule update --init --recursive
   ```

2. Configure the project with CMake:
   ```bash
   cmake -B build -DCMAKE_BUILD_TYPE=Release
   ```

3. Compile the main executable:
   ```bash
   cmake --build build -j
   ```

## Run Instructions

Run the interactive viewer from the root directory of the repository:

```bash
./build/tfm_pathtracer
```

> **Important**: Run the executable from the repository root directory (or keep the `shaders/` and `assets/` directories next to the binary in release packages) so that the application finds the GLSL shaders and STBN textures.

### Controls

- **Right Mouse Button + Drag**: Rotate the camera.
- **W / A / S / D**: Move the camera horizontally.
- **Space / Left Shift**: Move the camera up or down.
- **1 - 5**: Switch between predefined validation scenes (Cornell Box, SDF Primitives, 3D SDF Texture Placeholders).
- **Dear ImGui Panel**: Adjust SVGF denoiser settings, resolution scale, 3D SDF texture resolution ($32^3$ to $256^3$), and diagnostic views (Final, Raw 1-SPP, Denoised Irradiance, Variance, Normals, Albedo).

### Automated Tests

Run the automated headless validation mode to verify that all scenes render without `NaN` or `Inf` artifacts:

```bash
./build/tfm_pathtracer --test
```

To build and run the full GPU regression test suite and lighting benchmarks:

```bash
cmake -B build -DTFM_BUILD_RENDER_TESTS=ON -DTFM_BUILD_BENCHMARKS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Academic Thesis Compilation

To compile the LaTeX thesis document (`docs/memoria.tex`) into `docs/memoria.pdf`, run:

```bash
bash docs/scripts/compile_latex.sh docs/memoria.tex
```

## License

The academic documentation and work are licensed under a [Creative Commons Attribution-NonCommercial-NoDerivs 3.0 Spain License (CC BY-NC-ND 3.0 ES)](https://creativecommons.org/licenses/by-nc-nd/3.0/es/).
