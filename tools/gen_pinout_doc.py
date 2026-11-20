# -*- coding: utf-8 -*-
import os
from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.oxml.ns import qn
from docx.oxml import OxmlElement

OUT = r'G:\Memoria OS\贴片器件引脚认法与接线表.docx'

doc = Document()
sec = doc.sections[0]
sec.page_width = Cm(21)
sec.page_height = Cm(29.7)
sec.top_margin = Cm(2.5)
sec.bottom_margin = Cm(2.5)
sec.left_margin = Cm(2.5)
sec.right_margin = Cm(2.5)

st = doc.styles['Normal']
st.font.name = 'Times New Roman'
st.font.size = Pt(12)
st.element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
st.paragraph_format.line_spacing = 1.5
st.paragraph_format.space_before = Pt(0)
st.paragraph_format.space_after = Pt(0)


def body(text):
    p = doc.add_paragraph(text)
    p.paragraph_format.first_line_indent = Pt(24)
    p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    return p


def lead(text):
    p = doc.add_paragraph(text)
    p.paragraph_format.first_line_indent = Pt(24)
    p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
    for r in p.runs:
        r.font.bold = True
    return p


def h1(text):
    p = doc.add_heading(text, level=1)
    for r in p.runs:
        r.font.name = 'Times New Roman'
        r.font.size = Pt(16)
        r.font.bold = True
        r.font.color.rgb = RGBColor(0, 0, 0)
        r._element.rPr.rFonts.set(qn('w:eastAsia'), '黑体')
    p.paragraph_format.space_before = Pt(14)
    p.paragraph_format.space_after = Pt(6)
    p.paragraph_format.first_line_indent = Pt(0)
    return p


def caption(text):
    p = doc.add_paragraph(text)
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.first_line_indent = Pt(0)
    p.paragraph_format.space_before = Pt(6)
    p.paragraph_format.space_after = Pt(3)
    for r in p.runs:
        r.font.size = Pt(10.5)
        r.font.name = 'Times New Roman'
        r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
    return p


def table(headers, rows, widths, fpt=10.5):
    t = doc.add_table(rows=1, cols=len(headers))
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
    for i, htxt in enumerate(headers):
        c = hdr.cells[i]
        c.width = Cm(widths[i])
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
        r.font.size = Pt(fpt)
        r.font.bold = True
        r.font.name = 'Times New Roman'
        r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
    for row in rows:
        cells = t.add_row().cells
        trPr = t.rows[-1]._tr.get_or_add_trPr()
        el = OxmlElement('w:cantSplit')
        trPr.append(el)
        for i, val in enumerate(row):
            c = cells[i]
            c.width = Cm(widths[i])
            p = c.paragraphs[0]
            p.paragraph_format.first_line_indent = Pt(0)
            p.paragraph_format.line_spacing = 1.0
            p.paragraph_format.space_before = Pt(2)
            p.paragraph_format.space_after = Pt(2)
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if i == 0 else WD_ALIGN_PARAGRAPH.LEFT
            r = p.add_run(str(val))
            r.font.size = Pt(fpt)
            r.font.name = 'Times New Roman'
            r._element.rPr.rFonts.set(qn('w:eastAsia'), '宋体')
    return t


# ---------- 封面标题 ----------
p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(4)
r = p.add_run('贴片器件引脚认法与接线表')
r.font.name = 'Times New Roman'
r.font.size = Pt(18)
r.font.bold = True
r._element.rPr.rFonts.set(qn('w:eastAsia'), '黑体')

p = doc.add_paragraph()
p.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.space_after = Pt(16)
r = p.add_run('Memoria OS 掌机 · ESP32-S3 · 键盘矩阵与电源')
r.font.name = 'Times New Roman'
r.font.size = Pt(12)
r._element.rPr.rFonts.set(qn('w:eastAsia'), '楷体')

# ---------- 一 ----------
h1('一、怎么认贴片芯片的 1 脚')

body('贴片芯片表面没有丝印不要紧。所有这类芯片都只标记 1 脚，找到它，其余按顺序数就行。1 脚有四种常见标记，看到任何一种，那个角就是 1 脚：')
body('（1）表面有一个小凹坑或者小圆点；（2）靠近某一角有一道短粗的横线；（3）某一个角被削掉，呈斜口；（4）芯片一端有一道竖着的粗边。')

body('数脚的方向是死的，不会变：把 1 脚放在左上角，沿逆时针走。左边一列从上往下数，数到底，再从右边一列的最下面往上数。')

body('8 脚的芯片：左边 4 只（1~4，从上到下），右边 4 只（5~8，从下往上）。')
body('28 脚的芯片：左边 14 只（1~14，从上到下），右边 14 只（15~28，从下往上）。')

caption('表1 两种常用封装的脚位编号方向')
table(['封装', '1 脚位置', '左边一列', '右边一列'],
      [['SOP-8', '左上角（有凹点那端）', '1~4，从上往下', '5~8，从下往上（8 在右上）'],
       ['SOIC-28 / SSOP-28', '左上角（有凹点那端）', '1~14，从上往下', '15~28，从下往上（28 在右上）']],
      [3.4, 5.0, 4.6, 4.6])

body('记不住就记一句话：数字从凹点那个角开始绕圈加，永远逆时针。')

# ---------- 二 ----------
h1('二、MCP23017（28 脚，两片）')

body('两片是同一种芯片，脚位完全一样，区别只在 15、16、17 三只地址脚接法不同，所以地址不同（0x20 和 0x21）。表2 里给出每只脚在两片上的接法。')

caption('表2 MCP23017 引脚对照与接线')
rows = []
mcp = [
    ('1', 'GPB0', '#1 接列 C0', '#2 空'),
    ('2', 'GPB1', '#1 接列 C1', '#2 空'),
    ('3', 'GPB2', '#1 接列 C2', '#2 空'),
    ('4', 'GPB3', '#1 接列 C3', '#2 空'),
    ('5', 'GPB4', '空', '空'),
    ('6', 'GPB5', '空', '空'),
    ('7', 'GPB6', '空', '空'),
    ('8', 'GPB7', '空', '空'),
    ('9', 'VDD 电源正', '3V3', '3V3'),
    ('10', 'VSS 地', 'GND', 'GND'),
    ('11', 'NC 空脚', '空', '空'),
    ('12', 'SCL 时钟', 'GPIO6', 'GPIO6'),
    ('13', 'SDA 数据', 'GPIO1', 'GPIO1'),
    ('14', 'NC 空脚', '空', '空'),
    ('15', 'A0 地址', 'GND', '3V3'),
    ('16', 'A1 地址', 'GND', 'GND'),
    ('17', 'A2 地址', 'GND', 'GND'),
    ('18', 'RESET 复位', '3V3', '3V3'),
    ('19', 'INTB 中断', '空', '空'),
    ('20', 'INTA 中断', '空', '空'),
    ('21', 'GPA0', '#1 接行 R0', '#2 接列 C4'),
    ('22', 'GPA1', '#1 接行 R1', '#2 接列 C5'),
    ('23', 'GPA2', '#1 接行 R2', '#2 接列 C6'),
    ('24', 'GPA3', '#1 接行 R3', '#2 接列 C7'),
    ('25', 'GPA4', '#1 接行 R4', '#2 接列 C8'),
    ('26', 'GPA5', '#1 接行 R5', '#2 接列 C9'),
    ('27', 'GPA6', '空', '空'),
    ('28', 'GPA7', '空', '空'),
]
for m in mcp:
    rows.append(list(m))
table(['脚号', '名称', 'MCP#1（0x20）', 'MCP#2（0x21）'], rows, [1.4, 4.0, 5.3, 5.3])

body('两片接同一根 I2C 总线：12 脚 SCL 都接 GPIO6，13 脚 SDA 都接 GPIO1。这两根线上必须各挂一只 4.7kΩ 电阻到 3V3，否则总线拉不起来，扫不到芯片。')
body('每片的 9 脚（VDD）和 10 脚（VSS）之间并一只 100nF 电容，就近焊在芯片旁边。')
body('19、20 脚是中断脚，本项目不用，直接空着。11、14 脚是空脚，也空着。')

# ---------- 三 ----------
h1('三、TP4056（8 脚 SOP）')

body('这是充电芯片。芯片表面有散热焊盘的那种（ESOP-8），底部那块方形的焊盘必须接地，不能空。')

caption('表3 TP4056 引脚接线')
table(['脚号', '名称', '怎么接'],
      [['1', 'TEMP 温度检测', '直接接 GND，关闭电池温度检测'],
       ['2', 'PROG 充电电流设定', '接一只电阻到 GND，阻值见下表'],
       ['3', 'GND 地', '接 GND'],
       ['4', 'VCC 输入', '接 USB 的 5V（充电输入）'],
       ['5', 'BAT 电池', '接电池正极'],
       ['6', 'STDBY 充满指示', '空着（或按需接 LED）'],
       ['7', 'CHRG 充电指示', '接 GPIO42，充电中会拉低'],
       ['8', 'CE 使能', '接 VCC，高电平才工作']],
      [1.6, 4.4, 10.0])

body('充电电流由 2 脚那颗电阻决定，算法是 1200 除以阻值（单位 kΩ，结果单位 A）。常用两档：')
body('2.4kΩ 对应大约 500mA；1.2kΩ 对应大约 1A。电池容量在 1000mAh 以下就用 2.4kΩ，2000mAh 以上才用 1.2kΩ。')

body('另外两个状态脚是开漏输出，本身不会输出电压，只能拉低。7 脚 CHRG 充电时拉低、其余时间悬空，接 GPIO42 就能判断在不在充电。')

# ---------- 四 ----------
h1('四、AO3401（3 脚 SOT-23，P 型 MOS）')

body('先说三只脚分别干什么，这个不用记公式，记用途就行：')
lead('G 叫栅极，是控制脚。它不通过大电流，只负责“开关”。相当于水龙头的把手，拧不拧它决定水通不通。')
lead('S 叫源极，是电源进来的那只脚。')
lead('D 叫漏极，是接负载的那只脚，电流从这里出去。')

body('这是 P 型 MOS，开关方向是反的：G 比 S 低 2V 以上就导通，S 和 D 接上；G 和 S 一样高就断开。所以接 5V 的场合，G 给低电平是开，给高电平是关。')

body('SOT-23 认脚：三只脚是“一边一只、另一边两只”。把有字的一面朝上，单独那一只在顶部，它是 3 脚（D）；底部并排两只，左边是 1 脚（G），右边是 2 脚（S）。')

caption('表4 AO3401 引脚接线')
table(['脚号', '名称', '怎么接'],
      [['1', 'G 栅极', '接 GPIO48；同时用 100kΩ 电阻接到 5V（S 脚）'],
       ['2', 'S 源极', '接 5V（MT3608 输出正）'],
       ['3', 'D 漏极', '接 5V_PERIPH，也就是屏、功放、SD 卡这一路负载的正极']],
      [1.6, 4.4, 10.0])

body('那只 100kΩ 电阻的作用是上电时把 G 拉到 5V，保证默认是断开的，不会一上电就把负载全打开。GPIO48 输出低电平时导通，输出高电平时断电。')

body('这一路是可选的。不做深睡眠断电，可以整只不装，屏和功放直接吃 5V 也能正常工作。')

# ---------- 五 ----------
h1('五、MT3608 升压模块')

body('模块上有四个焊盘，两进两出，没有别的脚。')

caption('表5 MT3608 模块接线')
table(['焊盘', '接到哪'],
      [['VIN+', 'TP4056 的 OUT+（或模块上的 OUT+）'],
       ['VIN-', 'TP4056 的 OUT-（或模块上的 OUT-）'],
       ['VOUT+', '核心板的 5V 脚（同时是 AO3401 的 S 脚）'],
       ['VOUT-', '核心板的 GND']],
      [3.4, 12.6])

body('接核心板之前，先只给模块通电，用万用表量 VOUT+ 和 VOUT- 之间，拧电位器把它调到 5.0V，再往下接。模块出厂默认输出常常是 9~12V，直接接核心板会烧。')

# ---------- 六 ----------
h1('六、上电顺序')

body('按顺序来，每步都有可以量的点，出错能立刻发现：')
body('第一步，只接电池和 TP4056，量电池两端电压，应该在 3.0~4.2V 之间。')
body('第二步，接 MT3608，先不接核心板，量输出调到 5.0V。')
body('第三步，接核心板 5V 和 GND，插 USB，串口能看到启动日志，量核心板 3V3 脚是 3.3V。')
body('第四步，接两片 MCP23017，先不接键盘，扫 I2C 应该读到 0x20 和 0x21。')
body('第五步，接键盘的行线和列线，按键应该在串口上打印出对应点位。')

# ---------- 七 ----------
h1('七、焊之前先量一遍')

body('这几项量完再上电，能省掉大部分返工：')

caption('表6 通电前的检查项')
table(['量什么', '应该是什么结果'],
      [['每只键的两组脚', '不按的时候只有一组通，按下之后两组都通'],
       ['每只二极管', '黑环那头是负极；万用表二极管档正接读数 0.5~0.7V，反接不通'],
       ['MT3608 输出', '5.0V，不是 9V 或 12V'],
       ['3V3 与 GND 之间', '不通（用电阻档量，不能是 0Ω）'],
       ['两片 MCP 的 15/16/17 脚', '#1 三只都接 GND；#2 只有 15 脚接 3V3'],
       ['I2C 两根线对 3V3', '各约 4.7kΩ（说明上拉电阻在）'],
       ['两个卡座的 VDD', '3.3V，不能是 5V']],
      [6.0, 10.0])

body('最后一条最容易漏：键盘的 60 只键上，只有 1 脚和 3 脚接线，2 脚和 4 脚空着，不要把它们接到 3V3 或者并在一起。')

# ---------- 八 ----------
h1('八、SD / TF 卡座（贴片）')

body('卡座和芯片不是一类东西。它不是标准封装，没有凹点，也没有固定的 1 脚位置，不同厂家的焊盘顺序反过来都可能。所以这一节不能靠数脚，只能靠“卡的标准触点顺序 + 卡座朝向 + 万用表核对”这三步。')

body('先记卡本身的触点顺序。小卡（microSD）插到最里面、金手指朝上，从卡尖端往卡尾数，八个触点依次是：1 DAT2、2 CD/DAT3、3 CMD、4 VDD、5 CLK、6 VSS、7 DAT0、8 DAT1。')

body('大卡（SD）有九个触点，顺序是：1 CD/DAT3、2 CMD、3 VSS1、4 VDD、5 CLK、6 VSS2、7 DAT0、8 DAT1、9 DAT2。')

body('卡座的焊盘一般按这个顺序排成一排，但左右镜像很常见。核对方法：插一张卡进去，万用表打到通断档，一根表笔点在某个焊盘上，另一根在卡的金手指上挨个刮，响的那一对就是它的真实位置。几个焊盘量一遍，顺序就出来了，比猜快得多。')

body('卡座外围那圈金属壳要接 GND，一是抗干扰，二是防止手摸上去误触。')

caption('表7 小卡（microSD）接 SPI，作为启动卡')
table(['触点', '名称', '接到哪'],
      [['1', 'DAT2', '空着'],
       ['2', 'CD/DAT3', 'GPIO17，片选 CS'],
       ['3', 'CMD', 'GPIO11，MOSI'],
       ['4', 'VDD', '3V3，只能 3.3V，不能接 5V'],
       ['5', 'CLK', 'GPIO12，SCLK'],
       ['6', 'VSS', 'GND'],
       ['7', 'DAT0', 'GPIO13，MISO'],
       ['8', 'DAT1', '空着']],
      [2.6, 4.4, 9.0])

caption('表8 大卡（SD）接 SDMMC 1-bit，作为扩展卡')
table(['触点', '名称', '接到哪'],
      [['1', 'CD/DAT3', '空着'],
       ['2', 'CMD', 'GPIO39'],
       ['3', 'VSS1', 'GND'],
       ['4', 'VDD', '3V3'],
       ['5', 'CLK', 'GPIO40'],
       ['6', 'VSS2', 'GND'],
       ['7', 'DAT0', 'GPIO41'],
       ['8', 'DAT1', '空着'],
       ['9', 'DAT2', '空着']],
      [2.6, 4.4, 9.0])

body('两个卡座可以同时接，各走各的总线，互不影响：小卡走 SPI，只管启动和字库；大卡走 SDMMC，开机后再插，用来导数据。')

body('扫不到卡先别换卡座，按顺序查三处：VDD 是不是 3.3V；CLK 有没有波形；CMD、DAT0、DAT3 这三根线各加一只 10kΩ 上拉到 3V3。最后这条能解决大部分识别不到的问题。')

body('小卡座上通常还有一只 CD 脚，用来检测卡有没有插进去，本项目不用，空着就行；如果卡座内部把它和外壳连在一起，直接接 GND。')

doc.save(OUT)
print('SAVED', OUT, os.path.getsize(OUT))
