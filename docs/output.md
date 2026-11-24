# 文本、图表、JSON/XML 与 Prometheus 输出

## 文本与图片账期提示

在展示月度数据的文本输出（如 `vnstat -m`、`vnstat` 默认总览）以及 `vnstati` 图片头部：

- 当 `MonthRotateVisible` 允许显示，且 `MonthRotateHour != 0` 或 `MonthRotateMinute != 0` 时，提示语会带具体时刻，例如：
  `Calculation formula: monthly traffic starts from day 31 at 18:24`
- 时分均为 `00:00` 时，保持上游格式不附加多余时刻。

相关格式化逻辑见 [src/misc.c](../src/misc.c) 的 `ismonthrotatenoteneeded` 和 `getmonthrotatenote`。

## JSON v2 与 XML v2 输出

上游 `vnStat 2.14` 开始在每个 `<interface>` / `interfaces[]` 内部自带 `monthrotate` 和 `monthrotateaffectsyears`。为了既对齐 `v2.14`，又不破坏之前围绕 `billing-v2.13` 顶层字段写的脚本，`billing-v2.14` 同时保留两处计费元数据：

- **JSON 顶层与接口内双输出**（[src/dbjson.c](../src/dbjson.c)）：
  - 顶层根对象和每个 `interfaces[i]` 对象下均包含：
    `monthrotate`、`monthrotatehour`、`monthrotateminute`、`monthrotateaffectsyears`、`useutc`
  - 新增 `jsonpercentage`：部分 locale（如 `de_DE.UTF-8`）把浮点数小数点印成逗号，JSON 要求使用标准点号，输出 `coverage_percentage` 和 `over_limit_percentage` 时会显式转成 `.` 字符，同时不修改全局 `LC_NUMERIC` 状态。
  - 字符串字段直接沿用 `v2.14` 原生 `jsonstring()` 转义。

- **XML 顶层与接口内双输出**（[src/dbxml.c](../src/dbxml.c)）：
  - 顶层新增 `<billing>` 块，每个 `<interface>` 内部也保留同样的计费节点：
    `<monthrotate>`、`<monthrotatehour>`、`<monthrotateminute>`、`<monthrotateaffectsyears>`、`<useutc>`
  - 字符串字段沿用 `v2.14` 原生 `xmlstring()` 转义。
  - 修复 `xmlpercentile` 中的两个原版问题：
    1. `(percentile_rx + percentile_tx)` 和 `(average_rx + average_tx)` 缺少括号导致总速率算错的问题。
    2. 原先以数字开头的 `<95th_percentile>` 元素不符合 XML 1.0 规范，改名为 `<percentile_95>`。

## 图片输出（`vnstati`）

- 5 分钟流量图 `drawfiveminutes` 与 95 百分位图 `drawpercentile`（[src/image_graph.c](../src/image_graph.c)）在样本全 0（`max == 0`、`maxrx == 0`、`maxtx == 0`）时跳过除零路径，避免触发浮点除零异常或产生坏刻度。
- 95 百分位图按分钟级边界精确传递 `"%Y-%m-%d %H:%M"` 起止时间区间给 `db_getdata_range`。
- 摘要图历史整月平均速率改用真实月秒数计算。

## Prometheus 导出脚本

`examples/vnstat-metrics.cgi` 执行 `vnstat --json s 1`，根据 JSON 里的 `monthrotate`、`monthrotatehour`、`monthrotateminute`、`monthrotateaffectsyears` 和 `useutc` 计算当前进行中的 `month` 和 `year` 桶时间戳，并结合 5 分钟四舍五入槽位规则过滤出当前指标。
