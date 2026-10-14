# 云端语音翻译与独立终端

方案核验日期：2026-10-14，范围为官方接口文档与当前板卡能力之间的接入设计；尚未执行云端语音调用。选择来源见 [D13](../context/DECISIONS.md#d13-云端语音翻译与独立终端)，实际进展由 [NOW](../context/NOW.md)维护。

## 已选方案与替代项

| 路线 | 适合本产品的能力 | 状态 |
|---|---|---|
| 阿里百炼 `qwen3.8-livetranslate-flash-realtime` | 音频流输入、翻译文本及原文增量；北京地域 WSS，可仅返回文本 | 用户选择优先验证；[官方接口](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime) |
| 腾讯云实时语音翻译 | WebSocket 同时返回原文、译文与句子完成状态，英语到中文；可选 `hunyuan-translation-lite` | 备用接口，未接入；[官方文档](https://cloud.tencent.com/document/product/1093/127565) |
| 火山引擎豆包语音同传 | 中英互译，S2T 流式音频到译文 | 备用模型，未接入；[官方简介](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-3?lang=zh) |
| 云端 STT + DeepSeek | 例如 Qwen3-ASR 实时识别后再调用文本翻译，两个服务分别测延迟 | 当前不优先；[ASR 文档](https://help.aliyun.com/zh/model-studio/qwen3-asr-flash-realtime)，历史文本实测见 [MODEL_BACKEND](MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译) |

以上是接入适配性的选择，不是同条件速度、质量或费用排名。官方延迟宣传不能替代本产品实测；此次没有购买服务或代办云账号设置。

串口 C0 的执行证据由 [高速 UART](BOARD_MIGRATION.md#m5-2-921600-串口)路由；C1-0 已取得下方固定查询证据，持续传输、可信 TLS/WSS 和云端语音仍待验证。当前状态统一见 NOW，不把本文件的计划当成放行记录。

## 产品结构

音频模块 → PL 采集与必要前处理 → PS 音频队列 → ESP8266/Wi-Fi → 百炼流式翻译 → PS 字幕模型 → HDMI 中文 UI。USB 触摸控制开始、暂停、字号、原文和历史。

电脑仅做开发、测试音频生成、取证与 JTAG 调试，最终运行链路没有电脑服务依赖。继续复用裸机显示和触摸；是否需要更换网络软件或模块，由实际带宽、TLS/WSS 验证决定，不因选云端就安装 Linux。

第一版只验证英语到简体中文字幕，不生成译音。云端临时结果更新当前句，完成事件才进入历史；保留现有暂停、清空与 epoch 隔离语义，防止旧会话结果污染新会话。

## 百炼接口与凭据准备

采用北京地域模型 `qwen3.8-livetranslate-flash-realtime`。连接地址按官方契约由 Workspace ID 组成：`wss://{WorkspaceId}.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model=qwen3.8-livetranslate-flash-realtime`。

会话使用 `session.output_modalities=["text"]`，目标设为中文。输入采用 16 kHz 单声道 16 位 PCM，按接口封装 Base64 音频块；处理原文增量/完成和译文增量/完成，结束会话等待服务端确认。[接口合同](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime)。旧 3.5 版本字段不同，不混用事件协议。

用户已有阿里云账号，以下凭据是否可用尚未确认：

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
