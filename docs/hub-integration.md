# 配套 macOS Hub 集成

此仓库不包含整个多设备 M5StackHub App，也不会单独读取 Codex 账户。设备端与任意实现 [BLE 协议](protocol.md) 的主机兼容。

## 原项目中的职责

现有 Hub 的 PaperMono 功能由以下模块组成；这里记录接口而不复制其他设备的控制代码：

- `PaperMonoHub.swift`：加载聚合 JSON、展示活动摘要、组装 v3 二进制包。
- `codex_activity_export.py`：本机扫描 Codex `sessions` 与 `archived_sessions` 下的 JSONL，仅聚合任务事件。按 turn_id 去重，统计 task_started/task_complete 及完成耗时，不向设备发送内容。
- `StopWatchHub.swift`：复用 `StackChanCodex` BLE Peripheral，注册 Activity 特征并响应读取；不要为 PaperMono 再启动同名冲突广播。
- Hub 构建脚本把 exporter 放入 App 的 `Resources/papermono/`。实际数据加载优先运行随 App 携带的 exporter，不依赖另行启动 Dashboard。

导出器依赖的是本机 Codex 会话事件格式，非官方稳定用量 API。用户轮次不等于账单 token，也不等于每日配额消耗。格式变化可能影响统计；无需也不应上传原会话文件来诊断。

## 最小主机流程

1. 加载本机聚合，生成连续 N 天（N≤112）的日期、turns 和 level，缺失日期补零。
2. 启动主机 BLE，添加服务及可读 Activity 特征，服务注册成功后广播 `StackChanCodex`。
3. 首次读取时获取本地公历日期时间，生成完整 v3 包和 CRC，冻结为该次读取快照。
4. 按请求 offset 返回同一快照，及时响应；对错误 offset 返回错误。
5. 若主机需要加密和授权，必须在服务端实现和实测，不能依靠客户端主动配对作为访问控制。

现有 Hub 是主线程队列的 CoreBluetooth Peripheral，读取回调目前同步处理。此前变更了广播名称、加密读取权限和回调处理，随后确认 v2 同步成功；**没有单变量证据能断言其中某一项是唯一根因**。

## 升级与检查

先构建兼容 v2/v3 的设备固件，再协同更新主机到 v3。第一次成功读取后应同时检查：

- 状态栏 `HUB / SYNCED`。
- 非今天、位于有效窗口内的活跃日期出现底纹。
- 日期和分钟与主机一致，并在下一个分钟继续走时。
- 校时不引发连续全屏刷新。

主机日志里出现“已发送”或 137 字节，只能证明主机提供了包，不能证明固件验 CRC、RTC 写入和屏幕更新均成功。
