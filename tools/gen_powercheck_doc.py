# -*- coding: utf-8 -*-
import os
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

OUT = r'G:\Memoria OS\上电前检测表.docx'

doc = Document()
sec = doc.sections[0]
sec.page_width = Cm(21)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.0)
sec.bottom_margin = Cm(2.0)
sec.left_margin = Cm(2.5)
sec.right_margin = Cm(2.5)

st = doc.styles['Normal']
st.font.name = 'Times New Roman'
st.font.size = Pt(12)
st.element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
st.paragraph_format.line_spacing = 1.5
st.paragraph_format.space_before = Pt(0)
st.paragraph_format.space_after = Pt(0)


def body(text, indent=True, bold=False):
    p = doc.add_paragraph()
    p.paragraph_format.first_line_indent = Pt(24) if indent else Pt(0)
    p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    r = p.add_run(text)
    r.font.name = 'Times New Roman'
    r.font.size = Pt(12)
    r.font.bold = bold
    r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
    return p


# ---------- 标题 ----------
p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(4)
r = p.add_run('上电前检测表')
r.font.name = 'Times New Roman'
r.font.size = Pt(18)
r.font.bold = True
r._element.rPr.rFonts.set(qn('w:eastAsia'), '黑体')

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(14)
r = p.add_run('Memoria OS · 每项测 10 次 · 全过再上电')
r.font.name = 'Times New Roman'
r.font.size = Pt(12)
r._element.rPr.rFonts.set(qn('w:eastAsia'), '楷体')

# ---------- 用法 ----------
body('用法有三条，照着做就行。', indent=False, bold=True)

body('第一，每一项连续测 10 次，10 次全部符合才算合格。通断类项目之所以要重复 10 次，是因为虚焊的特征就是十次里有一两次不通——只测一次，那个坏点就放过去了。')

body('第二，填法分两种。通断、短路、阻值这类，填“通 X/10”；电压、电流这类，填 10 次的读数范围，比如“3.28~3.31”。范围越窄说明越稳，跳动超过 0.05V 就要查接头。')

body('第三，顺序不能跳。前八组全部断电测，把电池和 USB 都拔掉；只有第九组才上电。任何一项不合格就停在那，修完从该项重测，不要带着已知的问题往下走。')

# ---------- 主表 ----------
HEAD = ['序号', '检查项', '怎么测（表笔点 / 档位）', '合格标准', '10 次结果']
W = [1.0, 4.4, 5.0, 3.4, 2.2]

t = doc.add_table(rows=1, cols=len(HEAD))
t.style = 'Table Grid'
t.alignment = WD_TABLE_ALIGNMENT.CENTER
t.autofit = False
tblPr = t._tbl.tblPr
layout = OxmlElement('w:tblLayout')
layout.set(qn('w:type'), 'fixed')
tblPr.append(layout)

hdr = t.rows[0]
trPr = hdr._tr.get_or_add_trPr()
el = OxmlElement('w:tblHeader')
el.set(qn('w:val'), 'true')
trPr.append(el)
for i, htxt in enumerate(HEAD):
    c = hdr.cells[i]
    c.width = Cm(W[i])
    shd = OxmlElement('w:shd')
    shd.set(qn('w:fill'), 'D9D9D9')
    c._tc.get_or_add_tcPr().append(shd)
    p = c.paragraphs[0]
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.line_spacing = 1.0
    p.paragraph_format.space_before = Pt(2)
    p.paragraph_format.space_after = Pt(2)
    r = p.add_run(htxt)
    r.font.size = Pt(10)
    r.font.bold = True
    r.font.name = 'Times New Roman'
    r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')


def row(vals):
    cells = t.add_row().cells
    trPr = t.rows[-1]._tr.get_or_add_trPr()
    el = OxmlElement('w:cantSplit')
    trPr.append(el)
    for i, val in enumerate(vals):
        c = cells[i]
        c.width = Cm(W[i])
        p = c.paragraphs[0]
        p.paragraph_format.first_line_indent = Pt(0)
        p.paragraph_format.line_spacing = 1.0
        p.paragraph_format.space_before = Pt(1)
        p.paragraph_format.space_after = Pt(1)
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER if (i == 0 or i == 4) else WD_ALIGN_PARAGRAPH.LEFT
        r = p.add_run(str(val))
        r.font.size = Pt(10)
        r.font.name = 'Times New Roman'
        r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')


def group(text):
    r = t.add_row()
    merged = r.cells[0]
    for i in range(1, len(HEAD)):
        merged = merged.merge(r.cells[i])
    merged.width = Cm(sum(W))
    p = merged.paragraphs[0]
    for run in list(p.runs):
        run._element.getparent().remove(run._element)
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.line_spacing = 1.0
    p.paragraph_format.space_before = Pt(3)
    p.paragraph_format.space_after = Pt(3)
    p.alignment = WD_ALIGN_PARAGRAPH.LEFT
    run = p.add_run(text)
    run.font.size = Pt(10)
    run.font.bold = True
    run.font.name = 'Times New Roman'
    run._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
    shd = OxmlElement('w:shd')
    shd.set(qn('w:fill'), 'F2F2F2')
    merged._tc.get_or_add_tcPr().append(shd)


group('一、电源与地（断电：电池、USB 全部拔掉）')
row(['1', '电池座正极与负极', '电阻档 20k', '不能是 0Ω，正常大于 1kΩ；读数 0 说明短路', ''])
row(['2', '3V3 与 GND', '电阻档 20k', '大于 1kΩ，不能导通', ''])
row(['3', '5V 与 GND', '电阻档 20k', '大于 1kΩ，不能导通', ''])
row(['4', '3V3 与 5V 之间', '通断档', '不通；导通说明两路电源串了', ''])
row(['5', '所有 GND 点互通', '通断档，核心板 GND 固定一端，另一端依次点', 'TP4056、两片 MCP 10 脚、卡座外壳、键区公共地全部响', ''])
row(['6', '电源开关', '通断档，开关拨两次', '关断时不响，接通时 0Ω', ''])

group('二、键盘矩阵（59 只键，二极管 59 只）')
row(['7', '每只键 1 脚与 3 脚', '通断档，逐个按下再松开', '按下不响也算不合格：应该按下响、松开不响', ''])
row(['8', '每只键 2 脚与 4 脚', '通断档，逐个按下再松开', '同上一组，两只是同一组的常通脚', ''])
row(['9', '每只键 1 脚与 2 脚', '通断档，不按、按下各试一次', '任何状态都不响；响了说明把两组脚并一起了', ''])
row(['10', '每只键 3 脚与 4 脚', '通断档，不按、按下各试一次', '任何状态都不响', ''])
row(['11', '每只二极管正向压降', '二极管档，红笔接正（无黑环端）', '读数 0.5~0.7V', ''])
row(['12', '每只二极管反向', '二极管档，红黑对调', '显示 1 或不导通，不能有读数', ''])
row(['13', '二极管朝向', '目视逐排看', '黑环一端全部朝按键（3 脚），不能反', ''])
row(['14', '行线之间', '通断档，R0 到 R5 两两互测（共 15 对）', '全部不通；导通说明行间短路', ''])
row(['15', '列线之间', '通断档，C0 到 C9 两两互测（共 45 对）', '全部不通', ''])
row(['16', '行线与列线之间', '通断档，不按任何键，任取一行对任一列', '不通', ''])
row(['17', '整矩阵扫一遍', '通断档，逐行逐列按下，量该行与该列', '按下那只键时响，其余键保持不响', ''])

group('三、MCP23017（两片，28 脚）')
row(['18', '9 脚 VDD 与 3V3', '通断档', '响', ''])
row(['19', '10 脚 VSS 与 GND', '通断档', '响', ''])
row(['20', '12 脚 SCL 与 GPIO6', '通断档', '响；两片的 12 脚都接到同一根上', ''])
row(['21', '13 脚 SDA 与 GPIO1', '通断档', '响；两片的 13 脚都接到同一根上', ''])
row(['22', 'SCL 与 SDA 之间', '通断档', '不通，连在一起总线就废了', ''])
row(['23', 'SCL、SDA 对 3V3', '电阻档 20k', '各约 4.7kΩ，两只上拉电阻都要在', ''])
row(['24', '18 脚 RESET 与 3V3', '通断档', '响，两片都接 3V3', ''])
row(['25', '#1 的 15/16/17 脚', '通断档对 GND', '三只全响（地址 0x20）', ''])
row(['26', '#2 的 15/16/17 脚', '15 对 3V3，16、17 对 GND', '15 响 3V3，16/17 只响 GND（地址 0x21）', ''])
row(['27', '相邻脚之间', '通断档，沿芯片两侧逐对点', '全部不通；这是抓拖锡、连桥最快的办法', ''])

group('四、TP4056（8 脚）')
row(['28', '4 脚 VCC 与 USB 5V', '通断档', '响；这是充电输入', ''])
row(['29', '5 脚 BAT 与电池正极', '通断档', '响', ''])
row(['30', '3 脚 GND 与 GND', '通断档', '响', ''])
row(['31', '2 脚 PROG 对 GND', '电阻档 20k', '1.2kΩ（约 1A）或 2.4kΩ（约 500mA），按电池容量选', ''])
row(['32', '1 脚 TEMP 与 GND', '通断档', '响；接地就是关闭温度检测', ''])
row(['33', '8 脚 CE 与 4 脚 VCC', '通断档', '响；CE 要高电平芯片才工作', ''])
row(['34', '7 脚 CHRG 与 GPIO42', '通断档', '响；不接影响不大，只是看不到充电状态', ''])
row(['35', '底部散热焊盘与 GND', '通断档', '响；ESOP-8 这块不接地会过热', ''])

group('五、MT3608 与 AO3401')
row(['36', 'VIN+ 与 TP4056 的 OUT+', '通断档', '响', ''])
row(['37', 'VIN- 与 GND', '通断档', '响', ''])
row(['38', 'VOUT+ 与核心板 5V 脚', '通断档', '响；先别接核心板，等第九组调好电压再连', ''])
row(['39', 'VIN 与 VOUT 之间', '通断档', '不通；导通说明输入输出短了', ''])
row(['40', 'AO3401 的 2 脚 S 与 5V', '通断档', '响', ''])
row(['41', 'AO3401 的 3 脚 D 与 5V_PERIPH', '通断档', '响；这一路是屏、功放、卡的负载正极', ''])
row(['42', 'AO3401 的 1 脚 G 与 GPIO48', '通断档', '响', ''])
row(['43', 'AO3401 的 G 与 S 之间', '电阻档 200k', '约 100kΩ，这是默认关断的上拉', ''])

group('六、屏与 SPI 总线')
row(['44', '屏 CS 与 GPIO10', '通断档', '响', ''])
row(['45', '屏 DC 与 GPIO9，RST 与 GPIO8', '通断档', '各响一根', ''])
row(['46', '屏 BLK 与 GPIO14', '通断档', '响；背光控制', ''])
row(['47', 'SCLK 与 GPIO12，MOSI 与 GPIO11', '通断档', '各响一根', ''])
row(['48', 'MISO 与 GPIO13', '通断档', '响；屏幕不用这根，卡座要用', ''])
row(['49', '屏 VCC 与 3V3，GND 与 GND', '通断档', '各响；屏不能进 5V', ''])
row(['50', '排线相邻脚之间', '通断档，逐对点', '全部不通；3V3 与 GND 相邻，串了会烧屏', ''])

group('七、音频、摇杆、侧键')
row(['51', '功放 BCLK 与 GPIO18', '通断档', '响', ''])
row(['52', '功放 WS 与 GPIO21，DOUT 与 GPIO15', '通断档', '各响一根', ''])
row(['53', '功放 EN 与 GPIO47', '通断档', '响；高电平才有声', ''])
row(['54', '喇叭两端与功放 + / -', '通断档', '各响一根，两线不能互短', ''])
row(['55', '摇杆 X 与 GPIO7，Y 与 GPIO5', '通断档', '各响一根', ''])
row(['56', '摇杆按键与 GPIO38', '通断档', '响', ''])
row(['57', '侧键（电源键）与 GPIO2', '通断档', '按下时响，松开不响', ''])
row(['58', '麦克风 SD 与 GPIO16', '通断档', '响；没有麦克风可以跳过', ''])

group('八、卡座')
row(['59', '小卡 VDD 与 3V3，VSS 与 GND', '通断档', '各响；VDD 绝不能是 5V', ''])
row(['60', '小卡 CS 与 GPIO17', '通断档', '响', ''])
row(['61', '小卡 CLK 与 GPIO12，CMD 与 GPIO11，DAT0 与 GPIO13', '通断档', '各响一根；CMD 与 DAT0 接反是最常见的错', ''])
row(['62', '大卡 CMD 与 GPIO39，CLK 与 GPIO40，D0 与 GPIO41', '通断档', '各响一根', ''])
row(['63', '大卡 VDD 与 3V3，VSS1/VSS2 与 GND', '通断档', '各响', ''])
row(['64', '两卡座金属外壳与 GND', '通断档', '都响', ''])
row(['65', '卡座相邻焊盘之间', '通断档，逐个点', '全部不通；卡座焊盘密，最容易拖锡', ''])

group('九、上电瞬间（这一组才通电）')
row(['66', '电池电压', '直流电压档，量电池两端', '3.0~4.2V；低于 3.0V 先充电再测', ''])
row(['67', '5V 输出', '直流电压档，MT3608 输出对 GND', '5.0V ±0.2V；超过 6V 立刻断电', ''])
row(['68', '3V3 输出', '直流电压档，核心板 3V3 对 GND', '3.3V ±0.1V', ''])
row(['69', '整机待机电流', 'USB 电流表串在供电线上', '小于 300mA；明显偏大说明有短路', ''])
row(['70', '发热', '上电 30 秒后手指背轻触各芯片', '都不烫手；某一颗发烫立刻断电', ''])

# ---------- 收尾 ----------
body('三项最容易漏的，单独盯一遍。', indent=False, bold=True)
body('第一，键盘的 2 脚和 4 脚必须空着。这两只脚和 1、3 脚本来就是两组独立的触点，接上任何东西都会让按键矩阵串行。')
body('第二，两个卡座的 VDD 只能 3.3V。接 5V 当场烧卡，而且芯片不会立刻冒烟，是慢慢坏的。')
body('第三，二极管黑环必须朝按键。反了整列读不出，而且从外观上看不出来，只能靠第 12 项的反向测试抓。')

body('70 项全过，再插屏排线、再上电跑固件。', indent=False, bold=True)

doc.save(OUT)
print('SAVED', OUT, os.path.getsize(OUT))
