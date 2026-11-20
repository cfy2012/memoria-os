Add-Type -AssemblyName System.Drawing

$W = 2700
$H = 1880
$bmp = [System.Drawing.Bitmap]::new($W, $H)
$bmp.SetResolution(72, 72)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
$g.Clear([System.Drawing.Color]::White)

$penK = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, [float]2)
$penKB = [System.Drawing.Pen]::new([System.Drawing.Color]::Black, [float]4)
$penGy = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(150, 150, 150), [float]2)
$penG = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(0, 130, 0), [float]3)
$penDash = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(170, 170, 170), [float]2)
$penDash.DashStyle = [System.Drawing.Drawing2D.DashStyle]::Dash

$brDark = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(30, 30, 30))

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

T '键盘矩阵 · 实物排布图' 1350 30 44 0 0 0 $true $true
T '60 只键，每只配一只 1N4148。格子按 R0~R5 从上到下、C0~C9 从左到右排，和表 3 一一对应' 1350 92 22 110 110 110 $false $true

$x0 = 150
$y0 = 240
$Wg = 250
$Hg = 225

for ($i = 0; $i -lt 10; $i++) {
    $cx = $x0 + $i * $Wg + 125
    T ('C' + $i) $cx 182 24 0 70 175 $false $true
}

T '第一对 = 1、2 脚，接 R 行线' 96 118 17 0 130 0 $false $false
T '第二对 = 3、4 脚，接二极管' 96 144 17 40 40 40 $false $false

$KEYS = @(
    @('空', 'F1', 'F2', 'F3', 'F4', 'F5', 'F6', 'F7', '背光', '退格'),
    @('1', '2', '3', '4', '5', '6', '7', '8', '9', '0'),
    @('q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'),
    @('a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';'),
    @('Shift', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.'),
    @('Caps', 'Ctrl', '音量-', '音量+', '空格', '←', '→', '↓', '↑', '回车')
)

for ($j = 0; $j -lt 6; $j++) {
    $gy = $y0 + $j * $Hg
    T ('R' + $j) 88 ($gy + 70) 24 0 130 0 $false $false

    for ($i = 0; $i -lt 10; $i++) {
        $gx = $x0 + $i * $Wg
        $name = $KEYS[$j][$i]

        if ($name -eq '空') {
            $g.DrawRectangle($penDash, [float]($gx + 30), [float]($gy + 40), [float]84, [float]96)
            T '不装键' ($gx + 125) ($gy + 150) 17 175 175 175 $false $true
            continue
        }

        # 按键本体
        $g.DrawRectangle($penK, [float]($gx + 30), [float]($gy + 40), [float]84, [float]96)

        # 四只脚：只用 1 脚接行线、3 脚接二极管；2、4 脚空着不焊
        L $penG ($gx + 30) ($gy + 58) ($gx + 8) ($gy + 58)
        L $penK ($gx + 114) ($gy + 58) ($gx + 152) ($gy + 58)

        $g.DrawEllipse($penGy, [float]($gx + 16), [float]($gy + 113), [float]10, [float]10)
        $g.DrawEllipse($penGy, [float]($gx + 120), [float]($gy + 113), [float]10, [float]10)

        T '1' ($gx + 8) ($gy + 32) 18 0 0 0 $false $false
        T '2' ($gx + 8) ($gy + 96) 18 165 165 165 $false $false
        T '3' ($gx + 118) ($gy + 32) 18 0 0 0 $false $false
        T '4' ($gx + 118) ($gy + 96) 18 160 160 160 $false $false

        # 二极管：阴极竖杠在左（朝按键），阳极三角在右
        L $penKB ($gx + 152) ($gy + 34) ($gx + 152) ($gy + 82)
        $pts = [System.Drawing.PointF[]]::new(3)
        $pts[0] = [System.Drawing.PointF]::new([float]($gx + 158), [float]($gy + 58))
        $pts[1] = [System.Drawing.PointF]::new([float]($gx + 196), [float]($gy + 34))
        $pts[2] = [System.Drawing.PointF]::new([float]($gx + 196), [float]($gy + 82))
        $g.FillPolygon($brDark, $pts)
        L $penK ($gx + 196) ($gy + 58) ($gx + 228) ($gy + 58)

        T '负' ($gx + 138) ($gy + 6) 17 190 40 40 $false $false
        T '正' ($gx + 182) ($gy + 6) 17 40 40 40 $false $false

        T $name ($gx + 125) ($gy + 150) 17 90 90 90 $false $true
    }
}

L $penGy 150 1620 2550 1620
T '1. 每格 = 一只轻触按键 + 一只 1N4148。1、2 脚是一对，3、4 脚是另一对；不按下时两对各自常通。' 160 1634 21 30 30 30 $false $false
T '2. 每只键只焊两个点：1 脚接 R 行线（图上绿线），3 脚接二极管负极；2、4 脚空着，不接线。' 160 1678 21 30 30 30 $false $false
T '3. 二极管带黑环的一端是负极，必须朝按键，正极接 C 列线。焊反了，这一整列按下去一个字符都读不出来。' 160 1722 21 190 40 40 $false $false
T '4. 别把同一对的两只脚用锡连成一块：一只键只留两个焊点，2 与 1、4 与 3 都不要并。' 160 1766 21 190 40 40 $false $false
T '5. 左边那截绿线连本行的 R 线，二极管右端连本列的 C 线。R0C0 是空位，这只位置不装键。' 160 1810 21 30 30 30 $false $false

$bmp.Save('G:\Memoria OS\键盘矩阵-实物排布图.png', [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output 'OK'
