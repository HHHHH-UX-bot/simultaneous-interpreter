# 英语候选接续证据

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

核验对象：PR #5 的准确提交，以及桌面英译中/英译英共享源码的本轮修正版。范围为限定源码复核、本机回归、ARM 构建和分项板测；不代表完整产品验收。

- `pr5_merge.json`：已复核头及实际合并提交。
- `source_reconciliation.json`、`platform_binding.json`：资料包范围、原理图及初轮复用的已发布位流与全部 22 项构建输入的哈希核对。后续 PL 修正另有独立构建记录，不混用。
- `<mode>_validation.json`、`<mode>_session.json`：实际 C 会话/事件/UI/发送边界回归，英译中 4637 项、英译英 1647 项。
- `<mode>_<kind>_build.json`：四套新编译 ARM ELF、运行参数及实际输入哈希，编译器位置已省略。
- `enzh_board_summary.json`、`enzh_board_load.json`：英译中固定样本上板、公共计数及实屏验收。无耳机听感验收。
- `enen_first_failure.json`、`enen_clean_repeat.json`：首次缺少 Pong 的失败和同版 98 秒干净启动对照，均保留其原范围。
- `fifo_regression.json`、`fifo_fixed_pl_build.json`：地址宽度缺陷及首次修正的独立 PL 构建；该版没有解决采音尖峰。
- `audio_simulation.json`、`cdc_regression.json`、`cdc_fixed_pl_build.json`：最终 8 组音频仿真、跨时钟错误的失败/修正对照、独立 bus skew 检查及位流/全部输入哈希。
- `microphone_capture_comparison.json`：同探针的三版 10 秒采音；不是相同模拟声源的录音质量评分。
- `microphone_pc_diagnostics.json`、`microphone_local_asr.json`：修正前两项失败、固定样本正例、修正后未经增益/去噪的真人录音准确识别。电脑 TLS 路径与板端路径分开。
- `enzh_microphone_cdc_build.json`、`enzh_microphone_cdc_load.json`：当前 ELF、位流、PS 初始化及真实加载/启动检查，不能仅靠本机 ELF 哈希推定加载身份。
- `enzh_microphone_live_summary.json`、`enzh_microphone_live_latest.json`、`enzh_microphone_caption.json`：最终板端直连实测，68 秒连续观察通过、上传递增且采样/TLS/UART/丢帧错误为零；稍后的公共快照和实际字幕分别保留。
- `enzh_microphone_acceptance.json`：用户确认实时英文和中文译文正常；耳机未接、当前触摸未启用，不外推到听感或完整产品验收。

当前状态与操作入口分别见 [ENGLISH](../../context/ENGLISH.md) 和 [RUNBOOK](../../context/RUNBOOK.md#英语输入模式2026-10-20)。原始公开计数、构建日志和本轮工作材料保留在本机 `output/`、`tmp/` 对应阶段目录；未提交凭据、原始麦克风录音或厂商工程缓存。
