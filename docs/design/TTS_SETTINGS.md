# 译文语音与设置页

实现日期：2026-10-21。正式真实麦克风输出目录为 `output/tts/microphone_final/`；当前证据和板载固件由 [NOW](../context/NOW.md)维护。

## 云端与音频路径

本轮已用同一工作空间密钥调用百炼标准 `qwen3-tts-flash` HTTP 接口并取得 WAV；无需另建账号。标准入口为 `https://dashscope.aliyuncs.com/api/v1/services/aigc/multimodal-generation/generation`，不能把用于 OpenAI 格式文本调用的 `compatible-mode/v1` 当成语音合成入口。该 HTTP 验证是电脑端服务能力证据。

板端沿用已实现的 `qwen3.5-livetranslate-flash-realtime` 一条 WSS 连接，同时请求 `modalities=["text","audio"]`、`voice=Tina`、`output_audio_format=pcm`。云端返回目标语言文本和 24 kHz、16 位单声道合成语音。中文输入时目标是英文；切换到英译中时目标改为中文。这里实际使用 LiveTranslate 的译音合成能力，无须另开一条 HTTP TTS 连接。

实际路径为：WM8731 采音 → PL 16 kHz PCM → PS 固定点 Opus/Ogg → ARM TLS → ESP8266 TCP/Wi-Fi → 百炼 → 目标文本/合成 PCM → PS 有界队列、输出增益及速度 → PL 播放 FIFO → WM8731 DAC。正式构建不带 `--text-only-response`，DAC 选择译音 FIFO，空闲时输出静音，不输出输入回环。保留的 L1 回环测量支路只作诊断。

代码：`software/network_ui/cloud_client.c`、`cloud_live.c`、`speech_playback.[ch]`；PL 播放实现为 `rtl/audio_playback.v`。没有为音量操作重启 codec 或更改 I2S/采样率/引脚。

## 参数的实际作用

| 参数 | 默认/范围 | 生效路径 |
| --- | --- | --- |
| 译文语音播报 | 开 | 关闭时丢弃当前软件队列与硬件 FIFO，后续音频不会回到本地监听 |
| 输出音量 | 70%，0–100%，每次 10% | 对写入 DAC FIFO 的有符号 PCM 作定点衰减；对 LINE OUT 和耳机的数字信号均生效，不改变上传采音 |
| 静音 | 关 | 将后续写入 DAC 的 PCM 置零；已写入硬件 FIFO 的尾部最多约 341 ms |
| 播放速度 | 1.0x；0.8/1.0/1.2x | DAC 支路重采样；会同时改变音调，页面已明确说明；1.0x 保持原采样顺序 |
| 字号、显示原文 | 标准、开 | 与主页面共用模型状态，首次连接和重新连接均保留用户选择 |
| 翻译方向 | 中文 → English | 与顶部方向菜单共用状态；改变方向走既有新 epoch / 重新连接流程，保留已完成历史 |

设置页打开/返回不暂停采音、不清空字幕或历史、不改变 epoch。设置仅本次上电有效。触摸使用原 CPU1 的 64 字节反馈 ABI，新增命令计入扩展计数。UI 模型变为 135804 字节，快照为 135868 字节；主程序断言、捕获工具和回归夹具已同步。

## 延迟与状态

页面保留浅色背景、圆角卡片、青绿色强调色和抗锯齿字体；主页面增加 `T2 VOICE` 卡片及等待/缓冲/播放/静音/关闭/未就绪状态。

| 读数 | 定义 |
| --- | --- |
| L1 LOOP | 既有本地数字回环支路时间，单位 µs；不是当前译音的声学延迟 |
| T1 TEXT | 收到云端 speech-start 事件 → 收到首条识别/翻译文字，单位 ms |
| T2 VOICE | 收到该响应第一段合成 PCM → 使能 DAC 播放，单位 ms；是接收与播放缓冲等待，不含此前上行、识别、翻译和合成耗时 |

三者均不是麦克风到扬声器的完整声学延迟。新语音段或停止会清除相应有效标记；未测得显示 `--`。长响应按实际到包速度选择1.28秒或2秒音频预缓冲；慢链路及欠载后保留2秒，已完成的短响应可提前播放；软件每次最多搬运 512 个样本，先填足实际 8192 样本 FIFO 或整段短响应，再启动 DAC，避免软件队列很深但硬件仅有 21 ms 音频的起播欠载。

主动IPD下行省去逐次读命令，TLS明文按块送入WebSocket，每轮最多4KiB，及时返回采音和UART服务。当前使用2Mbaud确认上传、Opus O2编译，保持编码参数；连续IPD的重复CRLF不再积累到AT回复。CPU1每毫秒服务触摸，减少空闲总线访问。测量、失败对照和长尾限制见[长句验证](../validation/long-speech-20261022/README.md)，前轮记录见[延迟验证](../validation/tts-latency-20261021/README.md)。

## 构建、加载与凭据

- `scripts/build_tts.ps1` 使用 Vivado/SDK 2018.3；`-RebuildFpga` 会先跑八项音频仿真，再实现位流。默认重建真实麦克风应用和 CPU1 Host。
- `scripts/load_tts.py` 核对源码和产物哈希、JTAG 身份及真实显示报告，再加载 CPU1 触摸。加载流程打开屏幕 USB0 供电。
- 桌面工程根目录的 `启动译声.cmd` 可重新加载并输入运行期配置。设置、Wi-Fi/API 配置和 TLS 种子均不写入 Flash/SD；JTAG 配置断电后需重新加载。
- API 密钥和 Wi-Fi 密码只保存在主机进程及板端 RAM。工程与交付报告不包含秘密。
- SDK 2018.3 的 CPU1 链接显式使用 `-z max-page-size=4096`，避免默认 64 KiB 对齐产生覆盖地址 0 的 PT_LOAD；原 OCM 范围检查仍保留。

本次字体占用接近 2 MiB 限额，后续增加大量文案前需检查字库与链接报告。

## 定稿事件的局部修正

真实事件顺序记录只保存 ID 哈希、事件类型、文本长度与时间，不保存识别内容。已定位迟到预览重建旧槽位，以及原文空定稿被当作 schema 错误拒绝的路径。解析器现在只接受明确的空字符串 ASR final；缺少 transcript 或 null 仍报 schema。空定稿会终止对应的推测槽位，移除待配对链接并记住终止 ID，后续迟到译文不会重建槽位；不补造原文，也不写成完整句对历史。已存在的非空原文定稿不会被迟到空结果覆盖。

当独立译文done缺失时，读取 `response.done` 中唯一、已完成的assistant输出作为最终译文，检查目标ID，不把预览提升为定稿。明确空的最终译文不形成虚假句对；有效服务错误码不因中文或过长说明而被忽略。

`session_diag[6/7/8]` 分别统计空ASR终止、明确空译文终止、response.done最终译文补齐；空结果不是正常定稿。32个空原文、8个空译文及96条正常句的原生检查无阻塞、无虚假历史；现场事件回放与当前板端复测见[本轮验证](../validation/tts-settings-20261021/README.md)。这不是 R28/R29 所有成因的长期放行，也没有新增自动重连策略。

官方依据：[LiveTranslate 语音与文本输出](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime)、[客户端事件](https://help.aliyun.com/zh/model-studio/live-translator-client-events)、[Qwen-TTS HTTP API](https://help.aliyun.com/zh/model-studio/qwen-tts-api)。
