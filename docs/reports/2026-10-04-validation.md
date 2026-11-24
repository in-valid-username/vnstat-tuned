# 2026-10-04 分钟级账期验证记录

- 日期：2026-10-04
- 对应提交：`f0cb7b9828c4d518bc5fa8b8fd474c7ea23b5f7d`
- 测试环境：WSL2 Ubuntu 22.04（x86-64），GCC 11.4、Clang 14、SQLite 3.37.2、GD 2.3、Perl 5.34、PHP 8.1

当时基于 [vnStat v2.13](../reference/baseline.md) 原版（589 项自带检查通过），手工走查了新增修改涉及的全部 C 源码、单元测试、CGI/PHP 脚本和配置模板，然后跑了一轮完整回归。

## 实测结果

- **C 单元测试**：GCC、Clang 与 Clang ASan/UBSan 构建全部通过 861 项检查（0 失败、0 错误，覆盖 UTC 与 UTC+8 时区；Sanitizer 日志无诊断报错）。
- **守护进程联调**：按 `24` 分非 5 分钟整点配置，从上游创建的数据库启动，在独立网络命名空间里走 `veth` 跑通流量采集；经过 `SIGHUP` 重载与重启后 `rx`/`tx` 持续增长，原版 CLI 读出来的总流量完全一致，JSON/XML 与 PNG 图片均可正常生成和解析，SQLite `integrity_check` 为 `ok` 且表结构未改。
- **脚本与输出解析**：输出解析回归 11 项通过（覆盖 0..59 全部分钟以及逗号小数 locale）；当时还带了一组 21 项 CGI/PHP 检查一并通过；`make distcheck` 构建、测试、安装、卸载全过。

## 场景说明

单元测试把 0..59 全部合法分钟在直接 SQL 查询与 `processifinfo`/缓存刷盘路径上都过了一遍，验证了跨边界按采样起点归属、闰年、UTC、夏令时、导入数据、不完整月、缺失整 5 分钟格子的百分位计算以及历史新加坡时区，还验证了二次空刷盘不重复累计。

## Clang 14 PIE 与 ASan 启动踩坑说明

在当时那台 WSL2 + Clang 14 机器上，开启 PIE 的 Sanitizer 测试程序会在进 `main` 之前随机报 SIGSEGV（stderr 为空）。拿一个只调 `puts` 的极简程序复测，PIE 下 30 次崩了 14 次，加上 `-fno-pie` 后 30 次全过。因为原版部分单元测试没有在退出前释放全局/缓存内存，测试脚本在开启 Sanitizer 时固定传 `--no-pie` 并设置 `ASAN_OPTIONS=detect_leaks=0`。复现命令见 [编译与测试说明](../build-and-test.md)。
