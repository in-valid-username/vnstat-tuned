#!/usr/bin/env python3
"""在隔离的 Linux 网络命名空间里跑真实 vnstatd 采集、重载、重启与上下游兼容测试。

必须以 root 配合 unshare --net 执行，禁止直接在宿主机默认网络命名空间运行。
依赖 iproute2、util-linux、Python 3，以及本分支和上游原版两个编译目录。
"""

import argparse
import json
import os
from pathlib import Path
import signal
import socket
import sqlite3
import subprocess
import tempfile
import time
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--upstream-build", required=True, type=Path)
    parser.add_argument("--outdir", required=True, type=Path)
    args = parser.parse_args()
    if os.geteuid() != 0:
        parser.error("需要在隔离网络命名空间中以 root 身份运行")
    if Path("/proc/self/ns/net").readlink() == Path("/proc/1/ns/net").readlink():
        parser.error("拒绝在宿主机网络命名空间运行，请使用 unshare --net")
    build, upstream = args.build.resolve(), args.upstream_build.resolve()
    outdir = args.outdir.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TZ="America/New_York", LC_ALL="C")
    processes = []
    with tempfile.TemporaryDirectory(prefix="vnstat-smoke-") as tmp:
        root = Path(tmp)
        config, oldconfig = root / "vnstat.conf", root / "upstream.conf"
        base = (f'DatabaseDir "{root}"\nUseUTC 1\nUseLogging 0\n'
                'UpdateInterval 2\nPollInterval 2\nSaveInterval 1\n'
                'OfflineSaveInterval 1\nTimeSyncWait 0\nBandwidthDetection 0\n'
                'MaxBandwidth 1000\nMonthRotate 7\n')
        oldconfig.write_text(base)
        config.write_text(base + 'MonthRotateHour 18\nMonthRotateMinute 24\n')
        log = (outdir / "commands.log").open("w")

        def run(command, capture=False):
            log.write("$ " + " ".join(map(str, command)) + "\n")
            log.flush()
            return subprocess.run(list(map(str, command)), env=env, check=True,
                                  stdout=subprocess.PIPE if capture else log,
                                  stderr=log, timeout=30).stdout

        def cli(binary, cfg, *extra):
            return run([binary, "--config", cfg, "-i", "billing0", *extra], True)

        def stop(process):
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)

        def totals():
            with sqlite3.connect(root / "vnstat.db") as db:
                return db.execute("SELECT rxtotal, txtotal FROM interface WHERE name='billing0'").fetchone()

        def daemon():
            process = subprocess.Popen([str(build / "vnstatd"), "--config", str(config),
                                        "--nodaemon", "--noadd", "--noremove"],
                                       env=env, stdout=log, stderr=log)
            processes.append(process)
            time.sleep(4)
            if process.poll() is not None:
                raise RuntimeError("守护进程在流量采集前提前退出")
            return process

        def traffic():
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
                sock.settimeout(2)
                for _ in range(300):
                    payload = b"v" * 1200
                    sock.sendto(payload, ("172.31.254.2", 59001))
                    answer, _ = sock.recvfrom(2048)
                    if answer != payload:
                        raise RuntimeError("UDP 回环数据包内容不一致")
                    time.sleep(0.005)
            time.sleep(5)

        try:
            peer = subprocess.Popen(["unshare", "--net", "sleep", "180"], stdout=log, stderr=log)
            processes.append(peer)
            time.sleep(0.3)
            run(["ip", "link", "add", "billing0", "type", "veth", "peer", "name", "billing1"])
            run(["ip", "link", "set", "billing1", "netns", peer.pid])
            run(["ip", "addr", "add", "172.31.254.1/30", "dev", "billing0"])
            run(["ip", "link", "set", "billing0", "up"])
            run(["nsenter", "-t", peer.pid, "-n", "ip", "addr", "add", "172.31.254.2/30", "dev", "billing1"])
            run(["nsenter", "-t", peer.pid, "-n", "ip", "link", "set", "billing1", "up"])
            echo_code = ("import socket\ns=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)\n"
                         "s.bind(('172.31.254.2',59001))\nwhile True:\n"
                         " data,addr=s.recvfrom(2048)\n s.sendto(data,addr)\n")
            receiver = subprocess.Popen(["nsenter", "-t", str(peer.pid), "-n", "python3", "-c", echo_code],
                                        stdout=log, stderr=log)
            processes.append(receiver)
            run([upstream / "vnstatd", "--config", oldconfig, "--initdb", "--noadd"])
            cli(upstream / "vnstat", oldconfig, "--add")
            with sqlite3.connect(root / "vnstat.db") as db:
                schema_before = db.execute("SELECT type,name,sql FROM sqlite_master ORDER BY name").fetchall()
            before = totals()
            first = daemon()
            traffic()
            first.send_signal(signal.SIGHUP)
            time.sleep(3)
            traffic()
            stop(first)
            after_reload = totals()
            second = daemon()
            traffic()
            stop(second)
            after_restart = totals()
            if not all(b < a < r for b, a, r in zip(before, after_reload, after_restart)):
                raise AssertionError((before, after_reload, after_restart))
            if min(after_reload) < 700000 or min(after_restart) < 1050000:
                raise AssertionError("重载或重启后采样流量偏低，可能有漏采")
            data = json.loads(cli(build / "vnstat", config, "--json"))
            if data["monthrotate"] != 7 or data["monthrotatehour"] != 18 or data["monthrotateminute"] != 24:
                raise AssertionError("JSON 顶层账期字段不匹配")
            if data["interfaces"][0]["monthrotatehour"] != 18 or data["interfaces"][0]["monthrotateminute"] != 24:
                raise AssertionError("JSON interface 账期字段不匹配")
            xml = cli(build / "vnstat", config, "--xml")
            ET.fromstring(xml)
            (outdir / "output.json").write_text(json.dumps(data, indent=2) + "\n")
            (outdir / "output.xml").write_bytes(xml)
            png = outdir / "summary.png"
            run([build / "vnstati", "--config", config, "-i", "billing0", "-s", "-o", png])
            if png.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n" or png.stat().st_size < 500:
                raise AssertionError("生成的摘要 PNG 无效")
            legacy = json.loads(cli(upstream / "vnstat", oldconfig, "--json"))
            if data["interfaces"][0]["traffic"]["total"] != legacy["interfaces"][0]["traffic"]["total"]:
                raise AssertionError("上游原版读取的总量与本分支不一致")
            with sqlite3.connect(root / "vnstat.db") as db:
                schema_after = db.execute("SELECT type,name,sql FROM sqlite_master ORDER BY name").fetchall()
                integrity = db.execute("PRAGMA integrity_check").fetchone()[0]
            if schema_before != schema_after or integrity != "ok":
                raise AssertionError("数据库结构发生变化或完整性校验失败")
            results = dict(before=before, after_reload=after_reload, after_restart=after_restart,
                           schema_unchanged=True, integrity=integrity, upstream_totals_match=True,
                           json_parsed=True, xml_parsed=True, image_bytes=png.stat().st_size,
                           useutc=1, process_timezone=env["TZ"], network="isolated veth UDP echo")
            (outdir / "results.json").write_text(json.dumps(results, indent=2) + "\n")
            print(json.dumps(results, indent=2))
        finally:
            for process in reversed(processes):
                stop(process)
            log.close()


if __name__ == "__main__":
    main()
