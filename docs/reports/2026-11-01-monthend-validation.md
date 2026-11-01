# 月底账期验证 2026-11-01

基线为 `24cecda65b657dbc1270d64e7e42306c134ccff3`。此次实现支持每月 29–31 日的账期，短月取该月最后一天并保留时分，下个月重新按原约定日计算。配置和实现入口见[账期指南](../billing.md#月底日期)。

## 测试

环境为 Ubuntu 22.04.5、glibc 2.35、GCC 11.4、Clang 14、SQLite 3.37.2、Check 0.15.2、GD 2.3.0。

| 项目 | 结果 |
| --- | --- |
| 基线完整 GCC 回归 | 861 项通过 |
| 新增测试搭配旧实现 | 999 项中 71 项失败，0 错误，复现月底限制及标签问题 |
| 最终 GCC 完整回归 | 1001 项通过，0 失败、0 错误 |
| 最终 Clang ASan/UBSan 回归 | 1001 项通过，0 失败、0 错误 |
| JSON/XML 与小数逗号 locale | 11 项通过，无跳过 |

矩阵覆盖 1–31 日、00:00/18:24/23:59、跨年、30 天月份、平闰年二月和 2000/2100 年世纪规则；UTC、Asia/Singapore、America/New_York、Europe/Berlin 分别检查 `UseUTC 0/1`。落库检查切点前后一分钟、前后秒及切点本身的 RX/TX、月标签、缓存、年度记录、直接导入和百分位窗口；柏林缺失及重复的夏令时时刻另有专门测试。

原 1–28 日午夜 SQL 在部分夏令时月份的已有归属行为保持不变，回归测试与冻结的旧表达式逐项比较。本地夏令时边界仍由 libc 处理，相关范围见[时区与历史数据](../fork-review.md#时区与历史数据)。Sanitizer 运行器沿用 `--no-pie` 和关闭泄漏检测的设置，命令说明见[构建指南](../build-and-test.md)。

## 复现

```sh
python3 tests/run_fork_checks.py /var/tmp/vnstat-monthend-gcc
python3 tests/run_fork_checks.py /var/tmp/vnstat-monthend-clang \
  --compiler clang --sanitize --no-pie
python3 tests/output_parse_tests.py /var/tmp/vnstat-monthend-gcc/vnstat \
  --comma-locale de_DE.utf8
```

确认测试环境有 Check 和图像依赖后，检查各目录 `test.log` 的实际执行数量；仅有 `make check` 成功不足以确认全部测试已执行。不带图像输出的发布构建会减少图形测试项。

此次不改变数据库结构，不重算已有月度记录。生产升级保留旧二进制和一致性数据库备份，不执行 `--sync` 或重新初始化；新建数据库只在首次启动时同步网卡基线。最终发布构建与服务器安装记录另行保存，不把本地源码测试等同于生产验证。
