# 2026-10-10 构建与现网升级验证

- 源码提交：`38ed3cfba6e325d58d93d74ff07ad26d8ff00d57`（分支 `billing-v2.13`）
- 构建方式：`autoreconf -fi`、`./configure --disable-image-output`、`make -j1`、`make check`

相比 [10 月 4 日完整构建](2026-10-04-validation.md)，这轮关闭了图像输出，少编了 `vnstati` 对应的图形测试，最终 825 项 C 测试全部通过（0 失败、0 错误）。`src/`、`cfg/` 和 `tests/` 与上一轮 `f0cb7b9828c4d518bc5fa8b8fd474c7ea23b5f7d` 保持一致，版本串仍为 `2.13`。

## 产物校验和

| 二进制 | SHA-256 |
| --- | --- |
| `vnstat` | `93f756adc51492fbcc17117fa90471def2262c0895b89968850e86dc3ea5d0fd` |
| `vnstatd` | `6ea7695b1de12d466c1261ccd05aa8e0da41981653f960a88d95eed72ce74cf4` |

## 升级与账本检查

这组二进制实际装到了 5 台 Debian 12 / Ubuntu 22.04 机器上，涉及的账期（UTC+8）分别为：每月 `1 日 00:00`、`5 日 16:51`、`20 日 00:35`、`26 日 22:24` 和 `26 日 23:00`。

升级时先正常停掉旧 `vnstatd` 让内存缓存刷完，做好 SQLite 备份再换新二进制启动，没有执行 `--sync` 也没有删库重来。上线后检查：

- 5 台机器数据库 `PRAGMA quick_check` 全部返回 `ok`。
- 表结构、建库时间、历史账期和配置记录没有意外变动。
- 累计 `rx`/`tx` 接着旧数值往上涨，后续采样时间推进正常。
