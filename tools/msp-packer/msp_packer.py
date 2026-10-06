# -*- coding: utf-8 -*-
"""
@file  tools/msp-packer/msp_packer.py
@brief Memoria OS 打包器（图形界面）—— .msp 安装包 + store manifest.json
@usage 双击运行，或: D:\\py\\python.exe msp_packer.py    无界面自测: msp_packer.py --selftest
格式对齐: components/memoria_package/package_format.cpp（MSPACK 50B 头 + payload）
依赖: 仅 Python 3 标准库（tkinter/ttk），无需安装任何第三方包
"""
import json, struct, sys, zlib, os, time

MAGIC = b"MSPACK"
HEADER_SIZE = 50
MAX_PAYLOAD = 1024 * 1024          # 固件 verify_package 上限 1MB
NAME_CHARS = set("abcdefghijklmnopqrstuvwxyz"
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")


# ---------------- 核心打包逻辑（与固件逐位对齐，可脱离 GUI 单测） ----------------

def crc32(data: bytes) -> int:
    """zlib 标准 CRC32 —— 与固件 crc32_compute 一致（初值/末异或 0xFFFFFFFF）"""
    return zlib.crc32(data) & 0xFFFFFFFF


def check_name(name: str):
    """固件白名单: [A-Za-z0-9_.-]，防路径穿越"""
    if not name or name in (".", ".."):
        raise ValueError("包名不合法: %r" % name)
    if any(c not in NAME_CHARS for c in name):
        raise ValueError("包名只允许 A-Za-z0-9_.- : %s" % name)


def pack_msp(name: str, version: int, payload: bytes) -> bytes:
    """打 MSPACK 包并回读自校验（模拟固件 verify_package 全部检查）"""
    check_name(name)
    if not 0 <= version <= 0xFFFFFFFF:
        raise ValueError("版本号超出 0~4294967295")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload %d 字节超过固件 1MB 上限" % len(payload))
    hdr = struct.pack("<6sI32sII", MAGIC, version, name.encode()[:31],
                      len(payload), crc32(payload))
    blob = hdr + payload
    # 回读自校验
    magic, ver, nm, psz, crc = struct.unpack("<6sI32sII", blob[:HEADER_SIZE])
    if magic != MAGIC or psz != len(payload) or crc != crc32(payload):
        raise RuntimeError("自校验失败（头部回读不一致）")
    if crc32(blob[HEADER_SIZE:HEADER_SIZE + psz]) != crc:
        raise RuntimeError("自校验失败（CRC 回算不一致）")
    return blob


def build_manifest(base_url: str, files: list) -> list:
    """生成 store 源清单（对齐 package_manager.cpp manifest 格式注释）"""
    entries = []
    for path in files:
        name = os.path.basename(path)
        check_name(name)
        with open(path, "rb") as f:
            data = f.read()
        if len(data) > MAX_PAYLOAD:
            raise ValueError("%s 超过 1MB" % name)
        ext = name.lower().rsplit(".", 1)[-1] if "." in name else ""
        e = {"name": name, "version": time.strftime("%Y.%m.%d"),
             "size": len(data), "crc": crc32(data),
             "url": base_url.rstrip("/") + "/" + name}
        if ext in ("wav", "mp3", "m4a"):
            e["type"] = "media"        # media 包: 固件走 MSPACK 解包路径
        entries.append(e)
    return entries


def selftest() -> int:
    """无 GUI 自测: 打包→回读→CRC 逐位验证"""
    payload = b"#!/memoria\nprint \"msp packer selftest\"\n" * 24
    blob = pack_msp("selftest.bas", 1, payload)
    magic, ver, nm, psz, crc = struct.unpack("<6sI32sII", blob[:HEADER_SIZE])
    ok = (magic == MAGIC and psz == len(payload)
          and crc == crc32(payload)
          and blob[HEADER_SIZE:] == payload)
    print("selftest: %s | header %d B + payload %d B = %d B | crc=0x%08X"
          % ("PASS" if ok else "FAIL", HEADER_SIZE, len(payload), len(blob), crc))
    m = build_manifest("https://example.com/pkg", [])
    print("manifest empty list: %s" % ("PASS" if m == [] else "FAIL"))
    return 0 if ok else 1


# ---------------- 图形界面 ----------------

def run_gui():
    import tkinter as tk
    from tkinter import ttk, filedialog, messagebox, scrolledtext

    app = tk.Tk()
    app.title("Memoria OS 打包器 — .msp 安装包 / store 清单")
    app.geometry("680x520")
    app.minsize(620, 460)

    style = ttk.Style(app)
    try:
        style.theme_use("vista")
    except Exception:
        pass
    style.configure("TButton", padding=(10, 4))

    log = scrolledtext.ScrolledText(app, height=10, state="disabled",
                                    font=("Consolas", 9), bg="#101418", fg="#d8e4f0")
    btn_state = {"fg": "#35c46a"}

    def say(msg, ok=True):
        log.configure(state="normal")
        log.tag_config("ok", foreground="#35c46a")
        log.tag_config("err", foreground="#ff6b6b")
        log.insert("end", msg + "\n", "ok" if ok else "err")
        log.see("end")
        log.configure(state="disabled")

    nb = ttk.Notebook(app)
    nb.pack(fill="both", expand=True, padx=8, pady=(8, 4))

    # ---- 页 1: MSP 安装包 ----
    f1 = ttk.Frame(nb, padding=14)
    nb.add(f1, text=" .msp 安装包（音频 media 包） ")
    rows = [("包名", None), ("版本号", None), ("payload 文件", None), ("输出 .msp", None)]
    v_name = tk.StringVar()
    v_ver = tk.StringVar(value="1")
    v_in = tk.StringVar()
    v_out = tk.StringVar()

    def mkrow(r, label, var, browse=None, save=False):
        ttk.Label(f1, text=label).grid(row=r, column=0, sticky="w", pady=6)
        e = ttk.Entry(f1, textvariable=var, width=52)
        e.grid(row=r, column=1, sticky="we", padx=6)
        if browse:
            def pick():
                p = filedialog.asksaveasfilename() if save else filedialog.askopenfilename()
                if p:
                    var.set(p)
                    if label == "payload 文件" and not v_out.get():
                        base = os.path.splitext(p)[0]
                        v_out.set(base + ".msp")
                    if label == "包名":
                        pass
            ttk.Button(f1, text="浏览…", command=pick).grid(row=r, column=2)

    mkrow(0, "包名", v_name)
    mkrow(1, "版本号", v_ver)
    mkrow(2, "payload 文件", v_in, browse=True)
    mkrow(3, "输出 .msp", v_out, browse=True, save=True)
    ttk.Label(f1, text="包名规则: A-Za-z0-9_.-（固件白名单）｜media 包扩展名 .wav/.mp3/.m4a",
              foreground="#888").grid(row=4, column=0, columnspan=3, sticky="w", pady=(2, 0))

    def do_msp():
        try:
            name = v_name.get().strip() or os.path.basename(v_in.get())
            name = os.path.splitext(name)[0]
            ver = int(v_ver.get())
            with open(v_in.get(), "rb") as f:
                payload = f.read()
            blob = pack_msp(name, ver, payload)
            out = v_out.get().strip() or name + ".msp"
            with open(out, "wb") as f:
                f.write(blob)
            say("msp ok: %s | 头 50 B + payload %d B = %d B | crc=0x%08X"
                % (out, len(payload), len(blob), crc32(payload)))
            messagebox.showinfo("完成", "打包成功:\n%s" % os.path.abspath(out))
        except Exception as ex:
            say("msp 失败: %s" % ex, ok=False)
            messagebox.showerror("错误", str(ex))

    ttk.Button(f1, text="打 包", command=do_msp).grid(row=5, column=1, sticky="we", pady=12)
    f1.columnconfigure(1, weight=1)

    # ---- 页 2: Store 清单 ----
    f2 = ttk.Frame(nb, padding=14)
    nb.add(f2, text=" store 清单（manifest.json） ")
    v_url = tk.StringVar()
    v_mout = tk.StringVar(value="manifest.json")
    files = []

    ttk.Label(f2, text="源地址 base_url").grid(row=0, column=0, sticky="w", pady=6)
    ttk.Entry(f2, textvariable=v_url, width=52).grid(row=0, column=1, columnspan=2, sticky="we", padx=6)
    ttk.Label(f2, text="文件列表").grid(row=1, column=0, sticky="nw", pady=6)
    lb = tk.Listbox(f2, height=8, font=("Consolas", 9))
    lb.grid(row=1, column=1, sticky="nwe", padx=6)

    def add_files():
        for p in filedialog.askopenfilenames():
            if p not in files:
                files.append(p)
                lb.insert("end", os.path.basename(p))

    def rm_sel():
        for i in reversed(lb.curselection()):
            lb.delete(i)
            files.pop(i)

    bf = ttk.Frame(f2)
    bf.grid(row=1, column=2, sticky="nw", padx=6)
    ttk.Button(bf, text="添加", command=add_files).pack(fill="x", pady=2)
    ttk.Button(bf, text="移除选中", command=rm_sel).pack(fill="x", pady=2)
    ttk.Button(bf, text="清空", command=lambda: (lb.delete(0, "end"), files.clear())).pack(fill="x", pady=2)

    ttk.Label(f2, text="清单输出").grid(row=2, column=0, sticky="w", pady=(12, 6))
    ttk.Entry(f2, textvariable=v_mout, width=52).grid(row=2, column=1, sticky="we", padx=6)

    def do_manifest():
        try:
            entries = build_manifest(v_url.get().strip(), files)
            out = v_mout.get().strip() or "manifest.json"
            with open(out, "w", encoding="utf-8") as f:
                json.dump(entries, f, ensure_ascii=False, indent=2)
            for e in entries:
                say("  %s | %d B | crc=0x%08X | %s"
                    % (e["name"], e["size"], e["crc"], e.get("type", "script")))
            say("manifest ok: %s (%d 条)" % (out, len(entries)))
            messagebox.showinfo("完成", "清单已生成:\n%s" % os.path.abspath(out))
        except Exception as ex:
            say("manifest 失败: %s" % ex, ok=False)
            messagebox.showerror("错误", str(ex))

    ttk.Button(f2, text="生成清单", command=do_manifest).grid(row=3, column=1, sticky="we", pady=10)
    f2.columnconfigure(1, weight=1)

    say("Memoria OS 打包器就绪。包名白名单 A-Za-z0-9_.-，payload 上限 1MB（固件约束）。")
    app.mainloop()


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        sys.exit(selftest())
    run_gui()
