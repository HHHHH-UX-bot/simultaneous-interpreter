# 云端语音翻译与独立终端

方案核验日期：2026-10-14，范围为官方接口、电脑受控文件调用与板卡测试数据传输；板端云端语音尚未完成。选择来源见 [D13](../context/DECISIONS.md#d13-云端语音翻译与独立终端)，实际进展由 [NOW](../context/NOW.md)维护。

## 已选方案与替代项

| 路线 | 适合本产品的能力 | 状态 |
|---|---|---|
| 阿里百炼 `qwen3.8-livetranslate-flash-realtime` | 音频流输入、翻译文本及原文增量；北京地域 WSS，可仅返回文本 | 用户选择优先验证；[官方接口](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime) |
| 腾讯云实时语音翻译 | WebSocket 同时返回原文、译文与句子完成状态，英语到中文；可选 `hunyuan-translation-lite` | 备用接口，未接入；[官方文档](https://cloud.tencent.com/document/product/1093/127565) |
| 火山引擎豆包语音同传 | 中英互译，S2T 流式音频到译文 | 备用模型，未接入；[官方简介](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-3?lang=zh) |
| 云端 STT + DeepSeek | 例如 Qwen3-ASR 实时识别后再调用文本翻译，两个服务分别测延迟 | 当前不优先；[ASR 文档](https://help.aliyun.com/zh/model-studio/qwen3-asr-flash-realtime)，历史文本实测见 [MODEL_BACKEND](MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译) |

以上是接入适配性的选择，不是同条件速度、质量或费用排名。官方延迟宣传不能替代本产品实测；此次没有购买服务或代办云账号设置。

串口 C0 的执行证据由 [高速 UART](BOARD_MIGRATION.md#m5-2-921600-串口)路由；C1-0 已取得下方固定查询证据，板端持续带宽与可信 TLS/WSS 仍待验证；电脑云端对照见 C2-PC。当前状态统一见 NOW，不把本文件的计划当成放行记录。

## 产品结构

音频模块 → PL 采集与必要前处理 → PS 音频队列 → ESP8266/Wi-Fi → 百炼流式翻译 → PS 字幕模型 → HDMI 中文 UI。USB 触摸控制开始、暂停、字号、原文和历史。

电脑仅做开发、测试音频生成、取证与 JTAG 调试，最终运行链路没有电脑服务依赖。继续复用裸机显示和触摸；是否需要更换网络软件或模块，由实际带宽、TLS/WSS 验证决定，不因选云端就安装 Linux。

第一版只验证英语到简体中文字幕，不生成译音。云端临时结果更新当前句，完成事件才进入历史；保留现有暂停、清空与 epoch 隔离语义，防止旧会话结果污染新会话。

## 百炼接口与凭据准备

采用北京地域模型 `qwen3.8-livetranslate-flash-realtime`。连接地址按官方契约由 Workspace ID 组成：`wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model=qwen3.8-livetranslate-flash-realtime`。

会话使用 `session.output_modalities=["text"]`，目标设为中文。输入采用 16 kHz 单声道 16 位 PCM，按接口封装 Base64 音频块；处理原文增量/完成和译文增量/完成，结束会话等待服务端确认。[接口合同](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime)。旧 3.5 版本字段不同，不混用事件协议。

2026-10-14 用户已提供北京地域业务空间与 Key；电脑经可信 TLS/WSS 完成会话配置，见 [预检](../../output/cloud_speech/preflight.json)。密钥只通过隐藏输入进入运行进程，不写入源码或证据。提供的 OpenAI 兼容地址用于文本接口；本产品语音使用上述实时 WSS 地址。以下保留重建环境时的准备步骤：

1. 在 [百炼控制台](https://bailian.console.aliyun.com/)选择华北2（北京），查看或创建归属相应业务空间的 API Key，见[官方 Key 指南](https://help.aliyun.com/zh/model-studio/get-api-key/)。
2. 在业务空间管理查看同一空间的 Workspace ID，见[官方 ID 指南](https://help.aliyun.com/zh/model-studio/obtain-the-app-id-and-workspace-id)。模型、地域、空间与 Key 必须对应。
3. 实测前通过本机隐藏输入提供密钥；不写入 Git、命令行或公开证据。现有 DeepSeek Key 不能用于百炼。最终独立产品的凭据配置与更新方式尚未实现，开发时 RAM 输入不等于产品已经可独立启动。

## 带宽与网络前置条件

16 kHz × 16 位 × 1 声道 = 32000 B/s 原始音频。Base64 后约 42667 B/s，还需 JSON/WebSocket/TLS 开销。

921600、8N1 的串口理论载荷为 92160 B/s；这是预算，不是连续实测。当前 FPGA 实际分频及模块协商由[高速 UART](BOARD_MIGRATION.md#m5-2-921600-串口)维护。仅接 TX/RX，没有 RTS/CTS，音频分块、软件发送节奏和队列需实测后确定。

现有 AT/TCP 与短字幕验收不证明公网 DNS、可信 TLS、WebSocket、连续音频或云端翻译可用。下一阶段先核对已装 AT 固件的 SSL 能力、证书与时间条件，决定 TLS 在 ESP 或 PS 执行，再实现 WSS；不得关闭证书验证来算通过。

## C1-0 固定加密配置查询

2026-10-14 在当前 Z7N3 网络应用上执行五项固定查询，结果由 [封存阶段](../../output/z7_nano/capabilities/stage.json)绑定程序、固件响应哈希、画面比对和用户验收。方法复核见 [审查](../../output/z7_nano/capabilities/review.json)，本机协议检查见 [验证](../../output/z7_nano/capabilities/host_validation.json)。

| 查询 | 实际观察 |
|---|---|
| `AT+CIPSSLSIZE?` | 命令返回拒绝；未取得 SSL 缓冲大小 |
| `AT+CIPSSLCCONF?` | 返回四个数字 `0,0,0,0`；仅保存参数，未验证服务端证书 |
| `AT+CIPSSLCSNI?` | 未取得可用的返回字段 |
| `AT+CIPSSLCCN?` | 未取得可用的返回字段 |
| `AT+CIPSNTPCFG?` | 有配置应答；没有执行联网校时 |

旧 NONOS 与 ESP-AT 的命令和参数存在差异，查询拒绝不能直接证明 SSL 不可用，见 [官方对照](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp32/AT_Command_Set/AT_Command_Set_Comparison.html)及 [SSL/SNI/SNTP 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)。本阶段不作为可信 TLS、WSS 或连续音频放行。

实现复用原 AT 任务的 `action=2`，仅从空闲态发送这五项查询；没有配置无线关联、证书或 Flash。查询任务不会接管 UI，完成后继续演示字幕。新增的 64 字节公开结果与网络状态使用同一 sequence；工具不读取私有凭据。执行前后维持 921600 已验收配置，614400 像素比对零差异，用户确认本版画面和主按钮/A+ 正常。

## C1-1 连续上传与串口调度

2026-10-14 在 OnePlus12 局域网，以 921600 UART 与逐包 CIPSEND 发送 1536 字节已知数据块，60 秒收到 1484 块、约 37984 B/s；顺序和内容正确，串口错误计数为零，但低于 Base64/协议预算 48000 B/s。最长包间隔为 2.531 秒，存在零吞吐区间，尚未进行 30 分钟负载试验，C1 未放行。证据由 [封存测量](../../output/z7_nano/upload/cipsend_60s/latest.json)维护，用户确认同版画面清楚、主按钮与 A+ 正常，见 [实屏验收](../../output/z7_nano/upload/cipsend_60s/visual_acceptance.json)。

绘制矩形与字形时继续服务串口，单个发送缓冲持有至物理停止位完成。公开诊断快照每 200 ms 发布：实测一次 JTAG 三次读耗时 42–49 ms，原 1/20 ms 快照无法稳定读取，见 [读取计时](../../output/z7_nano/upload/cipsend_60s/jtag_read_timing.json)。这些调整通过本机验证及实际帧比对，仍不能替代有效吞吐验证。

随后进行了单连接 TCP 透传对照：51.2 kB/s 目标下仅前 8 块正确，之后损坏，退出 AT 超时；降低目标到 25.6 kB/s 后前 27 块正确，第 27 号块头部正确、载荷从偏移 1268 开始不符，此次退出普通 AT 并关闭 TCP 成功。两次都未完成 60 秒，均失败，不能用部分平均值证明容量。证据分别为 [较高速尝试](../../output/z7_nano/upload/attempts/20261014T131753388793Z/result.json)与 [低速对照](../../output/z7_nano/upload/attempts/20261014T133224462048Z/result.json)。板端错误计数为零不能证明 ESP 内部没有丢字节；根因未确定，透传不作为产品已可用路径。较高速程序的画面/按钮获用户确认，见 [对应验收](../../output/z7_nano/upload/attempts/20261014T131753388793Z/visual_acceptance.json)。

透传测试使用 NCF1 reserved 非零表示测试电脑 IPv4、port 表示目标端口；reserved=0 保持原 LAN 字幕服务。测试结束等待串口空闲、守卫间隔、发送三个加号、至少等待一秒，再确认普通 AT 模式并关闭 TCP；依据为 [ESP8266 官方 TCP/IP 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)。源码审查与隔离用例见 [定向复核](../../output/z7_nano/upload/passthrough_review.json)，其通过范围不替代上述实机失败。此实验不含真实音频、TLS 或云密钥，未进行 30 分钟负载。

2026-10-14 官方接口另列出 Qwen3.5 支持 PCM/Opus，采样率支持 8/16 kHz；当前 Qwen3.8 文档默认输入是 16 kHz PCM，其会话字段不同，不能直接套用旧字段。见 [客户端事件](https://help.aliyun.com/zh/model-studio/live-translator-client-events)。Qwen3.5 + 16 kHz Opus 可作为降低联网流量的候选，但编码器、包封装、板端 CPU 开销及同条件质量/时延均未验证；用户询问区别后要求暂停，尚未选择替代路线，不写入已确认决策。

## C2-PC 受控音频云端对照

2026-10-14 电脑通过实际 WLAN 直连北京地域服务，以实时节奏发送已有的 9.025 秒公开合成英语 PCM。API 返回了完整英语原文、中文译文及完成事件，见 [本次文件结果](../../output/cloud_speech/attempts/20261014T111342583658Z/result.json)；音频来源由 [MODEL_BACKEND](MODEL_BACKEND.md#受控文件证据与噪声修复)维护，客户端由 [run_bailian_file](../../scripts/run_bailian_file.py)实现。

首段译文在发送开始后 2.203 秒到达，定稿在 9.734 秒到达，比最后一块输入晚 0.719 秒。这是单次受控文件对照，未验证真实麦克风、代表性质量或板卡独立云端链路，未输出译音。客户端默认验证证书与主机名、绕过代理并绑定指定 WLAN 地址；完成事件才计入定稿，未完成增量不进入最终结果，异步失败会收回发送任务。五个事件/任务生命周期用例见 [独立复核](../../output/cloud_speech/review.json)。

## 分阶段放行

| 阶段 | 工作 | 放行依据 |
|---|---|---|
| C0 串口提速 | 临时协商 921600，核对实际分频和模块查询，冷/暖启动、HDMI 与常驻触摸 | 本机和串行波形检查、板端收发/错误计数、像素与用户实屏分别通过 |
| C1 持续数据与加密连接 | 用已知测试流测持续有效带宽，核对 SSL、DNS、证书、时间及 WSS | 持续负载无丢字节/溢出，真实可信加密连接；未测不承诺吞吐 |
| C2 云端公开音频 | 受控公开英语 PCM，开发电脑可先对照接口，测首字/定稿延迟与译文 | 真实 API、源文/译文、音频来源和计时证据；不是板端完成 |
| C3 板端云端上屏 | 板卡主动发测试音频，消费增量/定稿事件，扩展所需字形 | 串口/网络和完整字幕核账，实际画面与触摸验收 |
| C4 真实采样 | 音频模块到货后核对接口、电平、时钟、采样质量 | 真实音频与代表性英语识别/翻译通过 |
| C5 独立产品 | 独立上电、启动介质、联网配置、凭据管理、长时间运行 | 电脑完全断开后的整机验收；写启动介质前核对具体目标与授权 |

既有电脑模型、LAN 模拟字幕和触摸证据各保留原范围。字库覆盖/许可、间歇暗屏和真实模块资料缺口通过 [RISKS](../context/RISKS.md)及 NOW 路由，不以云端选型消除这些问题。
