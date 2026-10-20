# 云端连接就绪提示验证

核验日期：2026-10-20。范围为当前 ARM TLS 的等待、就绪、暂停、重连提示及字幕保留；不是全产品或长时可靠性验收。

- `cpu0_build.json`、`cpu0_load.json`、`cpu0_load_result.json`：当前来源哈希与真实暖加载，沿用上一阶段 PL；加载前确认旧 Host 停止及 USB DMA Halt、退出 ESP 透传并关闭旧 TCP。
- `native_enzh.json`、`native_enen.json`：实际 C 客户端/事件/UI 各 14600/11610 项与 500 次定稿回归；编码和 TLS 使用 mock，不是模型质量或板测。
- `native_renderer.json`：12 类界面场景，覆盖 Connecting、Offline、Ready 与原字幕控件；当前英文预览使用与产品一致的 ARM TLS 定义。
- `legacy_nonarm.json`：旧非 ARM 中译英路径 251 项回归，不将其 Listening 误称为当前 Ready 功能。
- `frames.json`：板上 Connecting、空字幕 Paused、真实字幕保留 Paused 三张帧各比较 614400 像素，均零差异、无缺字/裁切。Ready 流式状态只读公共快照，不暂停 CPU 抓帧，避免阻塞音频。
- `observations.json`：真实配置后网络/客户端均为 4、实际显示快照为运行/Listening（渲染为 Ready）；最后暂停、网络/客户端 idle，UART 硬件错误为零。
- `recognition_counters.json`：本轮公开计数保留 TLS fatal alert 20（`-0x7780`）及恢复后的真实定稿，不用短时连通覆盖故障。
- `touch_live.json`：同一 CPU1 Host 的当前 generation、原始 HID 报告和控件核账；物理效果另由用户确认。
- `review.json`：非 ARM 提前 Ready 问题已修正并独立复核；不是硬件证明。
- `acceptance.json`：用户确认就绪提示、真人双语字幕、画面，以及触摸暂停→重连→就绪和保留字幕；最后暂停采集，触摸常驻。

本地原始帧和日志保存在阶段 `output/`，临时工具在 `tmp/`；本目录不保存 Wi-Fi 密码、API key、原始录音或完整聊天。Offline 画面的实屏验收、TLS 告警根因、USB 间歇问题、持续运行和耳机听感均未通过本阶段放行。当前状态见 [ENGLISH](../../context/ENGLISH.md#云端连接就绪提示2026-10-20)。
