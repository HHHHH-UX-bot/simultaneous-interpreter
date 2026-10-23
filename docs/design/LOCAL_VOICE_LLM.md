# 云端语音与板上 Qwen（2026-10-23）

正式程序为 `output/voice_local/microphone/cloud_app.elf`，由 SDK 2018.3 构建，`Selftest=false`。本地模式现在使用真实麦克风：百炼 Qwen ASR 识别 → Cortex-A9 上的 Qwen2.5-0.5B-Instruct Q6 翻译 → 百炼 Qwen TTS 合成 → WM8731 DAC。电脑只负责画面、控制或键盘输入，不执行翻译模型。本地模式的语音部分仍需要网络。

## 使用和恢复

当前已加载真实麦克风版。板屏或 `http://127.0.0.1:8787` 的 **设置 → 模型设置** 可选云端或本地 Qwen；同一个板端状态驱动两边显示。云端模式使用 LiveTranslate，本地模式使用板上翻译。两种语言方向共用选择菜单。请逐句说话，等待播报结束再继续；这版不是连续全双工同传。

`http://127.0.0.1:8790` 可提交任意文本并显示生成内容。麦克风任务的结果也可显示；麦克风任务用板屏镜像的暂停按钮控制。译音处理期间禁止覆盖当前文本，结束后可从麦克风任务转为电脑输入。

断电后运行根目录 `断电后启动本地LLM.cmd`；板卡还在运行时运行 `启动本地LLM.cmd`。两者已指向正式语音版，恢复 FPGA/CPU0/CPU1、USB 屏幕供电、权重与分词器，再配置运行期 Wi-Fi/百炼凭据。冷启动权重上传约 6 分钟；凭据不保存在工程中，重新上电需输入。程序、模型、会话和配置仍在 RAM，没有写入 Flash/SD。

重建用 `scripts/build_voice_llm.ps1`；需要重建 PL 时加 `-RebuildFpga`。调试同一 PL/PS 平台时可使用 `warm_reload_tinyqwen.py --app-dir <新构建目录> --current-app-dir <当前已加载目录>`。它核对加载记录和 CPU1 所有权，关闭旧 AT/TLS 后只重载 CPU0，再恢复触摸；新程序必须重新读取全部 DDR 资产并匹配固定 SHA，约需两分钟。校验通过之前不允许模型推理。

## 延迟优化与实测

采用精确 token 前缀 KV 复用、四 token 批量预填充、较短而一致的翻译 system。TTS 连接与会话准备和后续 token 生成重叠；模型完成后发送实际译文与 commit，收到 response.done 后发送 finish，等待 session.finished 和 DAC 排空后恢复识别。

快速起播档为 1.28 秒，慢链路保留 2 秒储备，完整短句可在收齐后提前起播。320 毫秒实验档曾出现欠载，已撤回。语速和 PCM 内容不因提速改变。

| 当前受控测试 | 模型首字 | 模型完成 | 语音起播或完成 |
| --- | ---: | ---: | ---: |
| 中文音频，空 KV | 模型开始后 29.77 秒 | 39.61 秒 | 识别定稿后 43.48 秒起播 |
| 重复相同中文音频 | 模型开始后 1.80 秒 | 11.61 秒 | 识别定稿后 15.83 秒起播 |
| 英文音频，空 KV | 模型开始后 29.70 秒 | 39.52 秒 | 识别定稿后 43.12 秒起播 |
| 新文字“请开门。” | HTTP 提交后 10.89 秒 | HTTP 提交后 22.56 秒 | HTTP 提交后 27.48 秒播完 |

实麦固件连续 12 / 12 轮文字 → 真实板上 Qwen → 百炼 TTS → DAC 播完，覆盖换句与长句。每轮必须有首个 PCM、完成计数增加、PCM 接收与实际 DAC 播放一致、零欠载；仅模型 DONE 不算通过。UDP 客户端 voice() 可读取这些状态与计数。

中文、英文受控源音频分别为“请把窗户打开。”和“Please close the door.”，源音频在测试固件中注入板端 Opus/云端识别链路；不是房间真人准确率测试。首轮 PCM 分别为 32,640 和 24,960 个，全部播放且零欠载。电脑扬声器在真实监听阶段播放后没有识别定稿，该声学尝试不确定，未作为通过项。

这些是特定输入的实测，不是人声声学 P50/P95；重复输入不能代表新句。新句输出“Let the door open.”，表达不够自然。原文本版 65–77 秒基线保留在 [BOARD_LLM](BOARD_LLM.md)。

## 模型选择与边界

保留现有 Qwen Q6 的完整权重和真实分词器，详见 [BOARD_LLM](BOARD_LLM.md)。未改成固定短句或电脑推理。查询到的 [DeepSeek R1 蒸馏系列](https://huggingface.co/deepseek-ai/DeepSeek-R1-Distill-Qwen-1.5B) 最小为 1.5B，仅普通 4 bit 权重就超过本板 512 MiB 内存，不适合作为本次提速替换。

当前本地模型仍达不到实时同传速度。语音期间的模型执行按句串行，推理和播报阶段不接受下一句。上下文 512 token，语音生成上限 96 token；电脑输入可配置到 256，但总上下文仍受限。小模型可能误译，部署成功不代表通用翻译质量通过。

## 验证记录

- output/voice_local/deployment_summary.json：正式固件身份、计时边界、证据范围及限制。
- validation_zh/end_to_end.json、validation_en/end_to_end.json：本次受控中英文音频的识别、板上推理和 DAC 计数；对应 result.json 保存实际源文与译文，build/load 绑定测试固件。
- microphone/tls_stress.json：正式实麦固件 12 轮实际文字与语音闭环，包含 PCM 收放和欠载计数。
- microphone/text_benchmark.json、text_benchmark_counters.json：新文字实际输出、历史和 DAC 计数。
- microphone/build.json、load.json、touch_live.json、final_public_probe.log：正式软件、平台及 USB/CPU1 身份。
- microphone/cloud_mode_check.json、pc_local_pointer.json、final_listening_observations.json：电脑真实点击云端/本地及最终实麦监听。
- unit_validation.json、playback_validation.json：控制器、实际 C 帧序列化与模拟 DAC 的边界、顺序和抖动测试。

调试经历两类问题：提交后立即 finish 的电脑端 6 次对照中有 1 次 SERVER_ERROR，等待 response.done 的 6 次正常；板端旧版也出现仅 commit ACK、无 PCM。现行时序与[官方示例](https://www.alibabacloud.com/help/en/model-studio/interactive-process-of-qwen-tts-realtime-synthesis)一致，但不据此断言所有云故障同源。后续双语复核发现 320 毫秒缓冲的欠载，恢复 1.28 秒后重新完成双语和 12 轮检查。历史失败和早期单次成功记录保留，不代替现行测试。

最后恢复真实麦克风固件。听感与真人房间环境仍需实际体验，不能由数字计数代替。
