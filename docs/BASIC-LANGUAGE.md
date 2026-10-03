# Memoria BASIC 语言手册

> 版本：v1.0（对齐固件 bin 0x1DD130）· 解释器源码：`components/memoria_basic/`
> 用途：第三方开发者给 Memoria OS 应用商店写程序时的语法参考。

## 1. 软件本质：一条链看懂

```
BASIC 源码（.bas 文本）
    → 打包成 .msp 安装包（payload = 源码文本）
    → 上架 manifest.json（软件源清单）
    → STORE 商店下载安装到 /mem_fat/scripts/
    → PRGM 模式运行（解释执行）
```

- **程序 = BASIC 源码**。安装包 .msp 只是"信封"：50 字节头（魔数/包名/版本/CRC）+ 源码正文。
- 全部语法能力 = 本手册；硬件能力（屏幕/WiFi/GPIO/串口/蓝牙）由系统注入，见 §6。

## 2. 程序形态

| 规则 | 说明 |
|---|---|
| 行号 | 每行以行号开头：`10 PRINT "HI"`。经典行号 BASIC，行号决定执行顺序 |
| 行号步进 | 编辑器自动 +10（10, 20, 30...），便于中间插行 |
| 多语句同行 | 用 `:` 分隔：`10 X=1 : PRINT X` |
| IF 独占整行 | IF 后不能跟 `:` 续其他语句（THEN/ELSE 体内可以多语句，见 §3） |
| 变量 | 只有数值型（double）。变量名大小写不敏感（`ABC` 与 `abc` 同一变量） |
| 单行长度 | 上限 96 字节（UTF-8，约 32 个汉字） |
| 文件存放 | TF 卡 `/mem_fat/scripts/*.bas` |

## 3. 语句总表（标准核心）

| 语句 | 写法 | 说明 |
|---|---|---|
| PRINT | `PRINT "SUM=";S` | 输出。分号 `;` / 逗号 `,` 混合分隔；字符串字面量与表达式混排 |
| LET | `LET X = 10` | 赋值。`LET` 前缀必须带边界（`LETX` 会被拒） |
| INPUT | `INPUT X` | 读一个数值。真机上弹输入框：摇杆上/下 ±1、左/右 ±10、按下确认 |
| IF..THEN..ELSE | `IF X>0 THEN PRINT "P" ELSE PRINT "N"` | 条件分支。**THEN 后跟纯数字 = GOTO 该行**；否则按语句执行（可多语句） |
| FOR..NEXT | `FOR I=1 TO 10 STEP 2` ... `NEXT I` | 计数循环。STEP 可省略（默认 1）。支持嵌套 |
| WHILE..WEND | `WHILE X<3` ... `WEND` | 条件循环。支持嵌套 |
| GOTO | `GOTO 100` | 无条件跳转。**半禁用**：每轮运行提示一次注意事项（易死循环/逻辑混乱） |
| GOSUB / RETURN | `GOSUB 500` ... `RETURN` | 子程序调用/返回（栈式，可嵌套） |
| END | `END` | 结束程序 |
| SYSTEM | `SYSTEM "REBOOT"` | 系统动作，当前支持 `"REBOOT"` 重启 |
| INCLUDE | `INCLUDE "lib.bas"` | 编译期合并另一文件的全部行（同号覆盖）。适合公共库 |

## 4. 表达式与内置函数

### 4.1 运算符

| 类别 | 运算符 |
|---|---|
| 算术 | `+` `-` `*` `/` |
| 比较 | `=` `<>` `<` `>` `<=` `>=` |
| 逻辑 | 无专用关键字——用算术组合：`IF (A>0)*(B>0) THEN`（非 0 即真） |

### 4.2 条件真值

条件表达式**非 0 即真**（数值条件）。`IF X THEN` 合法。

### 4.3 数学函数

| 函数 | 说明 |
|---|---|
| `SIN(x)` `COS(x)` `TAN(x)` | 三角（弧度） |
| `ABS(x)` | 绝对值 |
| `SQR(x)` | 平方根 |
| `INT(x)` | 取整（截断） |
| `LOG(x)` `LOG10(x)` | 自然对数 / 常用对数 |
| `EXP(x)` | e^x |
| `POW(a,b)` | a 的 b 次幂（必须两参数） |
| `RND()` | 随机数，0 ~ 1 |

## 5. 硬件扩展 API（系统注入，掌机专属）

### 5.1 屏幕绘图语句

| 语句 | 写法 | 说明 |
|---|---|---|
| COLOR | `COLOR 255,0,0` | 设置前景色（R,G,B 各 0~255） |
| CLEAR | `CLEAR` | 清屏（黑） |
| TEXT | `TEXT 10,20,"HELLO"` | 在 (x,y) 画文本（当前色） |
| PIXEL | `PIXEL 5,5` | 画 1 像素点 |
| LINE | `LINE 0,0,100,50` | 画线段（Bresenham） |
| RECT | `RECT 10,10,50,30` | 画矩形边框（宽 w 高 h） |
| FILL | `FILL 10,10,50,30` | 画实心矩形 |
| CIRCLE | `CIRCLE 120,80,40` | 画圆（中点算法） |

屏幕分辨率 320×240，左上角为原点。

### 5.2 连接与系统

| 语句/函数 | 写法 | 说明 |
|---|---|---|
| WIFI | `WIFI "ssid","pass"` | 连接 WiFi |
| WIFISTAT() | `X = WIFISTAT()` | 返回 1（已连）/ 0 |
| GPIOIN | `X = GPIOIN(4)` | 读 GPIO 电平（0/1），参数为引脚号 |
| UART | `UART "text"` | 输出到串口控制台（USB 调试口可见） |
| BLE | `BLE "text"` | 蓝牙透传发送（需上位机已连接） |
| SYSTEM | `SYSTEM "REBOOT"` | 重启 |

## 6. 保护机制

| 机制 | 说明 |
|---|---|
| 步数上限 | 默认 100000 步，超限强制终止（防死循环"杀线程"），输出"超时保护" |
| 跳转语义 | GOTO/GOSUB/RETURN/循环跳转后不再自动前进一行，目标行真正被执行 |
| 包校验 | .msp 安装时 CRC32 校验，损坏/被篡改的包拒绝落盘运行 |

## 7. 最小示例

```basic
10 COLOR 0, 255, 0
20 CLEAR
30 FOR I = 1 TO 5
40   TEXT 10, I*20, "MEMORIA OS"
50 NEXT I
60 LET S = 0
70 FOR I = 1 TO 100
80   LET S = S + I
90 NEXT I
100 PRINT "SUM="; S
110 IF WIFISTAT() THEN PRINT "WiFi OK" ELSE PRINT "WiFi OFF"
120 END
```

## 8. 已知边界（写程序时避开）

- **没有字符串变量**：字符串只能作为字面量直接传给 PRINT/TEXT/UART 等（不能 `LET A="HI"`）。
- **没有数组**、没有自定义函数（DEF FN）、没有 REM 注释语句。
- INPUT 只能读**数值**（摇杆调值输入）。
- 幂运算用 `POW(a,b)`，不支持 `^` 运算符。
- GOTO 建议只用于大骨架跳转，逻辑组织优先用 FOR/WHILE/GOSUB。

---
*本文档随解释器演进更新；语法以 `components/memoria_basic/` 实现为准。*
