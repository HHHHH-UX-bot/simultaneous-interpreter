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

## 2026-10-18 实施结果（MMU/Cache + Opus + Ogg + qwen3.5）

已完成并验证：

- **MMU/Cache 启动路径**（`start.S` 默认路径保持不变，cloud_app 定义 `Z7_MMU_CACHE` 启用）：1 MB section 表在 `0x01D00000`，section 0（OCM/邮箱）Normal 非缓存、DDR Normal WB-WA、外设 Device；开启 MMU/I-Cache/D-Cache/分支预测；L2 保持关闭（PL HP 不与其相干，`DCCMVAC` 才能到 DRAM）。应用代码/rodata/data 迁到 `0x01C00000`，栈 128 KB 在 `0x01CE0000`，`.bss`/`cloud_runtime`/`cloud_final` 仍在 CLOUD_DDR；`cloud_final`、framebuffer、snapshot、guard 在发布/换帧前用 `cache_clean_range()` 清到 PoC。启动踩过的坑：首次实现忘了把 section 0 描述符写进 r6（表项 0 为 0，MMU 一开就 prefetch abort）、缺 VBAR 与 TLBIALL；均已修复。
- **回归**：显示 `format_errors=0`、整帧 614400、flips 正常；音频回环 L1=62 µs、over/under=0；ESP TLS/WSS phase 4；DDR 抓帧与实屏一致；`ui_render` 最大渲染从约 246 ms 降到约 15–18 ms（缓存收益）。
- **libopus 接入**：`third_party/opus`（v1.4 固定点）128 个 C 文件以 `-Os` 编进 APP_DDR（`.text` 约 270 KB），静态池覆盖 `opus_alloc/free/alloc_scratch`，`opus_codec.c` 封装 16 kHz 单声道 20 ms、16 kbit/s CBR（每包 40 B）。
- **关键协议发现**：qwen3.5 的 `input_audio_format=opus` 接受的是 **Ogg Opus 字节流**，不是裸 Opus 包。已实现 `ogg_opus.c`（CRC32/OpusHead/OpusTags/音频页；10 包=200 ms 一页），并用主机工具验证与能工作的 Python 参考实现**逐字节一致**（27,737 B、sha 相同）。
- **板端实测**：会话 `session.created/session.updated` 通过（qwen3.5 + opus/16000），Ogg 头页 + 5 页/秒流式发送，上行约 **3.4 kB/s**（对比 PCM 时 13–14 kB/s 的链路上限，余量充足），8 秒内 40 页连续发送、无服务端错误、音频 FIFO 无溢出、云 phase=1/error=0。
- **qwen3.5 事件差异**：译文增量用 `response.text.text`（累计全文，含预测文本），完成后 `response.text.done` 给全文；原文识别增量为 `conversation.item.input_audio_transcription.text`，完成为 `.completed`。板端已支持 `response.text.text`（累计替换式流式字幕）并保留 3.8 的 `response.text.delta` 兼容路径；用真实 qwen3.5 事件 JSON 做了本机解析器验证（说话开始/物品链接/ASR/译文/response.done 全部按预期解析）。
- **待用户验收**：板端麦克风真实语音（中译中）多句连续、长时静默后首句、30 分钟稳定性；PC 侧已用 `test_en.wav` 验证同一 Ogg 流与同一 session.update 能被服务端识别并返回中文译文。
## 2026-10-18 晚：中译中显示 ASR 原文、T1 口径修正、无麦克风自测

- **中译中字幕改为显示 ASR 原文**（编译开关 `Z7_ASR_SUBTITLE`）：qwen3.5 在“源语言=目标语言”时返回的 `translation` 是对原文的改写（实测把“老师好”译成“大家好”、“今天天气非常好”译成“谢谢你，你人非常好”），不适合作为中译中字幕。现在流式字幕直接用 `conversation.item.input_audio_transcription.text`（`text` 已确认部分 + `stash` 预测部分，逐次累计替换），`conversation.item.input_audio_transcription.completed` 的 `transcript` 作为定稿并立即提交，译文事件全部忽略；不再等 `response.done`，提交更快。
- **T1 口径修正**：服务端 `speech_started.audio_start_ms` 按它自己“已接受音频”计时，长静音后与板端已发送音频位置严重不一致（自测实测：板端已发 44.2 s，服务端报 14.4 s），按原“音频时间轴映射”会把长静音后的 T1 放大到 9999 并钳位。现改为 **T1 = 收到 speech_started 事件 → 首条 ASR 文本**，即云端 VAD 触发到首字的服务端延迟，跨多句/长静音稳定（约 0.5–2 s 量级）；不再声称包含上行网络与采集时间。
- **`input_audio_buffer.clear` 在 server_vad 下非法**：PC 实测服务端返回 `Error clear input audio buffer: server_vad is enabled.`，因此长静音期间不能靠 clear 维持会话；板端已移除该逻辑。
- **长静音本身没有问题**：PC PCM 连续流中插入 40 s 静音后，第二轮 4 句仍正常触发 `speech_started`（audio_start_ms=52 620 起递增）；板端自测每个 30 s 静音周期后的下一轮 4 句都继续提交（committed 每周期 +4、无 error）。
- **无麦克风自测工具**：`python scripts/generate_selftest_audio.py` 用 Windows 内置中文语音（Microsoft Huihui）生成 4 句中文 + 0.8 s 间隔的 16 kHz PCM，写入 `output/cloud_app/selftest_pcm.c`；`python scripts/build_cloud_app.py --selftest` 编出 `cloud_app_selftest.elf`，板上以“14.58 s 语音 + 30 s 静音”循环喂入编码器，可无人值守验证 Opus→Ogg→WSS→ASR→字幕→T1→历史全链路（APP_DDR 占用约 81%）。
- **自测结果（两块周期）**：每轮 4 句全部提交（committed 4→8→…），字幕为 ASR 中文（如“今天天气非常好。”“我想喝一杯咖啡，再吃一点东西。”），云 phase=1/error=0，FIFO 无溢出；30 分钟连续自测进行中（结果记入 `docs/context/NOW.md` 与 `output/cloud_app/status.json`）。
- **网络瞬时故障自恢复**：`network.c` 保存最近一次合法的 SSID/口令；出现 `phase=6` 且错误属于 29/32/34/43–47（AT/CWJAP/SNTP/SSL 建链失败）时，5 s 退避后用保存的凭据自动重建设置（最多 6 次，成功后清零）。此前这类瞬时失败是终态、必须重烧或断电，表现为“说着说着没反应”。
- **待用户验收**：板端真实麦克风语音（中译中）的识别准确度、多句连续、长时静默后首句、30 分钟稳定性。

## 实施步骤（2026-10-18 状态）

1. ✅ 启用 MMU/Cache 并迁移代码/栈到 DDR；显示/音频/网络回归通过（本地备份见桌面 `simultaneous-interpreter-zhzh-20261018`）。
2. ✅ 把 `third_party/opus` 接入 `scripts/build_cloud_app.py`：独立编译 128 个 C 文件、静态内存池、`opus_codec` 封装（20 ms 帧、16 kbit/s CBR、VOIP）。
3. ✅ 新增 Ogg Opus 上行路径：16 kHz PCM FIFO → 320 样本帧 → `opus_encode` → Ogg 页（10 包/页）→ Base64 append；PCM 路径仍在源码中作为回退（`Z7_OPUS` 未定义时编译）。
4. ✅ 切换 `cloud_config.path` 到 qwen3.5 模型并验证会话与流式发送；✅ 板端无麦克风自测（中文 TTS 循环）验证多句连续、长静音后首句、T1 稳定；⬜ 真实麦克风语音与 30 分钟稳定性的用户验收。
5. ⬜ 达标后再拆英译中/中译英/英译英工程。
