# 编译与测试说明

## 构建依赖

在 Debian / Ubuntu 上编译全套程序、PNG 画图支持与单元测试需要：

```sh
sudo apt-get install -y \
  build-essential pkg-config libsqlite3-dev libgd-dev check \
  clang python3 perl locales iproute2
```

如果不编译 `vnstati` 图像输出，可加 `./configure --disable-image-output`。

> 注意 Autotools 版本：上游 `v2.14` 源码自带的 `configure` 和 `Makefile.in` 由 Autoconf 2.72 / Automake 1.17 生成。如果本地发行版版本较旧（例如 Ubuntu 22.04 的 Automake 1.16.5），正常编译直接执行 `./configure && make` 即可，不要随意运行 `autoreconf -fi` 覆盖已跟踪文件。

## 本地快速验证

在单独目录执行打包测试与回归：

```sh
python3 tests/run_fork_checks.py /tmp/billing-gcc --werror
python3 tests/run_fork_checks.py /tmp/billing-clang --compiler clang --sanitize --werror --no-pie
```

默认会在目标目录做 out-of-tree 编译（不污染仓库根目录），依次运行：

- `./configure` 与 `make`
- `make check`（UTC 与 `TZ=Asia/Singapore` 时区下检查所有 C 测试）
- 输出日志检查（确认 0 失败、0 错误，Sanitizer 无告警）

如果需要验证不同地区小数点逗号下的 JSON/XML 导出以及 Perl 脚本语法：

```sh
python3 tests/output_parse_tests.py /tmp/billing-gcc/vnstat --comma-locale de_DE.UTF-8
perl -c examples/vnstat-metrics.cgi
```

确认源码包打包、安装、检查和卸载全流程：

```sh
cd /tmp/billing-gcc
make distcheck
```

## 隔离守护进程兼容性测试

`tests/daemon_smoke.py` 会在独立网络命名空间中拉起原版 `v2.14` 与当前分支的 `vnstatd`，构造一对 `veth` 网卡发包，验证以下几点：

1. 数据库表结构与 `v2.14` 完全一致，`PRAGMA integrity_check` 返回 `ok`。
2. 沿用上游创建的数据库启动 `billing-v2.14` 守护进程，累计字节数一致。
3. 发送 `SIGHUP` 重载、优雅退出重启后流量持续累加，`--json` / `--xml` / `vnstati` PNG 输出都可正常解析。

运行方式：

```sh
sudo unshare --net python3 tests/daemon_smoke.py /tmp/billing-gcc \
  --upstream-build /tmp/upstream-v214 --outdir /tmp/billing-smoke
```

## Sanitizer 与 PIE 说明

在部分内核 / WSL2 环境下，Ubuntu 22.04 自带的 Clang 14 默认开启 PIE 并搭配 ASan 时，程序启动进入 `main` 之前可能随机触发段错误。`tests/run_fork_checks.py --no-pie` 会关闭 PIE；另外因为上游部分单元测试没有释放测试结束后的全局/缓存分配，`run_fork_checks.py` 在 `--sanitize` 下会设置 `ASAN_OPTIONS=detect_leaks=0`。CI 上的 `ubuntu_clang_sanitizers` 保持原版配置运行。
