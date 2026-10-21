# 英文控件与时延界面验证

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

核验日期：2026-10-20。范围为本阶段 UI、暂停/恢复语义、英文构建和真实 USB 对照；未完成整个产品或可靠性验收。

- `pl_build.json`：关闭旧 PL 字符覆盖、保留输出寄存级的位流与来源哈希、时序结果。旧音频回归仍由 [前一阶段](../english-20261020/README.md)解释，不能把该位流本身当成实屏验收。
- `cpu0_build.json`、`cpu0_load.json`、`cpu0_load_result.json`：最终字号候选的当前来源、实际 CPU0 易失加载和显示启动结果。加载保留同一 PL，未改写 Flash/SD。
- `cpu1_build.json`：本次重新编译的 CPU1 Host；真实启动和事件状态须结合 `touch_trials.json`。
- `transport_native.json`：当前共享传输/暂停恢复/播放 15 项本机回归通过，不代表板端长时链路稳定。
- `native_enzh.json`、`native_enen.json`、`native_renderer.json`：实际 C 会话、控件、字号与像素边界检查；TLS/编码边界使用 mock，不是识别或触摸验收。两英文模式分别 14589/11599 项，均覆盖 500 次定稿。
- `font_repro.json`：禁用新增常用汉字缩放后，新回归因“与替代方框相同”失败；主实现通过同一回归。
- `font_board_validation.json`：保留同一真实云译文，通过 JTAG 选择三档字号，三张 614400 像素画面均零差异；中央文字墨迹高度 27/35/43。不是触摸字号验收，也不替代肉眼确认。
- `latency_style_acceptance.json`、`ui_candidate_frame.json`：用户确认卡片清楚、风格协调，以及该次候选的完整帧零差异。它们绑定较早 CPU0 ELF，卡片实现此后未改变；不是最终触摸验收。
- `pause_validation.json`、`resume_snapshot.json`：较早控件候选的实际暂停 49 秒无上传增长，恢复进入新的 TLS/云会话；最终字号候选复用相同控制代码，仍分别保留版本，不冒称全部实屏控件通过。
- `touch_trials.json`：旧/新 Host 均曾收到真实报告并改变字号状态，最终字号候选启动后又零报告。仍未定位间歇 USB 故障，不能将成功枚举、心跳或 sample PASS 等同于报告可用。
- `usb_reset_native.json`、`usb_reset_trial.json`：实际板级函数的四项顺序/超时回归，以及按 AMD AR47540 增加复位门禁后的首次板测。两次 HCH/PR 门禁通过，但零报告和停机超时仍存在，未冒称修复成功。
- `touch_recovery.json`：用户同线单 Host 接电脑触摸有效，接回 FPGA 后收到 112 条报告并确认真实中文字幕字号变化正常；随后正常停止并确认 DMA Halt。这一受控恢复不覆盖先前间歇故障。
- `link_failure_snapshot.json`：较早候选 TLS fatal alert 20 后 AT 超时的公开计数；保留失败，不用后续短时连通覆盖长期风险。
- `review.json`：独立源码复核范围与结论，不代表硬件验收。
- `touch_restart.json`、`resume_before_speech.json`：正常停止后重启收到 172 条报告，字号及两次继续/暂停输入有记录；用户当时反馈无新识别，取样时已暂停，不冒称整个操作序列通过。
- `recognition_restored.json`：单独恢复会话并确认 network/client 均为 4 后，用户重新说参考句，确认新英文与中文出现；当次 TLS、网络、UART 错误为零，不外推为长期稳定。
- `final_paused.json`、`acceptance.json`：最终关闭云连接、保留字幕、触摸常驻的状态，以及各项用户实屏确认和仍未解决的边界。

原始帧、日志和中间工具仅在本机阶段 `output/`、`tmp/` 保存；本目录不包含密钥、Wi-Fi 密码、原始麦克风录音或完整聊天。当前状态与下一步见 [ENGLISH](../../context/ENGLISH.md#当前阶段触摸控制与时延界面2026-10-20)。
