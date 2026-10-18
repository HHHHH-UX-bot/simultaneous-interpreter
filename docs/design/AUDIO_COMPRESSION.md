# 音频压缩（Opus）与低带宽实时同传方案

核验日期：2026-10-18。目标是把板端上行从 16 kHz PCM/Base64 的约 44 kB/s 降到 ESP8266 单连接 TLS 能稳定承载的范围（实测约 13–14 kB/s，好时约 38 kB/s），从而让连续多句识别不再丢语音。相关现状见 [NOW](../context/NOW.md) 和 [CLOUD_SPEECH](CLOUD_SPEECH.md)。

## 为什么需要压缩

- 16 kHz、16-bit、单声道 PCM 原始码率 256 kbit/s；WS 文本帧里再 Base64 后约 44 kB/s（含 JSON 开销）。
- 板上 ESP8266 单条 TLS 连接实测 13–14 kB/s（AT+CIPSEND 约 145 ms/1992 B；好时约 38 kB/s），低于所需；客户端只能丢帧，连续多句时偶发“说了没识别”。
- Opus 16 kbit/s 即可保持语音可懂度，Base64 后约 2.7 kB/s，链路余量充足。

## 已完成的可行性验证（2026-10-18）

1. 供应商化：`third_party/opus` 引入 libopus v1.4（BSD-3-Clause，commit `82ac57d9f1aaf575800cf17373348e45b7ce6c0d`），来源与逐文件 sha256 记在 `third_party/opus/source.json`；只保留 C 源码/头文件，排除 x86/ARM/NEON 汇编、float 实现、测试与构建系统。
2. 固定点配置：`third_party/opus/config/config.h` 使用 `OPUS_BUILD + FIXED_POINT + DISABLE_FLOAT_API + VAR_ARRAYS`（无浮点、无 alloca、无 RTCD）。
3. 主机（MinGW x64）编译并跑通 16 kHz 单声道编码→解码回环：128 个 C 文件、`-O2` 全部通过；20 ms 帧在 16 kbit/s VBR 下平均约 6 B（正弦测试），首帧 33–54 B，解码 100/100 帧有效。
4. Cortex-A9 交叉编译（`arm-none-eabi-gcc -Os`）同样 128 个文件全部通过；编码器+解码器对象 `.text` 合计约 **215 KB**（编码器单独更小），远超 OCM 剩余空间，必须放在可缓存 DDR 运行。

## 关键前置：MMU / Cache

`software/ps_bringup/start.S` 当前关闭 MMU（SCTLR.M=0）和 I/D Cache，堆栈也在 OCM 顶端（`__stack_top=0x0001FFF0`，可用余量仅数 KB）。Opus 不能以未缓存 DDR 指令运行，动态栈也放不下。

计划（下一阶段实施）：

- 启用 MMU：1 MB section 映射 + 少量 4 KB 页表；DDR 映射 Normal、Write-Back Write-Allocate、可缓存；外设（0x43C00000 等）与 GIC/全局定时器映射 Device；OCM 中主机可见的 mailbox 区（0x1E000–0x1E3FF）与显示/音频寄存器区必须非缓存或显式 clean。
- 代码/数据/栈：把 `.text/.rodata/.data/.bss` 与栈迁到可缓存 DDR（例如 0x01C00000 起的空闲区），启动后启用 I-cache/D-cache；boot 早期未缓存执行只覆盖很小的初始化。
- 一致性：framebuffer 若映射可缓存，每次换帧前按地址范围 clean D-cache（供 VDMA 读取）；或把帧缓存所在 1 MB section 映射为非缓存（写带宽换简单性，先测量再定）。
- 主机 JTAG 读取的 `report/ui_info/network_report/cloud_report` 等 mailbox 必须保持非缓存（或每 200 ms 发布时 clean），否则抓不到最新值。
- 需要重新核验：显示（VDMA 帧完整/flips）、音频回环、ESP 链路时序、`ui_assets` 读取性能、栈深度与中断（当前无中断使用）。

## 云端接入（qwen3.5）

`qwen3.5-livetranslate-flash-realtime` 的文档支持：

- `session.update`: `modalities:["text"]`、`sample_rate:16000`、`input_audio_format:"opus"`、`output_audio_format:"pcm"`、`input_audio_transcription:{model:"qwen3-asr-flash-realtime", language:"zh"}`、`turn_detection:{type:"server_vad", threshold:0.2, silence_duration_ms:500}`、`translation:{language:"zh", same_language_skip_options:{skip_text:false, skip_audio:true}}`（中译中时保留文本输出）。
- `input_audio_buffer.append` 的 `audio` 为 Base64 的 Opus 包；具体是“每包一次 append”还是可拼接，需先在电脑端用同一模型做短对照确认，再改板端。
- 当前 3.8 模型的 16 kHz PCM 路径保留为回退（`path` 由运行期 `.cloud_config` 决定，不需要重新编译 ELF 即可切换 model 名）。

## 实施步骤

1. 启用 MMU/Cache 并迁移代码/栈到 DDR；保持现有中译中功能与显示/音频/网络回归通过（本地备份见桌面 `simultaneous-interpreter-zhzh-20261018`）。
2. 把 `third_party/opus` 接入 `scripts/build_cloud_app.py`：新增 `opus_encoder` 源文件集、静态内存池（覆盖 `OPUS_ALLOC/FREE`）、编码封装（20 ms 帧、16 kbit/s、VOIP）。
3. 新增 `opus` 上行路径：PL 仍输出 16 kHz PCM FIFO；PS 组 320 样本帧 → `opus_encode` → Base64 → append；保留 PCM 回退开关。
4. 切换 `cloud_config.path` 到 qwen3.5 模型，按上文 `session.update`；板上验证识别连续性、T1、长时静默后首句、多句连续。
5. 达标后再拆英译中/中译英/英译英工程。
