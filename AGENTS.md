# AGENTS.md - Master's Thesis (TFM) Guidelines and AI Rules

## 1. Project Overview and Identity
- **Student**: Daniel Martí Casanova (`damaca`)
- **Tutor/a de TF**: Gonzalo Nicolás Barreales
- **Profesor/a Responsable de la Asignatura (PRA)**: Joan Arnedo Moreno
- **Program**: Máster Universitario en Diseño y Programación de Videojuegos (UOC)
- **Project Type**: Master's Thesis (*Trabajo Fin de Máster* - TFM).
- **Tech Stack**: C++, CMake, OpenGL, SDL3, VS Code.
- **Repository Structure**:
  - Source code and project files: `src/`, `include/`, `third-party/`.
  - Academic documentation: `docs/`.

---

## 2. Strict Language Policy
Contributors and AI agents must follow this language separation:

| Scope | Language | Tone and Conventions |
| :--- | :--- | :--- |
| **Source Code** | **English** | Clean code, clear names, comments in English. Follow ASD-STE100. |
| **Commit Messages** | **English** | Conventional Commits (`feat:`, `fix:`, `docs:`, `refactor:`, `build:`, `test:`). Follow ASD-STE100. |
| **CMake and Build Scripts** | **English** | Comments and target names in English. Follow ASD-STE100. |
| **Technical Diagrams** | **English** | Schemas, UML, and diagrams in English. Follow ASD-STE100. |
| **Academic Thesis Document (*Memoria*)** | **Spanish** | Formal academic Spanish in impersonal style (passive *se*). Do not use first-person forms (*yo*, *nosotros*). |
| **Academic Role Naming** | **Spanish** | Use only "**Tutor/a de TF**" and "**Profesor/a Responsable de la Asignatura (PRA)**". Never use "consultor" or "PDC". |

---

## 3. ASD-STE100 Standard (Simplified Technical English)
All English texts must follow the ASD-STE100 standard. This includes documentation, code comments, commit messages, and agent messages.

Follow these rules:
1. **Sentence length**:
   - Write a maximum of 20 words for each instruction.
   - Write a maximum of 25 words for each descriptive sentence.
2. **Paragraph length**: Write a maximum of 6 sentences in each paragraph.
3. **Voice and tense**: Use the active voice. Use simple tenses (present, past, and future).
4. **Vocabulary**:
   - Use approved words with their approved meanings.
   - Use "make sure" instead of "ensure".
   - Use "use" instead of "utilize".
   - Use "show" instead of "display" or "illustrate".
   - Use "give" instead of "provide".
5. **Clarity**:
   - Write one instruction per sentence.
   - Do not use more than three nouns together in a noun cluster.
   - Do not omit articles (*the*, *a*, *an*).

---

## 4. Academic Deliverables and Workflow (UOC Standards)
The university evaluates the thesis through four deliverables (*PECs*):
- **PEC 1**: Project Definition, Objectives, Methodology, Initial Planning, Introduction draft, Project Sheet. Deliverable: `damaca_PEC1.pdf`.
- **PEC 2**: Detailed Specification, State of the Art, Materials and Methods, Architecture. Deliverable: `damaca_PEC2.pdf`.
- **PEC 3**: Implementation, Core Systems, Intermediate Results. Deliverable: `damaca_PEC3.pdf`.
- **PEC 4**: Final Results, Discussion, Conclusions, Final Memory, Presentation. Deliverable: `damaca_PEC4.pdf`.

### Document Guidelines
- University submissions use the official template exported to PDF named `damaca_PEC[X].pdf`.
- The LaTeX files in `docs/` are the single source of truth for the academic thesis.
- The root document is `docs/memoria.tex`.
- Put modular chapters in `docs/capitulos/` (for example, `01_introduccion.tex`).
- Compile documents with `bash docs/scripts/compile_latex.sh docs/memoria.tex`.
- Match the headings of the official university template.
- Number all figures and add captions (`Figura X: Descripción`). Refer to figures in the text (`Tal como muestra la Figura X`).
- Number all tables and add captions (`Tabla X: Descripción`).
- Use standardized numbered citations (`[1]`, `[2]`).
- Do not use footnotes. Use inline text or references instead.

---

## 5. Fundamental Rules for AI Agents

1. **Do Not Invent Project Content**:
   - Do not invent project topics, algorithms, or content before Daniel defines them.
   - Keep clean structures and neutral templates.

2. **Check Before You Write**:
   - Check existing files before you create new files.
   - Keep the language boundary between English and Spanish.

3. **Academic Integrity and AI Disclosure**:
   - Declare all third-party resources and AI usage.
   - Record tool usage, purpose, and prompts in `docs/internal/declaracion_recursos_ia.md`.
