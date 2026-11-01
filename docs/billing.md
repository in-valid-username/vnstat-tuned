# 账期配置

月度账期由日期、小时和分钟共同确定。守护进程 `vnstatd`、查询工具 `vnstat` 和图片工具 `vnstati` 必须使用相同配置及时区。

## 参数

例如使用本地时间，每月 7 日 18:24 开始新账期：

```ini
MonthRotate 7
MonthRotateHour 18
MonthRotateMinute 24
MonthRotateAffectsYears 0
UseUTC 0
PollInterval 5
UpdateInterval 20
```

| 字段 | 作用 | 范围和默认值来源 |
| --- | --- | --- |
| `MonthRotate` | 每月起始日，接受 1–31 的整数 | [cfg.c](../src/cfg.c) 的 `validatecfg`，默认值 `MONTHROTATE` |
| `MonthRotateHour` | 起始小时 | 同上，默认值 `MONTHROTATEHOUR` |
| `MonthRotateMinute` | 起始分钟，接受 0–59 的整数 | 同上，默认值 `MONTHROTATEMINUTE` |
| `MonthRotateAffectsYears` | 年度边界也采用 1 月的账期起点 | 同上，默认值 `MONTHROTATEYEARS` |
| `UseUTC` | 使用 UTC 或进程本地时间 | 同上，默认值 `USEUTC` |

默认值定义在 [common.h](../src/common.h)，完整配置见 [vnstat.conf 手册](../man/vnstat.conf.5)。越界值由 `validateint` 或 `validatebool` 发出警告并恢复默认值。

## 时区与标签

`UseUTC 1` 按 UTC 切换；`UseUTC 0` 按进程本地时区切换。固定 UTC+8 可在所有采集和查询进程中使用 `TZ=Asia/Singapore`。前端访客时区只影响显示，不改变计量账期。

月度标签仍为 `YYYY-MM-01`。上述配置下，1 月标签覆盖 1 月 7 日 18:24 至 2 月 7 日 18:24，包含起点、不含终点。月度 SQL 标签由 [dbsql.c](../src/dbsql.c) 的 `db_get_date_generator` 生成，边界计算由 [common.c](../src/common.c) 的 `billingperiodstart` 维护。

## 月底日期

设置 29、30 或 31 日时，若当月没有该日期，就取当月最后一天，小时和分钟不变；下一月重新按配置日期计算，不沿用上月钳制后的日期。例如 `MonthRotate 31`、`MonthRotateHour 18`、`MonthRotateMinute 24` 对应 4 月 30 日 18:24、平年 2 月 28 日 18:24、闰年 2 月 29 日 18:24，3 月仍为 31 日 18:24。2 月标签覆盖钳制后的 2 月起点至 3 月配置起点，包含起点、不含终点。

29–31 日的采集标签通过 [dbsql.c](../src/dbsql.c) 的 `db_billing_monthlabel` 复用 `billingperiodstart`，与周期长度、估算和 95 百分位窗口采用同一边界。`UseUTC` 的含义不变；本地时间遇到夏令时缺失或重复时刻，仍由 `mktime` 和 `tm_isdst = -1` 处理。年度边界仅在 `MonthRotateAffectsYears 1` 时使用 1 月的配置日期和时分。

上游 v2.13 的 [validatecfg](https://github.com/vergoh/vnstat/blob/v2.13/src/cfg.c) 只接受 1–28 日；月底钳制是本分支新增的兼容行为，不是恢复上游已有功能。原 1–28 日的 SQL 归属路径不变。

`UseUTC 1` 的查询工具宜同时设置 `TZ=UTC`。旧数据库的日历时间表示在夏令时重复或缺失的时刻，以及部分历史时区偏移下存在歧义；适用范围和相关回归见[修复与兼容性](fork-review.md#时区与历史数据)。

## 采样与保存

非五分钟整点的账期使用一分钟内存缓存；五分钟整点保留原有缓存粒度。分桶由 [datacache.c](../src/datacache.c) 的 `xferlog_add` 维护。写入 SQLite 的五分钟表仍保持五分钟分辨率，月度汇总分别累计边界两侧的缓存。

采集按网卡计数器差分，一个采样间隔的流量归入该间隔起点。跨越账期边界的采样无法按数据包时刻拆分。保持较短的 `PollInterval` 和 `UpdateInterval`，避免数分钟的采集间隔扩大边界误差；`SaveInterval` 控制数据库写入频率。

95 百分位只使用完整落在账期内的五分钟样本。例如 18:24 开始的账期，首个完整样本从 18:25 开始。部分重叠样本被排除，但对应流量仍计入月度总量。新账期尚无完整样本时，百分位查询返回无可用数据。窗口对齐由 [percentile.c](../src/percentile.c) 的 `percentilegridcutoff` 和 `getpercentiledata` 维护。

## 升级与切换

原有五分钟整数倍设置继续有效，任意合法分钟均不取整。数据库无需结构迁移，修改配置也不会重算已有月度记录。

1. 备份配置和数据库，选定切换边界。
2. 在同一维护窗口修改采集与查询配置，核对各进程时区。
3. 重启守护进程，检查日志和 JSON 中的账期字段。
4. 核对累计 RX/TX 连续，保留切换前后的记录。

同一数据库只运行一个采集守护进程。旧二进制可读取原有数据库总量，但不理解新增的小时、分钟语义，不能交替用于采集。其他上游升级步骤见 [UPGRADE.md](../UPGRADE.md)。
