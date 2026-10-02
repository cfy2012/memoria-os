// demo.ms —— Memoria OS 脚本演示（.ms 语言）
// 拷到 TF 卡 /mem_fat/scripts/ 目录，在 PRGM 模式里选中即可运行。
// 语法：类 JavaScript 迷你语言，解释器为 C++ 编写的 memoria-script。

// ---- 1) 计算 1 加到 100 ----
var sum = 0;
var i = 1;
while (i <= 100) {
    sum = sum + i;      // 支持 += 简写：sum += i;
    i = i + 1;          // 也支持 i++ / i--
}
println("1+2+...+100 =", sum);

// ---- 2) 斐波那契数列前 10 项 ----
var a = 0;
var b = 1;
var k = 0;
while (k < 10) {
    println("fib(", k, ") =", a);
    var t = a + b;
    a = b;
    b = t;
    k = k + 1;
}

// ---- 3) 掷骰子：rand() 返回 0~1 的浮点 ----
var dice = rand() * 6 + 1;
println("骰子点数 ≈", dice);
if (dice > 3) {
    println("结果：大");
} else {
    println("结果：小");
}

// ---- 4) 字符串长度 ----
var hello = "Memoria OS";
println("len(", hello, ") =", len(hello));

// ---- 5) 计数延时（sleep_ms 在真机上桥接为系统延时）----
var n = 0;
while (n < 3) {
    println("倒计时", 3 - n, "...");
    sleep_ms(500);
    n = n + 1;
}
println("完成！");

// 也支持：var / function / if / else / while / for /
//         return / break / continue / true / false / null
//         内置函数：print println rand sleep_ms len
