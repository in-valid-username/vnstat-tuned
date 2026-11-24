# 计费边界与数据库实现

`vnstat-tuned` 不改动 SQLite 表结构，也不改动历史表数据格式，只调整“流量和样本怎么按账期切分”。所有逻辑集中在时间换算、SQL 分组以及守护进程采样缓存三处。

## 配置项

| 配置项 | 取值范围 | 默认值 | 说明 |
| --- | --- | --- | --- |
| `MonthRotate` | `1..31` | `1` | 每月切账日；短月无该日时取当月最后一天 |
| `MonthRotateHour` | `0..23` | `0` | 切账小时 |
| `MonthRotateMinute` | `0..59` | `0` | 切账分钟，精确到 1 分钟 |
| `MonthRotateAffectsYears` | `0..1` | `0` | 设为 `1` 时，年表也按配置中的月/日/时/分切账 |
| `MonthRotateVisible` | `0..2` | `1` | 控制文本与图片输出底部是否显示账期起算提示（`0` 不显示，`1` 非默认值时显示，`2` 总是显示） |
| `UseUTC` | `0..1` | `0` | 为 `1` 时按 UTC 日历计算账期，否则按进程本地时区计算 |

`validatecfg` 对超出范围的 `MonthRotate`、`MonthRotateHour`、`MonthRotateMinute` 会打警告并回退到默认值，不会静默四舍五入。

## 日历与边界计算

核心逻辑在 [src/common.c](../src/common.c) 的 `billingperiodstart` 与 `billingcalendartime`：

1. **先归月份，再截天数**：算上一期或下一期切点时，先把 `tm_mon` 调整并进位/借位归入 `0..11`，再按目标月份的实际天数把 `MonthRotate` 截到 `lastday`（含闰年 2 月规则）。这样可以避免 1 月 31 日加 1 个月直接喂给 `mktime` 溢出串到 3 月。
2. **时区一致**：开启 `UseUTC` 时，所有日历切点通过 `timegm` 得出 UTC 时间戳；关闭时通过 `mktime`（`tm_isdst = -1`）交给 libc 按本地规则处理夏令时。
3. **兼容数据库日历时间**：`db_getdata` / `db_getdata_range` 用 SQLite `strftime('%s', date, 'utc')` 返回时间戳；当 `UseUTC = 1` 时，库里存的其实是 UTC 墙钟文本，经过 `'utc'` 修饰后需要用 `billingcalendartime`（`localtime_r` + `timegm`）还原回真实 UTC epoch 再做周期比较和长度计算。

## SQLite 分月策略

[src/dbsql.c](../src/dbsql.c) 的 `db_get_date_generator` 按配置走三条路径：

- **`MonthRotate` 为 `1..28` 且 `00:00`**：继续使用原版 SQLite `datetime(...)` 偏移表达式，保持与上游完全一致的账期切分（包括 SQLite 在远期年份本地时区换算的历史表现）。
- **`MonthRotate` 为 `1..28` 且非零点时刻**：在原偏移基础上减去 `MonthRotateHour` 和 `MonthRotateMinute`，再截成 `%Y-%m-01`。
- **`MonthRotate` 为 `29..31`**：因为 SQLite 自带的 `-30 days` 无法在 28/29/30/31 天的月份间准确对齐月底，SQLite 连接初始化时注册确定性标量函数 `vnstat_monthlabel`（[db_billing_monthlabel](../src/dbsql.c)），直接调用 C 侧 `billingperiodstart` 判断记录属于上月还是当月。

合并多网卡（例如 `eth0+eth1`）以及导入历史数据时，各子查询按同样的月份标签分组求和。

## 守护进程采样与缓存

- **缓存分桶粒度**：当 `MonthRotateMinute % 5 != 0` 时，[src/daemon.c](../src/daemon.c) 的 `processifinfo` 把内存中还没落盘的采样按 60 秒（1 分钟）归并；能被 5 整除时沿用 300 秒（5 分钟）。跨账期那一分钟的内核增量计数器由于拿不到逐包时间戳，仍随该采样区间起点归属，但不会把切点前后的几分钟全部混进同一个 5 分钟内存格里。
- **启动缓存恢复**：`initcachevalues` 通过 `db_getinterfaceinfo_epoch` 读取接口最后更新时间戳的真实 Unix 秒数，避免小时/分钟在跨边界比较时丢失亚天精度。

## 历史月份均速、95 百分位与监控脚本

- **整月均速**：`showdb`（[src/dbshow.c](../src/dbshow.c)）和摘要图 `drawsummary_digest`（[src/image_summary.c](../src/image_summary.c)）在计算已结束月份的平均速率时，调用 `getperiodseconds(LT_Month, ...)` 获取该账期真实持续秒数，而不是统一用 `dmonth() * 86400`，因此月底截断、闰年 2 月以及本地夏令时 23/25 小时切换日的均速都准确无误。
- **95 百分位窗口**：`percentilegridcutoff` 与 `getpercentiledata`（[src/percentile.c](../src/percentile.c)）把查询收紧到完全落在账期 `[periodstart, periodend]` 内部的 5 分钟槽 `[firstslot, lastslot]`。落盘的 5 分钟表无法再拆细，恰好横跨切点的那个 5 分钟槽两头都不算入 95 计费统计。同时在 `slotlimit` 上做了乘法溢出保护，防止配置了极大带宽上限时折算溢出。
- **Prometheus 监控脚本**：[examples/vnstat-metrics.cgi](../examples/vnstat-metrics.cgi) 读取每块网卡或顶层的计费设置，在账期所属时区确定当前日历月/年，再换算回与 `vnstat --json s 1` 匹配的 bucket 时间戳。
