# Memoria BASIC 扩展层设计：结构体、对象与容器（BASIC-OOP）

> 版本：v0.1 草案 · 2026-10-06 · 作者：豆包-2
> 定位：**独立扩展层**。主解释器（`components/memoria_basic/`）保持现状；本设计是"编译期翻译层 + 少量运行时"，不向主语言引入 C++ 语义负担。
> 前置：主语法清单（mod / and-or-not / rnd 范围 / rem / dim / data-read / 文件流 / 块 if / elseif / def fn）由 GLM 按桥单实施；本层待其落地后开工。

---

## 0. 设计原则（先说死）

1. **全部向后兼容**：老程序一行不改照样跑。扩展层是"加"，不是"改"。
2. **命名守 BASIC 血脉**：不搬 C++ 关键字。用户点名的 `struct` / `class` 保留原名；容器功能用动词/下标表达（`push` / `m$(k$)`），不叫 vector/map/set。
3. **解释器零类概念**：对象的"类"只在加载期翻译，运行时全是普通变量 + 子程序。没有 new/delete、没有指针、没有引用传递。
4. **对象全局共享**：跟现有变量一致（BASIC 变量本就全局），不做作用域。
5. **省内存优先**：所有结构 = 变量表的键组织，不搞内存布局、不搞虚表。

---

## 1. struct（结构体）

### 1.1 核心机制：扁平字段名

`p.x` **整个作为单个变量名**存入变量表。词法层识别 `标识符(.标识符)+`（每段可带 `$` 后缀），其余解析、求值、执行零改动。

```
10 p.x = 10            ' 数字字段
20 p.y = 20
30 p.name$ = "旺财"    ' 字符串字段（$ 后缀规则不变）
40 print p.x + p.y
```

### 1.2 可选声明（纯可读性，不强制）

```
10 struct p.x, p.y, p.name$
```

- 声明不分配任何东西，只是"告诉读代码的人这是个结构"。
- 不声明直接用也合法（BASIC 本来就是隐式变量）。

### 1.3 能力与边界

| 能力 | 说明 |
|---|---|
| 任意深度嵌套 | `a.b.c` = 一个变量名，字段名可以继续加点 |
| 字符串字段 | `p.name$`，`$` 在最后一段 |
| 数组字段 | `p.list(5)`，字段名本身是数组（配合 §4 vector） |
| 无 | 没有指针、没有引用传参、没有字段级访问控制（public/private 都是概念外的事） |

### 1.4 成本

词法层 + 变量名规则，半天量级。**这是全扩展层的地基**（class 和 map 都站在它上面）。

---

## 2. class（对象）

### 2.1 语法（方案 A：编译期翻译）

```
10 class dog
20   name$
30   age
40   method bark(n)
50     print "woof "; n
60   end method
70 end class
```

**翻译规则（加载期做，运行时无类）**：

| 源写法 | 翻译后 |
|---|---|
| `class dog` | 声明命名空间前缀 `dog.` |
| 字段 `name$` / `age` | 默认值登记：`dog.name$` / `dog.age`（可选初值） |
| `method bark(n)` | 方法 = 子程序 `dog_bark(dog_name$, dog_age, n)`：self 显式第一参数（Python 风格），方法体按普通语句执行 |
| `d = new dog` | 把 `dog.` 字段默认值复制到前缀 `d.`（不写 `new` 也行——`d.name$ = "x"` 直接隐式创建） |
| `d.bark(2)` | 翻译为 `gosub dog_bark` 并传 `d.name$, d.age, 2` |

### 2.2 实例即命名空间

```
10 d = new dog
20 d.name$ = "旺财"
30 d.age = 3
40 d.bark(2)          ' 输出 woof 2
```

- "实例" = 一组以 `d.` 为前缀的变量，跟 struct 同构。
- `new` 只是复制默认值，不是堆分配，没有释放问题。

### 2.3 继承：编译期字段/方法复制

```
10 class cat : dog
20   method meow()
30     print "meow"
40   end method
50 end class
```

`cat` 继承 `dog`：加载期把 `dog.` 的字段默认值 + 方法表复制进 `cat.`，再叠加自己的。运行时无继承链概念。

### 2.4 边界（明说做不到的）

| 不做 | 原因 |
|---|---|
| 多态 / 虚方法 | 需要运行时类型分派，违背"零类概念" |
| 运算符重载 | 解析器复杂度不成比例 |
| 构造/析构 | 无堆分配，不需要 |
| 私有成员 | 全局共享模型下无意义 |
| 对象数组 | `d(3).x` 下标 + 字段组合暂不支持（变量名带括号 + 点会撞词法，列入后议） |

### 2.5 成本

中。主要是加载期 class 块的收集与翻译，执行引擎不动。

---

## 3. 容器（vector / map / set 的 BASIC 化）

### 3.1 vector → `dim` + `push` / `pop` / `len`

```
10 dim a(10)
20 push a, 5          ' 尾部追加，自动扩容（超界按需翻倍，上限受内存约束）
30 push a, 7
40 x = pop a          ' x = 7
50 n = len(a)         ' n = 1（当前元素个数）
60 for i = 1 to len(a) : print a(i) : next i
```

- `dim a(10)` 定长；`push` 超过定长自动扩容；`pop` 缩回。
- `len()` 对数组返回**当前元素个数**（区别于定长声明值）。
- 实现：数组变量表项加 `used` 计数 + 扩容标志。中等成本，跟 DIM 一起做。

### 3.2 map → 字符串下标数组 `m$(k$)`

```
10 m$("apple") = 5
20 m$("banana") = 3
30 print m$("apple")             ' 5
40 if m$("orange") = 0 then print "没有"   ' 未赋值 = 0
```

- 运行时字符串当索引——关联数组，老 BASIC 血脉（非 STL 移植）。
- 数字值 `m$("k") = n`、字符串值 `m$("k") = s$` 都支持（值类型跟随赋值）。
- 实现：变量表 key 支持括号内表达式键（`m$(k$)` 的 `k$` 求值后拼进变量名）。低成本，站在 struct 词法之上。

### 3.3 set → 标记法 `s$(k$) = 1`

```
10 s$("apple") = 1               ' 加入集合
20 if s$("apple") = 1 then print "在集合里"
30 s$("apple") = 0               ' 移除
```

- 集合 = map 的特例（键存在即元素在集合，值恒为 1/0）。去重天然，重复赋值只留一份。
- 不需要新关键字——map 落地后 set 免费。

### 3.4 string → 已内置（现状确认）

`a$` 就是 string：`left$` / `right$` / `mid$` / `len` 全套串函数 + 中文原生（UTF-8 按字符计）。**本层不再动字符串，它早就是好的。**

### 3.5 容器成本总览

| 项 | 成本 | 依赖 |
|---|---|---|
| dim 定长数组 | 中 | —（主语法清单 #5） |
| push / pop / len | 低 | dim |
| m$(k$) 关联数组 | 低 | struct 词法 |
| s$(k$) 集合 | 0（免费） | m$ |

---

## 4. 示例：游戏记分板（struct + 数组 + map 全家桶）

```
10 struct player.name$, player.score
20 dim names$(10) : dim scores(10)
30 player.name$ = "小明" : player.score = 5
40 push names$, player.name$ : push scores, player.score
50 m$("小明") = player.score
60 for i = 1 to len(names$)
70   print names$(i); " "; scores(i)
80 next i
90 if m$("小明") > 0 then print "记录在案"
100 end
```

## 5. 示例：class 版宠物（继承 + 方法）

```
10 class dog
20   name$
30   method bark(n)
40     print "woof "; n
50   end method
60 end class
70 class cat : dog
80   method meow()
90     print "meow"
100   end method
110 end class
120 d = new dog
130 d.name$ = "旺财"
140 d.bark(2)
150 c = new cat
160 c.name$ = "咪咪"
170 c.meow() : c.bark(1)
180 end
```

---

## 6. 实施顺序与验收

| 阶段 | 内容 | 验收 |
|---|---|---|
| 1 | struct（扁平字段名）+ dim/push/pop/len + m$(k$)/s$(k$) | 示例 §4 在真机 PRGM 跑通 |
| 2 | class 翻译层 + 继承 | 示例 §5 跑通；老程序回归零变化 |
| 3 | 教材追加"进阶：结构、对象与容器"一章（豆包-2 负责） | 与实现一致 |

**回归底线**：每阶段编译后，主语法清单的旧示例程序全量重跑，行为不变才算过。

---

## 7. 与本层相关的其他挂账

- 对象数组（`d(3).x`）撞词法，后议。
- `new` 是否保留关键字：初版建议保留（可读性），若解释器关键字表冲突可降为纯注释语义。
- class 块与主语法"块 if"的解析器共用一套块收集逻辑（缩进/end 收尾），GLM 实施主语法时留好接口。

---

*本文档为设计规格，非教材；语法以 `components/memoria_basic/` 最终实现为准。*
