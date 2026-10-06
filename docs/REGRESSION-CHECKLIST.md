# Memoria BASIC 语法批次回归清单

> 目的：GLM 语法批次（A/B/C/D：rem/mod/and/or/not/rnd 范围/data-read/dim 数组/文件流/块 if/elseif/def fn/HTTP 1MB）落地后，全量回归 .bas 旧程序，确保"老 BASIC 语法不减"。
> 执行人：豆包-2（真机烧录 + 逐例跑）· 模拟器同步：豆包-1 · 批次交付：GLM
> 覆盖对象：**Memoria BASIC（.bas）**；`.ms`（memoria-script）是另一解释器，不受本批次影响。

## 0. 回归前置

- [ ] GLM 报语法批次落地（桥上报出 bin SHA + 语法清单确认）
- [ ] 烧录新固件到测试板（COM5）
- [ ] 每例跑 3 次：新语法程序 + 旧语法程序各一次，旧的不许炸

## 1. 教材核心示例（从 BASIC-BOOK.md 抽取）

| # | 覆盖语法 | 最小测试程序 | 预期 |
|---|---|---|---|
| T1 | print / 变量 / 算术 | `10 x = 3 : y = 4 : print x + y * 2 : end` | 打印 `11` |
| T2 | input x（摇杆） | 教材 4.1 门牌示例 | 摇杆输入生效 |
| T3 | input a$（含中文） | `10 input a$ : print "hi, "; a$ : end` | 键盘输入，中文拼音上屏，回车回显 |
| T4 | if / else / 多行 if | `10 if 5 > 3 then print "a" : else print "b" : end` | 打印 `a` |
| T5 | elseif 链 | 三分支示例 | 命中正确分支 |
| T6 | for / next | `10 for i = 1 to 5 : print i; : next i : end` | `1 2 3 4 5` |
| T7 | while / wend + 超时骨架 | 教材 7.5 | 等 wifistat 变 1 或超时退出 |
| T8 | len/left$/mid$（按字符） | `10 print len("好abc") : print mid$("你好世界", 2, 2) : end` | `4` / `好世`（不切半字） |
| T9 | val / str$ | `10 print val("42") + 1 : print str$(7) + "x" : end` | `43` / `7x` |
| T10 | 绘图 text/rect/circle | 教材 9 章弹跳球 | 画面正常，无异常退出 |
| T11 | gosub / return | `10 gosub 100 : end : 100 print "sub" : return` | 打印 `sub` |
| T12 | include | 两文件合并示例 | 合并运行，同号覆盖规则生效 |

## 2. 新语法专项（本批次新增，正向验证）

| # | 新语法 | 最小测试 | 预期 |
|---|---|---|---|
| N1 | rem 注释 | `10 rem 这是注释 : print "ok" : end` | 注释被忽略，打印 `ok` |
| N2 | mod | `10 print 17 mod 5 : end` | `2` |
| N3 | and / or / not | `10 if 1 = 1 and 2 = 2 then print "y" : end` | `y` |
| N4 | rnd 范围 | `10 print rnd(1, 6) : end` | 1~6 整数（多跑几次都在范围内） |
| N5 | data / read | data 表 + read 逐行读 | 数据按序读出 |
| N6 | dim 数组 | `dim a(10)` + 循环填读 | 赋值/读取正确 |
| N7 | 文件流 | 写文件 → 读回 | 内容一致 |
| N8 | def fn | `def fn double(x) = x * 2` + 调用 | 结果正确 |
| N9 | HTTP 大响应 | httpget 拉 >8KB 接口 | r$ 完整收到 1MB 内响应 |

## 3. 真机既有程序回归

| # | 程序 | 状态 | 预期 |
|---|---|---|---|
| R1 | `sdcard/scripts/chat.bas`（v1 旧客户端） | 待 GUI 替换 | 语法批次后仍可运行（走旧 GET 协议） |
| R2 | 商店安装的 .bas 程序 | 用户确认装了哪些 | 全部可运行 |
| R3 | 用户自写 .bas | 用户提供 | 可运行 |

## 4. 重点风险点（回归时优先盯）

1. **wifi 语句语义**：系统管网下是"保留兼容"，旧程序里残留 `wifi "ssid","pass"` 不许变成报错（教材已改，但旧 .bas 可能有）
2. **if 多行化**：新块 if 落地后，旧的单行 `if ... then ... else ...` 必须原语义不变（**else 同行优先**）
3. **len/切片按字符**：旧程序按字节切 3 的倍数的地方，行为变化是否可接受（结果变"正确"但可能和旧输出不同——需要用户确认口径）
4. **rnd() 无参**：必须仍返回 0~1（向后兼容），只有带参才走范围模式
5. **行长 96 字节**：新语法（rem/mod/def）别让示例超行长

## 5. 交付标准

- [ ] T1~T12 全绿（模拟器 + 真机）
- [ ] N1~N9 全绿（真机）
- [ ] R1~R3 无回归破坏
- [ ] 教材附录 D/F 同步更新（我负责，随批次落地走）

---

*清单随语法批次交付更新；跑挂一项即报 GLM 修，不静默通过。*
