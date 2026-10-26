#!/usr/bin/env python3
# SkinPen V0.2 Mac 端接收脚本 —— 实时显示 / 备份 / QC
#
# 协议 (USB CDC JSONL, 与固件 measurement.c / main.c 一致):
#   {"type":"hello","device":..,"firmware":..,"cards":N,"sd":bool,"as7341":bool}
#   {"type":"start","seq":N,"code":"..","timestamp":".."}
#   {"type":"sample","seq":N,"code":"..","repeat":R,"f1":..,...,"nir":..}
#   {"type":"saved","seq":N,"code":"..","file":"..","status":"PASS|FAIL"}
#   {"type":"cancelled","seq":N,"code":".."}
#
# 用法:
#   python3 skinpen_receiver.py                 # 自动探测 /dev/cu.usbmodem*
#   python3 skinpen_receiver.py -p /dev/cu.usbmodem21101
#   python3 skinpen_receiver.py -b ./skinpen_backup --cv-warn 0.03 -v
#
# 依赖: pip3 install pyserial
# 功能:
#   1. 实时: 每帧打印进度(repeat/10), 保存后打印 CVmax/文件名
#   2. 备份: 全部 JSONL 追加到 backup/skinpen_YYYYMMDD.jsonl,
#            每条测量另存 backup/TF文件名.txt (与 TF 卡 TXT 内容对应)
#   3. QC:   CVmax 超阈值(默认 0.05)打印 [QC-WARN];
#            seq 序列缺失/重复、均值饱和(>=60000)提示
# Ctrl+C 退出并打印会话统计。

import argparse
import glob
import json
import math
import os
import sys
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial: pip3 install pyserial")

BAUD = 115200
CH_NAMES = ["f1", "f2", "f3", "f4", "f5", "f6", "f7", "f8", "clear", "nir"]


def detect_port():
    ports = sorted(set(glob.glob("/dev/cu.usbmodem*")))
    if not ports:
        sys.exit("未找到 /dev/cu.usbmodem* 设备,请检查 USB 连接")
    if len(ports) == 1:
        return ports[0]
    print("发现多个设备:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p}")
    k = int(input("选择序号: "))
    return ports[k]


def stats(rows):
    """10 通道 mean/std/cv(与固件 compute_stats 相同: 总体方差)"""
    n = len(rows)
    mean, std, cv = [], [], []
    for ch in range(10):
        vals = [r[ch] for r in rows]
        m = sum(vals) / n if n else 0.0
        var = sum((v - m) ** 2 for v in vals) / n if n else 0.0
        s = math.sqrt(var)
        mean.append(m)
        std.append(s)
        cv.append(s / m if m > 1.0 else 0.0)
    return mean, std, cv


class Session:
    def __init__(self, backup_dir, cv_warn):
        self.backup_dir = backup_dir
        self.cv_warn = cv_warn
        self.n_meas = 0        # saved(含 FAIL)条数
        self.n_pass = 0
        self.n_warn = 0
        self.seqs = []
        self.jsonl_path = None
        self.cur = None        # 当前采集中记录
        if backup_dir:
            os.makedirs(backup_dir, exist_ok=True)
            self.jsonl_path = os.path.join(
                backup_dir, f"skinpen_{datetime.now():%Y%m%d}.jsonl")

    # ---- 事件处理 ----
    def on_hello(self, obj, jf):
        sim = " [SIM MODE - demo 数据]" if obj.get("sim") else ""
        print(f"[HELLO] {obj.get('device')} fw={obj.get('firmware')} "
              f"cards={obj.get('cards')} sd={obj.get('sd')} as7341={obj.get('as7341')}{sim}")

    def on_start(self, obj, jf):
        self.cur = {
            "type": "meas", "seq": obj.get("seq", 0),
            "code": obj.get("code", "?"), "ts": obj.get("timestamp", ""),
            "repeats": [],
        }
        print(f"\n[{datetime.now():%H:%M:%S}] START  #{obj.get('seq', 0):3d} "
              f"{obj.get('code', '?'):8s} {obj.get('timestamp', '')}")

    def on_sample(self, obj, jf):
        if self.cur is None:               # 丢掉 start(如中途接入),补建
            self.cur = {"type": "meas", "seq": obj.get("seq", 0),
                        "code": obj.get("code", "?"), "ts": "", "repeats": []}
        row = [obj.get(c, 0) for c in CH_NAMES]
        self.cur["repeats"].append(row)
        print(f"  sample {obj.get('repeat', len(self.cur['repeats'])):2d}/10  "
              + " ".join(f"{v:5d}" for v in row))
        if jf:
            jf.write(json.dumps({"type": "sample", **{c: obj.get(c, 0) for c in CH_NAMES},
                                 "seq": obj.get("seq"), "code": obj.get("code"),
                                 "repeat": obj.get("repeat")}, ensure_ascii=False) + "\n")
            jf.flush()

    def on_saved(self, obj, jf):
        m = self.cur or {"seq": obj.get("seq", 0), "code": obj.get("code", "?"),
                         "ts": "", "repeats": []}
        m["file"] = obj.get("file", "")
        m["status"] = obj.get("status", "?")
        self.cur = None

        self.n_meas += 1
        seq = m.get("seq")
        self.seqs.append(seq)
        rows = m.get("repeats", [])
        mean, std, cv = stats(rows) if rows else ([], [], [])
        cv_max = max(cv) if cv else 0.0
        m["mean"], m["std"], m["cv"], m["cv_max"] = mean, std, cv, cv_max

        ok = m["status"] == "PASS"
        if ok:
            self.n_pass += 1
        warn = cv_max > self.cv_warn
        if warn:
            self.n_warn += 1
        flag = " [QC-WARN]" if warn else ""
        print(f"[{datetime.now():%H:%M:%S}] SAVED  #{seq:3d} {m.get('code', '?'):8s} "
              f"n={len(rows)} CVmax={cv_max:.3f} status={m['status']}{flag} "
              f"-> {m.get('file', '') or '(未保存)'}")

        # 均值饱和检查(AS7341 16bit, >=60000 视为饱和)
        sat = [CH_NAMES[i] for i, v in enumerate(mean) if v >= 60000]
        if sat:
            print(f"    [QC-SAT] 饱和通道: {', '.join(sat)} (建议降低 GAIN)")

        if jf:
            jf.write(json.dumps(m, ensure_ascii=False) + "\n")
            jf.flush()
        self.save_txt(m)

    def on_cancelled(self, obj, jf):
        self.cur = None
        print(f"[{datetime.now():%H:%M:%S}] CANCEL #{obj.get('seq', 0):3d} "
              f"{obj.get('code', '?')}")

    # ---- TXT 备份(与 TF 卡 SKINPEN_RECORD_V1 字段对应,统计由本机复算) ----
    def save_txt(self, m):
        if not self.backup_dir or not m.get("file"):
            return
        path = os.path.join(self.backup_dir, m["file"])
        n = 0
        stem, _ = os.path.splitext(path)
        while os.path.exists(path):
            n += 1
            path = f"{stem}_{n}.txt"
        with open(path, "w", encoding="utf-8") as f:
            f.write("# SKINPEN_RECORD_V1\n")
            f.write(f"timestamp={m.get('ts', '')}\n")
            f.write(f"session_id=\n")
            f.write("device_id=SKINPEN_V01\n")
            f.write("firmware=0.2.0\n")
            f.write("sensor=AS7341-V2\n")
            f.write(f"card_seq={m.get('seq', 0):03d}\n")
            f.write(f"card_code={m.get('code', '')}\n")
            f.write(f"card_name_en=\n")
            f.write("columns=repeat,f1,f2,f3,f4,f5,f6,f7,f8,clear,nir\n")
            for i, row in enumerate(m.get("repeats", [])):
                f.write(str(i + 1) + "," + ",".join(str(v) for v in row) + "\n")
            f.write("summary_mean=" + ",".join(f"{v:.1f}" for v in m.get("mean", [])) + "\n")
            f.write("summary_std=" + ",".join(f"{v:.2f}" for v in m.get("std", [])) + "\n")
            f.write("summary_cv=" + ",".join(f"{v:.4f}" for v in m.get("cv", [])) + "\n")
            f.write(f"summary_cv_max={m.get('cv_max', 0.0):.4f}\n")
            f.write(f"status={m.get('status', '')}\n")

    def summary(self):
        print("\n---- 会话统计 ----")
        print(f"测量条数: {self.n_meas}  PASS: {self.n_pass}  QC 警告: {self.n_warn}")
        if self.seqs:
            missing = []
            uniq = sorted(set(self.seqs))
            for a, b in zip(uniq, uniq[1:]):
                missing.extend(range(a + 1, b))
            if missing:
                print(f"[QC] seq 缺失: {missing}")
            dup = len(self.seqs) - len(uniq)
            if dup:
                print(f"[QC] seq 重复 {dup} 条")
        if self.jsonl_path:
            print(f"备份: {self.jsonl_path}")


def main():
    ap = argparse.ArgumentParser(description="SkinPen V0.2 USB CDC 接收/备份/QC")
    ap.add_argument("-p", "--port", help="串口设备, 默认自动探测")
    ap.add_argument("-b", "--backup", default="./skinpen_backup", help="备份目录(空串禁用)")
    ap.add_argument("--cv-warn", type=float, default=0.05, help="CVmax 告警阈值")
    ap.add_argument("-v", "--verbose", action="store_true", help="打印 ESP-IDF 日志行")
    args = ap.parse_args()

    port = args.port or detect_port()
    print(f"监听 {port} @ {BAUD} (Ctrl+C 退出)  备份目录: {args.backup or '(禁用)'}")

    sess = Session(args.backup, args.cv_warn)
    ser = serial.Serial(port, BAUD, timeout=2)
    jf = sess.open_jsonl()
    try:
        while True:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            if line.startswith("{"):
                try:
                    obj = json.loads(line)
                except json.JSONDecodeError:
                    print(f"[RAW] {line[:120]}")
                    continue
                t = obj.get("type")
                if t == "start":
                    sess.on_start(obj, jf)
                elif t == "sample":
                    sess.on_sample(obj, jf)
                elif t == "saved":
                    sess.on_saved(obj, jf)
                elif t == "cancelled":
                    sess.on_cancelled(obj, jf)
                elif t == "hello":
                    sess.on_hello(obj, jf)
            elif args.verbose:
                print(f"[LOG] {line[:160]}")
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()
        if jf:
            jf.close()
        sess.summary()


if __name__ == "__main__":
    main()
