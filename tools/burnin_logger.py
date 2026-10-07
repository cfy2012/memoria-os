# -*- coding: utf-8 -*-
# @file burnin_logger.py
# @brief Memoria OS 挂测日志抓取（42h/72h 冒烟实测配套）
#
# 用法（命令行）：
#   D:\py\python.exe burnin_logger.py                  # COM5 @115200 开始挂测
#   D:\py\python.exe burnin_logger.py --port COM7      # 指定串口
#   D:\py\python.exe burnin_logger.py --interval 1800  # 电压轮询间隔秒（默认 1800=30min）
#   D:\py\python.exe burnin_logger.py --list-ports     # 列可用串口
#   D:\py\python.exe burnin_logger.py --selftest       # 内置模拟日志自检（不碰串口）
#
# 功能：
#   1. 串口日志实时落盘（burnin_logs/burnin_时间戳.log），行前加时间戳
#   2. panic / 非正常重启 / BOD 自动标红 + 单独写 .alerts.log
#   3. 定时发 battery 命令，电压/百分比写入 .voltage.csv（画曲线用）
#   4. 串口断线自动重连（USB 掉线不中断挂测）
#   5. Ctrl+C 结束输出总结报告（时长 / panic 数 / 重启数 / 最低电压）
#
# 输出格式参考固件 memoria_shell/shell.cpp:243 —— battery: 85%  3.92V  discharging

import os
import re
import sys
import csv
import time
import threading
import datetime
import argparse

# ANSI 色在 Windows 终端启用
if os.name == 'nt':
    os.system('')

C_RED = '\033[91m'
C_YEL = '\033[93m'
C_GRN = '\033[92m'
C_CYN = '\033[96m'
C_DIM = '\033[90m'
C_END = '\033[0m'

# ---------- 异常模式表 ----------
RE_RST = re.compile(r'rst:0x([0-9a-fA-F]+)\s*\(([^)]*)\)')          # boot 重启原因
RE_PANIC = re.compile(r'Guru Meditation|LoadProhibited|StoreProhibited|'
                      r'abort\(\)|assert failed|backtrace', re.I)   # panic 族
RE_BOD = re.compile(r'brownout', re.I)                              # 欠压复位
RE_BATT = re.compile(r'battery:\s*(\d+)%\s+([\d.]+)V', re.I)        # 固件 shell 输出

# rst 原因里视为正常的值：0x1 POWERON / 0x5 DEEPSLEEP / 0x8 SDIO
RST_OK = {'1', '5', '8'}


def classify(line):
    """单行日志 → (级别, 标签)。级别：0 正常 / 1 提示(黄) / 2 告警(红)。bytes/str 通吃"""
    if isinstance(line, bytes):
        line = line.decode('utf-8', errors='replace')
    m = RE_RST.search(line)
    if m:
        if m.group(1).lower() in RST_OK:
            return 0, 'boot rst=0x%s (%s)' % (m.group(1), m.group(2))
        return 2, '非正常重启 rst=0x%s (%s)' % (m.group(1), m.group(2))
    if RE_BOD.search(line):
        return 2, 'BOD 欠压复位'
    if RE_PANIC.search(line):
        return 2, 'panic 迹象'
    mb = RE_BATT.search(line)
    if mb:
        v = float(mb.group(2))
        if v < STATE['vlow']:
            return 2, '低电压 %.2fV' % v
        return 0, '电压 %.2fV %s%%' % (v, mb.group(1))
    if line.lstrip().startswith('E ('):
        return 1, 'ESP_LOGE'
    return 0, ''


STATE = {'vlow': 3.3}


class BurninLogger:
    def __init__(self, port, baud, interval):
        self.port, self.baud, self.interval = port, baud, interval
        os.makedirs('burnin_logs', exist_ok=True)
        ts = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
        base = os.path.join('burnin_logs', 'burnin_' + ts)
        self.f_log = open(base + '.log', 'a', encoding='utf-8')
        self.f_alert = open(base + '.alerts.log', 'a', encoding='utf-8')
        self.f_volt = open(base + '.voltage.csv', 'a', newline='', encoding='utf-8')
        self.csv = csv.writer(self.f_volt)
        self.csv.writerow(['time', 'percent', 'voltage'])
        self.t0 = time.time()
        self.stat = {'lines': 0, 'alert': 0, 'rst': 0, 'recon': 0, 'vmin': 99.0, 'vmin_at': ''}
        self.lock = threading.Lock()
        self.stop = threading.Event()

    def stamp(self):
        return datetime.datetime.now().strftime('%m-%d %H:%M:%S')

    def on_line(self, raw):
        """一行串口数据 → 分类、落盘、上屏。selftest 也走这里。"""
        try:
            line = raw.decode('utf-8', errors='replace').rstrip('\r\n')
        except Exception:
            return
        if not line.strip():
            return
        self.stat['lines'] += 1
        lv, tag = classify(line)
        full = '[%s] %s' % (self.stamp(), line)
        self.f_log.write(full + '\n')
        if lv == 2:
            self.stat['alert'] += 1
            self.f_alert.write(full + '   << %s\n' % tag)
            if '非正常重启' in tag:
                self.stat['rst'] += 1
            print(C_RED + full + '   << ' + tag + C_END)
        elif lv == 1:
            print(C_YEL + full + '   << ' + tag + C_END)
        else:
            if tag:
                print(C_DIM + full + '   << ' + tag + C_END)
            else:
                print(full)

    def poll_battery(self):
        """后台线程：定时发 battery 命令（设备停在命令行时才有响应，GUI 态发之无害）"""
        while not self.stop.wait(self.interval):
            try:
                with self.lock:
                    self.ser.write(('battery\r\n').encode())
            except Exception:
                pass

    def grab_voltage(self, raw):
        """从响应行提取电压入 CSV"""
        m = RE_BATT.search(raw.decode('utf-8', errors='replace'))
        if not m:
            return
        pct, v = int(m.group(1)), float(m.group(2))
        t = self.stamp()
        self.csv.writerow([t, pct, v])
        self.f_volt.flush()
        if v < self.stat['vmin']:
            self.stat['vmin'] = v
            self.stat['vmin_at'] = t

    def run(self):
        import serial
        import serial.tools.list_ports
        print(C_CYN + '=== Memoria OS 挂测记录  %s @ %d ===' % (self.port, self.baud) + C_END)
        print('停止: Ctrl+C（自动出总结报告）')
        threading.Thread(target=self.poll_battery, daemon=True).start()
        while not self.stop.is_set():
            try:
                self.ser = serial.Serial(self.port, self.baud, timeout=1)
                if self.stat['recon']:
                    print(C_GRN + '串口已重连 %s' % self.port + C_END)
                buf = b''
                while not self.stop.is_set():
                    chunk = self.ser.read(512)
                    if not chunk:
                        continue
                    buf += chunk
                    while b'\n' in buf:
                        raw, buf = buf.split(b'\n', 1)
                        self.on_line(raw)
                        self.grab_voltage(raw)
            except KeyboardInterrupt:
                break
            except Exception as e:
                self.stat['recon'] += 1
                print(C_YEL + '串口异常(%s)，5s 后重连...' % e + C_END)
                try:
                    self.ser.close()
                except Exception:
                    pass
                if self.stop.wait(5):
                    break
        self.report()

    def report(self):
        secs = int(time.time() - self.t0)
        h, rem = divmod(secs, 3600)
        m, s = divmod(rem, 60)
        print('\n' + C_CYN + '========== 挂测总结 ==========' + C_END)
        print('挂测时长  : %02d:%02d:%02d' % (h, m, s))
        print('日志行数  : %d' % self.stat['lines'])
        print('告警条数  : %d（详见 .alerts.log）' % self.stat['alert'])
        print('非正常重启: %d 次' % self.stat['rst'])
        print('电压采样  : %s 最低 %.2fV @ %s'
              % ('无（设备未停在命令行）' if self.stat['vmin'] > 90 else '',
                 self.stat['vmin'], self.stat['vmin_at']) if self.stat['vmin'] <= 90
              else '电压采样  : 无（设备未停在命令行界面，正常）')
        print('断线重连  : %d 次' % self.stat['recon'])
        ok = self.stat['alert'] == 0
        print('三查结论  : ' + (C_GRN + '零告警，全过 PASS' + C_END if ok
                                else C_RED + '有 %d 条告警，翻 .alerts.log 定位' % self.stat['alert'] + C_END))
        for f in (self.f_log, self.f_alert, self.f_volt):
            f.close()
        input('\n按回车退出...')


def list_ports():
    import serial.tools.list_ports
    for p in serial.tools.list_ports.comports():
        print('%s  %s' % (p.device, p.description))


def selftest():
    """内置模拟日志自检：验证 classify 分类逻辑，不碰串口"""
    cases = [
        (b'rst:0x1 (POWERON),boot:0x8 (SPI_FAST_FLASH_BOOT)', 0),
        (b'rst:0x3 (SW_CPU_RESET)', 2),
        (b"Guru Meditation Error: Core 0 panic'ed (LoadProhibited)", 2),
        (b'Brownout detector was triggered', 2),
        (b'battery: 85%  3.92V  discharging', 0),
        (b'battery: 20%  3.28V  discharging', 2),
        (b'I (1746) JOYSTICK: KY-023 OK. X=7 Y=5 BTN=38', 0),
        (b'E (1735) SDCARD: FAT mount failed: ESP_ERR_INVALID_RESPONSE', 1),
    ]
    print('=== burnin_logger selftest ===')
    all_ok = True
    for raw, want_lv in cases:
        lv, tag = classify(raw)
        good = (lv == want_lv)
        mark = C_GRN + 'PASS' + C_END if good else C_RED + 'FAIL' + C_END
        print(' [%s] %s | 期望级%d 实际级%d %s' % (mark, raw.decode(), want_lv, lv, tag))
        if not good:
            all_ok = False
    print('结论: ' + (C_GRN + 'ALL PASS' + C_END if all_ok else C_RED + '存在 FAIL' + C_END))
    sys.exit(0 if all_ok else 1)


def main():
    ap = argparse.ArgumentParser(description='Memoria OS 挂测日志抓取')
    ap.add_argument('--port', default='COM5')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--interval', type=int, default=1800, help='电压轮询间隔秒')
    ap.add_argument('--vlow', type=float, default=3.3, help='低电压告警阈值 V')
    ap.add_argument('--list-ports', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()
    STATE['vlow'] = a.vlow
    if a.selftest:
        selftest()
        return
    if a.list_ports:
        list_ports()
        return
    BurninLogger(a.port, a.baud, a.interval).run()


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        print('\n已退出。')
