import re
import shutil
import subprocess
import time
import os
from pathlib import Path

import docx
from docx.shared import Pt, Inches, RGBColor
from docx.oxml import OxmlElement, parse_xml
from docx.oxml.ns import qn, nsdecls

def parse_markdown_runs(text):
    tokens = []
    pattern = re.compile(r'(\*\*[^*]+\*\*|\*[^*]+\*|`[^`]+`)')
    parts = pattern.split(text)
    for part in parts:
        if not part:
            continue
        if part.startswith('**') and part.endswith('**'):
            tokens.append((part[2:-2], {'bold': True}))
        elif part.startswith('*') and part.endswith('*'):
            tokens.append((part[1:-1], {'italic': True}))
        elif part.startswith('`') and part.endswith('`'):
            tokens.append((part[1:-1], {'code': True}))
        else:
            tokens.append((part, {}))
    return tokens

def add_runs_to_paragraph(paragraph, text, base_bold=False, base_italic=False, font_size=None):
    tokens = parse_markdown_runs(text)
    for content, fmt in tokens:
        run = paragraph.add_run(content)
        if fmt.get('bold') or base_bold:
            run.font.bold = True
        if fmt.get('italic') or base_italic:
            run.font.italic = True
        if fmt.get('code'):
            run.font.name = 'Courier New'
            run.font.size = Pt(10)
        elif font_size:
            run.font.size = font_size

def set_table_borders_and_margins(tbl):
    tblPr = tbl._element.xpath('w:tblPr')[0]
    borders = parse_xml(
        r'<w:tblBorders %s>'
        r'  <w:top w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'  <w:left w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'  <w:bottom w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'  <w:right w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'  <w:insideH w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'  <w:insideV w:val="single" w:sz="4" w:space="0" w:color="CCCCCC"/>'
        r'</w:tblBorders>' % nsdecls('w')
    )
    tblPr.append(borders)
    
    # Enable cantSplit on all rows
    for r in tbl.rows:
        trPr = r._element.get_or_add_trPr()
        trPr.append(parse_xml(r'<w:cantSplit %s/>' % nsdecls('w')))

def set_table_columns_and_widths(tbl, col_widths_dxa):
    tblPr = tbl._element.xpath('w:tblPr')[0]
    total_w = sum(col_widths_dxa)
    for old in tblPr.xpath('w:tblW'):
        tblPr.remove(old)
    tblPr.append(parse_xml(f'<w:tblW {nsdecls("w")} w:w="{total_w}" w:type="dxa"/>'))

    # Update or add tblGrid
    for old in tbl._element.xpath('w:tblGrid'):
        tbl._element.remove(old)
    grid = parse_xml(f'<w:tblGrid {nsdecls("w")}/>')
    for w in col_widths_dxa:
        grid.append(parse_xml(f'<w:gridCol {nsdecls("w")} w:w="{w}"/>'))
    tbl._element.insert(tbl._element.index(tblPr) + 1, grid)

    # Row and cell widths + compact cell paragraph spacing
    for row in tbl.rows:
        for i, cell in enumerate(row.cells):
            tcPr = cell._element.get_or_add_tcPr()
            for old in tcPr.xpath('w:tcW'):
                tcPr.remove(old)
            w = col_widths_dxa[i]
            tcPr.append(parse_xml(f'<w:tcW {nsdecls("w")} w:w="{w}" w:type="dxa"/>'))
            for p in cell.paragraphs:
                pPr = p._p.get_or_add_pPr()
                for old in pPr.xpath('w:spacing'):
                    pPr.remove(old)
                pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="40" w:line="240" w:lineRule="auto"/>'))

def format_table_header(row):
    trPr = row._element.get_or_add_trPr()
    trPr.append(parse_xml(r'<w:tblHeader %s/>' % nsdecls('w')))
    
    for cell in row.cells:
        tcPr = cell._element.get_or_add_tcPr()
        shd = parse_xml(r'<w:shd %s w:fill="F2F2F2"/>' % nsdecls('w'))
        tcPr.append(shd)
        for p in cell.paragraphs:
            for r in p.runs:
                r.font.bold = True

def remove_elements_between(start_el, end_el):
    curr = start_el.getnext()
    while curr is not None and curr != end_el:
        nxt = curr.getnext()
        curr.getparent().remove(curr)
        curr = nxt

def format_heading_paragraph(p, text, style_name, font_size_pt, space_before_pt, space_after_pt, page_break_before=False):
    p.style = style_name
    p.text = ''
    pPr = p._p.get_or_add_pPr()

    for old in pPr.xpath('w:numPr'):
        pPr.remove(old)
    for old in pPr.xpath('w:ind'):
        pPr.remove(old)
    for old in pPr.xpath('w:spacing'):
        pPr.remove(old)
    for old in pPr.xpath('w:keepNext'):
        pPr.remove(old)
    pPr.append(parse_xml(f'<w:keepNext {nsdecls("w")}/>'))

    if page_break_before:
        if not pPr.xpath('w:pageBreakBefore'):
            pPr.append(parse_xml(f'<w:pageBreakBefore {nsdecls("w")}/>'))
    else:
        for old in pPr.xpath('w:pageBreakBefore'):
            pPr.remove(old)

    sp_b = int(space_before_pt * 20)
    sp_a = int(space_after_pt * 20)
    pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="{sp_b}" w:after="{sp_a}"/>'))

    run = p.add_run(text)
    run.font.name = 'Arial'
    run.font.size = Pt(font_size_pt)
    run.font.bold = True

def main():
    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent
    docs_dir = repo_root / 'docs'

    template_path = str(docs_dir / 'internal' / 'TFx_Plantilla_Memoria_es.docx')
    output_path = str(docs_dir / 'damaca_PEC1.docx')
    output_pdf = str(docs_dir / 'damaca_PEC1.pdf')

    if not os.path.exists(template_path):
        raise FileNotFoundError(f"Template not found at {template_path}")

    shutil.copyfile(template_path, output_path)
    doc = docx.Document(output_path)

    # =========================================================================
    # CONFIGURE DOCUMENT STYLES:
    # 1. Heading 1: Arial 18pt bold, clear spacing (18pt before, 6pt after), outlineLvl 0.
    # 2. Heading 2: Arial 14pt bold, clear spacing (12pt before, 4pt after), outlineLvl 1.
    # 3. Heading 3: Arial 12pt bold, clear spacing (9pt before, 3pt after), outlineLvl 2.
    # 4. Normal: Arial 11pt, clean line spacing (1.15) and paragraph spacing (6pt after).
    # 5. List Paragraph: Arial 11pt, 3pt space after, 1.1 line spacing.
    # 6. Remove outlineLvl from all non-Heading styles so body text is not treated as headings.
    # =========================================================================
    h1_style = doc.styles['Heading 1']
    h1_style.font.name = 'Arial'
    h1_style.font.size = Pt(18)
    h1_style.font.bold = True
    h1Pr = h1_style.element.get_or_add_pPr()
    for old in h1Pr.xpath('w:spacing'):
        h1Pr.remove(old)
    h1Pr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="360" w:after="120"/>'))

    h2_style = doc.styles['Heading 2']
    h2_style.font.name = 'Arial'
    h2_style.font.size = Pt(14)
    h2_style.font.bold = True
    h2Pr = h2_style.element.get_or_add_pPr()
    for old in h2Pr.xpath('w:spacing'):
        h2Pr.remove(old)
    h2Pr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="240" w:after="80"/>'))

    h3_style = doc.styles['Heading 3']
    h3_style.font.name = 'Arial'
    h3_style.font.size = Pt(12)
    h3_style.font.bold = True
    h3Pr = h3_style.element.get_or_add_pPr()
    for old in h3Pr.xpath('w:spacing'):
        h3Pr.remove(old)
    h3Pr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="180" w:after="60"/>'))

    normal_style = doc.styles['Normal']
    normal_style.font.name = 'Arial'
    normal_style.font.size = Pt(11)
    normalPr = normal_style.element.get_or_add_pPr()
    for old in normalPr.xpath('w:spacing'):
        normalPr.remove(old)
    normalPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="120" w:line="276" w:lineRule="auto"/>'))
    for old in normalPr.xpath('w:ind'):
        normalPr.remove(old)
    for old in normal_style.element.xpath('w:rPr/w:position'):
        old.getparent().remove(old)

    list_style = doc.styles['List Paragraph']
    list_style.font.name = 'Arial'
    list_style.font.size = Pt(11)
    listPr = list_style.element.get_or_add_pPr()
    for old in listPr.xpath('w:spacing'):
        listPr.remove(old)
    listPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="60" w:line="260" w:lineRule="auto"/>'))

    for s in doc.styles.element.xpath('.//w:style[@w:styleId="Normal"] | .//w:style[@w:styleId="TableGrid"]'):
        ppr = s.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}pPr')
        if ppr is not None:
            lvl = ppr.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}outlineLvl')
            if lvl is not None:
                ppr.remove(lvl)

    for s in doc.styles.element.xpath('.//w:style[@w:styleId="Heading1"]'):
        ppr = s.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}pPr')
        if ppr is None:
            ppr = parse_xml(f'<w:pPr {nsdecls("w")}><w:outlineLvl w:val="0"/></w:pPr>')
            s.append(ppr)
        else:
            lvl = ppr.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}outlineLvl')
            if lvl is None:
                lvl = parse_xml(f'<w:outlineLvl {nsdecls("w")} w:val="0"/>')
                ppr.append(lvl)

    for s in doc.styles.element.xpath('.//w:style'):
        sid = s.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}styleId')
        if not sid.startswith('Heading') and not sid.startswith('heading'):
            ppr = s.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}pPr')
            if ppr is not None:
                lvl = ppr.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}outlineLvl')
                if lvl is not None:
                    ppr.remove(lvl)

    # =========================================================================
    # 1. Cover page (Table 0)
    # =========================================================================
    t0 = doc.tables[0]
    p_title = t0.rows[0].cells[0].paragraphs[1]
    p_title.text = ''
    r = p_title.add_run('Generación acelerada en GPU de campos de distancia con signo (SDF) a partir de mallas poligonales')
    r.font.size = Pt(24)
    r.font.bold = True

    p_sub = t0.rows[0].cells[0].paragraphs[4]
    p_sub.text = ''
    r_sub = p_sub.add_run('Evaluación de técnicas de propagación y aplicación a iluminación global interactiva')
    r_sub.font.size = Pt(14)

    r2c1 = t0.rows[2].cells[1]
    r2c1.paragraphs[0].text = ''
    r_name = r2c1.paragraphs[0].add_run('Daniel Martí Casanova')
    r_name.font.bold = True
    r_name.font.size = Pt(12)

    r2c1.paragraphs[2].text = ''
    r2c1.paragraphs[2].add_run('Máster Universitario en Diseño y Programación de Videojuegos').font.size = Pt(12)

    r2c1.paragraphs[3].text = ''
    r2c1.paragraphs[3].add_run('Área del trabajo final: Programación Gráfica y Motores de Videojuegos').font.size = Pt(12)

    r2c1.paragraphs[5].text = ''
    r_tut_title = r2c1.paragraphs[5].add_run('Nombre Tutor/a de TF')
    r_tut_title.font.bold = True
    r_tut_title.font.size = Pt(12)

    r2c1.paragraphs[6].text = ''
    r2c1.paragraphs[6].add_run('Gonzalo Nicolás Barreales').font.size = Pt(10.5)

    r2c1.paragraphs[7].text = ''
    r_pra_title = r2c1.paragraphs[7].add_run('Profesor/a responsable de la asignatura')
    r_pra_title.font.bold = True
    r_pra_title.font.size = Pt(12)

    r2c1.paragraphs[8].text = ''
    r2c1.paragraphs[8].add_run('Joan Arnedo Moreno').font.size = Pt(10.5)

    r2c1.paragraphs[10].text = ''
    r2c1.paragraphs[10].add_run('11/10/2026').font.size = Pt(12)

    # =========================================================================
    # CLEAN LICENSE PAGE:
    # Keep only the CC BY-NC-ND 3.0 ES statement. Remove the 28 template
    # instructional paragraphs of "Licencias alternativas...".
    # =========================================================================
    t0_el = doc.tables[0]._element
    t1_el = doc.tables[1]._element
    p_first = t0_el.getnext()
    curr = t0_el.getnext()
    p_lic = None
    p_ficha = None
    while curr is not None and curr != t1_el:
        text = ''.join(curr.itertext()).strip()
        if 'Esta obra está sujeta a una licencia' in text and p_lic is None:
            p_lic = curr
        if 'FICHA DEL TRABAJO FINAL' in text:
            p_ficha = curr
        curr = curr.getnext()

    # Remove all empty paragraphs between p_first and p_lic to eliminate blank page 2
    if p_first is not None and p_lic is not None:
        c = p_first.getnext()
        while c is not None and c != p_lic:
            nxt = c.getnext()
            c.getparent().remove(c)
            c = nxt
        pPr_lic = p_lic.find('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}pPr')
        if pPr_lic is not None:
            for pb in pPr_lic.xpath('w:pageBreakBefore'):
                pPr_lic.remove(pb)

    if p_lic is not None and p_ficha is not None:
        pPr = p_ficha.get_or_add_pPr()
        for pb in pPr.xpath('w:pageBreakBefore'):
            pPr.remove(pb)
        pPr.append(parse_xml(f'<w:pageBreakBefore {nsdecls("w")}/>'))
        to_remove = []
        c = p_lic.getnext()
        while c is not None and c != p_ficha:
            to_remove.append(c)
            c = c.getnext()
        for el in to_remove:
            el.getparent().remove(el)

    # =========================================================================
    # 2. Table 1 (Ficha del Trabajo Final)
    # =========================================================================
    t1 = doc.tables[1]
    
    def set_cell(cell, text, bold=False, italic=False):
        cell.text = ''
        p = cell.paragraphs[0]
        pPr = p._p.get_or_add_pPr()
        for old in pPr.xpath('w:spacing'):
            pPr.remove(old)
        pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="20" w:line="240" w:lineRule="auto"/>'))
        add_runs_to_paragraph(p, text, base_bold=bold, base_italic=italic)

    set_cell(t1.rows[0].cells[1], 'Generación acelerada en GPU de campos de distancia con signo (SDF) a partir de mallas poligonales')
    set_cell(t1.rows[1].cells[1], 'Daniel Martí Casanova')
    set_cell(t1.rows[2].cells[0], 'Nombre del tutor/a de TF:', bold=True)
    set_cell(t1.rows[2].cells[1], 'Gonzalo Nicolás Barreales')
    set_cell(t1.rows[3].cells[0], 'Nombre del PRA:', bold=True)
    set_cell(t1.rows[3].cells[1], 'Joan Arnedo Moreno')
    set_cell(t1.rows[4].cells[1], '12/2026')
    set_cell(t1.rows[5].cells[1], 'Máster Universitario en Diseño y Programación de Videojuegos')
    set_cell(t1.rows[6].cells[1], 'Programación Gráfica y Motores de Videojuegos')
    set_cell(t1.rows[7].cells[1], 'Castellano')
    set_cell(t1.rows[8].cells[1], 'Campos de distancia con signo, generación acelerada en GPU, iluminación global.')

    # Resumen paragraphs
    resumen_texts = [
        "Este Trabajo Final de Máster propone diseñar, implementar y evaluar dos métodos de generación de campos de distancia con signo (*Signed Distance Fields*, SDF) volumétricos en GPU a partir de mallas poligonales. Estas representaciones permiten realizar consultas de distancia para el trazado de rayos por software, pero su generación puede resultar costosa al aumentar la resolución del volumen y la complejidad de la geometría.",
        "Se desarrollará un método base de cálculo de distancias mediante *compute shaders* y una alternativa que combine la captura de datos de superficie por rasterización con el algoritmo *Jump Flooding* (JFA) en tres dimensiones. El método base se comprobará con geometrías de control antes de utilizarlo como referencia.",
        "Los campos generados se integrarán en un visor interactivo construido a partir de un prototipo previo del autor. El visor permitirá examinar el volumen y observar su aplicación a iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves. Su función será apoyar la evaluación, sin constituir un motor gráfico de propósito general.",
        "La comparación analizará el tiempo de generación, el consumo de memoria de vídeo y el error de distancia sobre un conjunto delimitado de escenas. Se espera identificar las ventajas y limitaciones de cada método, sin presuponer una mejora uniforme en todas las condiciones. Esta primera entrega presenta la definición del proyecto, sus objetivos, la metodología y la planificación inicial."
    ]
    c_res = t1.rows[10].cells[0]
    c_res.text = ''
    for i, p_txt in enumerate(resumen_texts):
        p = c_res.paragraphs[0] if i == 0 else c_res.add_paragraph()
        add_runs_to_paragraph(p, p_txt)

    # Abstract paragraphs
    abstract_texts = [
        "This master's thesis proposes the design, implementation, and evaluation of two GPU methods for generating volumetric Signed Distance Fields (SDFs) from polygonal meshes. These representations support distance queries for software ray tracing, but their generation can become computationally expensive as volume resolution and geometric complexity increase.",
        "A baseline distance computation method will be implemented using compute shaders. An alternative approach will combine rasterized surface data with the three-dimensional Jump Flooding Algorithm (JFA). The baseline will first be checked against control geometries with known distances before being used as a reference.",
        "The generated fields will be integrated into an interactive viewer adapted from a prototype previously developed by the author. The viewer will support volume inspection and demonstrate one-bounce diffuse indirect lighting, ambient occlusion, and soft shadows. It will serve as an evaluation tool rather than a general-purpose graphics engine.",
        "The experimental comparison will examine generation time, video memory consumption, and distance error across a limited set of test scenes. The aim is to identify the advantages and limitations of each method without assuming consistent improvements under all conditions. This initial submission presents the project definition, objectives, methodology, and development schedule."
    ]
    c_abs = t1.rows[12].cells[0]
    c_abs.text = ''
    for i, p_txt in enumerate(abstract_texts):
        p = c_abs.paragraphs[0] if i == 0 else c_abs.add_paragraph()
        add_runs_to_paragraph(p, p_txt)

    # Set cantSplit on Ficha table rows
    for r in t1.rows:
        trPr = r._element.get_or_add_trPr()
        trPr.append(parse_xml(r'<w:cantSplit %s/>' % nsdecls('w')))

    # =========================================================================
    # Locate key anchor paragraphs
    # =========================================================================
    def find_p(text, style_prefix=None):
        for p in doc.paragraphs:
            if style_prefix and not p.style.name.startswith(style_prefix):
                continue
            if text in p.text:
                return p
        return None

    p_agradecimientos = find_p('Agradecimientos')
    p_declaracion = find_p('Declaración de uso de recursos de terceros')
    p_indice = find_p('Índice')
    p_figuras = find_p('Lista de figuras')
    p_intro = find_p('Introducción', 'Heading 1')
    p_contexto = find_p('Contexto y justificación del Trabajo', 'Heading 2')
    p_objetivos = find_p('Objetivos del Trabajo', 'Heading 2')
    p_impacto = find_p('Impacto en sostenibilidad, ético-social y de diversidad', 'Heading 2')
    p_enfoque = find_p('Enfoque y método seguido', 'Heading 2')
    p_planificacion = find_p('Planificación del Trabajo', 'Heading 2')
    p_productos = find_p('Breve sumario de productos obtenidos', 'Heading 2')
    p_otros_capitulos = find_p('Breve descripción de los otros capítulos de la memoria', 'Heading 2')
    p_estado_arte = find_p('Estado del arte', 'Heading 1')
    p_materiales = find_p('Materials y métodos', 'Heading 1') or find_p('Materiales y métodos', 'Heading 1')
    p_diseno = find_p('Diseño', 'Heading 1')
    p_resultados = find_p('Resultados', 'Heading 1')
    p_conclusiones = find_p('Conclusiones y Trabajo futuro', 'Heading 1')
    p_glosario = find_p('Glosario', 'Heading 1')
    p_biblio = find_p('Bibliografía', 'Heading 1')
    p_anexos = find_p('Anexos', 'Heading 1')

    # Clean elements between t1 (Ficha) and Agradecimientos:
    # Keep the paragraph with sectPr (section break to start Section 2 on Page 5).
    # Remove any empty paragraphs or explicit page breaks between t1 and p_agradecimientos.
    if p_agradecimientos is not None:
        curr = t1_el.getnext()
        while curr is not None and curr != p_agradecimientos._element:
            nxt = curr.getnext()
            if curr.xpath('.//w:sectPr'):
                for br in curr.xpath('.//w:br[@w:type="page"]'):
                    br.getparent().remove(br)
            else:
                if not ''.join(curr.itertext()).strip():
                    curr.getparent().remove(curr)
            curr = nxt

        # Remove pageBreakBefore from p_agradecimientos because sectPr already starts a new page
        pPr_ag = p_agradecimientos._p.get_or_add_pPr()
        for pb in pPr_ag.xpath('w:pageBreakBefore'):
            pPr_ag.remove(pb)

    remove_elements_between(p_agradecimientos._element, p_declaracion._element)
    p_ag_note = p_declaracion.insert_paragraph_before('*(Apartado reservado para la memoria final de la PEC 4)*')
    p_ag_note.style = 'Normal'
    p_ag_note.runs[0].font.italic = True

    # Helper to insert sections
    def insert_section(before_p, items, compact=False):
        for item_type, val in items:
            if item_type == 'h3':
                p = before_p.insert_paragraph_before('', style='Heading 3')
                pPr = p._p.get_or_add_pPr()
                for old in pPr.xpath('w:numPr'):
                    pPr.remove(old)
                for old in pPr.xpath('w:ind'):
                    pPr.remove(old)
                for old in pPr.xpath('w:spacing'):
                    pPr.remove(old)
                for old in pPr.xpath('w:keepNext'):
                    pPr.remove(old)
                pPr.append(parse_xml(f'<w:keepNext {nsdecls("w")}/>'))
                pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="200" w:after="60"/>'))
                run = p.add_run(val)
                run.font.name = 'Arial'
                run.font.size = Pt(12)
                run.font.bold = True
            elif item_type == 'subheading_bold':
                p = before_p.insert_paragraph_before('', style='Normal')
                pPr = p._p.get_or_add_pPr()
                sp_b = "120" if compact else "180"
                sp_a = "30" if compact else "60"
                pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="{sp_b}" w:after="{sp_a}"/>'))
                run = p.add_run(val)
                run.font.bold = True
                run.font.size = Pt(11) if compact else Pt(12)
            elif item_type == 'p':
                p = before_p.insert_paragraph_before('', style='Normal')
                if compact:
                    pPr = p._p.get_or_add_pPr()
                    pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="40" w:line="240" w:lineRule="auto"/>'))
                    add_runs_to_paragraph(p, val, font_size=Pt(10))
                else:
                    add_runs_to_paragraph(p, val)
            elif item_type == 'bullet':
                p = before_p.insert_paragraph_before('', style='List Paragraph')
                if compact:
                    pPr = p._p.get_or_add_pPr()
                    pPr.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="0" w:after="30" w:line="240" w:lineRule="auto"/>'))
                    add_runs_to_paragraph(p, val, font_size=Pt(10))
                else:
                    add_runs_to_paragraph(p, val)
            elif item_type == 'image':
                img_path, caption_text, fig_num = val
                p_img = before_p.insert_paragraph_before('', style='Normal')
                pPr_img = p_img._p.get_or_add_pPr()
                for old in pPr_img.xpath('w:ind'):
                    pPr_img.remove(old)
                for old in pPr_img.xpath('w:spacing'):
                    pPr_img.remove(old)
                for old in pPr_img.xpath('w:keepNext'):
                    pPr_img.remove(old)
                pPr_img.append(parse_xml(f'<w:keepNext {nsdecls("w")}/>'))
                pPr_img.append(parse_xml(f'<w:jc {nsdecls("w")} w:val="center"/>'))
                pPr_img.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="120" w:after="40"/>'))
                r_img = p_img.add_run()
                r_img.add_picture(img_path, width=Inches(3.0))

                p_cap = before_p.insert_paragraph_before('', style='Caption')
                pPr_cap = p_cap._p.get_or_add_pPr()
                for old in pPr_cap.xpath('w:ind'):
                    pPr_cap.remove(old)
                for old in pPr_cap.xpath('w:spacing'):
                    pPr_cap.remove(old)
                pPr_cap.append(parse_xml(f'<w:jc {nsdecls("w")} w:val="center"/>'))
                pPr_cap.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="40" w:after="140" w:line="240" w:lineRule="auto"/>'))

                r_fig = p_cap.add_run(f'Figura {fig_num}: ')
                r_fig.font.name = 'Arial'
                r_fig.font.size = Pt(9.5)
                r_fig.font.bold = True

                r_txt = p_cap.add_run(caption_text)
                r_txt.font.name = 'Arial'
                r_txt.font.size = Pt(9.5)
                r_txt.font.bold = False

    # Fill Declaración de uso de recursos de terceros (using compact normal styling so it fits neatly on page 5)
    remove_elements_between(p_declaracion._element, p_indice._element)
    declaracion_items = [
        ('subheading_bold', 'Bibliotecas y tecnologías'),
        ('p', 'El desarrollo utilizará C++ y OpenGL 4.6 como base de la aplicación y de los procesos de renderizado y cálculo en GPU. Se prevé emplear las siguientes bibliotecas:'),
        ('bullet', '• SDL3: creación de ventanas, gestión del contexto gráfico y tratamiento de eventos de entrada.'),
        ('bullet', '• GLM: operaciones de álgebra lineal y transformaciones.'),
        ('bullet', '• Assimp: importación de mallas y escenas tridimensionales.'),
        ('bullet', '• Dear ImGui: interfaz para modificar parámetros, seleccionar modos de visualización y consultar métricas.'),
        ('subheading_bold', 'Modelos y escenas tridimensionales'),
        ('p', 'Se prevé utilizar Cornell Box como escena controlada para examinar la iluminación indirecta y las sombras, y Crytek Sponza para realizar pruebas con mayor complejidad geométrica y oclusión. Estas escenas se complementarán con modelos geométricos de control.'),
        ('p', 'La selección de las versiones concretas y las adaptaciones necesarias se realizará durante la fase de especificación y diseño, de acuerdo con las condiciones de entrada admitidas por los generadores. Se documentarán la procedencia y las condiciones de uso de los recursos empleados.'),
        ('subheading_bold', 'Prototipo previo del autor'),
        ('p', 'El proyecto reutilizará un prototipo propio de *path tracer* interactivo basado en *ray marching* sobre primitivas SDF analíticas y trazado en espacio de pantalla sobre el búfer de profundidad.'),
        ('p', 'Este desarrollo previo servirá como punto de partida del visor. Su adaptación permitirá disponer de un entorno de pruebas antes de integrar los campos volumétricos generados a partir de mallas.'),
        ('p', 'La generación de SDF en GPU, la implementación de la alternativa basada en JFA en tres dimensiones y la evaluación comparativa se desarrollarán durante el TFM. Estas contribuciones se distinguirán de las funcionalidades procedentes del prototipo anterior.'),
        ('subheading_bold', 'Uso de herramientas de inteligencia artificial'),
        ('p', 'Se declara el uso de Antigravity / Gemini y OpenAI ChatGPT como apoyo para estructurar la propuesta, redactar y revisar el texto, detectar incoherencias y discutir decisiones técnicas.'),
        ('p', 'La revisión del contenido, la selección de los métodos y la validación de las implementaciones y los resultados serán responsabilidad del autor. Las respuestas de estas herramientas no sustituirán la consulta de las fuentes originales ni la comprobación experimental.')
    ]
    insert_section(p_indice, declaracion_items, compact=True)

    # Clean empty Heading 1 paragraphs before Introducción
    prev_intro = p_intro._element.getprevious()
    if prev_intro is not None and prev_intro.tag.endswith('p'):
        p_style = prev_intro.xpath('.//w:pStyle/@w:val')
        if p_style and p_style[0] == 'Heading1' and not ''.join(prev_intro.itertext()).strip():
            prev_intro.getparent().remove(prev_intro)

    # Introducción: remove template instructions between p_intro and p_contexto
    remove_elements_between(p_intro._element, p_contexto._element)

    # 1.1 Contexto
    remove_elements_between(p_contexto._element, p_objetivos._element)
    img_file = str(docs_dir / 'assets' / 'screenshot_20261011_164754.bmp')
    fig1_caption = 'Ejemplo de iluminación global, la luz rebota en el suelo e ilumina la parte inferior de la esfera. La luz que rebota en las paredes, ilumina la esfera con el color de cada pared.'
    contexto_items = [
        ('p', 'La iluminación global en tiempo real plantea un compromiso entre calidad visual y coste de cálculo. Además de la luz que llega directamente desde una fuente hasta una superficie, considera las contribuciones indirectas producidas por las reflexiones sobre otras superficies de la escena, descritas por la ecuación de renderizado [1]. Estas interacciones permiten representar fenómenos como la transferencia de color entre objetos próximos, conocida como *color bleeding* (Figura 1).'),
        ('image', (img_file, fig1_caption, 1)),
        ('p', 'En este contexto, los campos de distancia con signo ofrecen una representación de la geometría que puede utilizarse para realizar consultas espaciales durante el trazado de rayos por software. Su aplicación a sistemas de iluminación como Lumen, de Unreal Engine 5 [2], constituye una referencia para estudiar este enfoque. El presente trabajo se centrará en la generación de dichos campos y en una demostración acotada de su uso, sin intentar reproducir un sistema de iluminación de la complejidad de Lumen.'),
        ('p', 'Un campo de distancia con signo, o SDF, representa la distancia mínima desde un punto del espacio hasta la superficie más cercana. El signo permite distinguir el interior del exterior cuando la geometría admite esa clasificación. A partir de esta información, el algoritmo de *sphere tracing* [3] adapta la longitud de los pasos de un rayo según la distancia a la superficie. En una representación volumétrica discretizada, la precisión del campo y los errores de aproximación pueden afectar al trazado y producir artefactos, por lo que su comportamiento debe evaluarse.'),
        ('p', 'La generación del volumen de distancias es uno de los problemas que plantea el uso de SDF a partir de mallas poligonales. El cálculo de distancias entre los puntos de una cuadrícula tridimensional y los triángulos de una malla puede requerir un número elevado de evaluaciones. Su coste depende tanto de la resolución del volumen como de la complejidad de la geometría. Aunque el precálculo puede ser suficiente para determinados usos estáticos, reducir el tiempo de generación permitiría explorar otras condiciones de actualización y uso.'),
        ('p', 'La motivación del proyecto parte de un límite concreto de un prototipo previo del autor: el visor existente utiliza primitivas SDF analíticas. Para trabajar con mallas poligonales, se propone sustituir estas primitivas por texturas tridimensionales que almacenen el campo de distancias. Esta transición proporciona un contexto práctico para estudiar la generación del volumen y observar cómo sus errores afectan al resultado visual.'),
        ('p', 'El trabajo comparará un método base de cálculo de distancias en GPU con una alternativa que combine la captura de datos de superficie mediante rasterización y la propagación espacial mediante *Jump Flooding* (JFA) [4]. La pregunta que orienta la evaluación es qué compromiso ofrece esta alternativa entre tiempo de generación, consumo de memoria y error de distancia respecto al método de referencia.'),
        ('p', 'El producto será un prototipo técnico de generación y evaluación de SDF, acompañado de un visor interactivo, dentro del área de programación gráfica y motores de videojuegos. En el visor se podrá cargar una escena de prueba, seleccionar el campo generado por cada método, inspeccionar cortes del volumen y observar su utilización en la iluminación. La generación de los campos y su comparación constituirán el núcleo del trabajo; el visor actuará como herramienta de demostración y análisis.')
    ]
    insert_section(p_objetivos, contexto_items)

    # 1.2 Objetivos
    remove_elements_between(p_objetivos._element, p_impacto._element)
    objetivos_items = [
        ('h3', 'Objetivo general'),
        ('p', 'Diseñar, implementar y evaluar dos métodos de generación de campos de distancia con signo en GPU a partir de mallas poligonales, comparando una alternativa basada en rasterización y propagación espacial con un método de referencia, y analizando su tiempo de generación, consumo de memoria y precisión mediante pruebas numéricas y un visor interactivo de iluminación.'),
        ('h3', 'Objetivos específicos'),
        ('bullet', '• **OE1.** Adaptar el visor de partida: integrar el prototipo previo del autor en una aplicación modular en C++ con OpenGL 4.6 y SDL3, con controles de cámara, iluminación e interfaz, para disponer de un entorno de pruebas durante el desarrollo.'),
        ('bullet', '• **OE2.** Incorporar el muestreo de campos volumétricos: sustituir las primitivas SDF analíticas y el trazado en espacio de pantalla por el muestreo de texturas tridimensionales de distancia, adaptando el *ray marching* para representar iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves. El sombreado utilizará también la información de superficie necesaria.'),
        ('bullet', '• **OE3.** Implementar y comprobar el método de referencia: desarrollar un generador de distancias volumétricas en GPU mediante *compute shaders*. Antes de utilizar sus resultados como referencia, comprobar su funcionamiento con geometrías de control cuyas distancias sean conocidas.'),
        ('bullet', '• **OE4.** Implementar la alternativa de generación rápida: diseñar y desarrollar un método que combine la extracción de datos de superficie mediante rasterización de un G-Buffer con la propagación mediante JFA en tres dimensiones. Se estudiarán la cobertura de la captura, el comportamiento de la propagación y el procedimiento de determinación del signo.'),
        ('bullet', '• **OE5.** Facilitar la inspección y comparación de los campos: incorporar un selector entre los volúmenes generados por ambos métodos y modos de visualización para examinar cortes del volumen y componentes de iluminación. La visualización adicional de vóxeles tendrá menor prioridad que estas herramientas.'),
        ('bullet', '• **OE6.** Ejecutar la evaluación experimental: diseñar y realizar pruebas sobre modelos geométricos de control y las escenas seleccionadas para comparar el tiempo de generación en milisegundos, el consumo de memoria de vídeo y el error de distancia de la alternativa respecto al método base. Las condiciones de comparación y el procedimiento de medición se concretarán durante la fase de diseño.'),
        ('h3', 'Delimitación del alcance'),
        ('p', 'El trabajo se centrará en dos procedimientos de generación de SDF en GPU: un método de referencia y una alternativa basada en rasterización y JFA. No se plantea desarrollar un generador universal para cualquier tipo de malla ni un motor gráfico de propósito general.'),
        ('p', 'Durante la fase de especificación y diseño se definirán las condiciones de entrada de las mallas, el procedimiento de obtención del signo, la captura de datos de superficie y el protocolo experimental. La selección final de las versiones de Cornell Box y Crytek Sponza dependerá de su adecuación a estas condiciones. Los modelos geométricos de control permitirán comprobar el funcionamiento antes de abordar escenas de mayor complejidad.'),
        ('p', 'El rendimiento del visor se analizará por separado del tiempo de generación del campo. La navegación y la visualización deberán permitir examinar los resultados de forma interactiva, pero no se establece como requisito inicial reconstruir todo el volumen en cada fotograma.'),
        ('p', 'Ante una reducción de alcance, se conservarán los dos generadores, su comprobación numérica, la comparación experimental y una demostración básica de iluminación indirecta difusa de un rebote. La inspección por cortes tendrá prioridad sobre la visualización de vóxeles. También podrán reducirse los controles secundarios de la interfaz y los refinamientos visuales, sin sustituir con ellos el trabajo de evaluación.')
    ]
    insert_section(p_impacto, objetivos_items)

    # 1.3 Impacto
    remove_elements_between(p_impacto._element, p_enfoque._element)
    impacto_items = [
        ('h3', 'Impacto en sostenibilidad'),
        ('p', 'El proyecto estudiará el uso de recursos computacionales mediante la comparación del tiempo y la memoria necesarios para generar campos de distancia en GPU. Los resultados permitirán identificar en qué condiciones una aproximación basada en propagación resulta ventajosa y qué pérdida de precisión puede introducir.'),
        ('p', 'La implementación se planteará sin depender de extensiones específicas de trazado de rayos acelerado por hardware. No obstante, seguirá requiriendo una GPU compatible con las tecnologías seleccionadas.'),
        ('p', 'El trabajo no incluirá mediciones de consumo energético ni de emisiones. Por tanto, una posible mejora del tiempo de ejecución o del uso de memoria no se presentará como una reducción demostrada del impacto ambiental.'),
        ('h3', 'Impacto ético-social'),
        ('p', 'Se prevé publicar el código y la documentación para facilitar el estudio de los métodos y la reproducción de las pruebas. La documentación describirá los requisitos, las condiciones de reutilización y las limitaciones observadas.'),
        ('p', 'Se mantendrá la atribución de las bibliotecas, modelos y fuentes utilizadas, así como una declaración del uso de asistentes de inteligencia artificial. La utilidad del proyecto para estudiantes y desarrolladores se considerará una contribución potencial, sin atribuirle una reducción demostrada de los costes de producción.'),
        ('h3', 'Impacto en diversidad'),
        ('p', 'El proyecto no contempla recopilar datos demográficos ni realizar estudios con usuarios. Las medidas relacionadas con la diversidad se orientarán a facilitar el acceso al conocimiento mediante una documentación clara y una descripción de los requisitos técnicos.'),
        ('p', 'La necesidad de disponer de una GPU compatible puede limitar el acceso al prototipo. Por ello, no se dará por supuesta su ejecución adecuada en equipos de prestaciones reducidas.')
    ]
    insert_section(p_enfoque, impacto_items)

    # 1.4 Enfoque y método
    remove_elements_between(p_enfoque._element, p_planificacion._element)
    enfoque_items = [
        ('p', 'El proyecto combinará un marco de investigación, un método de desarrollo del software y un sistema de seguimiento del trabajo.'),
        ('h3', 'Marco de investigación'),
        ('p', 'Se utilizará la metodología de investigación en ciencias del diseño, *Design Science Research Methodology* (DSRM), propuesta por Peffers et al. [5]. Este marco organiza el trabajo alrededor de la construcción y evaluación de un artefacto tecnológico.'),
        ('p', 'En este proyecto, el artefacto principal estará formado por los métodos de generación de campos de distancia en GPU. El visor servirá para demostrar su uso y examinar las consecuencias visuales de los campos obtenidos.'),
        ('p', 'El proceso abarcará la identificación del problema, la definición de objetivos, el diseño y desarrollo, la demostración, la evaluación y la comunicación de resultados. La comparación experimental permitirá contrastar los métodos sin presuponer que la alternativa basada en JFA será superior en todas las métricas o escenas.'),
        ('h3', 'Metodología de producción del software'),
        ('p', 'El desarrollo seguirá un enfoque incremental e iterativo basado en prototipos. Se adaptará primero el visor existente y se incorporará después el muestreo de texturas tridimensionales. Para esta integración inicial se utilizarán volúmenes de prueba, sin esperar a que los generadores a partir de mallas estén terminados.'),
        ('p', 'A continuación, se incorporarán el método base, la alternativa JFA y los modos de inspección. Cada incremento se comprobará con casos reducidos antes de pasar a escenas de mayor complejidad.'),
        ('p', 'Las decisiones de diseño y los problemas encontrados se documentarán durante la implementación. La memoria se redactará de forma progresiva, en lugar de concentrar toda la documentación al final del proyecto.'),
        ('h3', 'Gestión del proyecto'),
        ('p', 'El seguimiento se realizará mediante un tablero Personal Kanban, con tareas vinculadas a los objetivos y a los hitos de entrega. Se revisará semanalmente el avance para detectar bloqueos, actualizar prioridades y registrar las desviaciones respecto a la planificación.'),
        ('p', 'Git se utilizará para el control de versiones y la trazabilidad de los cambios, siguiendo la convención *Conventional Commits*. Las revisiones con el tutor permitirán contrastar el alcance y orientar las decisiones que afecten a la viabilidad del trabajo.'),
        ('h3', 'Distribución del trabajo entre entregas'),
        ('bullet', '• **PEC 1 (Definición y planificación):** se concretarán el problema, la motivación, los objetivos, el alcance y la planificación inicial. Esta entrega no incluirá nueva implementación del proyecto.'),
        ('bullet', '• **PEC 2 (Estudio y diseño):** se desarrollarán el estado del arte, los requisitos, la arquitectura y el protocolo experimental. También se definirán las restricciones geométricas, la estrategia de signo y la captura de superficies. Una exploración técnica limitada permitirá comprobar los supuestos de mayor riesgo.'),
        ('bullet', '• **PEC 3 (Implementación principal):** se adaptará el visor, se integrarán las texturas 3D y se desarrollarán ambos generadores. Se realizarán las primeras comprobaciones numéricas y mediciones comparativas, documentando las decisiones y los cambios de diseño.'),
        ('bullet', '• **PEC 4 (Evaluación y cierre):** se completará la batería experimental, se analizarán los resultados y se redactarán las conclusiones. También se consolidará la memoria y se preparará la presentación y defensa en vídeo.')
    ]
    insert_section(p_planificacion, enfoque_items)

    # 1.5 Planificación
    remove_elements_between(p_planificacion._element, p_productos._element)
    plan_intro_items = [
        ('h3', 'Recursos necesarios'),
        ('bullet', '• **Hardware:** se prevé utilizar una estación de trabajo con procesador multinúcleo x86-64, al menos 16 GB de memoria RAM y una tarjeta gráfica dedicada compatible con OpenGL 4.6, *compute shaders* y texturas tridimensionales con acceso mediante *shader image load/store*. Las resoluciones de trabajo se ajustarán a la memoria disponible y a los resultados de las pruebas.'),
        ('bullet', '• **Software y entorno de desarrollo:** se utilizará GNU/Linux, un compilador compatible con C++20 (GCC o Clang), CMake 3.20 o superior y Visual Studio Code. RenderDoc y las herramientas de monitorización de GPU servirán para inspeccionar recursos y analizar el comportamiento de la aplicación.'),
        ('bullet', '• **Bibliotecas:** la aplicación utilizará SDL3, GLM, Assimp y Dear ImGui para la gestión de ventanas y entrada, las operaciones matemáticas, la importación de mallas y la interfaz.'),
        ('bullet', '• **Modelos de prueba:** se emplearán geometrías de control y las versiones seleccionadas de Cornell Box y Crytek Sponza, en formatos compatibles con el importador, como OBJ o glTF.'),
        ('h3', 'Dedicación y seguimiento'),
        ('p', 'La planificación se elaborará a partir de una dedicación estimada de 20 horas semanales (correspondiente a los 12 créditos ECTS de la asignatura, con un total estimado de 300 horas distribuidas a lo largo del semestre), que incluirá investigación, desarrollo, pruebas, documentación y revisión de las entregas.'),
        ('p', 'La implementación principal se concentrará en el periodo correspondiente a la PEC 3. La documentación se mantendrá durante todas las fases, y la preparación del procedimiento de medición deberá quedar avanzada antes de comenzar la batería experimental final.'),
        ('p', 'La disponibilidad durante el periodo final se tendrá en cuenta al concretar la dedicación. La entrega final de la PEC 4 se realizará el 23 de diciembre de 2026, concluyendo el trabajo antes del receso navideño. Si fuera necesario resolver incidencias durante la recta final, se ajustará el número de configuraciones experimentales secundarias, preservando la validez de la comparación principal entre los dos métodos.'),
        ('p', 'El seguimiento semanal permitirá contrastar el trabajo realizado con el previsto y justificar las modificaciones del calendario.'),
        ('h3', 'Hitos por PEC'),
        ('bullet', '• **PEC 1 (11/10/2026) — Definición del proyecto e introducción:** se completarán la ficha del trabajo y el capítulo 1 de la memoria, con la justificación, los objetivos, el impacto, la metodología y la planificación. También se organizarán el repositorio y la documentación inicial.'),
        ('bullet', '• **PEC 2 (08/11/2026) — Estado del arte, especificación y arquitectura:** se redactará el capítulo 2 y se desarrollará el contenido inicial del capítulo 3: requisitos, escenas previstas y protocolo de evaluación. Se definirá la arquitectura del sistema, junto con las restricciones de las mallas y las estrategias de captura y signo. Se realizará una exploración técnica limitada de los supuestos críticos de la alternativa.'),
        ('bullet', '• **PEC 3 (29/11/2026) — Implementación y resultados intermedios:** se adaptará el visor, se sustituirán las primitivas analíticas por texturas 3D y se implementarán los dos generadores. Se incorporarán la selección de campos y los modos de inspección prioritarios. La entrega incluirá comprobaciones iniciales, primeras mediciones y la actualización del capítulo 4.'),
        ('bullet', '• **PEC 4 (23/12/2026) — Resultados, memoria final y defensa:** se completarán las pruebas, el análisis del capítulo 5 y las conclusiones del capítulo 6. Se revisará el diseño documentado y se consolidará la memoria. La presentación y la grabación de la defensa se prepararán antes del margen final de contingencia.'),
        ('h3', 'Cronograma'),
        ('p', 'La Tabla 1 recoge los periodos previstos para cada actividad. Los solapamientos responden a tareas que pueden avanzar parcialmente en paralelo: por ejemplo, la integración del visor con texturas 3D comenzará con volúmenes de prueba antes de disponer del generador base.')
    ]
    insert_section(p_productos, plan_intro_items)

    # Insert Table 1 (Cronograma) before p_productos
    p_t1_title = p_productos.insert_paragraph_before('Tabla 1. Cronograma del proyecto.', style='Caption')
    p_t1_title.runs[0].font.bold = True

    cronograma_data = [
        ["Fase", "Actividad", "Inicio", "Fin"],
        ["PEC 1", "Definición del proyecto y ficha del trabajo", "23/09/2026", "06/10/2026"],
        ["PEC 1", "Redacción del capítulo 1 y preparación de la entrega", "06/10/2026", "11/10/2026"],
        ["PEC 2", "Investigación y redacción del estado del arte", "12/10/2026", "25/10/2026"],
        ["PEC 2", "Especificación de requisitos y protocolo de evaluación", "23/10/2026", "01/11/2026"],
        ["PEC 2", "Diseño arquitectónico y exploración técnica preliminar", "30/10/2026", "08/11/2026"],
        ["PEC 3", "Adaptación del prototipo previo del visor", "09/11/2026", "15/11/2026"],
        ["PEC 3", "Transición a texturas 3D e integración del trazado para iluminación", "13/11/2026", "19/11/2026"],
        ["PEC 3", "Implementación y comprobación del generador base", "16/11/2026", "22/11/2026"],
        ["PEC 3", "Implementación del generador alternativo basado en rasterización y JFA", "19/11/2026", "25/11/2026"],
        ["PEC 3", "Integración, primeras mediciones y preparación de la entrega", "23/11/2026", "29/11/2026"],
        ["PEC 4", "Batería experimental final y recogida de datos", "30/11/2026", "08/12/2026"],
        ["PEC 4", "Análisis de resultados y discusión", "07/12/2026", "14/12/2026"],
        ["PEC 4", "Revisión del diseño, conclusiones y consolidación de la memoria", "12/12/2026", "18/12/2026"],
        ["PEC 4", "Preparación de la presentación y grabación de la defensa", "17/12/2026", "20/12/2026"],
        ["PEC 4", "Margen de contingencia y comprobación final de los entregables", "21/12/2026", "23/12/2026"]
    ]
    t_crono = doc.add_table(rows=len(cronograma_data), cols=4)
    p_productos._element.addprevious(t_crono._element)
    set_table_borders_and_margins(t_crono)
    col_w_crono_dxa = [1000, 5105, 1200, 1200]
    set_table_columns_and_widths(t_crono, col_w_crono_dxa)
    for r_idx, row_data in enumerate(cronograma_data):
        row = t_crono.rows[r_idx]
        for c_idx, val in enumerate(row_data):
            cell = row.cells[c_idx]
            cell.text = val
            p = cell.paragraphs[0]
            p.runs[0].font.size = Pt(9.5)
    format_table_header(t_crono.rows[0])

    p_crono_source = p_productos.insert_paragraph_before('*Fuente: elaboración propia.*', style='Normal')
    p_crono_source.runs[0].font.italic = True
    p_crono_source.runs[0].font.size = Pt(9.5)

    p_crono_post = p_productos.insert_paragraph_before('La entrega final está prevista para el 23 de diciembre de 2026. El margen de contingencia no se considerará tiempo disponible para añadir funcionalidades nuevas, sino una reserva para resolver incidencias y corregir los entregables.', style='Normal')

    p_riesgos_h3 = p_productos.insert_paragraph_before('Riesgos y medidas de mitigación', style='Heading 3')
    p_riesgos_intro = p_productos.insert_paragraph_before('La Tabla 2 recoge los principales riesgos técnicos y organizativos. Las valoraciones de probabilidad e impacto son iniciales y se revisarán conforme avance el proyecto.', style='Normal')

    p_t2_title = p_productos.insert_paragraph_before('Tabla 2. Riesgos y estrategias de mitigación.', style='Caption')
    p_t2_title.runs[0].font.bold = True

    riesgos_data = [
        ["ID", "Riesgo", "Probabilidad", "Impacto", "Mitigación y contingencia"],
        ["R1", "Consumo excesivo de memoria de vídeo por los volúmenes y las estructuras auxiliares.", "Media", "Alto", "Parametrizar la resolución, utilizando inicialmente tamaños exploratorios como 64³, 128³ y 256³. Estudiar formatos como GL_R16F y GL_R32F y contabilizar también los recursos temporales utilizados durante la generación."],
        ["R2", "Rendimiento insuficiente para utilizar el visor de forma interactiva.", "Media", "Medio", "Medir por separado el renderizado y la generación del campo. Ajustar el número de pasos de trazado, los criterios de parada y la resolución de cálculo de la iluminación indirecta."],
        ["R3", "Cobertura incompleta de la geometría, errores de distancia o fallos en la determinación del signo.", "Alta", "Alto", "Comenzar con mallas de control simples y cerradas. Examinar por separado la captura, la distancia y el signo, y distinguir los errores del campo de los artefactos introducidos por el renderizado. Concretar las correcciones durante el diseño y la implementación."],
        ["R4", "Dificultades en la implementación de JFA en tres dimensiones.", "Media", "Alto", "Desarrollar y comprobar la propagación en casos reducidos antes de integrarla con escenas completas. Contrastar los resultados esperados para distinguir los fallos de implementación del error de aproximación del método."],
        ["R5", "Retrasos respecto a los hitos de entrega.", "Media", "Alto", "Revisar semanalmente el avance. Proteger los dos generadores, las comprobaciones numéricas, la comparación experimental y la demostración básica de iluminación. Reducir primero la visualización de vóxeles, los controles secundarios y los refinamientos visuales."]
    ]
    t_riesgos = doc.add_table(rows=len(riesgos_data), cols=5)
    p_productos._element.addprevious(t_riesgos._element)
    set_table_borders_and_margins(t_riesgos)
    col_w_r_dxa = [450, 2450, 1350, 1050, 3200]
    set_table_columns_and_widths(t_riesgos, col_w_r_dxa)
    for r_idx, row_data in enumerate(riesgos_data):
        row = t_riesgos.rows[r_idx]
        for c_idx, val in enumerate(row_data):
            cell = row.cells[c_idx]
            cell.text = val
            p = cell.paragraphs[0]
            p.runs[0].font.size = Pt(9.0)
    format_table_header(t_riesgos.rows[0])

    p_r_source = p_productos.insert_paragraph_before('*Fuente: elaboración propia.*', style='Normal')
    p_r_source.runs[0].font.italic = True
    p_r_source.runs[0].font.size = Pt(9.5)

    p_r_post = p_productos.insert_paragraph_before('Las medidas de contingencia darán prioridad a obtener resultados comparables y documentados. Una demostración visual más completa no compensará la ausencia de comprobaciones o de datos suficientes para evaluar los métodos.', style='Normal')

    # 1.6 Breve sumario de productos obtenidos
    remove_elements_between(p_productos._element, p_otros_capitulos._element)
    productos_items = [
        ('p', 'En esta primera entrega se definen los productos previstos. Su desarrollo y evaluación se realizarán en las fases posteriores.'),
        ('h3', 'Aplicación interactiva de pruebas'),
        ('p', 'Se desarrollará una aplicación modular en C++ y OpenGL 4.6 que permita cargar las escenas seleccionadas, navegar por ellas y modificar las fuentes de luz.'),
        ('p', 'La interfaz permitirá seleccionar los campos generados por cada método, modificar los parámetros previstos y consultar métricas de funcionamiento.'),
        ('h3', 'Módulos de generación de SDF en GPU'),
        ('p', 'Se implementarán un método base de cálculo de distancias mediante *compute shaders* y una alternativa que combine la captura de superficie por rasterización con JFA en tres dimensiones.'),
        ('p', 'Los módulos se acompañarán de comprobaciones sobre geometrías de control y de documentación sobre sus condiciones de entrada y limitaciones.'),
        ('h3', 'Sistema de iluminación e inspección'),
        ('p', 'El visor utilizará los campos de distancia para representar iluminación indirecta difusa de un rebote, oclusión ambiental y sombras suaves.'),
        ('p', 'Se incorporará la inspección mediante cortes del volumen y la visualización de componentes de iluminación. La representación adicional de vóxeles quedará subordinada al cumplimiento del núcleo del proyecto.'),
        ('h3', 'Banco de pruebas y datos de evaluación'),
        ('p', 'Se obtendrá un conjunto de mediciones sobre las geometrías y escenas seleccionadas. Los datos incluirán tiempos de generación, consumo de memoria y error de distancia respecto al método de referencia.'),
        ('p', 'Los resultados se presentarán mediante tablas y gráficos, junto con las condiciones necesarias para interpretar y reproducir las pruebas.'),
        ('h3', 'Memoria y presentación del trabajo'),
        ('p', 'Se entregará la memoria final conforme a la plantilla y a los requisitos de la asignatura. El documento recogerá el diseño, la implementación, los resultados, las limitaciones y las conclusiones.'),
        ('p', 'También se preparará una presentación audiovisual y una grabación de la defensa que expliquen el trabajo y muestren el funcionamiento del prototipo.')
    ]
    insert_section(p_otros_capitulos, productos_items)

    # 1.7 Breve descripción de los otros capítulos de la memoria
    remove_elements_between(p_otros_capitulos._element, p_estado_arte._element)
    capitulos_items = [
        ('h3', 'Capítulo 2. Estado del arte'),
        ('p', 'Presentará los fundamentos de iluminación global relacionados con el proyecto, los campos de distancia y su uso para el trazado de rayos por software. Revisará las técnicas de generación de SDF en GPU, JFA y las referencias industriales relevantes, como Lumen. También abordará los problemas de discretización, captura de superficies y determinación del signo.'),
        ('h3', 'Capítulo 3. Materiales y métodos'),
        ('p', 'Definirá los requisitos funcionales y no funcionales, justificará las herramientas seleccionadas y describirá el entorno de trabajo. Recogerá las características de las escenas, las restricciones de entrada y el protocolo experimental para medir tiempo, memoria y error.'),
        ('h3', 'Capítulo 4. Diseño'),
        ('p', 'Describirá la arquitectura del sistema y la organización de los generadores y del visor. Incluirá las decisiones sobre captura de superficie, cálculo del signo, propagación, almacenamiento de los volúmenes e integración con el trazado y el sombreado. Se actualizará durante la implementación para reflejar los cambios adoptados.'),
        ('h3', 'Capítulo 5. Resultados'),
        ('p', 'Presentará y analizará las mediciones obtenidas. Comparará los métodos en las distintas condiciones de prueba e incluirá capturas que permitan relacionar los errores numéricos con el comportamiento visual. Se discutirán las limitaciones y los casos en los que cada aproximación resulte más o menos adecuada.'),
        ('h3', 'Capítulo 6. Conclusiones y trabajo futuro'),
        ('p', 'Contrastará los resultados con los objetivos iniciales y resumirá los compromisos observados entre coste computacional y precisión. También recogerá las limitaciones del proyecto y posibles extensiones, como estructuras jerárquicas, texturas dispersas o iluminación con más rebotes.'),
        ('h3', 'Capítulo 7. Glosario'),
        ('p', 'Definirá los términos y acrónimos empleados, como SDF, GI, VRAM, JFA, G-Buffer y *ray marching*.'),
        ('h3', 'Capítulo 8. Bibliografía'),
        ('p', 'Recogerá las fuentes académicas y técnicas citadas, con un formato uniforme y ordenadas según su primera aparición en el texto.'),
        ('h3', 'Capítulo 9. Anexos'),
        ('p', 'Incluirá el material complementario necesario para consultar o reproducir el trabajo: detalles del entorno de pruebas, instrucciones de ejecución, tablas ampliadas de resultados y fragmentos de código relevantes.')
    ]
    insert_section(p_estado_arte, capitulos_items)

    # =========================================================================
    # NORMALIZE CHAPTER 1 HEADINGS (STRIP numPr/ind, UNIFY FONT & WEIGHT)
    # =========================================================================
    format_heading_paragraph(p_intro, '1. Introducción', 'Heading 1', 18, 20, 8, page_break_before=True)
    format_heading_paragraph(p_contexto, '1.1. Contexto y justificación del Trabajo', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_objetivos, '1.2. Objetivos del Trabajo', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_impacto, '1.3. Impacto en sostenibilidad, ético-social y de diversidad', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_enfoque, '1.4. Enfoque y método seguido', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_planificacion, '1.5. Planificación del Trabajo', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_productos, '1.6. Breve sumario de productos obtenidos', 'Heading 2', 14, 14, 4)
    format_heading_paragraph(p_otros_capitulos, '1.7. Breve descripción de los otros capítulos de la memoria', 'Heading 2', 14, 14, 4)

    # =========================================================================
    # FORMAT FUTURE CHAPTERS (2-7, 9) AND ACTIVE BIBLIOGRAPHY (8)
    # Group future empty chapters together so they don't generate blank pages
    # =========================================================================
    format_heading_paragraph(p_estado_arte, '2. Estado del arte', 'Heading 1', 18, 20, 8, page_break_before=True)
    remove_elements_between(p_estado_arte._element, p_materiales._element)
    p_ea_note = p_materiales.insert_paragraph_before('*(Este capítulo se desarrollará en la PEC 2)*', style='Normal')
    p_ea_note.runs[0].font.italic = True
    p_ea_note.runs[0].font.size = Pt(10.5)

    format_heading_paragraph(p_materiales, '3. Materiales y métodos', 'Heading 1', 18, 16, 6, page_break_before=False)
    remove_elements_between(p_materiales._element, p_diseno._element)
    p_mat_note = p_diseno.insert_paragraph_before('*(Este capítulo se desarrollará a partir de la PEC 2)*', style='Normal')
    p_mat_note.runs[0].font.italic = True
    p_mat_note.runs[0].font.size = Pt(10.5)

    format_heading_paragraph(p_diseno, '4. Diseño', 'Heading 1', 18, 16, 6, page_break_before=False)
    remove_elements_between(p_diseno._element, p_resultados._element)
    p_dis_note = p_resultados.insert_paragraph_before('*(Este capítulo se desarrollará a partir de la PEC 2)*', style='Normal')
    p_dis_note.runs[0].font.italic = True
    p_dis_note.runs[0].font.size = Pt(10.5)

    format_heading_paragraph(p_resultados, '5. Resultados', 'Heading 1', 18, 16, 6, page_break_before=False)
    remove_elements_between(p_resultados._element, p_conclusiones._element)
    p_res_note = p_conclusiones.insert_paragraph_before('*(Este capítulo se desarrollará en la PEC 3 y PEC 4)*', style='Normal')
    p_res_note.runs[0].font.italic = True
    p_res_note.runs[0].font.size = Pt(10.5)

    format_heading_paragraph(p_conclusiones, '6. Conclusiones y Trabajo futuro', 'Heading 1', 18, 16, 6, page_break_before=False)
    remove_elements_between(p_conclusiones._element, p_glosario._element)
    p_con_note = p_glosario.insert_paragraph_before('*(Este capítulo se desarrollará en la PEC 4)*', style='Normal')
    p_con_note.runs[0].font.italic = True
    p_con_note.runs[0].font.size = Pt(10.5)

    format_heading_paragraph(p_glosario, '7. Glosario', 'Heading 1', 18, 16, 6, page_break_before=False)
    remove_elements_between(p_glosario._element, p_biblio._element)
    p_glo_note = p_biblio.insert_paragraph_before('*(Apartado reservado para la memoria final de la PEC 4)*', style='Normal')
    p_glo_note.runs[0].font.italic = True
    p_glo_note.runs[0].font.size = Pt(10.5)

    # 8. Bibliografía: active citations
    format_heading_paragraph(p_biblio, '8. Bibliografía', 'Heading 1', 18, 20, 8, page_break_before=True)
    remove_elements_between(p_biblio._element, p_anexos._element)
    biblio_items = [
        ('p', '[1] J. T. Kajiya, «The rendering equation», en *Proceedings of the 13th Annual Conference on Computer Graphics and Interactive Techniques (SIGGRAPH ’86)*, Association for Computing Machinery, Nueva York, Estados Unidos, 1986, pp. 143–150. doi: 10.1145/15922.15902.'),
        ('p', '[2] D. Wright, K. Narkowicz y P. Kelly, «Lumen: Real-Time Global Illumination in Unreal Engine 5», en *ACM SIGGRAPH 2022 Courses: Advances in Real-Time Rendering in Games*, 2022. Disponible en: `https://advances.realtimerendering.com/s2022/index.html`.'),
        ('p', '[3] J. C. Hart, «Sphere tracing: A geometric method for the antialiased ray tracing of implicit surfaces», *The Visual Computer*, vol. 12, n.º 10, pp. 527–545, 1996. doi: 10.1007/s003710050084.'),
        ('p', '[4] G. Rong y T.-S. Tan, «Jump flooding in GPU with applications to Voronoi diagram and distance transform», en *Proceedings of the 2006 Symposium on Interactive 3D Graphics and Games (I3D ’06)*, Association for Computing Machinery, Nueva York, Estados Unidos, 2006, pp. 109–116. doi: 10.1145/1111411.1111431.'),
        ('p', '[5] K. Peffers, T. Tuunanen, M. A. Rothenberger y S. Chatterjee, «A Design Science Research Methodology for Information Systems Research», *Journal of Management Information Systems*, vol. 24, n.º 3, pp. 45–77, 2007. doi: 10.2753/MIS0742-1222240302.')
    ]
    insert_section(p_anexos, biblio_items)

    # 9. Anexos
    format_heading_paragraph(p_anexos, '9. Anexos', 'Heading 1', 18, 16, 6, page_break_before=False)
    curr = p_anexos._element.getnext()
    while curr is not None:
        nxt = curr.getnext()
        curr.getparent().remove(curr)
        curr = nxt
    p_anx_note = doc.add_paragraph('*(No se incluyen anexos en esta entrega)*', style='Normal')
    p_anx_note.runs[0].font.italic = True
    p_anx_note.runs[0].font.size = Pt(10.5)

    # Remove any empty Heading paragraphs from template
    for p in list(doc.paragraphs):
        if p.style.name.startswith('Heading') and not p.text.strip():
            p._element.getparent().remove(p._element)

    # =========================================================================
    # Position Table of Contents under 'Índice'
    # =========================================================================
    p_ind = find_p('Índice')
    p_fig = find_p('Lista de figuras')

    if p_ind is not None:
        pPr_ind = p_ind._p.get_or_add_pPr()
        for old in pPr_ind.xpath('w:ind'):
            pPr_ind.remove(old)
        for pb in pPr_ind.xpath('w:pageBreakBefore'):
            pPr_ind.remove(pb)
        pPr_ind.append(parse_xml(f'<w:pageBreakBefore {nsdecls("w")}/>'))

    # Remove placeholder instruction under Índice
    p_placeholder = find_p('Generad un índice')
    if p_placeholder is not None:
        p_placeholder._element.getparent().remove(p_placeholder._element)

    # Remove extra empty paragraphs between p_ind and p_fig
    if p_ind is not None and p_fig is not None:
        curr = p_ind._element.getnext()
        while curr is not None and curr != p_fig._element:
            nxt = curr.getnext()
            if curr.tag.endswith('p') and not ''.join(curr.itertext()).strip():
                curr.getparent().remove(curr)
            curr = nxt

    sdts = doc.element.xpath('.//w:sdt')
    if sdts and p_fig is not None:
        toc_sdt = sdts[0]
        # Position TOC SDT right before Lista de figuras
        p_fig._element.addprevious(toc_sdt)
        # Update field instruction to TOC \o "1-2" \h \z \u
        for el in toc_sdt.findall('.//{http://schemas.openxmlformats.org/wordprocessingml/2006/main}instrText'):
            el.text = r' TOC \o "1-2" \h \z \u '

    # Under Lista de figuras, list Figura 1
    if p_fig is not None:
        pPr_fig = p_fig._p.get_or_add_pPr()
        for old in pPr_fig.xpath('w:ind'):
            pPr_fig.remove(old)
        remove_elements_between(p_fig._element, p_intro._element)
        p_fig_item = p_intro.insert_paragraph_before('', style='Normal')
        pPr_fi = p_fig_item._p.get_or_add_pPr()
        for old in pPr_fi.xpath('w:tabs'):
            pPr_fi.remove(old)
        pPr_fi.append(parse_xml(f'<w:tabs {nsdecls("w")}><w:tab w:val="right" w:leader="dot" w:pos="8500"/></w:tabs>'))
        pPr_fi.append(parse_xml(f'<w:spacing {nsdecls("w")} w:before="60" w:after="60"/>'))

        r_f1 = p_fig_item.add_run('Figura 1: Ejemplo de iluminación global')
        r_f1.font.name = 'Arial'
        r_f1.font.size = Pt(10)

        r_tab = p_fig_item.add_run('\t')

        r_pg = p_fig_item.add_run('6')
        r_pg.font.name = 'Arial'
        r_pg.font.size = Pt(10)

    doc.save(output_path)
    print('Saved intermediate docx:', output_path)

    # =========================================================================
    # LIBREOFFICE AUTOMATION:
    # Update document indexes and export to PDF
    # =========================================================================
    lo_profile = '/tmp/lo_profile'
    os.makedirs(lo_profile, exist_ok=True)
    pipe_name = 'lo_pipe_build'
    cmd = [
        'soffice',
        '--headless',
        f'-env:UserInstallation=file://{lo_profile}',
        f'--accept=pipe,name={pipe_name};urp;StarOffice.ServiceManager'
    ]
    proc = subprocess.Popen(cmd)
    time.sleep(2)

    try:
        import uno
        local_context = uno.getComponentContext()
        resolver = local_context.ServiceManager.createInstanceWithContext('com.sun.star.bridge.UnoUrlResolver', local_context)
        context = resolver.resolve(f'uno:pipe,name={pipe_name};urp;StarOffice.ComponentContext')
        desktop = context.ServiceManager.createInstanceWithContext('com.sun.star.frame.Desktop', context)

        url = uno.systemPathToFileUrl(os.path.abspath(output_path))
        doc_uno = desktop.loadComponentFromURL(url, '_blank', 0, ())

        indexes = doc_uno.getDocumentIndexes()
        for i in range(indexes.getCount()):
            indexes.getByIndex(i).update()

        # Save back to docx
        prop_docx = uno.getClass('com.sun.star.beans.PropertyValue')()
        prop_docx.Name = 'FilterName'
        prop_docx.Value = 'MS Word 2007 XML'
        doc_uno.storeToURL(url, (prop_docx,))

        # Save to PDF
        pdf_url = uno.systemPathToFileUrl(os.path.abspath(output_pdf))
        prop_pdf = uno.getClass('com.sun.star.beans.PropertyValue')()
        prop_pdf.Name = 'FilterName'
        prop_pdf.Value = 'writer_pdf_Export'
        doc_uno.storeToURL(pdf_url, (prop_pdf,))

        doc_uno.close(True)
        print('Successfully updated docx with TOC and exported to PDF!')
    finally:
        proc.terminate()

if __name__ == '__main__':
    main()
