# TLS 传输边界诊断

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

本阶段完成到故障边界定位，TLS 间歇故障尚未修复。实现、分析与下一阶段由 [ENGLISH](../../context/ENGLISH.md#tls-传输诊断进行中2026-10-20)维护；当前状态见 [stage.json](stage.json)，用户验收及其范围见 [acceptance.json](acceptance.json)。

## 证据路由

| 问题 | 记录 |
| --- | --- |
| 1 Mbaud 首 TLS 故障及内部链路 | [冻结快照](wire_1m_snapshot.json)、[内部核账](wire_1m_internal.json)、[电脑接收](wire_1m_relay.json)、[观察摘要](wire_1m_observation.json) |
| 内部串行线与电脑共同前缀 | [指纹对照](wire_network_tx_wire_trace_comparison.json)；其他三个边界同目录以 trace 名命名 |
| 降速是否消除问题 | [460800 冻结快照](wire_460800_snapshot.json)、[内部核账](wire_460800_internal.json)、[电脑接收](wire_460800_relay.json)、[对照](low_network_tx_wire_trace_comparison.json) |
| 本机与 RTL 验证 | [真实 TLS 正反例](native_tls.json)、[传输回归](native_transport.json)、[串行指纹仿真](rtl_simulation.json)、[复核](review.json) |
| 诊断 PL 与实际加载 | [PL 构建](diagnostic_pl_build.json)、[CPU0 构建](diagnostic_cpu0_build.json)、[加载绑定](diagnostic_cpu0_load.json)、[实际结果](diagnostic_load_result.json) |
| 正常麦克风版本恢复 | [构建](normal_cpu0_build.json)、[最终加载](normal_final_load.json)、[加载结果](normal_final_load_result.json)、[触摸](normal_final_touch.json)、[暂停快照](normal_final_paused.json)、[整帧核对](normal_final_frame.json) |
| 真人验收后的额外失败 | [AT 所有权未确认现场](normal_followup_unknown_at.json)；随后恢复并确认停稳，不能据此宣称长期稳定 |

所有 trace/relay 文件仅含公开状态、计数和密文字节块的 FNV 指纹，不含 API Key、TLS 种子、音频或数据正文。只有停止或首个 TLS API 故障冻结后的快照可用于最终核账；两个连接还必须核对首块身份。未收到的尾部不当作损坏。

构建/加载记录保留原运行目录中的路径与散列，二进制和临时采集文件不进入 Git。本目录是验证摘要及原始公开元数据，不是可直接下载的固件包。
