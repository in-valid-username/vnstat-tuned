# vnstat-tuned

基于上游 `vnStat 2.14` 维护的流量计费分支，默认分支为 `billing-v2.14`。

原版 `vnStat` 的月度结算只支持选定 `MonthRotate` 日期的 `00:00`（且 `MonthRotate` 仅允许 `1..28`），遇到运营商按特定时分结账、或者账单日落在每月 29～31 日时不够用。这个分支在不改 SQLite 表结构的前提下，补齐了分钟级结账边界和月末截断逻辑，同时保留原版所有采集、查询、画图和导出能力。

## 相对原版 2.14 的主要改动

- **分钟级结算时刻**：新增 `MonthRotateHour`（`0..23`，默认 `0`）与 `MonthRotateMinute`（`0..59`，默认 `0`）。当分钟不是 5 的整数倍时，守护进程 `vnstatd` 内存缓存按 1 分钟分桶，保证跨边界流量分账准确；落盘的 5 分钟表仍按 5 分钟粒度写入。
- **月底账单日 `1..31`**：放宽 `MonthRotate` 范围到 `1..31`。遇到没有那一天的短月（例如设了 `31` 号遇到 2 月、4 月），自动落到当月最后一天的同一时刻；到 3 月重新按 `31` 号切账。其中 `1..28` 号 `00:00` 继续走原版 SQLite 表达式。
- **统计口径对齐**：月估算、已过秒数、历史整月平均速率、95 百分位统计窗口（只纳入完整落在本账期内的 5 分钟槽）、文本/图片账期提示以及 Prometheus 导出脚本 `examples/vnstat-metrics.cgi` 均与上述边界一致。
- **JSON / XML 双位置兼容字段**：在保持 `vnStat 2.14` 每个 `interface` 下输出计费字段的基础上，同时保留顶层全局字段与每个接口下独立的 `monthrotatehour`、`monthrotateminute`、`useutc`，方便旧消费脚本与 2.14 下游工具平滑兼容。
- **顺带修掉的几个小问题**：修复逗号小数地区设置下的 JSON 百分比格式、XML `<percentile_95>` 合法标签名及均值运算优先级、`db_setinfo`/`db_removeoldentries`/`dbmerge` 回滚等零星缺陷；详细清单见 [分支对照说明](docs/fork-review.md)。

## 快速配置

在 `/etc/vnstat.conf`（或 `vnstat.conf(5)` 支持的用户配置目录）中按需设置：

```conf
MonthRotate 31
MonthRotateHour 18
MonthRotateMinute 24
MonthRotateAffectsYears 1
MonthRotateVisible 1
```

如果 `vnstatd` 与本地 `vnstat` 查询进程不在同一个默认时区，两边需要保持相同的 `vnstat.conf` 与 `UseUTC` / 系统时区设置。更改 `MonthRotate*` 只影响之后的增量统计和按当前配置解释的数据归属，不会重算历史已写入的表记录。

## 构建与回归

常规构建与自检：

```sh
./configure --enable-werror
make -j$(nproc)
make check
```

从上游 `v2.14` 源码打出的源码包里已经带齐 `configure` 和 `Makefile.in`。除非改动了 `configure.ac` 或 `Makefile.am` 且本机安装了对应版本的 Autotools（`2.14` 上游使用 Automake 1.17 / Autoconf 2.72），一般不要直接执行 `autoreconf -fi`。

分支额外的输出解析与隔离守护进程测试：

```sh
python3 tests/run_fork_checks.py /tmp/vnstat-gcc --werror
python3 tests/output_parse_tests.py /tmp/vnstat-gcc/vnstat --comma-locale de_DE.UTF-8
```

## 文档目录

- [计费边界与数据库实现](docs/billing.md)
- [文本、图表、JSON/XML 与 Prometheus 输出](docs/output.md)
- [编译与测试说明](docs/build-and-test.md)
- [与上游 2.14 的差异对照](docs/fork-review.md)
- [基线与上游原始 README](docs/reference/baseline.md)（上游原版见 [upstream-readme.txt](docs/reference/upstream-readme.txt)）
- 验证记录：[2026-10-04](docs/reports/2026-10-04-validation.md)、[2026-10-10](docs/reports/2026-10-10-update-validation.md)、[2026-11-01](docs/reports/2026-11-01-monthend-validation.md)、[2026-11-24](docs/reports/2026-11-24-v2.14-sync.md)
