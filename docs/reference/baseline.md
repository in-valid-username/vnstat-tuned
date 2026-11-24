# 基线与版本对照

## 基线信息

| 里程碑 | 分支 | 上游基线 Tag | 上游基线 Commit | 说明 |
| --- | --- | --- | --- | --- |
| 初始计费分支 | `billing-v2.13` | `v2.13` | `a77ace6e24028f22ec7213000daedfd5c7a9d70f` | 分钟级计费与 `29..31` 日月末截断 |
| 当前同步分支 | `billing-v2.14` | `v2.14` | `e55b2c8c3db982b5c73e8d7a52ea3b0a67d63731` | 对齐 `v2.14` 并收敛历史补丁 |

- 上游源码仓库：`https://github.com/vergoh/vnstat`
- 上游原始 `README.md` 存档：[upstream-readme.txt](upstream-readme.txt)
- 数据库兼容性：不新增表、不修改表结构或列定义，SQLite 库文件可与同版本号上游双向直接读写；`v2.14` 保持现有 `schema` 不变。
