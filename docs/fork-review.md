# 与上游 vnStat 2.14 的差异对照

同步到 `v2.14` 时没有直接硬 merge `billing-v2.13`，而是以 `v2.14` tag 为起点重新梳理：上游已经修好的不再重复保留，偏离主线的防御性冗余改回原版风格，确认在 `v2.14` 里仍然存在的缺陷继续带着单测留下来。

## 上游 2.14 自身带来的更新（相比 2.13）

- **`vnstati` 重构与 TrueType 字体**：图像模块拆分为 `image_font.c`、`image_graph.c`、`image_list.c`、`image_summary.c`、`image_widget.c`，新增 `FontFile`、`FontSize`、`FontScale*`、`--font` 和 `--common-width`（`CommonWidth`）。
- **配置与参数增强**：支持 XDG 用户配置路径（`$XDG_CONFIG_HOME/vnstat/vnstat.conf`、`$HOME/.config/vnstat/vnstat.conf`）；`MaxBandwidth` 上限从 `50000` 提到 `400000`；新增 `MonthRotateVisible`；`-D` 重复传入可提高调试日志级别；JSON/XML 每个接口内输出 `monthrotate` 和 `monthrotateaffectsyears`。
- **已知问题修复**：`-hg` 在 `UnitMode 1/2` 下的缓冲溢出；多网卡合并 `interface1+interface2` 的 top 天数漏行及 `SQLBUFLEN` 扩至 `4096`；`--json -l`/`-tr` 与 JSON/XML 接口名/别名转义；`vnstat.cgi`、`vnstat-json.cgi`、`vnstat-json.php`、`vnstat-metrics.cgi` 命令拼接、转义与缓存处理；Prometheus `vnstat-metrics.cgi` 桶时间戳、年份对齐及更新时间戳指标。

## 清理并舍弃的 v2.13 分支旧改动

- **示例 CGI/PHP 与安全脚本**：`examples/vnstat.cgi`、`examples/vnstat-json.cgi`、`examples/vnstat-json.php` 及此前配套的 `tests/example_security.py` 全部撤回上游 `v2.14` 实现，不再额外叠加冗余校验；监控脚本只保留对 [examples/vnstat-metrics.cgi](../examples/vnstat-metrics.cgi) 的分钟/月末/UTC 时区适配。
- **冗余文本工具与重复实现**：撤回早先重写的 `showhours` 和自建 JSON/XML 字符串转义，直接沿用 `v2.14` 原生 `jsonstring()` 和 `xmlstring()`。

## 在 2.14 基础上继续保留的修复

除了[计费边界与数据库实现](billing.md)中的核心计费功能外，以下从旧分支核实过的实际缺陷在 `v2.14` 中仍然存在，继续保留修复：

| 位置 | 问题与修复 |
| --- | --- |
| [src/dbsql.c](../src/dbsql.c) `db_setinfo` | 改用 `sqlite3_mprintf` / `sqlite3_free` 构造 SQL，避免接口名带单引号时语句构造异常。 |
| [src/dbsql.c](../src/dbsql.c) `db_removeoldentries` | `sqlite3_snprintf` 长度参数从硬编码 `512` 改为 `(int)sizeof(sql)`。 |
| [src/dbmerge.c](../src/dbmerge.c) `mergedb` | 读取源库失败回滚前补全 `db = dstdb;`，避免向空库或错误句柄回滚。 |
| [src/dbshow.c](../src/dbshow.c) / [src/image_summary.c](../src/image_summary.c) | 已结束月份平均速率改用 `getperiodseconds(LT_Month, ...)`，解决平闰年 2 月、29..31 截断月和夏令时切换日均速失准。 |
| [src/percentile.c](../src/percentile.c) | 5 分钟格严格按 `[firstslot, lastslot]` 收紧在账期内；增加 `slotlimit` 防止 `userlimitbytespersecond * 300` 溢出。 |
| [src/image_graph.c](../src/image_graph.c) | `drawfiveminutes` 与 `drawpercentile` 样本全 0 时跳过除法，防止 UBSan `float-divide-by-zero`。 |
| [src/dbjson.c](../src/dbjson.c) | 新增 `jsonpercentage` 在逗号小数地区强制输出点号；计费字段同时写顶层与接口内。 |
| [src/dbxml.c](../src/dbxml.c) | 修复 `xmlpercentile` 均值运算优先级括号，并将 `<95th_percentile>` 改名为合法标签 `<percentile_95>`；计费字段同时写顶层与接口内。 |

## 时区与历史数据约定

- `MonthRotate` 在 `1..28` 且 `00:00` 的旧边界继续由原 SQL 路径处理，以维持历史数据兼容性。
- 本地夏令时跳变或重复时钟统一交给 libc `mktime`（`tm_isdst = -1`）裁决。
- 数据库与守护进程升级只需替换二进制文件，不重算历史月表记录，不需要 `--sync` 或重建数据库。
