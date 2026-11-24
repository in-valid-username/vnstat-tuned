#!/usr/bin/env python3
"""在源码目录执行独立 out-of-tree 构建并运行 make check。"""

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("--compiler", default="gcc")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--werror", action="store_true")
    parser.add_argument("--autoreconf", action="store_true",
                        help="强制重新生成 configure（本地 autotools 版本不同时慎用）")
    parser.add_argument("--no-pie", action="store_true",
                        help="仅用于 Linux Sanitizer 构建，规避部分环境 PIE 启动崩溃")
    args = parser.parse_args()
    if args.no_pie and (not args.sanitize or not sys.platform.startswith("linux")):
        parser.error("--no-pie 仅支持 Linux 上的 --sanitize 模式")
    repo_dir = Path(__file__).resolve().parent.parent
    source = repo_dir if (repo_dir / "configure.ac").exists() else Path.cwd().resolve()
    build = args.build.resolve()
    build.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(CC=args.compiler, TZ="UTC", LC_ALL="C")
    flags = "-O2 -g -Wall -Wextra"
    if args.sanitize:
        flags = (
            "-O1 -g -Wall -Wextra -fsanitize=address -fsanitize-address-use-after-scope "
            "-fno-omit-frame-pointer -fsanitize=undefined -fsanitize=nullability "
            "-fsanitize=implicit-conversion -fsanitize=integer -fsanitize=float-divide-by-zero"
        )
        env["LDFLAGS"] = "-fsanitize=address,undefined"
        env["ASAN_OPTIONS"] = "detect_leaks=0:strict_string_checks=1:halt_on_error=1"
        env["UBSAN_OPTIONS"] = "print_stacktrace=1:print_summary=1:halt_on_error=1"
        if args.no_pie:
            flags += " -fno-pie"
            env["LDFLAGS"] += " -no-pie"
    env["CFLAGS"] = flags
    configure_cmd = [str(source / "configure")]
    if args.werror:
        configure_cmd.append("--enable-werror")

    with (build / "verification.log").open("w") as log:
        def run(command, cwd):
            log.write("$ " + " ".join(map(str, command)) + "\n")
            log.flush()
            result = subprocess.run(command, cwd=cwd, env=env, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode:
                print((build / "verification.log").read_text()[-16000:])
                raise SystemExit(result.returncode)

        if args.autoreconf or not (source / "configure").exists():
            run(["autoreconf", "-fi"], source)
        run(configure_cmd, build)
        run(["make", "-j2"], build)
        run(["make", "check"], build)
    print((build / "test.log").read_text()[-2000:])
    print("Log: " + str(build / "verification.log"))


if __name__ == "__main__":
    main()
