# Memoria BASIC 语言手册

> 版本：v1.5（新增：httpup/httpdl 文件收发语句——掌机语音全链路，见 §5.3；随下版固件编译发布）· 解释器源码：`components/memoria_basic/`
> 用途：第三方开发者给 Memoria OS 应用商店写程序时的语法参考。
> **官方风格：关键字全小写**。解释器大小写不敏感（`print` 与 `PRINT` 等价），本手册与官方示例一律用小写。

## 1. 软件本质：一条链看懂

```
BASIC 源码（.bas 文本）
    → 打包成 .msp 安装包（payload = 源码文本）
    → 上架 manifest.json（软件源清单）
    → STORE 商店下载安装到 /mem_fat/scripts/
    → PRGM 模式运行（解释执行）
```

- **程序 = BASIC 源码**。安装包 .msp 只是"信封"：50 字节头（魔数/包名/版本/CRC）+ 源码正文。
- 全部语法能力 = 本手册；硬件能力（屏幕/WiFi/GPIO/串口/蓝牙）由系统注入，见 §5。

## 2. 程序形态

| 规则 | 说明 |
|---|---|
| 行号 | 每行以行号开头：`10 print "hi"`。经典行号 BASIC，行号决定执行顺序 |
| 行号步进 | 编辑器自动 +10（10, 20, 30...），便于中间插行 |
| 多语句同行 | 用 `:` 分隔：`10 x=1 : print x` |
| IF 独占整行 | if 后不能跟 `:` 续其他语句（then/else 体内可以多语句，见 §3） |
| 变量 | 数值型（double）与字符串型（`a$` 经典 $ 变量，**原生存中文**：`let a$ = "你好"`、`print a$` 直接可用，串函数按"字"处理见 §4.4）。变量名大小写不敏感（`a$` 与 `A$` 同一变量，风格用小写） |
| 关键字 | 大小写不敏感；官方风格全小写：`print let input if then else for next while wend goto gosub return end system include` |
| 单行长度 | 上限 96 字节（UTF-8，约 32 个汉字） |
| 文件存放 | TF 卡 `/mem_fat/scripts/*.bas` |

## 3. 语句总表（标准核心）

| 语句 | 写法 | 说明 |
|---|---|---|
| print | `10 print "sum=";s` | 输出。分号 `;` / 逗号 `,` 混合分隔；字符串字面量、串变量与表达式混排 |
| let | `10 let x = 10` / `10 let a$ = "hi"` | 赋值。`let` 前缀必须带边界（`letx` 会被拒）；**let 可省**：裸赋值 `x=5`、`a$="hi"` 合法 |
| input | `10 input x` / `10 input a$` | 读数值：摇杆上/下 ±1、左/右 ±10、按下确认；读字符串：键盘英文录入、Enter 确认 |
| if..then..else | `10 if x>0 then print "p" else print "n"` | 条件分支。**then 后跟纯数字 = goto 该行**；否则按语句执行（可多语句） |
| for..next | `10 for i=1 to 10 step 2` ... `50 next i` | 计数循环。step 可省略（默认 1）。支持嵌套 |
| while..wend | `10 while x<3` ... `30 wend` | 条件循环。支持嵌套 |
| goto | `10 goto 100` | 无条件跳转。**半禁用**：每轮运行提示一次注意事项（易死循环/逻辑混乱） |
| gosub / return | `10 gosub 500` ... `500 return` | 子程序调用/返回（栈式，可嵌套） |
| end | `10 end` | 结束程序 |
| system | `10 system "reboot"` | 系统动作，当前支持 `"reboot"` 重启 |
| include | `10 include "lib.bas"` | 编译期合并另一文件的全部行（同号覆盖）。适合公共库 |

## 4. 表达式与内置函数

### 4.1 运算符

| 类别 | 运算符 |
|---|---|
| 算术 | `+` `-` `*` `/` |
| 比较 | `=` `<>` `<` `>` `<=` `>=` |
| 逻辑 | 无专用关键字——用算术组合：`if (a>0)*(b>0) then`（非 0 即真） |

### 4.2 条件真值

条件表达式**非 0 即真**（数值条件）。`if x then` 合法。

字符串比较：任一侧是串变量/串字面量即按字符串比较（`if a$="hi" then`；`> < >= <=` 按字节序）。

### 4.3 数学函数

| 函数 | 说明 |
|---|---|
| `sin(x)` `cos(x)` `tan(x)` | 三角（弧度） |
| `abs(x)` | 绝对值 |
| `sqr(x)` | 平方根 |
| `int(x)` | 取整（截断） |
| `log(x)` `log10(x)` | 自然对数 / 常用对数 |
| `exp(x)` | e^x |
| `pow(a,b)` | a 的 b 次幂（必须两参数） |
| `rnd()` | 随机数，0 ~ 1 |

### 4.4 字符串函数

| 函数 | 说明 |
|---|---|
| `str$(x)` | 数值 → 文本（如 `str$(12.5)` → `"12.5"`） |
| `val(s$)` | 文本 → 数值（非数字文本返回 0） |
| `len(s$)` | 按**字符数**计数（UTF-8 感知：`len("你好")` = 2，纯英文 = 字节数，老程序零差异） |
| `left$(s$, n)` | 左 n 个**字符**（按字符边界切，绝不切断汉字） |
| `right$(s$, n)` | 右 n 个**字符** |
| `mid$(s$, start, len)` | 取子串，start/len 均按**字符**计，start 从 1 计 |
| `chr$(n)` | 0~255 数值 → 单字节字符 |

**中文即字符串**：不做独立汉字类型——`let a$ = "你好"`、`input a$`（自动调起输入法）、`print a$`、串函数按字处理，与英文写法完全一致（内部 UTF-8 存储，TF 卡/HTTP 链路零转换）。老程序只含英文时字符=字节，行为不变。

字符串运算：`+` 拼接；`= <>` 比较。**字符串与数字不混算**——拼接前先 `str$(x)`。

## 5. 硬件扩展 API（系统注入，掌机专属）

### 5.1 屏幕绘图语句

| 语句 | 写法 | 说明 |
|---|---|---|
| color | `10 color 255,0,0` | 设置前景色（R,G,B 各 0~255） |
| clear | `10 clear` | 清屏（黑） |
| text | `10 text 10,20,"hello"` | 在 (x,y) 画文本（当前色） |
| pixel | `10 pixel 5,5` | 画 1 像素点 |
| line | `10 line 0,0,100,50` | 画线段（Bresenham） |
| rect | `10 rect 10,10,50,30` | 画矩形边框（宽 w 高 h） |
| fill | `10 fill 10,10,50,30` | 画实心矩形 |
| circle | `10 circle 120,80,40` | 画圆（中点算法） |

屏幕分辨率 320×240，左上角为原点。

### 5.2 连接与系统

**网络是系统级的**：WiFi / AP 热点 / 蓝牙统一在系统设置里管理（设置中提供像手机一样的 WiFi 扫描列表点选连接、AP 热点名称密码自定义、蓝牙开关），程序不允许（也不需要）自己切网。系统连网后自动把当前网络信息预注入为 BASIC 全局变量：

| 预注入变量 | 说明 |
|---|---|
| `wifi_ssid$` / `wifi_pass$` | 系统当前连接的 WiFi（系统设置中扫描选择） |
| `ap_ssid$` / `ap_pass$` | 系统开出的 AP 热点（系统设置中自定义名称密码） |
| `ble_name$` | 蓝牙名称（系统设置中配置） |

- 变量名大小写不敏感，程序直接读取即可；值随系统连接状态自动更新。
- `wifistat()` 返回系统网络状态，程序用它判断在线与否。

| 语句/函数 | 写法 | 说明 |
|---|---|---|
| wifi | `10 wifi "ssid","pass"` | **保留语句（兼容旧程序），联网程序禁止调用**——系统统一管网，调用会被忽略并提示 |
| wifistat() | `10 x = wifistat()` | 返回 1（系统已连网）/ 0 |
| gpioin() | `10 x = gpioin(4)` | 读 GPIO 电平（0/1），参数为引脚号 |
| uart | `10 uart "text"` | 输出到串口控制台（USB 调试口可见） |
| ble | `10 ble "text"` | 蓝牙透传发送（需上位机已连接） |
| system | `10 system "reboot"` | 重启 |

### 5.3 网络与语音（联网应用）

| 语句/函数 | 写法 | 说明 |
|---|---|---|
| httpget | `10 httpget "http://a/p", r$` | GET 请求，响应体存入 r$（上限 8KB） |
| httppost | `10 httppost "http://a/u", b$, r$` | POST 请求，b$ 原样字节上传（octet-stream），响应体存 r$ |
| httpup | `10 httpup u$, "/mem_fat/v.wav", r$` | POST 文件字节上传（≤2MB octet-stream），响应体存 r$（第三参可省） |
| httpdl | `10 httpdl u$, "/mem_fat/v.wav"` | GET 下载直写文件（流式不占内存），配 httpstat() 判定 |
| httpstat() | `10 s = httpstat()` | 上一次 HTTP 状态码（0 = 未请求/连接失败） |
| record | `10 record "/mem_fat/v1.wav", 5` | 麦克录音到 WAV（16kHz 单声道 16bit），阻塞 1~60 秒 |
| play | `10 play "/mem_fat/v1.wav"` | 播放 WAV/MP3（后台播放，不阻塞） |

HTTP 目前仅明文 http://（HTTPS 后补）。

## 6. 保护机制

| 机制 | 说明 |
|---|---|
| 步数上限 | 默认 100000 步，超限强制终止（防死循环"杀线程"），输出"超时保护" |
| 跳转语义 | goto/gosub/return/循环跳转后不再自动前进一行，目标行真正被执行 |
| 包校验 | .msp 安装时 CRC32 校验，损坏/被篡改的包拒绝落盘运行 |

## 7. 最小示例

```basic
10 color 0, 255, 0
20 clear
30 for i = 1 to 5
40   text 10, i*20, "memoria os"
50 next i
60 let s = 0
70 for i = 1 to 100
80   let s = s + i
90 next i
100 print "sum="; s
110 if wifistat() then print "wifi ok" else print "wifi off"
120 end
```

联网 + 语音示例（网络由系统管理，程序开机即在线）：

```basic
10 color 0, 255, 0
20 clear
30 if wifistat() = 0 then print "wifi 未连,去系统设置连网" : end
40 print "当前WiFi: "; wifi_ssid$
50 httpget "http://host/api/hello", r$
60 if httpstat() = 200 then print r$ else print "http 错误 "; httpstat()
70 record "/mem_fat/v1.wav", 3
80 play "/mem_fat/v1.wav"
90 end
```

## 8. 已知边界（写程序时避开）

- 字符串与数字不混算：数字转文本用 `str$(x)`，文本转数字用 `val(s$)`。
- **没有数组**、没有自定义函数（def fn）、没有 rem 注释语句。
- `input a$` 键盘录入：中文输入法随固件批次上线（规格见 `docs/IME-SPEC.md`，input 自动调起拼音面板），上线前仅英文键盘。
- HTTP 仅明文 http://，响应体上限 8KB；record 单段 1~60 秒且阻塞程序运行。
- WiFi / AP / 蓝牙由系统统一管理（在系统设置里像手机一样连），程序读预注入变量（wifi_ssid$ / ap_ssid$ / ble_name$ 等）获取网络信息，禁止调用 wifi 语句。
- 幂运算用 `pow(a,b)`，不支持 `^` 运算符。
- goto 建议只用于大骨架跳转，逻辑组织优先用 for/while/gosub。

---
*本文档随解释器演进更新；语法以 `components/memoria_basic/` 实现为准。*
