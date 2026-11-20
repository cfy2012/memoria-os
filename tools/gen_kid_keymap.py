from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.enum.table import WD_ALIGN_VERTICAL
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

DOC = Document()

sec = DOC.sections[0]
sec.page_width = Cm(21.0)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.5)
sec.bottom_margin = Cm(2.5)
sec.left_margin = Cm(2.5)
sec.right_margin = Cm(2.5)


def set_font(run, cn, en, size, bold=False, color=None):
    run.font.name = en
    run.font.size = Pt(size)
    run.font.bold = bold
    if color:
        run.font.color.rgb = color
    rPr = run._element.get_or_add_rPr()
    rFonts = rPr.find(qn('w:rFonts'))
    if rFonts is None:
        rFonts = OxmlElement('w:rFonts')
        rPr.append(rFonts)
    rFonts.set(qn('w:eastAsia'), cn)
    rFonts.set(qn('w:ascii'), en)
    rFonts.set(qn('w:hAnsi'), en)


for style_name, cn, en, size, bold in (
        ('Normal', '宋体', 'Times New Roman', 12, False),
        ('Heading 1', '黑体', 'Times New Roman', 16, True),
        ('Heading 3', '黑体', 'Times New Roman', 12, True),
        ('Title', '黑体', 'Times New Roman', 18, True)):
    st = DOC.styles[style_name]
    st.font.name = en
    st.font.size = Pt(size)
    st.font.bold = bold
    st.font.color.rgb = RGBColor(0, 0, 0)
    rPr = st.element.get_or_add_rPr()
    rFonts = rPr.find(qn('w:rFonts'))
    if rFonts is None:
        rFonts = OxmlElement('w:rFonts')
        rPr.append(rFonts)
    rFonts.set(qn('w:eastAsia'), cn)
    rFonts.set(qn('w:ascii'), en)
    rFonts.set(qn('w:hAnsi'), en)

ns = DOC.styles['Normal'].paragraph_format
ns.line_spacing_rule = WD_LINE_SPACING.ONE_POINT_FIVE
ns.space_before = Pt(0)
ns.space_after = Pt(0)
ns.first_line_indent = Pt(24)

DOC.styles['Heading 1'].paragraph_format.space_before = Pt(14)
DOC.styles['Heading 1'].paragraph_format.space_after = Pt(6)
DOC.styles['Heading 1'].paragraph_format.first_line_indent = Pt(0)
DOC.styles['Heading 3'].paragraph_format.space_before = Pt(9)
DOC.styles['Heading 3'].paragraph_format.space_after = Pt(4)
DOC.styles['Heading 3'].paragraph_format.first_line_indent = Pt(0)
DOC.styles['Title'].paragraph_format.space_after = Pt(18)
DOC.styles['Title'].paragraph_format.first_line_indent = Pt(0)
DOC.styles['Title'].paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER


def body(text, indent=True):
    p = DOC.add_paragraph()
    p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    if not indent:
        p.paragraph_format.first_line_indent = Pt(0)
    r = p.add_run(text)
    set_font(r, '宋体', 'Times New Roman', 12)
    return p


def cap(text):
    p = DOC.add_paragraph()
    p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(4)
    r = p.add_run(text)
    set_font(r, '宋体', 'Times New Roman', 10.5)
    return p


def set_cell(cell, text, bold=False, align='left'):
    cell.vertical_alignment = WD_ALIGN_VERTICAL.CENTER
    tf = cell.paragraphs[0]
    tf.paragraph_format.first_line_indent = Pt(0)
    tf.paragraph_format.space_before = Pt(0)
    tf.paragraph_format.space_after = Pt(0)
    tf.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
    tf.paragraph_format.alignment = {
        'left': WD_ALIGN_PARAGRAPH.LEFT,
        'center': WD_ALIGN_PARAGRAPH.CENTER,
        'right': WD_ALIGN_PARAGRAPH.RIGHT}[align]
    r = tf.add_run(str(text))
    set_font(r, '宋体', 'Times New Roman', 11, bold=bold)


def shade(cell, hexcolor):
    tcPr = cell._tc.get_or_add_tcPr()
    sh = OxmlElement('w:shd')
    sh.set(qn('w:val'), 'clear')
    sh.set(qn('w:color'), 'auto')
    sh.set(qn('w:fill'), hexcolor)
    tcPr.append(sh)


def table(header, rows, aligns, widths_cm):
    t = DOC.add_table(rows=1, cols=len(header))
    t.style = 'Table Grid'
    t.autofit = False
    tblPr = t._tbl.tblPr
    borders = OxmlElement('w:tblBorders')
    for edge in ('top', 'left', 'bottom', 'right', 'insideH', 'insideV'):
        e = OxmlElement('w:' + edge)
        e.set(qn('w:val'), 'single')
        e.set(qn('w:sz'), '4')
        e.set(qn('w:color'), '000000')
        borders.append(e)
    tblPr.append(borders)

    hdr = t.rows[0]
    trPr = hdr._tr.get_or_add_trPr()
    trPr.append(OxmlElement('w:tblHeader'))
    trPr.append(OxmlElement('w:cantSplit'))
    for i, h in enumerate(header):
        c = hdr.cells[i]
        set_cell(c, h, bold=True, align='center')
        shade(c, 'D9D9D9')

    for row in rows:
        tr = t.add_row()
        trPr = tr._tr.get_or_add_trPr()
        trPr.append(OxmlElement('w:cantSplit'))
        for i, v in enumerate(row):
            set_cell(tr.cells[i], v, align=aligns[i])

    for row in t.rows:
        for i, w in enumerate(widths_cm):
            row.cells[i].width = Cm(w)
    for i, w in enumerate(widths_cm):
        for cell in t.columns[i].cells:
            cell.width = Cm(w)
    return t


DOC.add_paragraph('键盘接线表', style='Title')
p = DOC.add_paragraph()
p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.first_line_indent = Pt(0)
p.paragraph_format.space_after = Pt(12)
r = p.add_run('60 个键，照着这张表焊')
set_font(r, '楷体', 'Times New Roman', 12)

DOC.add_heading('一、先看懂一只按键', level=1)
body('键盘上每一只按下去会响的按键，下面有 4 只金属脚。这 4 只脚不是四根独立的线，'
     '它们分成两对，每对两只脚在按键里面本来就是连在一起的。')
body('所以一只按键其实只有两个“接点”：第一对算一个，第二对算一个。')
cap('表1　4 只脚分成两对')
table(['脚的位置', '不按的时候', '按下去的时候', '要接到哪里'],
      [['第一对（两只脚）', '两只脚之间是通的', '和另一对接通', '一根横线'],
       ['第二对（两只脚）', '两只脚之间是通的', '和第一对接通', '一根竖线，中间串二极管']],
      ['center', 'center', 'center', 'center'],
      [4.2, 3.8, 3.4, 4.4])

DOC.add_heading('二、焊之前先用万用表量一下', level=1)
body('不同厂家的按键，4 只脚的排法不一样。有的是上边两只一对、下边两只一对；'
     '有的是一左一右斜着的那两只才是一对。光看脚的位置猜不准。')
body('把万用表拨到响的那一档（蜂鸣档），两只表笔随便点住两只脚，不按按键：'
     '会“嘀”的两只脚，就是同一对。量出两对之后，再往下焊。')
body('这一步不能省。认错一对脚，焊完的键盘会乱跳字符，查起来比重焊还费时间。')

DOC.add_heading('三、每只键还要串一只二极管', level=1)
body('每一只按键的第二对脚，不能直接接到竖线上，中间要串一只 1N4148 小二极管。'
     '60 只键就是 60 只二极管，一只都不能省。')
body('二极管有两头：带黑色圆环（或一段黑带）的那一头是负极，另一头是正极。')
body('接法是：按键的第二对脚接二极管的负极（带黑环那头），二极管的正极接到竖线。')
body('方向不能反。焊反了，按下这一列的键一个字符都读不出来。')
body('省掉二极管，同时按两个键的时候（比如 Shift 加 A）会多跳出一个不相干的字符，'
     '按得越多错得越离谱。')

DOC.add_heading('四、一只键的完整链路', level=1)
cap('表2　一只键从头到尾怎么接')
table(['按键的脚', '先接到', '再接到', '最后到哪'],
      [['第一对', '直接接', '不用串东西', '横线 R'],
       ['第二对', '二极管负极（带黑环那头）', '二极管正极（没环那头）', '竖线 C']],
      ['center', 'center', 'center', 'center'],
      [3.0, 5.6, 4.4, 3.0])
body('每一对脚里，只要挑一只脚去焊就够，两只都焊也行——同一对的两只脚在按键里面本来就是通的。')
body('有一条绝对不能犯的错：不能把同一对的两只脚拆开，一只接横线、一只接竖线。'
     '那样这只按键等于被焊成了“永远按着不放”的状态，那一整行、一整列都会乱，相邻的键还会跟着出错字。')

DOC.add_heading('五、60 个键分别接到哪里', level=1)
body('下面每一行是一只键。按“这个键”那一列找到手上正在焊的那只，'
     '第一对联到写着的横线；第二对先接二极管负极，二极管正极再接到写着的竖线。')
body('“装在哪”那一列是这只键在键盘上的位置：第几行、第几列，从左往右、从上往下数。')

KEYS = [['R0', '空', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', '背光', '退格'],
        ['R1', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
        ['R2', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
        ['R3', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'],
        ['R4', 'Shift', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.'],
        ['R5', 'Caps', 'Ctrl', '音量−', '音量+', '空格', '←', '→', '↓', '↑', '回车']]

rows = []
n = 0
for r in range(6):
    for c in range(10):
        name = KEYS[r][c + 1]
        if name == '空':
            continue
        n += 1
        rows.append([str(n), name, '第 ' + str(r + 1) + ' 行 第 ' + str(c + 1) + ' 列',
                     '横线 R' + str(r), '按键第二对', '竖线 C' + str(c)])
cap('表3　每只键的十字交叉接法')
table(['序号', '这个键', '装在哪', '第一对接', '二极管负极接', '二极管正极接'],
      rows,
      ['center', 'center', 'center', 'center', 'center', 'center'],
      [1.2, 2.6, 3.4, 2.2, 4.2, 2.4])
body('第 1 行第 1 列这一格不装键，所以表里只有 59 只，序号从 1 到 59。'
     '中间那列“按键第二对”就是这只键第二对脚里挑出来的一只脚，二极管负极焊在它上面。')

DOC.add_heading('六、焊完怎么检查', level=1)
body('上电之前，拿蜂鸣档把每一只键按一遍：不按的时候不响，按下去才响，这只就是焊对的。')
body('如果某只键不按就响，说明它的两只脚落到了同一对上，或者它的横线和竖线被锡连到了一起，'
     '把这一只重新焊一遍再看。')
body('如果某只键按下去也不响，先看它那只二极管是不是焊反了：带黑环的一头必须朝着按键。')
body('全部量通了再上电。整块键盘上任何一只焊错，都会让别的键也跟着乱。')

out = r'G:\Memoria OS\键盘接线-简单版.docx'
DOC.save(out)
print('SAVED', out)
