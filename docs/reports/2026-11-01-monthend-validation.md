# 2026-11-01 月底账期（29–31 日）验证记录

- 基线提交：`24cecda65b657dbc1270d64e7e42306c134ccff3`
- 测试环境：Ubuntu 22.04.5、glibc 2.35、GCC 11.4、Clang 14、SQLite 3.37.2、Check 0.15.2、GD 2.3.0

这轮把 `MonthRotate` 放开到 `1..31`，短月取当月最后一天并保留原来的时分，到下个月再切回原定日期（详细规则见 [计费边界与数据库实现](../billing.md)）。

## 测试对比

| 检查项 | 结果 |
| --- | --- |
| 原 861 项基线（GCC） | 861 项通过 |
| 新测试跑在旧实现上 | 999 项中 71 项失败、0 错误，复现旧版 29–31 日上限与月标签偏移问题 |
| 新实现 GCC 完整回归 | 1001 项通过，0 失败、0 错误 |
| 新实现 Clang ASan/UBSan 回归 | 1001 项通过，0 失败、0 错误 |
| JSON/XML 与小数逗号 locale | 11 项通过 |

覆盖矩阵包括 `1..31` 日、`00:00 / 18:24 / 23:59`、跨年、30 天小月、平闰年 2 月以及 `2000` / `2100` 跨世纪规则，在 `UTC`、`Asia/Singapore`、`America/New_York`、`Europe/Berlin` 四个时区下分别测了 `UseUTC 0/1`。每个边界检查了切点前后 1 分钟、前后 1 秒和切点整秒的数据归属、月标签、缓存刷盘、年表、导入导出与 95 百分位窗口，并针对柏林夏令时跳空和重叠的一小时做了单独校验。

对于 `1..28` 日 `00:00`，测试专门比对了原先冻结下来的 SQLite 表达式，确保旧账期的历史分月习惯没有改变。

## 复现方式

```sh
python3 tests/run_fork_checks.py /var/tmp/vnstat-monthend-gcc
python3 tests/run_fork_checks.py /var/tmp/vnstat-monthend-clang \
  --compiler clang --sanitize --no-pie
python3 tests/output_parse_tests.py /var/tmp/vnstat-monthend-gcc/vnstat \
  --comma-locale de_DE.utf8
```
