from docx import Document
from docx.shared import Pt, Cm, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.enum.table import WD_ALIGN_VERTICAL
from docx.enum.section import WD_SECTION
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
        ('Heading 2', '黑体', 'Times New Roman', 14, True),
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
DOC.styles['Heading 2'].paragraph_format.space_before = Pt(11)
DOC.styles['Heading 2'].paragraph_format.space_after = Pt(5)
DOC.styles['Heading 2'].paragraph_format.first_line_indent = Pt(0)
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
    set_font(r, '宋体', 'Times New Roman', 10.5, bold=bold)


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
    th = OxmlElement('w:tblHeader')
    trPr.append(th)
    cs = OxmlElement('w:cantSplit')
    trPr.append(cs)
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


DOC.add_paragraph('Memoria OS 硬件接线表', style='Title')
p = DOC.add_paragraph()
p.paragraph_format.alignment = WD_ALIGN_PARAGRAPH.CENTER
p.paragraph_format.first_line_indent = Pt(0)
p.paragraph_format.space_after = Pt(12)
r = p.add_run('v1.6.17 · 打印后可直接施工 · 引脚号全部取自 drivers_config.hpp')
set_font(r, '楷体', 'Times New Roman', 12)

DOC.add_heading('一、依据与适用范围', level=1)
body('本表用于把散件焊成整机。所有引脚号、电平约定、分压比、地址都取自固件唯一引脚源文件 '
     'components/memoria_drivers/include/drivers_config.hpp，行列扫描极性取自 '
     'components/memoria_kernel/keyboard_driver.cpp，键位布局取自 keyboard_keymap.hpp，'
     '功放与次级 MOS 极性取自 memoria_drivers/power.cpp。凡本表与任何旧文档、旧图纸冲突，以本表和上述源文件为准。')
body('与旧版 docs/WIRING.md 相比，本表已经反映三处硬件变更：WS2812 灯带整条删除，'
     'GPIO46 不再接任何东西；功放使能改为 GPIO47、外围 5V 轨改由 GPIO48 控的 P-MOS 通断；'
     '侧面增加 SD 大卡座，走 SDMMC 1-bit 三线。')

DOC.add_heading('二、电源轨与正负极定义', level=1)
body('整机共四条电源网络加一条地。正负极按下表固定，不得混接。所有模块的 GND 单点汇聚到电池负极，不串接、不环回。')
cap('表1　电源网络定义')
table(['网络', '电压', '正极来自', '去向', '要点'],
      [['BAT+', '3.0~4.2V', '电池正极 / TP4056 B+', 'MT3608 IN、分压 R1', '充放电都走这一条'],
       ['5V_MAIN', '5.0V', 'MT3608 OUT', '核心板 5V 脚', '核心板板载 LDO 由此出 3V3'],
       ['5V_PERIPH', '5.0V', 'AO3401 漏极（源极接 5V_MAIN）', '功放 VIN、AMS1117 输入', '深睡时由 GPIO48 拉高切断'],
       ['3V3', '3.3V', '核心板 3V3 脚', '只给 ESP32 自己、侧键 10k 上拉', '常供，不受 P-MOS 控制'],
       ['3V3_PERIPH', '3.3V', 'AMS1117-3.3 输出（输入取自 5V_PERIPH）', '屏、TF 卡座、MCP×2、摇杆、麦克风、I2C 上拉', '深睡随 5V_PERIPH 一起断'],
       ['GND', '0V', '电池负极 / TP4056 B−', '全部模块', '单星接地，先接地线再走信号']],
      ['center', 'center', 'left', 'left', 'left'],
      [2.2, 2.2, 4.4, 4.2, 3.6])

body('次级 MOS 是 AO3401（P 沟道）。源极接 5V_MAIN，漏极输出 5V_PERIPH，栅极接 GPIO48 并在栅源之间并一颗 100K 上拉到 5V_MAIN。'
     '栅极被拉低时导通、外围得电；栅极被拉高时关断、外围断电。上拉的作用是上电瞬间默认关断，等固件把 GPIO48 拉低后再给外围上电。', indent=True)
body('电池电压采样用两枚等值电阻串联分压：BAT+ → R1 → 采样节点 → R2 → GND，节点接 GPIO4，并在节点与 GND 之间并一颗 100nF。'
     '分压比固定为 2，固件按 VADC×2 还原电池电压。阻值取 200K+200K 与开机日志一致，静态漏电约 10µA；'
     'drivers_config.hpp 的注释仍写 100K+100K，两枚必须等值，比值不变，任选一组均可正常工作。', indent=True)

DOC.add_heading('三、ESP32-S3 引脚总表', level=1)
body('按 GPIO 编号排序。方向里的“出”是 ESP32 驱动，“入”是 ESP32 读取，“双向”用于 I2C 与 SD 数据线。')
cap('表2　已用引脚一览')
table(['GPIO', '信号', '方向', '接到', '电平约定'],
      [['1', 'I2C SDA', '双向', 'MCP #1、#2 的 SDA（并联）', '400kHz，外部 4.7k 上拉到 3V3_PERIPH'],
       ['2', '电源键 / 唤醒', '入', '侧键一端，另一端接 GND', '低有效，外部 10k 上拉，EXT0 唤醒，不进矩阵'],
       ['4', '电池采样', '入', '分压节点（R1/R2 中点）', 'ADC1_CH3，节点并 100nF'],
       ['5', '摇杆 Y', '入', 'KY-023 VRy', 'ADC1_CH4'],
       ['6', 'I2C SCL', '出', 'MCP #1、#2 的 SCL（并联）', '400kHz，外部 4.7k 上拉到 3V3_PERIPH'],
       ['7', '摇杆 X', '入', 'KY-023 VRx', 'ADC1_CH6'],
       ['8', 'LCD RST', '出', '屏模块 RESET', '低复位'],
       ['9', 'LCD DC', '出', '屏模块 DC/RS', '高数据低命令'],
       ['10', 'LCD CS', '出', '屏模块 CS', '低选中'],
       ['11', 'SPI MOSI', '出', '屏 SDI、屏板 TF 槽 MOSI', '共用 SPI2'],
       ['12', 'SPI SCLK', '出', '屏 SCK、屏板 TF 槽 SCK', '共用 SPI2'],
       ['13', 'SPI MISO', '入', '屏 SDO、屏板 TF 槽 MISO', '共用 SPI2'],
       ['14', 'LCD 背光', '出', '屏模块 LED', 'LEDC PWM，低=灭'],
       ['15', 'I2S DOUT', '出', 'MAX98357A DIN', 'I2S0 发送'],
       ['16', 'I2S DIN', '入', 'INMP441 SD', 'I2S0 接收'],
       ['17', 'TF 卡 CS', '出', '屏板 TF 槽 SD_CS / TF_CS', '低选中'],
       ['18', 'I2S BCLK', '出', '功放 BCLK + 麦克风 SCK（并联）', '收发共用一个时钟'],
       ['21', 'I2S LRC', '出', '功放 LRC + 麦克风 WS（并联）', '收发共用一个帧同步'],
       ['38', '摇杆按下', '入', 'KY-023 SW', '低=按下，内部上拉'],
       ['39', 'SDMMC CMD', '双向', 'SD 大卡座 CMD', '1-bit 模式'],
       ['40', 'SDMMC CLK', '出', 'SD 大卡座 CLK', '1-bit 模式'],
       ['41', 'SDMMC D0', '双向', 'SD 大卡座 DAT0', '只接这一根数据线'],
       ['42', '充电检测', '入', 'TP4056 CHRG', '低=充电中，内部上拉，建议外部再并 10k'],
       ['47', '功放使能', '出', 'MAX98357A SD/SHDN', '高=出声，低=静音，外部 100k 下拉'],
       ['48', '外围 5V 开关', '出', 'AO3401 栅极', '低=通电，高=断电，栅极 100k 上拉到 5V']],
      ['center', 'left', 'center', 'left', 'left'],
      [1.6, 3.0, 1.4, 6.0, 4.0])

body('下列引脚一律不许接任何外设：GPIO0 是 BOOT，用核心板自带按键；GPIO3、45、46 是 strapping 脚，上电瞬间电平决定启动模式；'
     'GPIO19、20 是 USB D−/D+；GPIO26 至 32 是片内 Flash 的 SPI 总线，GPIO33 至 37 被 Octal PSRAM 内部占用，接了必崩。'
     'GPIO22 至 25 在 S3 芯片内部根本不存在，不用管。GPIO46 原来规划的键盘灯带已整条删除，'
     '该脚在 S3 上本身是输入专用，做不了输出。', indent=True)
body('GPIO43、44 是 UART0 的 TX/RX，默认打印日志。这两只可以当普通 IO，但占了就没串口日志，出问题只能靠屏幕看。'
     '当前分配里没用它们，保持空闲。', indent=True)
body('GPIO39 至 42 兼职 JTAG 调试口（MTCK/MTDO/MTDI/MTMS）。本机把 39、40、41 给了 SDMMC，42 给了充电检测，'
     '所以接不了外挂调试器走这四根线。不过 S3 自带 USB-JTAG：直接插板子的 USB 口（GPIO19/20）就能调试，'
     '失去这四根线的调试复用不影响使用。19、20 只能当 USB 用，别拿去接别的外设。', indent=True)
body('算下来脚基本占满了。真正还空着的只有 GPIO43、44（UART0 TX/RX），要用就得放弃串口日志。'
     '以后加外设别随手挑脚，先回这张表查；能挂 I2C 的优先挂总线（MCP23017 还有 0x22 至 0x27 六个地址可用）。', indent=True)

DOC.add_heading('3.1　会烧器件的六个点', level=2)
body('这六处接错不是"不工作"，是直接烧芯片或烧模块，焊之前逐条对一遍。')
body('（1）5V 不许进任何 GPIO。S3 的 IO 绝对最大 3.6V。屏幕模块 VCC、TF 卡座 VCC、摇杆 VCC、MCP23017 VDD 全部接 3V3_PERIPH；'
     '屏幕模块如果同时标 5V 和 3V3 两档，只用 3V3 档并接到 3V3_PERIPH，用 5V 档模块的逻辑脚会输出 5V 电平，直接打穿 ESP32。', indent=True)
body('（2）电池分压电阻必须在。GPIO4 直接吃 4.2V 会烧。R1、R2 各 100k 焊好、中点并 100nF 之后才插电池；'
     '第一次上电先不插电池，用 USB 供电量中点对 GND，超过 1.6V 就别接电池，先查电阻。', indent=True)
body('（3）摇杆只能 3.3V 供电。KY-023 的电位器两端就是供电轨，接 5V 时 VRx、VRy 会摆到 5V，直灌 ADC 脚。', indent=True)
body('（4）AO3401 栅极上拉必须到 5V，不能到 3V3。源极是 5V，栅极只拉到 3.3V 时 Vgs 只有 −1.7V，处在微导通区，'
     '深睡眠时外围断不干净，MOS 还会发热。100k 上拉到 5V_MAIN 才能可靠关断。'
     '代价是 GPIO48 输出高时约有 17µA 从 5V 经 100k 倒灌进 IO，这个电流安全，但 GPIO48 必须配置成推挽输出，不能留输入。', indent=True)
body('（5）功放 SD 脚加 100k 下拉到 GND。GPIO47 从通电到固件跑起来之间有几百毫秒是高阻，MAX98357A 的 SD 悬空时状态不确定，'
     '可能"啪"一声。加下拉保证上电默认静音。', indent=True)
body('（6）两条 3.3V 绝对不能并联。核心板自己的 3V3 只养 ESP32，外设走 AMS1117 出来的 3V3_PERIPH，'
     '两轨只在 GND 共地，不许拿一根线短接，也不许一个模块一半接 3V3、一半接 3V3_PERIPH。'
     '两个稳压器输出对顶，会互相灌电流，轻则发热重启，重则烧 LDO。', indent=True)

DOC.add_heading('四、分模块接线表', level=1)

DOC.add_heading('4.1　电源链与充电', level=2)
body('电源分四级，从上往下：电池 → 5V_MAIN → 5V_PERIPH → 3V3_PERIPH；另外核心板自带一条 3V3，只给 ESP32 自己。')
body('第一级 电池 3.0~4.2V，进 TP4056 充电板。第二级 MT3608 把电池升到 5.0V，这条叫 5V_MAIN，'
     '同时喂核心板 5V 脚和 AO3401 源极。核心板板载 LDO 由此出 3V3，只养 ESP32 和侧键 10k 上拉。', indent=True)
body('第三级 5V_PERIPH 是 AO3401（P-MOS）漏极出来的，GPIO48 拉低才导通，深睡时拉高断开。'
     '它同时供功放 VIN 和 AMS1117 的输入。第四级 3V3_PERIPH 由 AMS1117 从 5V_PERIPH 降出来，'
     '屏、TF 卡座、MCP×2、摇杆、麦克风、I2C 上拉全挂这条。', indent=True)
body('所以全机是"两段 5V、两条 3.3V"：5V_MAIN 与 5V_PERIPH 由 MOS 隔开，3V3 与 3V3_PERIPH 由两颗不同的稳压器各自产生。'
     '两条 3.3V 只在 GND 共地，不能合并成一条。', indent=True)
body('电容不是稳压器，只做滤波和瞬态储能：MT3608 输出端挂 100µF 电解 + 100nF 陶瓷；'
     '每颗外设的 VCC 脚就近一颗 100nF；AMS1117 输入输出各一组 10µF + 100nF。'
     '电解耐压取 16V 以上。', indent=True)
cap('表3　电源链接线')
table(['连接点', '接到', '说明'],
      [['电池红', 'TP4056 B+', '电池正极入充电板'],
       ['电池黑', 'TP4056 B−', '电池负极，同时是全机 GND 汇点'],
       ['TP4056 OUT+', 'MT3608 IN+', '电池电压升到 5V'],
       ['TP4056 OUT−', 'MT3608 IN− / GND', '共地'],
       ['MT3608 OUT+', '核心板 5V 脚、AO3401 源极', '5V_MAIN'],
       ['MT3608 OUT−', 'GND', '共地'],
       ['TP4056 CHRG', 'GPIO42', '开漏输出，充电中拉低'],
       ['电池正极', 'R1 200K 一端', '分压上臂'],
       ['R1 另一端', 'R2 200K 一端 + GPIO4 + 100nF 一端', '采样节点'],
       ['R2 另一端 / 100nF 另一端', 'GND', '分压下臂与滤波'],
       ['侧键①一端', 'GPIO2', '电源键，长按判定'],
       ['侧键①另一端', 'GND', '按下即拉低']],
      ['center', 'left', 'left'],
      [4.0, 6.4, 5.0])
body('上电前必做：MT3608 是升压模块，出厂输出常在 9~12V。先只接电池，OUT+ 上什么都不接，用万用表量 OUT+ 对 OUT−，'
     '拧模块上的电位器把电压调到 5.0V（4.9~5.1V 可接受），再接核心板 5V 脚。'
     '按出厂电压直接接上去会把核心板烧掉。', indent=True)
body('调好后装机前再复量一次。MT3608 标称输出 2A，本机整机负载不到 1A，余量够；EN 脚悬空即可（模块板上一般已上拉到 IN+）。'
     '插 USB 和用电池时 MT3608 都在工作，不需要切换。', indent=True)
body('电源键只接 GPIO2 一处。请不要按早期注释把它再并到 MCP #1 的 GPA0 上：GPA0 是矩阵行输出脚，'
     '按键直接对地会把输出脚短到 GND。', indent=True)

DOC.add_heading('4.1.1　外设独立 3V3：AMS1117-3.3', level=3)
body('分工：核心板自带的 LDO 只给 ESP32 自己用；屏、TF 卡、MCP23017、摇杆、麦克风这些外设，'
     '另用一颗 AMS1117-3.3 从 5V_PERIPH 降到 3V3_PERIPH 单独供，不占核心板 LDO 的额度。'
     '好处是 ESP32 发射时瞬间 200mA 不会把外设电压拖下水，也不会因为外设短路把核心板 LDO 烧了。')
body('封装是 SOT-223：一侧三个细脚，另一侧一整块大铜片（tab），看着像"上面一个脚、下面三个脚"。'
     'tab 内部和 2 脚直接连通，它也是 VOUT，不是备用脚，必须焊到铜箔上，既导电也散热。')
body('认脚不用猜，用万用表通断档：先拿一支表笔顶住 tab 那块大铜片，另一支逐个点三个细脚，'
     '哪只和 tab 通（0Ω）哪只就是 2 脚 VOUT，通常是中间那只。剩下两只，把三细脚朝下、正对芯片看，'
     '左边是 1 脚 GND，右边是 3 脚 VIN。复核一下：二极管档红笔点 GND、黑笔点 VIN，应读出 0.5~0.7V，对调不通。')
body('焊的时候当心 tab 和 3 脚之间只有 1.7mm，手工焊最容易连锡——那等于把 5V 直接短到 3.3V，上电就烧。'
     '焊完先用通断档量 3 脚对 tab：不通才算过，量出 0Ω 立刻返工。')
cap('表3A　AMS1117-3.3 接线')
table(['引脚 / 元件', '接到', '说明'],
      [['3 脚 VIN', '5V_PERIPH（AO3401 漏极）', '深睡时 AO3401 一断，AMS1117 跟着断电'],
       ['1 脚 GND', 'GND', '共地'],
       ['2 脚 VOUT', '3V3_PERIPH', '输出轨，外设全部从这里取电'],
       ['大焊盘（tab）', '3V3_PERIPH', '与 2 脚同电位，必须焊到铜箔上当散热'],
       ['输入电容', '10µF + 100nF，都到 GND', '贴近 3 脚，抑制输入纹波'],
       ['输出电容', '10µF + 100nF，都到 GND', '贴近 2 脚，低 ESR，缺了会自激']],
      ['center', 'left', 'left'],
      [4.4, 5.6, 6.4])
cap('表3B　3V3_PERIPH 带哪些负载')
table(['负载', '从 3V3_PERIPH 取电', '峰值电流', '说明'],
      [['屏幕 ILI9488', 'VCC', '120mA', '含背光；背光亮度只由 GPIO14 的 PWM 调，不额外加开关'],
       ['屏板 TF 卡座', 'VCC', '100mA', '写卡瞬间峰值'],
       ['MCP23017 ×2', 'VDD（9 脚）', '2mA', '两片合计，静态很小'],
       ['摇杆 KY-023', 'VCC', '1mA', '电位器两端'],
       ['INMP441', 'VDD', '2mA', '不要接 5V'],
       ['I2C 上拉 4.7k ×2', '一端', '1mA', '必须与本轨同源，不能挂到核心板 3V3'],
       ['CHRG 上拉 10k', '一端', '0.3mA', '可选；接到核心板 3V3 也安全']],
      ['center', 'center', 'center', 'left'],
      [3.6, 4.4, 2.6, 7.2])
body('合计峰值约 230mA。AMS1117 从 5V 降到 3.3V，压差 1.7V，230mA 时耗散约 0.4W，'
     'SOT-223 背面的铜箔至少铺 2cm² 才不烫手；整机满负载时这颗摸着温热是正常的。', indent=True)
body('侧键那一路的 10k 上拉例外：必须接核心板常电 3V3，不能接 3V3_PERIPH。'
     '因为深睡时外设 3V3 断电，上拉跟着没了，GPIO2 会浮空，可能被噪声误唤醒。', indent=True)
body('接线顺序：先把 5V_PERIPH 和 3V3_PERIPH 两根轨分清楚并贴上标签，再往外设上电。'
     '两颗 LDO 的输出绝不能用一根线短接，也不要把某个模块的 VCC 接到核心板 3V3 上。', indent=True)

DOC.add_heading('4.2　屏幕 ILI9488', level=2)
cap('表4　屏幕模块接线')
table(['模块脚', '接到', '备注'],
      [['VCC', '3V3_PERIPH', '模块板载稳压，接 3V3_PERIPH 即可'],
       ['GND', 'GND', '共地'],
       ['CS', 'GPIO10', '片选'],
       ['RESET', 'GPIO8', '复位'],
       ['DC / RS', 'GPIO9', '数据命令选择'],
       ['SDI / MOSI', 'GPIO11', '与 TF 槽共用'],
       ['SCK', 'GPIO12', '与 TF 槽共用'],
       ['SDO / MISO', 'GPIO13', '与 TF 槽共用'],
       ['LED / BL', 'GPIO14', '背光 PWM']],
      ['center', 'center', 'left'],
      [3.4, 4.0, 8.0])

DOC.add_heading('4.3　启动 TF 卡（屏幕板上的卡槽）', level=2)
body('启动卡就是屏幕模块自带的那只 TF 槽，与屏幕共用 SPI 三根线，只多一根片选。字库、应用、脚本、音频都从这张卡读。')
body('卡座是贴片件，没有丝印，按卡上金手指的触点编号对：金手指面朝上、卡横放，从左往右数 1 到 8。'
     '如果翻的是卡座焊盘，顺序与卡面相反，别按焊盘个数去数，很容易把固定脚算进去。'
     '认不准就直接量：与 3.3V 导通的那只焊盘是 4 号 VDD，往一侧依次是 3、2、1，往另一侧依次是 5、6、7、8；'
     '再用 GND 复核，响的必须是 6 号，位置不对就是方向反了。')
cap('表5　microSD 触点与 SPI 接线（含上拉）')
table(['触点', '卡上丝印', 'SPI 用途', '接到', '上拉'],
      [['1', 'DAT2', '不用', '悬空', '10k 到 3V3_PERIPH'],
       ['2', 'CD / DAT3', 'CS', 'GPIO17', '10k 到 3V3_PERIPH，必须'],
       ['3', 'CMD', 'MOSI（DI）', 'GPIO11', '10k 到 3V3_PERIPH'],
       ['4', 'VDD', '电源', '3V3_PERIPH', '—'],
       ['5', 'CLK', 'SCLK', 'GPIO12', '—'],
       ['6', 'VSS', '地', 'GND', '—'],
       ['7', 'DAT0', 'MISO（DO）', 'GPIO13', '10k 到 3V3_PERIPH'],
       ['8', 'DAT1', '不用', '悬空', '10k 到 3V3_PERIPH'],
       ['CD 检测', '—', '不用', '悬空', '—'],
       ['固定脚', '—', '机械', 'GND', '—']],
      ['center', 'center', 'left', 'center', 'left'],
      [2.0, 3.2, 3.0, 2.8, 5.0])
body('不用的 DAT1、DAT2 不能接 GND。它们是双向 I/O，卡若误进 SD 模式会主动输出高电平，对地短路会烧卡或烧卡座焊盘。'
     '上拉到 3V3_PERIPH 最稳，直接悬空也行，但别往地上碰。')
body('CS 的 10k 上拉不能省。上电瞬间 GPIO17 是悬空的，片选浮空会让卡一上电就锁死在 SPI 模式，之后复位也读不出来，'
     '这是共用总线最常见的故障。')
body('屏幕与 TF 卡共用 SPI2：SCLK 用 GPIO12、MOSI 用 GPIO11、MISO 用 GPIO13，片选各自独立（屏 GPIO10、卡 GPIO17），'
     '两个设备在总线上各自有时钟配置，屏 80MHz、卡 20MHz。固件用一把递归互斥锁串行化全部 SPI 事务，两个设备不会同时抢总线。'
     '真机上卡挂载不稳，先把屏幕时钟降到 40MHz 复测，再查 MISO 上拉和走线长度。')

DOC.add_heading('4.4　扩展 SD 大卡座', level=2)
body('侧面大卡座走 SDMMC 1-bit，只连三根信号线。没插卡不影响开机，插上后挂 /sd_ext 用来导照片和数据。')
cap('表6　扩展 SD 卡座接线')
table(['卡座脚', '接到', '备注'],
      [['CLK', 'GPIO40', '时钟'],
       ['CMD', 'GPIO39', '命令'],
       ['DAT0 / D0', 'GPIO41', '唯一数据线'],
       ['DAT1 / DAT2 / DAT3', '悬空', '1-bit 模式不用'],
       ['CD（卡检测）', '悬空', '固件不做插拔检测'],
       ['VCC', '3V3_PERIPH', 'SD 卡不能接 5V'],
       ['GND', 'GND', '共地']],
      ['center', 'center', 'left'],
      [3.4, 4.0, 8.0])

DOC.add_heading('4.5　摇杆 KY-023', level=2)
cap('表7　摇杆接线')
table(['模块脚', '接到', '备注'],
      [['VCC', '3V3_PERIPH', '必须 3.3V，接 5V 会超 ADC 量程'],
       ['GND', 'GND', '共地'],
       ['VRx', 'GPIO7', 'X 轴，ADC1_CH6'],
       ['VRy', 'GPIO5', 'Y 轴，ADC1_CH4'],
       ['SW', 'GPIO38', '按下对地']],
      ['center', 'center', 'left'],
      [3.4, 4.0, 8.0])

DOC.add_heading('4.6　功放 MAX98357A 与喇叭', level=2)
body('功放是一块多针小板，板上没有喇叭；真正出声的是另外那只两脚喇叭。'
     '喇叭两只脚接板子的 + 和 −，不分正负也能响，但整机里两只喇叭要保持一致。')
cap('表8　功放接线')
table(['模块脚', '接到', '备注'],
      [['VIN', '5V_PERIPH', '经 AO3401 控制的 5V 外围轨'],
       ['GND', 'GND', '共地'],
       ['BCLK', 'GPIO18', '与麦克风 SCK 并联'],
       ['LRC / WS', 'GPIO21', '与麦克风 WS 并联'],
       ['DIN', 'GPIO15', 'I2S 数据'],
       ['SD / SHDN', 'GPIO47', '高电平出声，低电平静音'],
       ['GAIN', '悬空', '默认 9dB；接 GND 升到 12dB'],
       ['喇叭 +', '喇叭一端', '方形纸盆 4Ω 3W'],
       ['喇叭 −', '喇叭另一端', '极性反接不影响出声，保持一致即可']],
      ['center', 'center', 'left'],
      [3.4, 4.0, 8.0])

DOC.add_heading('4.7　麦克风 INMP441', level=2)
body('INMP441 这块板子本身就是麦克风。板子中间那颗带小孔的金属壳就是拾音口，声波从孔进，'
     '不用再外接两脚咪头，也没有接咪头的焊盘。认它很容易：整块板上只有它一个小方块、中间一个小圆孔。')
body('装壳时正对这个孔开一个 2~3mm 的孔，或者开一道 10mm 长的窄缝，孔后面留 2mm 以上的空腔，'
     '别灌胶、别贴防尘棉、别用密封条压死。堵住之后录进来的全是壳内回声和闷响，信噪比会掉一大截。')
body('麦克风与功放共用一个 I2S 端口，时钟和帧同步直接并联，只多一根数据线。')
cap('表9　麦克风接线')
table(['模块脚', '接到', '备注'],
      [['VDD', '3V3_PERIPH', '不要接 5V'],
       ['GND', 'GND', '共地'],
       ['SCK', 'GPIO18', '与功放 BCLK 同一条线'],
       ['WS', 'GPIO21', '与功放 LRC 同一条线'],
       ['SD', 'GPIO16', '麦克风数据'],
       ['L/R', 'GND', '接地选左声道，与固件约定一致；不能悬空']],
      ['center', 'center', 'left'],
      [3.4, 4.0, 8.0])

DOC.add_heading('4.8　键盘扫描器 2× MCP23017', level=2)
body('键盘不占 ESP32 的 GPIO，全部经两颗 I2C 扩展器扫描。两片挂同一条 I2C 总线，靠地址区分：'
     '#1 的 A0、A1、A2 全接地是 0x20，#2 的 A0 接 3V3_PERIPH、A1 与 A2 接地是 0x21。两片的 RESET 都必须接 3V3_PERIPH，'
     '悬空会一直复位、键盘完全无反应。')
cap('表10　MCP23017 公共接线')
table(['信号', 'MCP #1（0x20）', 'MCP #2（0x21）', '说明'],
      [['VDD', '3V3_PERIPH', '3V3_PERIPH', '两颗都从同一轨取'],
       ['VSS', 'GND', 'GND', '共地'],
       ['SDA', 'GPIO1', 'GPIO1', '两片并联同一条线'],
       ['SCL', 'GPIO6', 'GPIO6', '两片并联同一条线'],
       ['RESET', '3V3_PERIPH', '3V3_PERIPH', '必须拉高'],
       ['A0', 'GND', '3V3_PERIPH', '决定 I2C 地址'],
       ['A1 / A2', 'GND', 'GND', '同上'],
       ['INTA / INTB', '悬空', '悬空', '固件用轮询，不用中断']],
      ['center', 'center', 'center', 'left'],
      [2.6, 3.4, 3.4, 5.6])

DOC.add_heading('五、MCP23017 引脚对照（DIP-28）', level=1)
body('下表按芯片封装引脚号排列，照着丝印或缺口标记数脚，不要按功能名猜位置。')
body('贴片封装（SSOP-28，脚距 0.65mm）的脚号顺序和 DIP-28 一模一样：1 脚在缺口或凹点那一侧，逆时针数到 28。'
     '区别只是脚距，必须焊在 SSOP-28 转 DIP-28 的转接板上才能插洞洞板。'
     '贴片没有丝印，认 1 脚只认那个凹点或斜切角——凹点在哪一头，那头的第一个脚就是 1 脚，然后逆时针。')
cap('表11　MCP #1（0x20）引脚')
table(['脚号', '名称', '接到', '用途'],
      [['1', 'GPB0', '键盘列 C0', '列输入'],
       ['2', 'GPB1', '键盘列 C1', '列输入'],
       ['3', 'GPB2', '键盘列 C2', '列输入'],
       ['4', 'GPB3', '键盘列 C3', '列输入'],
       ['5~8', 'GPB4~GPB7', '悬空', '未用'],
       ['9', 'VDD', '3V3_PERIPH', '电源'],
       ['10', 'VSS', 'GND', '地'],
       ['11', 'NC', '悬空', '空脚'],
       ['12', 'SCL', 'GPIO6', '时钟'],
       ['13', 'SDA', 'GPIO1', '数据'],
       ['14', 'NC', '悬空', '空脚'],
       ['15~17', 'A0/A1/A2', 'GND', '地址选择'],
       ['18', 'RESET', '3V3_PERIPH', '必须拉高'],
       ['19', 'INTB', '悬空', '不用中断'],
       ['20', 'INTA', '悬空', '不用中断'],
       ['21', 'GPA0', '键盘行 R0', '行输出'],
       ['22', 'GPA1', '键盘行 R1', '行输出'],
       ['23', 'GPA2', '键盘行 R2', '行输出'],
       ['24', 'GPA3', '键盘行 R3', '行输出'],
       ['25', 'GPA4', '键盘行 R4', '行输出'],
       ['26', 'GPA5', '键盘行 R5', '行输出'],
       ['27~28', 'GPA6/GPA7', '悬空', '未用']],
      ['center', 'center', 'left', 'left'],
      [1.8, 3.2, 4.4, 5.6])
cap('表12　MCP #2（0x21）引脚')
table(['脚号', '名称', '接到', '用途'],
      [['1~8', 'GPB0~GPB7', '悬空', '未用'],
       ['9', 'VDD', '3V3_PERIPH', '电源'],
       ['10', 'VSS', 'GND', '地'],
       ['11', 'NC', '悬空', '空脚'],
       ['12', 'SCL', 'GPIO6', '时钟'],
       ['13', 'SDA', 'GPIO1', '数据'],
       ['14', 'NC', '悬空', '空脚'],
       ['15', 'A0', '3V3_PERIPH', '地址选择，必须接高'],
       ['16~17', 'A1/A2', 'GND', '地址选择'],
       ['18', 'RESET', '3V3_PERIPH', '必须拉高'],
       ['19~20', 'INTB/INTA', '悬空', '不用中断'],
       ['21', 'GPA0', '键盘列 C4', '列输入'],
       ['22', 'GPA1', '键盘列 C5', '列输入'],
       ['23', 'GPA2', '键盘列 C6', '列输入'],
       ['24', 'GPA3', '键盘列 C7', '列输入'],
       ['25', 'GPA4', '键盘列 C8', '列输入'],
       ['26', 'GPA5', '键盘列 C9', '列输入'],
       ['27~28', 'GPA6/GPA7', '悬空', '未用']],
      ['center', 'center', 'left', 'left'],
      [1.8, 3.2, 4.4, 5.6])

DOC.add_heading('六、键盘矩阵 6×10', level=1)
body('矩阵是 6 行 10 列共 60 个键位。每个键的两个脚一头接行线、一头接列线，同一行 10 个键的行脚并联成一条线，'
     '同一列 6 个键的列脚并联成一条线。固件逐行把当前行拉低，再读 10 条列线：读到低电平就是该行该列的键被按下。')
cap('表13　行线定义')
table(['行线', '键位表行', '接到 MCP #1 脚位', '对应芯片引脚号'],
      [['R0', '第 1 行 功能键', 'GPA0', '21'],
       ['R1', '第 2 行 数字', 'GPA1', '22'],
       ['R2', '第 3 行 Q~P', 'GPA2', '23'],
       ['R3', '第 4 行 A~;', 'GPA3', '24'],
       ['R4', '第 5 行 字母+Shift', 'GPA4', '25'],
       ['R5', '第 6 行 控制/方向', 'GPA5', '26']],
      ['center', 'left', 'center', 'center'],
      [2.2, 5.0, 4.4, 4.4])
cap('表14　列线定义')
table(['列线', '接到', '芯片引脚号'],
      [['C0', 'MCP #1 GPB0', '1'],
       ['C1', 'MCP #1 GPB1', '2'],
       ['C2', 'MCP #1 GPB2', '3'],
       ['C3', 'MCP #1 GPB3', '4'],
       ['C4', 'MCP #2 GPA0', '21'],
       ['C5', 'MCP #2 GPA1', '22'],
       ['C6', 'MCP #2 GPA2', '23'],
       ['C7', 'MCP #2 GPA3', '24'],
       ['C8', 'MCP #2 GPA4', '25'],
       ['C9', 'MCP #2 GPA5', '26']],
      ['center', 'center', 'center'],
      [3.0, 7.4, 5.0])
body('焊键帽时按下面的键位表摆放，行列交叉点就是该键所在位置。空白格表示该位不装键。')
KEYS = [['R0', '空', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', '背光', '退格'],
        ['R1', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
        ['R2', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
        ['R3', 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'],
        ['R4', 'Shift', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.'],
        ['R5', 'Caps', 'Ctrl', '音量−', '音量+', '空格', '←', '→', '↓', '↑', '回车']]
cap('表15　键位分布（R0~R5 × C0~C9）')
table(['行\\列', 'C0', 'C1', 'C2', 'C3', 'C4', 'C5', 'C6', 'C7', 'C8', 'C9'],
      KEYS,
      ['center'] + ['center'] * 10,
      [1.6, 1.44, 1.44, 1.44, 1.44, 1.44, 1.44, 1.44, 1.44, 1.44, 1.44])
body('R0 行的 C0 位是空的。侧面电源键不占矩阵位，单独接 GPIO2。')
body('每颗键都串一支 1N4148，方向只有一条规则：黑环那端是阴极，朝行线；另一头是阳极，朝列线。'
     '扫描时行线被 MCP 拉低、列线靠内部上拉保持高电平，电流只能从列经二极管流向行，反方向被挡住，鬼键才防得住。')
body('接反不会烧，但那一整列一个键都读不出来。焊完立刻验方向：万用表打二极管档，红笔接列线、黑笔接行线，'
     '按住这只键应该读出 0.5~0.7V；红黑对调应该不通。读出 0V 或 1 是接反或没焊上。')

DOC.add_heading('6.1　按键四脚判认', level=2)
body('四脚轻触开关内部是两组常通：同组的两只脚不按也响，按下之后两组才接通。'
     '哪一组算行、哪一组算列不写死，按实测结果落线。')
body('厂家的脚号排布不统一：多数是 1↔2 常通、3↔4 常通，也有 1↔4、2↔3 对角常通的。'
     '别按编号认，用万用表蜂鸣档实测每一只：不按的时候响的那一对，就是同一组。')
cap('表16　四脚各落到哪条线')
table(['脚', '不按时', '按下后', '接到'],
      [['组甲 脚A', '与脚B 常通', '与组乙接通', '该键的行线 R'],
       ['组甲 脚B', '与脚A 常通', '与组乙接通', '同上，与脚A 同为行线（可空）'],
       ['组乙 脚C', '与脚D 常通', '与组甲接通', '该键的列线 C'],
       ['组乙 脚D', '与脚C 常通', '与组甲接通', '同上，与脚C 同为列线（可空）']],
      ['center', 'center', 'center', 'left'],
      [2.6, 4.6, 4.0, 6.0])
body('同组的两只脚绝不能被拆开、一只接行一只接列。那样这只键等于被焊成永久按下，'
     '该行该列一路到底全乱，还会把相邻键带出错字。')
body('每只键至少焊两只脚，且必须一只取自组甲、一只取自组乙。四脚全焊也可以，'
     '同组两只并联，机械上更耐振，焊盘也不容易掉。')
body('焊完先别上电：拿蜂鸣档把每只键按一遍——不按不响、按下才响，才算焊对。'
     '某只键不按就响，说明它的两只脚落到了同一组，或者行线列线被锡连了。')
body('线材：0.1mm 漆包线电气上完全够。矩阵扫描时一条行线最多几百微安，十只键一起按也到不了 1mA，'
     '这根线能过 30mA 以上；100mm 长的电阻只有 0.2Ω 量级，压降可以忽略。')
body('只有 0.1mm 也能做完整盘，但要改做法：别让一根线从头拉到尾，改成"汇流条 + 短跳线"。'
     '行线、列线先用洞洞板铜箔、排针或者一根 0.5mm 裸铜丝做成汇流条铺在板面上，'
     '0.1mm 只负责键脚到汇流条这一小段 20~30mm 的跳线。线越短越不容易断，焊盘也不受力。')
body('焊锡比线还粗的时候，别靠锡做结构。先把线头在按键脚上绕一圈到两圈，靠"绕"固定住，锡只负责导通和保护。')
body('线头对折两三次变成双股或四股再镀锡，接触面更大、也不容易一烫就缩。有条件换 0.3mm 细锡丝加尖头烙铁；'
     '粗锡丝堆一大坨只会把线埋进锡球里，断没断都看不出来，还可能把相邻两个脚连锡。')

DOC.add_heading('6.2　逐键点到点表', level=2)
body('按这张表逐只焊：组甲的两只脚并到行线，组乙的两只脚并到列线。'
     '行线去 MCP #1 的 GPA0~GPA5；列线 C0~C3 去 MCP #1 的 GPB0~GPB3，C4~C9 去 MCP #2 的 GPA0~GPA5。')
cap('表17　每个键的四个脚分别接哪里')
rows = []
for row in KEYS:
    for c in range(1, 11):
        name = row[c]
        if name == '空':
            continue
        rows.append([row[0] + 'C' + str(c - 1), name, row[0], 'C' + str(c - 1),
                     '脚A、脚B → ' + row[0], '脚C、脚D → C' + str(c - 1)])
table(['位置', '键', '行线', '列线', '组甲两只脚', '组乙两只脚'],
      rows,
      ['center', 'center', 'center', 'center', 'center', 'center'],
      [2.0, 2.4, 1.6, 1.6, 4.2, 4.2])
body('R0C0 这一格不装键，所以表里是 59 行，其余位置与表15 的键位一一对应。')
body('侧键 ON/OFF 不在表里：它是矩阵外的独立键，只接 GPIO2 和 GND 两路，不接行线、列线，也不串二极管。'
     '它要按下的只是 GPIO2 那一路，键盘扫描跟它无关。', indent=True)

DOC.add_heading('七、施工顺序与上电自检', level=1)
body('先电源与地，再屏幕与摇杆，最后键盘。每一步焊完先量通断，别等整机装完再查。')
cap('表18　上电自检')
table(['步', '动作', '预期结果', '不对时先查'],
      [['1', '接电池或插 USB-C', 'TP4056 指示灯亮，核心板启动', '电池正负极、B+/B− 是否接反'],
       ['2', '等启动完成', '背光亮、桌面出现、不无限重启', 'GPIO8 复位脚、3V3_PERIPH 是否在 3.2~3.4V'],
       ['3', '看系统信息里的电池百分比', '显示与实测电压相符的值', 'GPIO4 分压节点、100nF 是否焊上'],
       ['4', '拨摇杆', '桌面焦点在格子间移动', 'VRx/VRy 是否接反'],
       ['5', '按键盘任意键', '终端出现对应字符', 'MCP 地址、RESET 是否拉高、行列线是否接反'],
       ['6', '按 F1~F7', '进入对应模式', '键位表行列是否对上'],
       ['7', '长按侧键 2 秒', '弹出电源菜单，选关机后屏灭', 'GPIO2 是否接对、另一端是否接地'],
       ['8', '播放音频文件', '喇叭出声', 'GPIO47 是否为高、功放 VIN 是否有 5V'],
       ['9', '拔插 TF 卡', '字库加载正常', 'GPIO17 片选、SPI 三线是否虚焊']],
      ['center', 'left', 'left', 'left'],
      [1.2, 4.2, 4.6, 5.4])

DOC.add_heading('八、代码内需注意的不一致', level=1)
body('以下几处是核对代码时发现、并已改掉的注释错误，以及还需要留意的取舍。')
body('唤醒键接法：drivers_config.hpp 原注释写 ONOFF 接 GPA0 并"双接"到 GPIO2，那是旧说法。'
     '现在的接法是单刀独立键：一端 GPIO2、一端 GND、10k 上拉到 3V3，不进矩阵，R0C0 那一格空着。'
     '按下时 GPIO2 直接读低，短按无动作，长按 2 秒关机，深睡时同一只键拉低唤醒。', indent=True)
body('键位表命名偏移：keyboard_keymap.hpp 里第一行叫 KEYMAP_R1，但它对应的是物理第 1 行（GPA0、本表称 R0），'
     '整个表从 R1 命名到 R6，实际索引是 0 到 5。照名字去接 GPA1 会整体错一行，接线一律以本表的 R0~R5 为准。', indent=True)
body('SPI MISO：原注释写"不需要 MISO 时悬空也安全"，实际 TF 卡必须接 MISO 且要 10k 上拉，注释已改。', indent=True)
body('充电检测脚：battery_adc.cpp 注释原先写 GPIO49，已改为 GPIO42，与常量一致。', indent=True)
body('电池分压阻值：drivers_config.hpp 注释写 100K+100K，battery_adc.cpp 的开机日志写 200K+200K，分压比都是 2。'
     '本表取 100K+100K，静态漏电 21µA；用 200K 更省电，两者都行，别混装。', indent=True)
body('键盘背光：drivers_config.hpp 已删除 KEY_LED_GPIO，memoria_kernel 里 backlight.cpp 与 backlight.hpp 也已删除，'
     'GPIO46 现在没有使用方。系统信息里显示的 backlight 255/255 是屏幕背光（LEDC PWM，走 GPIO14），与键盘灯带无关。', indent=True)

out = r'G:\Memoria OS\Memoria OS 硬件接线表 v1.6.24.docx'
DOC.save(out)
print('SAVED', out)
