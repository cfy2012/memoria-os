# 贡献指南

本仓库接受外部贡献。提交前请阅读 README 与本文件中的开发约定。

## 开发环境

- ESP-IDF v5.x / v6.x（官方安装器，含 PowerShell 环境）
- 芯片：ESP32-S3（N16R8）
- 推荐：任意编辑器配合 `idf.py` 命令行使用

## 代码规范

- 遵循 ESP-IDF 官方代码风格
- 公开 API 必须在头文件中写注释说明
- 变量命名：小写下划线（`snake_case`）
- 宏 / 常量：大写下划线（`UPPER_CASE`）
- 类型定义：`snake_case_t`（带 `_t` 后缀）
- 项目启用 `-Werror`：任何 warning 都会编译失败，提交前先本地过编译

## 组件架构

每个组件独立：

1. 拥有自己的 `CMakeLists.txt`，`SRCS` 只列真实源文件
2. 在 `REQUIRES` 中显式声明依赖（不靠隐式包含）
3. 头文件放 `include/`，源文件放组件根目录
4. 硬件引脚不允许硬编码：一律从 `memoria_drivers/include/drivers_config.hpp` 读取

当前组件：`memoria_drivers` / `memoria_kernel`（含 `modes/` 全部内置模式）/ `memoria_window` / `memoria_fs` / `memoria_package` / `memoria_script` / `memoria_basic` / `memoria_shell`。

## 自研边界

Memoria OS 的核心是自研系统架构：

- 不复用 Linux / Windows 等桌面 OS 的文件系统、UI 或进程模型
- 窗口调度、导航逻辑、文件系统、脚本引擎为原创实现
- 允许依赖 ESP-IDF 底层驱动 API 与 FreeRTOS 原语（任务、信号量、队列、定时器）

## 键盘（6×10 矩阵）

- 硬件方案：**2× MCP23017 I2C 扩展器**扫描（6 行 + 10 列 + 余量），不占 ESP32 GPIO
- 键位表在 `memoria_kernel/include/keyboard_keymap.hpp`（双层：主键 + Shift 层）
- 解析层 `keyboard_input.cpp`：维护 Shift/Caps 状态，分发系统键（背光/音量/电源）
- 修改键位：只改 `KEYMAP_R1..R6` 数组，特殊键码用 `K_*` 枚举
- 引脚一律从 `memoria_drivers/include/drivers_config.hpp` 读取，不硬编码

## 模拟器同步

`simulator/simulator.html` 与固件逻辑同步维护：

- 改固件交互逻辑后，同步更新模拟器对应分支
- 模拟器支持 `postMessage({type:'memoria-key', key})` 外部遥控（first_look 依赖此接口）
- 改动后浏览器打开 `simulator/simulator.html` 人工过一遍主要流程

## 提交 Pull Request

1. Fork 本仓库
2. 创建功能分支：`git checkout -b feature/your-feature`
3. 提交改动：`git commit -m "feat: 添加 xxx 功能"`
4. 推送分支：`git push origin feature/your-feature`
5. 提交 Pull Request

## 行为准则

尊重所有贡献者，保持友善和开放的讨论氛围。
