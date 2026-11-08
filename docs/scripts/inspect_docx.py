import zipfile
import xml.etree.ElementTree as ET
from pathlib import Path

DOCX_PATH = Path(__file__).resolve().parent.parent / 'internal' / 'TFx_Plantilla_Memoria_es.docx'

ns = {
    'w': 'http://schemas.openxmlformats.org/wordprocessingml/2006/main',
    'a': 'http://schemas.openxmlformats.org/drawingml/2006/main',
    'pic': 'http://schemas.openxmlformats.org/drawingml/2006/picture',
    'r': 'http://schemas.openxmlformats.org/officeDocument/2006/relationships',
    'wp': 'http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing'
}

with zipfile.ZipFile(DOCX_PATH) as z:
    doc_xml = z.read('word/document.xml')
    root = ET.fromstring(doc_xml)
    tbls = root.findall('.//w:tbl', ns)
    
    print("=" * 60)
    print("1. COVER TABLE ANALYSIS")
    print("=" * 60)
    cover = tbls[0]
    tblPr = cover.find('w:tblPr', ns)
    if tblPr is not None:
        bdr = tblPr.find('w:tblBorders', ns)
        print('tblBorders:', ET.tostring(bdr, encoding='unicode') if bdr is not None else 'None')
        mar = tblPr.find('w:tblCellMar', ns)
        print('tblCellMar:', ET.tostring(mar, encoding='unicode') if mar is not None else 'None')
        w = tblPr.find('w:tblW', ns)
        print('tblW:', w.attrib if w is not None else 'None')
    
    for r_i, tr in enumerate(cover.findall('w:tr', ns)):
        trPr = tr.find('w:trPr', ns)
        trHeight = trPr.find('w:trHeight', ns) if trPr is not None else None
        h_str = trHeight.attrib if trHeight is not None else 'auto'
        print(f"\n--- Row {r_i} (trHeight={h_str}) ---")
        for c_i, tc in enumerate(tr.findall('w:tc', ns)):
            tcPr = tc.find('w:tcPr', ns)
            shd = tcPr.find('w:shd', ns) if tcPr is not None else None
            fill = shd.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}fill') if shd is not None else 'None'
            tcW = tcPr.find('w:tcW', ns) if tcPr is not None else None
            w_val = tcW.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}w') if tcW is not None else ''
            tcBorders = tcPr.find('w:tcBorders', ns) if tcPr is not None else None
            borders_str = ET.tostring(tcBorders, encoding='unicode') if tcBorders is not None else 'None'
            tcMar = tcPr.find('w:tcMar', ns) if tcPr is not None else None
            mar_str = ET.tostring(tcMar, encoding='unicode') if tcMar is not None else 'None'
            print(f"  Cell {c_i}: width={w_val} dxa, fill={fill}")
            print(f"    borders: {borders_str.strip()}")
            print(f"    cellMar: {mar_str.strip()}")
            
            for p_i, p in enumerate(tc.findall('w:p', ns)):
                pPr = p.find('w:pPr', ns)
                sp = pPr.find('w:spacing', ns) if pPr is not None else None
                sp_str = sp.attrib if sp is not None else ''
                jc = pPr.find('w:jc', ns) if pPr is not None else None
                jc_str = jc.attrib if jc is not None else ''
                ind = pPr.find('w:ind', ns) if pPr is not None else None
                ind_str = ind.attrib if ind is not None else ''
                runs = []
                for r in p.findall('w:r', ns):
                    rPr = r.find('w:rPr', ns)
                    sz = rPr.find('w:sz', ns).attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val') if (rPr is not None and rPr.find('w:sz', ns) is not None) else ''
                    color = rPr.find('w:color', ns).attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val') if (rPr is not None and rPr.find('w:color', ns) is not None) else ''
                    b = rPr.find('w:b', ns) is not None if rPr is not None else False
                    i = rPr.find('w:i', ns) is not None if rPr is not None else False
                    rFonts = rPr.find('w:rFonts', ns) if rPr is not None else None
                    font_name = rFonts.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}ascii') if rFonts is not None else ''
                    t = ''.join(r.itertext())
                    drawings = r.findall('.//w:drawing', ns)
                    img_str = ""
                    if drawings:
                        for d in drawings:
                            blip = d.find('.//a:blip', ns)
                            embed = blip.attrib.get('{http://schemas.openxmlformats.org/officeDocument/2006/relationships}embed') if blip is not None else ''
                            extent = d.find('.//wp:extent', ns)
                            cx = extent.attrib.get('cx') if extent is not None else ''
                            cy = extent.attrib.get('cy') if extent is not None else ''
                            img_str += f" [IMG rel={embed} cx={cx} cy={cy}]"
                    runs.append(f"'{t}'{img_str}(font={font_name}, sz={sz}, col={color}, b={b}, i={i})")
                print(f"    P{p_i} (sp={sp_str}, jc={jc_str}, ind={ind_str}):")
                for run_txt in runs:
                    print(f"      {run_txt}")

print("\n" + "=" * 60)
print("2. FICHA TABLE ANALYSIS")
print("=" * 60)
with zipfile.ZipFile(DOCX_PATH) as z:
    doc_xml = z.read('word/document.xml')
    root = ET.fromstring(doc_xml)
    tbls = root.findall('.//w:tbl', ns)
    ficha = tbls[1]
    tblPr = ficha.find('w:tblPr', ns)
    print('tblBorders:', ET.tostring(tblPr.find('w:tblBorders', ns), encoding='unicode') if tblPr.find('w:tblBorders', ns) is not None else 'None')
    print('tblW:', tblPr.find('w:tblW', ns).attrib if tblPr.find('w:tblW', ns) is not None else 'None')
    print('tblInd:', tblPr.find('w:tblInd', ns).attrib if tblPr.find('w:tblInd', ns) is not None else 'None')
    
    for r_i, tr in enumerate(ficha.findall('w:tr', ns)):
        cells = tr.findall('w:tc', ns)
        row_desc = []
        for c_i, tc in enumerate(cells):
            tcPr = tc.find('w:tcPr', ns)
            shd = tcPr.find('w:shd', ns) if tcPr is not None else None
            fill = shd.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}fill') if shd is not None else 'None'
            tcW = tcPr.find('w:tcW', ns) if tcPr is not None else None
            w_val = tcW.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}w') if tcW is not None else ''
            span = tcPr.find('w:gridSpan', ns) if tcPr is not None else None
            span_val = span.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val') if span is not None else '1'
            txt = ' '.join([t.text for t in tc.findall('.//w:t', ns) if t.text]).strip()
            row_desc.append(f"C{c_i}(w={w_val}, span={span_val}, fill={fill}): {repr(txt[:40])}")
        print(f"Row {r_i}: " + " | ".join(row_desc))

print("\n" + "=" * 60)
print("3. BODY HEADINGS AND HIGHLIGHTING ANALYSIS")
print("=" * 60)
with zipfile.ZipFile(DOCX_PATH) as z:
    doc_xml = z.read('word/document.xml')
    root = ET.fromstring(doc_xml)
    body = root.find('w:body', ns)
    
    for p_i, p in enumerate(body.findall('w:p', ns)):
        pPr = p.find('w:pPr', ns)
        pStyle = pPr.find('w:pStyle', ns) if pPr is not None else None
        s_val = pStyle.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val') if pStyle is not None else ''
        
        # Check numPr
        numPr = pPr.find('w:numPr', ns) if pPr is not None else None
        num_str = ""
        if numPr is not None:
            ilvl = numPr.find('w:ilvl', ns)
            numId = numPr.find('w:numId', ns)
            num_str = f"[num: id={numId.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val')} lvl={ilvl.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}val')}] "
            
        sp = pPr.find('w:spacing', ns) if pPr is not None else None
        sp_str = f"sp(b={sp.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}before', '')}, a={sp.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}after', '')}, l={sp.attrib.get('{http://schemas.openxmlformats.org/wordprocessingml/2006/main}line', '')})" if sp is not None else ''
        
        runs = p.findall('w:r', ns)
        has_highlight = False
        run_texts = []
        for r in runs:
            rPr = r.find('w:rPr', ns)
            hl = rPr.find('w:highlight', ns) if rPr is not None else None
            if hl is not None:
                has_highlight = True
            t = ''.join(r.itertext())
            if t.strip():
                run_texts.append(t)
                
        text = " ".join(run_texts).strip()
        if s_val or has_highlight or numPr is not None or text.startswith(('Agradecimientos', 'Declaraci', 'Índice', 'Lista')):
            hl_tag = "[HL] " if has_highlight else ""
            print(f"P{p_i:03d} | Style={s_val:15s} | {sp_str:30s} | {num_str}{hl_tag}{text[:70]}")

print("\n" + "=" * 60)
print("4. STYLES XML DETAILED ANALYSIS")
print("=" * 60)
with zipfile.ZipFile(DOCX_PATH) as z:
    styles_xml = z.read('word/styles.xml')
    root = ET.fromstring(styles_xml)
    for s_id in ['Heading1', 'Heading2', 'Heading3', 'Heading4', 'Normal', 'Caption']:
        s = root.find(f'./w:style[@w:styleId="{s_id}"]', ns)
        if s is not None:
            print(f"\n--- Style {s_id} ---")
            pPr = s.find('w:pPr', ns)
            rPr = s.find('w:rPr', ns)
            if pPr is not None:
                print('  pPr:', ET.tostring(pPr, encoding='unicode').strip())
            if rPr is not None:
                print('  rPr:', ET.tostring(rPr, encoding='unicode').strip())
