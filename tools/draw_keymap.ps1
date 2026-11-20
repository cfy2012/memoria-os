Add-Type -AssemblyName System.Drawing

$W = 1700
$H = 1480
$bmp = [System.Drawing.Bitmap]::new($W, $H)
$bmp.SetResolution(72, 72)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::White)

$penG = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 120, 0), [float]4)
$penB = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 70, 170), [float]4)
$penK = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, [float]3)
$penT = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, [float]2)
$penD = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(150, 150, 150), [float]2)
$penD.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash
$penGray = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(170, 170, 170), [float]2)
$penRed = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(200, 40, 40), [float]2)

$brDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(70, 70, 70))
$brLight = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(242, 242, 242))
$brRedLight = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(253, 236, 236))

function L($pen, $x1, $y1, $x2, $y2) {
    $g.DrawLine($pen, [float]$x1, [float]$y1, [float]$x2, [float]$y2)
}

function T($text, $x, $y, $size, $r, $gr, $b, $bold = $false, $center = $false) {
    $style = [System.Drawing.FontStyle]::Regular
    if ($bold) { $style = [System.Drawing.FontStyle]::Bold }
    $font = [System.Drawing.Font]::new('Microsoft YaHei', [float]$size, $style)
    $brush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb($r, $gr, $b))
    if ($center) {
        $sz = $g.MeasureString($text, $font)
        $x = $x - $sz.Width / 2
    }
    $g.DrawString($text, $font, $brush, [float]$x, [float]$y)
    $font.Dispose()
    $brush.Dispose()
}

# ---------- 标题 ----------
T '键盘矩阵接线示意' 850 24 46 0 0 0 $true $true
T '60 只键 · 每只串一只 1N4148 · 横线一次只拉低一条，竖线读哪条被拉低' 850 88 20 110 110 110 $false $true

# ---------- 上半：整块矩阵 ----------
T '6 条横线 R0~R5（输出） × 10 条竖线 C0~C9（上拉输入）· 每个交叉点装一只键' 850 132 20 60 60 60 $false $true

$x0 = 300
$dx = 118
$y0 = 228
$dy = 78

for ($i = 0; $i -lt 10; $i++) {
    $x = $x0 + $i * $dx
    L $penB $x $y0 $x ($y0 + 5 * $dy)
    T ('C' + $i) $x 184 19 0 70 170 $false $true
}
for ($j = 0; $j -lt 6; $j++) {
    $y = $y0 + $j * $dy
    L $penG 300 $y 1362 $y
    T ('R' + $j) 208 ($y - 12) 19 0 120 0 $false $false
}

for ($j = 0; $j -lt 6; $j++) {
    for ($i = 0; $i -lt 10; $i++) {
        $cx = $x0 + $i * $dx
        $cy = $y0 + $j * $dy
        $g.FillEllipse($brDark, [float]($cx - 5), [float]($cy - 5), [float]10, [float]10)
    }
}

T 'F1' 430 232 14 100 100 100 $false $false
T '1' 312 310 14 100 100 100 $false $false
T 'q' 312 388 14 100 100 100 $false $false

T '交叉点 = 一只按键。行线一行一行往下扫，扫到哪一行，就看这一行哪条竖线被拉低。' 850 628 18 100 100 100 $false $true

# ---------- 分隔 ----------
L $penGray 120 668 1580 668

# ---------- 下半：单键放大 ----------
T '一只键怎么接（放大）' 130 684 30 0 0 0 $true $false
T '第一对：两只脚在按键内部相通，挑一只直接接横线' 140 728 21 0 110 0 $false $false
T '第二对：两只脚也相通，挑一只接二极管负极，二极管正极再接竖线' 140 762 21 20 20 20 $false $false
T '3. 每只键只焊两个点，另外两只脚空着不接；别把同一对的两只脚并成一块。脚位只是示意，哪两只同组以万用表量为准。' 140 796 18 120 120 120 $false $false

$yc = 960

# 横线（绿）：只接第一对里的一只脚
L $penG 200 890 600 890
T '← 到横线 R' 128 898 21 0 120 0 $false $false

# 按键本体
$g.FillRectangle($brLight, [float]620, [float]840, [float]360, [float]220)
$g.DrawRectangle($penK, [float]620, [float]840, [float]360, [float]220)
T '按键' 800 935 26 90 90 90 $false $true

# 内部常通虚线
L $penD 668 890 668 1030
L $penD 932 890 932 1030

# 四只脚：只用 1 脚和 3 脚，2、4 脚空着
$g.FillEllipse($brDark, [float]590, [float]880, [float]20, [float]20)
$g.DrawEllipse($penD, [float]591, [float]1021, [float]18, [float]18)
$g.FillEllipse($brDark, [float]990, [float]880, [float]20, [float]20)
$g.DrawEllipse($penD, [float]991, [float]1021, [float]18, [float]18)
T '空' 570 1036 17 150 150 150 $false $false
T '空' 1014 1036 17 150 150 150 $false $false
L $penK 600 890 620 890
L $penK 1000 890 980 890

T '第一对' 285 840 24 0 110 0 $true $false
T '第二对' 995 1062 22 20 20 20 $true $false

# 右侧汇合：只走 3 脚
L $penK 1000 890 1075 890
L $penT 1075 890 1075 960
L $penT 1075 960 1140 960

# 二极管：阴极竖杠在左（靠按键），阳极三角底边在右（靠竖线）
L $penK 1150 872 1150 1008
$pts = [System.Drawing.PointF[]]::new(3)
$pts[0] = [System.Drawing.PointF]::new([float]1150, [float]$yc)
$pts[1] = [System.Drawing.PointF]::new([float]1222, [float]872)
$pts[2] = [System.Drawing.PointF]::new([float]1222, [float]1008)
$g.FillPolygon($brDark, $pts)

T '负极（黑环）' 1036 818 18 190 40 40 $true $false
T '正极' 1188 1017 18 60 60 60 $false $false

# 到竖线
L $penB 1222 $yc 1400 $yc
L $penB 1400 762 1400 1108
T '竖线 C' 1368 722 21 0 70 170 $false $false

T '按下的时候，两对才接通，行和列连上' 800 1078 21 60 60 60 $false $true

# ---------- 错误提示 ----------
$g.FillRectangle($brRedLight, [float]120, [float]1140, [float]1460, [float]100)
$g.DrawRectangle($penRed, [float]120, [float]1140, [float]1460, [float]100)
T '×  不能把同一对的两只脚拆开：一只接横线、一只接竖线' 148 1155 22 190 40 40 $true $false
T '那样这只键等于永远按着不放，整行整列全乱，相邻的键还会跟着出错字' 148 1190 19 190 40 40 $false $false

# ---------- 底部三条 ----------
L $penGray 120 1262 1580 1262
T '1. 整块键盘就是 6 条横线 × 10 条竖线，每个交叉点摆一只键：第一对压在横线上，第二对经二极管挂到竖线上。' 130 1276 19 20 20 20 $false $false
T '2. 二极管方向：带黑环的一端（负极）朝按键，另一端（正极）朝竖线。焊反了，这一列按下去一个字符都读不出来。' 130 1318 19 20 20 20 $false $false
T '3. 上电前用蜂鸣档逐只量：不按的时候不响，按下去才响。不按就响的，是同一对被拆开了或者被锡连了。' 130 1360 19 20 20 20 $false $false

# ---------- 图例 ----------
L $penG 130 1440 210 1440
T '横线 R（行线）' 222 1426 19 0 120 0 $false $false
L $penB 490 1440 570 1440
T '竖线 C（列线）' 582 1426 19 0 70 170 $false $false
$g.FillEllipse($brDark, [float]752, [float]1430, [float]17, [float]17)
T '已接线的脚' 780 1426 19 60 60 60 $false $false
$g.DrawEllipse($penD, [float]905, [float]1431, [float]16, [float]16)
T '空着不接的脚' 930 1426 19 150 150 150 $false $false

$bmp.Save('G:\Memoria OS\键盘接线示意图.png', [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output 'OK'
