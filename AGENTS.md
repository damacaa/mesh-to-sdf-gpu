# AGENTS.md - Master's Thesis (TFM) Guidelines & AI Cooperation

## 1. Project Overview & Identity
- **Student**: Daniel Martí Casanova (`damaca`)
- **Tutor/a de TF**: Gonzalo Nicolás Barreales
- **Profesor/a Responsable de la Asignatura (PRA)**: Joan Arnedo Moreno
- **Program**: Máster Universitario en Diseño y Programación de Videojuegos (UOC)
- **Project Type**: Master's Thesis (*Trabajo Fin de Máster* - TFM).
- **Tech Stack**: C++, CMake, OpenGL, SDL3, VS Code.
- **Repository Structure**:
  - Source code and project files: `src/`, `include/`, `third-party/`, etc.
  - Academic documentation: `docs/`.

---

## 2. Strict Language Policy
All AI agents and contributors must strictly enforce the following language separation:

| Scope | Language | Tone & Conventions |
| :--- | :--- | :--- |
| **Source Code** | **English** | Clean code, expressive naming, Doxygen/standard comments in English. |
| **Commit Messages** | **English** | Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, `build:`, `test:`). |
| **CMake & Build Scripts** | **English** | Comments and target names in English. |
| **Technical Diagrams / Architecture** | **English** | Schemas, UML and diagrams in English. |
| **Academic Thesis Document (*Memoria*)** | **Spanish** | Formal, rigorous, academic, **impersonal style** (passive *se*, e.g., "*se ha implementado*", "*el objetivo consiste en...*"). Absolutely no first-person singular (*yo*) or plural (*nosotros*). |
| **Academic Role Naming** | **Spanish** | Mandatory use of "**Tutor/a de TF**" and "**Profesor/a Responsable de la Asignatura (PRA)**". Never use "consultor", "PDC", or "profesor colaborador". |

---

## 3. Academic Deliverables & Workflow (UOC Standards)
The master's thesis is evaluated through 4 continuous assessment deliverables (*PECs*):
- **PEC 1**: Project Definition, Objectives, Methodology, Initial Planning, Section 1 draft (*Introducción*), Project Sheet (*Ficha del Trabajo*). Deliverable: `damaca_PEC1.pdf`.
- **PEC 2**: Detailed Specification, State of the Art, Materials & Methods, Architecture. Deliverable: `damaca_PEC2.pdf`.
- **PEC 3**: Implementation, Core Systems, Intermediate Results. Deliverable: `damaca_PEC3.pdf`.
- **PEC 4**: Final Results, Discussion, Conclusions, Final Memory, Presentation. Deliverable: `damaca_PEC4.pdf`.

### Document Guidelines
- University submissions use the official MS Word template exported to **PDF** named `damaca_PEC[X].pdf`.
- All textual drafts in this repository reside in `docs/` in Markdown (`.md`) format, structured 1-to-1 matching the official university headings for straightforward copying into Word.
- **Formal Rules**:
  - Figures must be numbered and captioned (`Figura X: Descripción`) and explicitly referenced in the body text (`Tal como muestra la Figura X`).
  - Tables must be numbered and captioned (`Tabla X: Descripción`).
  - Standardized numbered citation format (`[1]`, `[2]`).
  - Avoid footnotes in favor of inline text or formal references.

---

## 4. Fundamental Rules for AI Agents

1. **Do Not Pre-Fill or Invent Project Content**:
   - Never assume, speculate, or pre-populate project themes, research topics, algorithms, or narrative content until Daniel explicitly defines and instructs them.
   - Maintain clean, neutral structures and templates without placeholder contamination.

2. **Check Before Writing**:
   - Check existing files before creating duplicate modules.
   - Respect the English (Code) / Spanish (Documentation) boundary without exception.

3. **Academic Integrity & Third-Party / AI Disclosure**:
   - The university requires declaring third-party resources and AI usage (*Declaración de uso de recursos de terceros*).
   - Log tool usage, purpose, and prompt summaries cleanly in `docs/declaracion_recursos_ia.md`.
