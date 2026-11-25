# vnstat-tuned

基于上游 `vnStat 2.14` 维护的流量计费分支，仓库默认分支为 `billing-v2.14`。

上游原版 `vnStat` 的月度结算周期仅支持每月 `1..28` 日的 `00:00`。针对按指定时刻结账或账单日位于 `29..31` 日的使用场景，本分支在不修改 SQLite 数据库表结构的前提下，扩展了分钟级结算边界与月末日期自适应规则，并完整保留上游原有的采集、查询、绘图与导出功能。

## 相对上游 2.14 的主要变更

- **分钟级账期边界**：新增 `MonthRotateHour`（`0..23`，默认 `0`）与 `MonthRotateMinute`（`0..59`，默认 `0`）。当分钟配置非 5 的整数倍时，守护进程 `vnstatd` 的内存缓存按 1 分钟粒度归并采样，确保跨账期流量切分准确；持久化 `fiveminute` 表仍维持 5 分钟分辨率。
- **月末账单日（`1..31`）**：将 `MonthRotate` 取值范围扩展至 `1..31`。若目标月份不存在该日期（例如配置为 `31` 日时遇到 2 月或 4 月），自动取当月最后一天的同一时刻作为切点，次月恢复按原配置日期计算。`1..28` 日 `00:00` 场景继续使用上游原有 SQLite 表达式，保持历史数据归属兼容。
- **统计与导出一致**：月度流量预估、账期已过时长、历史完整月平均速率、95 百分位统计窗口（仅纳入完整落在当前账期内的 5 分钟样本）、文本与图像账期说明及 `examples/vnstat-metrics.cgi` 均采用同一边界规则。
- **JSON / XML 双位置字段兼容**：在保留 `vnStat 2.14` 每个 `interface` 节点内计费字段的同时，在顶层结构与各接口节点内同步输出 `monthrotate`、`monthrotatehour`、`monthrotateminute`、`monthrotateaffectsyears` 与 `useutc`，兼顾旧版解析脚本与 2.14 下游工具。
- **其他问题修正**：包含小数逗号 locale 下的 JSON 百分比格式修正、XML `<percentile_95>` 标签命名合规化与均值运算括号修正，以及 `db_setinfo`、`db_removeoldentries`、`dbmerge` 事务回滚等细节修复，详见 [与上游 2.14 的差异对照](docs/fork-review.md)。

## 配置示例

在 `/etc/vnstat.conf`（或 `vnstat.conf(5)` 支持的用户配置路径）中按需设置：

```conf
MonthRotate 31
MonthRotateHour 18
MonthRotateMinute 24
MonthRotateAffectsYears 1
MonthRotateVisible 1
```

若 `vnstatd` 守护进程与客户端查询环境处于不同默认时区，需保持两端配置及 `UseUTC` / 时区设置一致。调整 `MonthRotate*` 配置仅作用于后续采样与当前查询口径，不会追溯重算历史持久化记录。

## 构建与测试

常规编译与基础验证：

```sh
./configure --enable-werror
make -j$(nproc)
make check
```

仓库已包含上游 `v2.14`（基于 Automake 1.17 / Autoconf 2.72）生成的 `configure` 与 `Makefile.in`。常规构建无需执行 `autoreconf -fi`；仅在修改 `configure.ac` 或 `Makefile.am` 且本机具备兼容版本 Autotools 时再重新生成。

本分支补充的输出解析及回归检查：

```sh
python3 tests/run_fork_checks.py /tmp/vnstat-gcc --werror
python3 tests/output_parse_tests.py /tmp/vnstat-gcc/vnstat --comma-locale de_DE.UTF-8
```

## 文档索引

- [计费边界与数据库实现](docs/billing.md)
- [文本、图表、JSON/XML 与 Prometheus 输出](docs/output.md)
- [编译与测试说明](docs/build-and-test.md)
- [与上游 2.14 的差异对照](docs/fork-review.md)
- [基线与版本对照](docs/reference/baseline.md)（上游原始 README 见 [upstream-readme.txt](docs/reference/upstream-readme.txt)）
- 验证记录：[2026-10-04](docs/reports/2026-10-04-validation.md)、[2026-10-10](docs/reports/2026-10-10-update-validation.md)、[2026-11-01](docs/reports/2026-11-01-monthend-validation.md)、[2026-11-24](docs/reports/2026-11-24-v2.14-sync.md)
