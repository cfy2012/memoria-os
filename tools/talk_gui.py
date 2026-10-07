# -*- coding: utf-8 -*-
# @file talk_gui.py (v4 · tkinter GUI · 浅色主题)
# @brief Memoria OS 监视器窗口：上半扇形仪表盘 + 下半命令框，30s 轮询
#
# 模式：
#   talk_gui.exe            纯窗口 GUI（无控制台黑窗）
#   talk_gui.exe --selftest 控制台解析自检（不碰串口不开窗）
#   talk_gui.exe --autotest 开窗 800ms 自动关闭（打包冒烟用）
#
# v4 修复：日志误截断（行数比较改 int）、滚动仅在已位于底部时跟随、
#          第二排仪表弧超出 Canvas 被裁（加高）、全盘浅色高亮主题。

import re
import sys
import queue
import threading
import datetime

import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import ttk

BAUD = 115200
POLL_S = 30

# ---------- sys 解析 ----------
RE_CPU = re.compile(r'cpu\s*:\s*(\d+)\s*MHz\s+temp\s+(-?[\d.]+)\s*C', re.I)
RE_LOAD = re.compile(r'load\s*:\s*total\s+(\d+)%\s+core0\s+~(\d+)%\s+core1\s+~(\d+)%', re.I)
RE_HEAP = re.compile(r'heap int\s*:\s*(\d+)\s*KB free', re.I)
RE_PSRAM = re.compile(r'psram\s*:\s*(\d+)\s*KB free', re.I)
RE_BATT = re.compile(r'battery\s*:\s*(\d+)%\s+([\d.]+)V', re.I)
RE_VER = re.compile(r'version\s*:\s*(\S+)\s+uptime\s+(\d+)\s*s', re.I)
RE_WIFI = re.compile(r'wifi\s*:\s*rf=(\S+)\s+conn=(\S+)', re.I)
RE_SD = re.compile(r'^sd\s*:\s*(.+)$', re.M)
RE_SPAM = re.compile(r'((?:memoria|safe)>\s*)+')

RE_LOGE = re.compile(r'^(\s*)E \(\d+\)')
RE_LOGW = re.compile(r'^(\s*)W \(\d+\)')
RE_LOGI = re.compile(r'^(\s*)I \(\d+\)')
RE_RST = re.compile(r'rst:0x|boot:|ESP-ROM|entry 0x|load:0x')
RE_PROMPT = re.compile(r'^(memoria|safe)>')
RE_PANIC = re.compile(r'Guru Meditation|LoadProhibited|StoreProhibited|'
                      r'abort\(\)|assert failed|backtrace|Brownout', re.I)


class Dash:
    """sys 数据仓"""
    def __init__(self):
        self.mhz = 0
        self.temp = -99.0
        self.load = -1
        self.c0 = -1
        self.c1 = -1
        self.heap = 0
        self.psram = 0
        self.batt = 0
        self.battv = 0.0
        self.ver = ''
        self.uptime = 0
        self.wifi = '?'
        self.sd = '?'
        self.updated = '--:--:--'

    def feed(self, line):
        m = RE_CPU.search(line)
        if m:
            self.mhz = int(m.group(1))
            self.temp = float(m.group(2))
        m = RE_LOAD.search(line)
        if m:
            self.load, self.c0, self.c1 = (int(m.group(1)),
                                           int(m.group(2)), int(m.group(3)))
        m = RE_HEAP.search(line)
        if m:
            self.heap = int(m.group(1))
        m = RE_PSRAM.search(line)
        if m:
            self.psram = int(m.group(1))
        m = RE_BATT.search(line)
        if m:
            self.batt = int(m.group(1))
            self.battv = float(m.group(2))
        m = RE_VER.search(line)
        if m:
            self.ver = m.group(1)
            self.uptime = int(m.group(2))
        m = RE_WIFI.search(line)
        if m:
            self.wifi = m.group(1) + '/' + m.group(2)
        m = RE_SD.search(line)
        if m:
            self.sd = m.group(1).strip()
        self.updated = datetime.datetime.now().strftime('%H:%M:%S')


def selftest():
    fake = [
        '==== Memoria OS System ====',
        'version  : 1.3.0  uptime 12400 s',
        'cpu      : 240 MHz  temp 32.6 C',
        'load     : total 12%  core0 ~5%  core1 ~8% (boot avg)',
        'tasks    : 18',
        'heap int : 156 KB free (largest 98, min-ever 88)',
        'psram    : 7800 KB free',
        'wifi     : rf=off conn=no',
        'sd       : not mounted',
        'battery  : 85%  3.92V',
    ]
    d = Dash()
    for ln in fake:
        d.feed(ln)
    checks = [
        ('mhz', d.mhz == 240), ('temp', abs(d.temp - 32.6) < .01),
        ('load', d.load == 12), ('heap', d.heap == 156),
        ('psram', d.psram == 7800), ('batt', d.batt == 85),
        ('battv', abs(d.battv - 3.92) < .01), ('ver', d.ver == '1.3.0'),
        ('wifi', d.wifi == 'off/no'), ('sd', d.sd == 'not mounted'),
    ]
    ok = True
    print('=== talk_gui selftest ===')
    for name, good in checks:
        print(' [%s] %s' % ('PASS' if good else 'FAIL', name))
        ok &= good
    print('结论: ' + ('ALL PASS' if ok else '存在 FAIL'))
    sys.exit(0 if ok else 1)


# ---------- IDE 深色高亮主题（VS Code 同款） ----------
C_BG = '#1e1e1e'        # 窗口底
C_CARD = '#252526'      # 日志/输入底
C_EDGE = '#3c3c3c'      # 弧底/边线
C_TXT = '#d4d4d4'       # 主文字
C_SUB = '#858585'       # 次文字
C_ACC = '#4fc1ff'       # 标签蓝


def arc_color(p):
    """值弧颜色：青绿→黄→红"""
    if p < 0.5:
        return '#4ec9b0'
    if p < 0.8:
        return '#dcdcaa'
    return '#f48771'


class Gauge:
    """单个半圆扇形仪表（Canvas 弧 + 数值）"""
    def __init__(self, cv, x, y, label, vmax, unit, vmin=0.0):
        self.cv, self.x, self.y = cv, x, y
        self.label, self.vmax, self.unit, self.vmin = label, vmax, unit, vmin
        self.r = 56
        self.base = cv.create_arc(x - self.r, y - self.r, x + self.r,
                                  y + self.r, start=180, extent=180,
                                  style='arc', outline=C_EDGE, width=12)
        self.val = cv.create_arc(x - self.r, y - self.r, x + self.r,
                                 y + self.r, start=180, extent=0.5,
                                 style='arc', outline='#1a7f37', width=12)
        self.t_val = cv.create_text(x, y - 14, text='--', fill=C_TXT,
                                    font=('Consolas', 16, 'bold'))
        self.t_unit = cv.create_text(x, y + 12, text=unit, fill=C_SUB,
                                     font=('Consolas', 9))
        self.t_label = cv.create_text(x, y + self.r + 18, text=label,
                                      fill=C_ACC,
                                      font=('Microsoft YaHei UI', 10, 'bold'))

    def set(self, value):
        v = max(self.vmin, min(self.vmax, value))
        pct = (v - self.vmin) / max(self.vmax - self.vmin, 1e-6)
        deg = max(0.5, 180.0 * pct)
        self.cv.itemconfig(self.val, extent=deg, outline=arc_color(pct))
        self.cv.itemconfig(self.t_val,
                           text=('%.1f' % value) if isinstance(value, float)
                           else str(value))


class App:
    def __init__(self, root, port):
        self.root = root
        self.port = port
        self.dash = Dash()
        self.q = queue.Queue()
        self.stop = threading.Event()
        root.title('Memoria OS 监视器 · ' + port)
        root.configure(bg=C_BG)
        root.geometry('920x760')
        root.minsize(920, 700)

        # ---- 上：仪表区 ----
        self.cv = tk.Canvas(root, width=900, height=330, bg=C_BG,
                            highlightthickness=0)
        self.cv.pack(pady=(8, 0))
        self.gauges = [
            Gauge(self.cv, 160, 100, 'CPU 主频', 240, 'MHz'),
            Gauge(self.cv, 450, 100, '芯片温度', 80, '℃', vmin=10),
            Gauge(self.cv, 740, 100, '双核负载', 100, '%'),
            Gauge(self.cv, 160, 265, '内部内存', 400, 'KB free'),
            Gauge(self.cv, 450, 265, 'PSRAM', 8192, 'KB free'),
            Gauge(self.cv, 740, 265, '电池', 100, '%'),
        ]
        self.info = tk.Label(root, text='等待第一次 sys 轮询…', bg=C_BG,
                             fg=C_SUB, font=('Microsoft YaHei UI', 10))
        self.info.pack(pady=(2, 4))

        # ---- 中：日志区 ----
        mid = tk.Frame(root, bg=C_BG)
        mid.pack(fill='both', expand=True, padx=8)
        self.log = tk.Text(mid, bg=C_CARD, fg=C_TXT, wrap='none',
                           font=('Consolas', 10), height=10, state='disabled',
                           relief='solid', bd=1)
        sb = ttk.Scrollbar(mid, command=self.log.yview)
        self.log.configure(yscrollcommand=sb.set)
        sb.pack(side='right', fill='y')
        self.log.pack(side='left', fill='both', expand=True)
        for tag, cfg in (('e', dict(foreground='#f48771')),
                         ('w', dict(foreground='#dcdcaa')),
                         ('i', dict(foreground='#6a9955')),
                         ('p', dict(foreground='#f48771',
                                    font=('Consolas', 10, 'bold'))),
                         ('b', dict(foreground='#c586c0')),
                         ('s', dict(foreground='#4fc1ff'))):
            self.log.tag_configure(tag, **cfg)

        # ---- 下：命令行 ----
        bot = tk.Frame(root, bg=C_BG)
        bot.pack(fill='x', padx=8, pady=6)
        tk.Label(bot, text='命令', bg=C_BG, fg=C_ACC,
                 font=('Microsoft YaHei UI', 10, 'bold')).pack(side='left')
        self.cmd = tk.Entry(bot, bg=C_CARD, fg=C_TXT,
                            font=('Consolas', 11), relief='solid', bd=1,
                            insertbackground=C_TXT)
        self.cmd.pack(side='left', fill='x', expand=True, padx=6, ipady=4)
        self.cmd.bind('<Return>', self.send)
        tk.Button(bot, text='发送', command=self.send, bg='#0e639c',
                  fg='#ffffff', activebackground='#1177bb',
                  activeforeground='#ffffff', relief='flat',
                  font=('Microsoft YaHei UI', 10, 'bold'),
                  width=8).pack(side='left', padx=(0, 2))
        tk.Button(bot, text='退出', command=self.quit, bg='#3c3c3c',
                  fg='#d4d4d4', activebackground='#505050', relief='flat',
                  font=('Microsoft YaHei UI', 10), width=6).pack(side='left')

        # ---- 串口 ----
        try:
            self.ser = serial.Serial(port, BAUD, timeout=0.1)
        except Exception as e:
            self.q.put(('__direct__', '串口 %s 打不开：%s' % (port, e), 'p'))
            self.ser = None
        if self.ser:
            threading.Thread(target=self.reader, daemon=True).start()
            self.q.put(('__direct__',
                        '已连上 %s @%d · 每 %ds 轮询 sys' % (port, BAUD, POLL_S),
                        's'))
            self.poll()
        self.root.after(100, self.flush_log)
        self.root.protocol('WM_DELETE_WINDOW', self.quit)

    # ---- 串口 ----
    def reader(self):
        buf = b''
        while not self.stop.is_set():
            try:
                chunk = self.ser.read(256)
            except Exception:
                break
            if not chunk:
                continue
            buf += chunk
            while b'\n' in buf:
                raw, buf = buf.split(b'\n', 1)
                try:
                    line = raw.decode('utf-8', errors='replace').rstrip('\r')
                except Exception:
                    continue
                line = RE_SPAM.sub(lambda m: m.group(1), line)
                if line.strip():
                    self.dash.feed(line)
                    self.q.put(line)

    def poll(self):
        if self.stop.is_set():
            return
        try:
            self.ser.write(b'sys\r\n')
        except Exception:
            self.q.put(('__direct__', '串口写失败，已停止轮询', 'p'))
            return
        self.root.after(400, self.refresh_gauges)
        self.root.after(POLL_S * 1000, self.poll)

    def refresh_gauges(self):
        d = self.dash
        self.gauges[0].set(d.mhz)
        self.gauges[1].set(d.temp if d.temp > -50 else self.gauges[1].vmin)
        self.gauges[2].set(d.load if d.load >= 0 else 0)
        self.gauges[3].set(d.heap)
        self.gauges[4].set(d.psram)
        self.gauges[5].set(d.batt)
        self.info.config(text='Memoria OS %s · uptime %d:%02d:%02d · wifi %s '
                              '· sd %s · 更新 %s'
                         % (d.ver or '?', d.uptime // 3600,
                            d.uptime % 3600 // 60, d.uptime % 60,
                            d.wifi, d.sd, d.updated))

    # ---- 日志 ----
    def flush_log(self):
        """队列日志 → Text（仅当用户停在底部才跟随滚动）"""
        drew = False
        try:
            while True:
                item = self.q.get_nowait()
                if isinstance(item, tuple):
                    _, line, tag = item
                else:
                    line, tag = item, None
                if tag is None:
                    if RE_PANIC.search(line):
                        tag = 'p'
                    elif RE_LOGE.search(line):
                        tag = 'e'
                    elif RE_LOGW.search(line):
                        tag = 'w'
                    elif RE_BATT.search(line):
                        tag = 'b'
                    elif RE_PROMPT.search(line) or line.startswith('===='):
                        tag = 's'
                    elif RE_RST.search(line):
                        tag = 's'
                    elif RE_LOGI.search(line):
                        tag = 'i'
                self.log.config(state='normal')
                self.log.insert('end', line + '\n', tag or ())
                drew = True
        except queue.Empty:
            pass
        if drew:
            lines = int(self.log.index('end-1c').split('.')[0])
            if lines > 3000:                      # 定量截断，int 比较
                self.log.delete('1.0', '%d.0' % (lines - 2000))
            at_bottom = self.log.yview()[1] >= 0.999
            self.log.config(state='disabled')
            if at_bottom:
                self.log.see('end')
        self.root.after(100, self.flush_log)

    # ---- 命令 ----
    def send(self, event=None):
        cmd = self.cmd.get().strip()
        if not cmd or not self.ser:
            return
        self.cmd.delete(0, 'end')
        self.log.config(state='normal')
        self.log.insert('end', '>> ' + cmd + '\n', 's')
        self.log.see('end')
        self.log.config(state='disabled')
        if cmd == '/q':
            self.quit()
            return
        try:
            self.ser.write((cmd + '\r\n').encode('utf-8'))
        except Exception:
            self.q.put(('__direct__', '发送失败：串口已断', 'p'))

    def quit(self):
        self.stop.set()
        try:
            if self.ser:
                self.ser.close()
        except Exception:
            pass
        self.root.destroy()


def pick_port():
    """启动端口选择窗口（每 5s 轮询刷新）→ 返回选中端口"""
    chosen = {'port': None}
    root = tk.Tk()
    root.title('选择 ESP32 串口')
    root.configure(bg=C_BG)
    root.geometry('420x340')
    tk.Label(root, text='发现以下串口设备（每 5 秒自动刷新）：', bg=C_BG,
             fg=C_TXT, font=('Microsoft YaHei UI', 10)).pack(pady=(12, 4))
    lb = tk.Listbox(root, bg=C_CARD, fg=C_TXT, relief='flat',
                    font=('Consolas', 10), height=6,
                    highlightthickness=1, highlightbackground=C_EDGE)
    lb.pack(fill='x', padx=16)

    def devs():
        return sorted(serial.tools.list_ports.comports(),
                      key=lambda p: p.device)

    def fill(items, keep):
        lb.delete(0, 'end')
        for p in items:
            lb.insert('end', '%-8s %s' % (p.device, p.description))
        if keep:
            for i in range(lb.size()):
                if lb.get(i).split()[0] == keep:
                    lb.selection_clear(0, 'end')
                    lb.selection_set(i)
                    lb.see(i)
                    return
        if lb.size():
            lb.selection_set(0)

    def refresh():
        sel = lb.curselection()
        keep = lb.get(sel[0]).split()[0] if sel else None
        items = devs()
        now = [p.device for p in items]
        old = [lb.get(i).split()[0] for i in range(lb.size())]
        if now != old:                       # 有插拔变化才重绘，防闪烁
            fill(items, keep)
        root.after(5000, refresh)

    fill(devs(), None)
    row = tk.Frame(root, bg=C_BG)
    row.pack(pady=10)
    tk.Label(row, text='端口号：', bg=C_BG, fg=C_SUB,
             font=('Microsoft YaHei UI', 10)).pack(side='left')
    ent = tk.Entry(row, bg=C_CARD, fg=C_TXT, width=10,
                   font=('Consolas', 11), relief='solid', bd=1,
                   insertbackground=C_TXT)
    ent.insert(0, 'COM5')
    ent.pack(side='left', padx=4, ipady=3)

    def ok(event=None):
        v = ent.get().strip()
        if not v:
            sel = lb.curselection()
            if sel:
                v = lb.get(sel[0]).split()[0]
        chosen['port'] = v.upper() if v else 'COM5'
        root.destroy()
    tk.Button(root, text='连接', command=ok, bg='#0e639c', fg='#ffffff',
              activebackground='#1177bb', relief='flat', width=14,
              font=('Microsoft YaHei UI', 10, 'bold')).pack(pady=6, ipady=3)
    root.bind('<Return>', ok)
    refresh()
    root.mainloop()
    return chosen['port']


def autotest():
    """打包冒烟：开窗 800ms 自动关闭"""
    import tkinter as tk2
    r = tk2.Tk()
    r.withdraw()
    app = App(r, 'COM_AUTOTEST')     # 串口故意打不开，验证 GUI 建窗路径
    r.after(800, app.quit)
    r.mainloop()
    print('autotest OK')
    sys.exit(0)


def main():
    if '--selftest' in sys.argv:
        selftest()
        return
    if '--autotest' in sys.argv:
        autotest()
        return
    port = pick_port()
    if not port:
        return
    root = tk.Tk()
    App(root, port)
    root.mainloop()


if __name__ == '__main__':
    main()
