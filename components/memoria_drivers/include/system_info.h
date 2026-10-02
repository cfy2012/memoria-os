/**
 * @file system_info.h
 * @brief 系统元信息：版本号 / 名称 / 纪念日
 *
 * 全系统唯一元信息源。所有显示版本号的位置（About 页、Shell、启动日志、
 * 固件更新检查）都从这里取值。
 * 放在 drivers 组件：它是全系统公共底座，kernel/package 均可见，
 * 避免 package 反向依赖 kernel 造成循环。
 */

#pragma once

/* ---------- 系统元信息 ---------- */
#define MEMORIA_OS_NAME        "Memoria OS"
#define MEMORIA_OS_VERSION     "1.0.0"
#define MEMORIA_BIRTHDAY_MMDD  1225   /* MMDD 格式：12 月 25 日 */
