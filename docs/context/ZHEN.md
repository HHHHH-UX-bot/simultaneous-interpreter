# 中文译英与 lineout 译音（2026-10-20）

## 当前范围

Z7-Nano / xc7z010，WM8731 主时钟 I2S 48 kHz、PL 降采样至 16 kHz；固定点 Opus/Ogg 上传到实时服务，英文字幕在中文原文上方，PCM 24 kHz 译音经 PL 送 DAC。当前实现选择 Zynq 执行 TLS 1.2，ESP8266 单连接转发 TCP，UART 1 Mbps；显式使用 `--mode zhen --speech --arm-tls --uart-baud 1000000`。原有不带这些选项的路径保留其验证范围。

## 故障与实现

桌面中译英输入版本连续四五句话后会停顿，基线曾出现 SEND FAIL、UART 错误、满上传队列和丢帧。仅改热点和 AT 调度仍复现，不能断言唯一原因是网差或持续内存泄漏。当前方案将 TLS 的内存及加解密负担移到 Zynq，ESP 只传 TCP；固定 DDR 内存池、密文接收环与非阻塞收发回调保持音频采集/播放可调度。证书链、实际域名及日期必须验证。

共享修复包括半条接收通知与发送回复的边界、零字节明文读取后的待接收状态、断链后的会话归属、串口实际整数分频结果的校验、不同速率的恢复探测，以及自测重连时不补发离线音频。TLS 引擎同时覆盖 WebSocket 数据、Ping 和 Pong。译音支持句界、预缓冲与短句尾部，保留原速播放。

## 验证证据与边界

以下是实际已执行的范围，证据在 [docs/validation/zhen-20261020](../validation/zhen-20261020/)；没有用自动重连冒充连续稳定。

- 同一实际板端会话先观察 180 秒，再继续观察 601.66 秒，累计 92 次中文定稿、92 次译音完成。会话数为 1；云端/网络/UART/TLS 错误、上传丢帧及播放欠流均为零；上传队列峰值 9 帧。
- 自测 ELF SHA256 为 `64c1a2552dbdf50af165c410e4047e1f6446fd629cc5f54d2db089f45697fb25`；随后加载的麦克风 ELF 为 `7fa69547b69acc9110ca6debfc32f3ffee3715307e6921e86d3a99a0d647994b`。麦克风就绪检查确认实际采集/上传增长、无溢出或丢帧，USB0 PP=1。
- 固定点编码、完整会话及上传暂停/补发的电脑对照通过；原生网络/播放 15 组、事件/UI 262 项通过。相同 TLS 引擎对真实服务的正常握手/读取、错误域名、过期证书、缺少随机种子、密文篡改 5 项检查通过。
- Vivado 2018.3 的 7 项音频仿真、构建和时序通过；setup slack 6.318 ns、hold slack 0.024 ns。切换应用的完整 PL 加载前增加系统复位，随后实际下载、显示/音频寄存器验证通过。

证据对应桌面修复工作副本；此 PR 的硬件/应用源文件与该副本比较核对，仓库检出的原生检查另行记录。不能用历史源码哈希冒充新仓库检出的测试结果。

仍待核实：真人连续语音和译音听感、完整声学端到端延迟、独立断电启动及 CPU1 触摸。当前仅为 JTAG 易失加载；未固化启动 Flash/SD，未改 ESP 固件。中文识别副本仅离线修复，本轮未烧录。

## 操作和代码入口

构建、运行时配置与重新加载见 [RUNBOOK 的中文译英入口](RUNBOOK.md#中文译英候选2026-10-20)。API 密钥、Wi-Fi 密码与一次性系统随机种子均由主机运行时传入，不写入源码、固件或提交。ESP8266 需要 2.4 GHz 网络。

- [cloud_client.c](../../software/network_ui/cloud_client.c)：会话、Opus/Ogg 上传、静音与心跳。
- [network.c](../../software/network_ui/network.c)、[wifi_recover.c](../../software/network_ui/wifi_recover.c)：AT/TCP 所有权、解析、恢复。
- [arm_tls.c](../../software/network_ui/arm_tls.c)、[tls_config.h](../../software/network_ui/tls_config.h)、[link_arm_tls.ld](../../software/network_ui/link_arm_tls.ld)：认证、固定内存及布局。
- [speech_playback.c](../../software/network_ui/speech_playback.c)、[audio_playback.v](../../rtl/audio_playback.v)：译音缓冲与 DAC。
- [Mbed TLS 来源和归属](../../third_party/mbedtls/SOURCE.json)、[许可证](../../third_party/mbedtls/LICENSE)：经官方发布包 SHA256 核对的 3.6.7，使用现有编译器，无系统依赖安装。
