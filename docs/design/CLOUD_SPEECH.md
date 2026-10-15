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

## C1-2 CYEA 与板端 TLS 预检

2026-10-15 恢复实施，电脑已在 CYEA，原高速显示/触摸固件恢复且用户确认正常。以下是电脑通过 JTAG 驱动空闲 UART 的可行性预检，尚未移入 ARM 自主云客户端；不发送云端 Key、HTTP 鉴权或音频。

[首次公开清单](../../output/z7_nano/tls/attempts/20261015T075839301938Z/inventory.json)确认当前 AT 固件与 8 KiB `client_ca` 分区。[初次预检](../../output/z7_nano/tls/attempts/20261015T080737084418Z/preflight.json)中，模块加入 CYEA、取得 `192.168.31.239`、解析百炼语音域名并同步到当前 UTC；`SYSSTORE=0` 已读回后才提交无线凭据，认证模式 2、SNI 与 CN 设置均应答成功，但受信 TLS 连接返回 ERROR。

该分区实际包含两条 `ESP Root CA S1` 测试证书。电脑经实际 WLAN 对当前语音端点完成系统信任与域名验证，链为 GlobalSign R3，见 [电脑证书链](../../output/z7_nano/tls/pc_chain.json)。[无 Key 传输对照](../../output/z7_nano/tls/transport.json)在 auth=0 时连接成功，明确关闭后已恢复并读回 auth=2，串口错误为零；它只证明 TLS 传输可行，不是受信连接放行。

已从 [GlobalSign 官方证书目录](https://support.globalsign.com/ca-certificates/root-certificates/globalsign-root-certificates)取得 R3，DER 与本次电脑验证链的根完全一致；可复用的[公开证书](../../hardware/esp8266/globalsign_r3.pem)和[来源](../../hardware/esp8266/source.json)纳入源码。[候选产物](../../output/z7_nano/tls/ca_candidate/candidate.json)按 [ESP-AT AtPKI 格式](https://raw.githubusercontent.com/espressif/esp-at/release/v2.2.0.0_esp8266/tools/AtPKI.py)生成单 CA。

CA 更新已完成：[完整 8192 字节备份](../../output/z7_nano/tls/attempts/20261015T081859598577Z/ca_operation.json)及[严格对象检查](../../output/z7_nano/tls/ca_backup_strict.json)通过；[安装记录](../../output/z7_nano/tls/attempts/20261015T082245676385Z/ca_operation.json)确认现场内容与备份相同后，仅擦除 `client_ca` 并写入 1244 字节，回读逐字节一致、串口错误为零。不更新 ESP 固件或其他证书/私钥分区。本次执行的工具从候选产物取证书；当前版本改从上述源码资源取同一张证书，输出图像 SHA256 相同。

[安装后安全预检](../../output/z7_nano/tls/attempts/20261015T082744827634Z/preflight.json)实际取得正确域名成功 → 错域名 ERROR → 恢复正确域名再次成功，两次已建立连接均明确关闭，认证保持模式 2，UART 错误为零。已通过本次硬件、网络及端点的无 Key 可信 TLS 范围，尚不等于 WSS 鉴权、ARM 自主云客户端或音频传输通过。

工具为 [公开清单/传输对照](../../scripts/probe_z7_nano_tls_inventory.py)、[无 Key 预检](../../scripts/probe_z7_nano_tls.py)和 [CA 备份/更新](../../scripts/provision_z7_nano_ca.py)。仅接管 fresh 应用：网络 request/ack 均为零、PS 网络空闲、UART 速率与平台匹配，且全程持硬件锁。原始 UART 回复留在进程 RAM；记录只保留公开字段。SNI/CN/SNTP 是否持久化未验证，不把 SYSSTORE=0 推广为所有设置绝不写 Flash。

## C1-3 ESP8266 云端 WSS 会话

2026-10-15 [无音频实测](../../output/z7_nano/tls/attempts/20261015T084434242382Z/wss.json)通过：ESP8266 在 CYEA 上执行 auth2 TLS，由电脑经私有 JTAG/UART 通道驱动命令，取得 HTTP 101、`session.created`，并收到中文/纯文本配置的 `session.updated`。UART 错误为零；TCP 明确关闭，MUX 恢复单连接。云 Key 未进入参数、源码或原始日志。

[WSS 入口](../../scripts/probe_z7_nano_wss.py)复用现有私有 Bridge、AT/IPD 通道及本机 `websockets 16` 的 Sans-I/O 客户端，处理标准升级、随机掩码及帧校验；没有新增依赖。发送的提示与 SEND OK、关闭时的 OK 都从按长度剔除 IPD 载荷后的控制流确认，避免网络内容冒充串口应答。[三项分片验证](../../tests/test_ipd_control.py)覆盖控制分离和原有帧 API。

此阶段证明经 ESP 的云端鉴权与会话配置，尚未上传音频，不代表 ARM 自主客户端、真实语音翻译或实时吞吐通过。下阶段先按后续计划验证编码/实际负载，再实现裸机的云端客户端及预存音频闭环。

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

## 2026-10-15 复盘后的验证顺序（建议）

本节为复盘形成的建议顺序。用户随后于 2026-10-15 明确恢复实施并指定 CYEA，已先执行独立的板端可信连接预检，结果见 [C1-2](#c1-2-cyea-与板端-tls-预检)；仍未选定 Qwen3.5/Opus 或更换网络模块。

首要缺口是板端可靠传输与可信 TLS/WSS。普通 CIPSEND 有数据正确的证据，透传有载荷损坏的证据；优先复用普通 AT 发送机制实现所需客户端，不把减小音频流量视为透传损坏已修复。原 48000 B/s 是 16 kHz PCM/Base64 路线的预算，不是 Opus 路线的固定门槛。

| 顺序 | 要验证的最小问题 | 依赖与通过标准 |
|---|---|---|
| 1 电脑编码与模型对照 | 同一小组公开英语音频比较 Qwen3.8 PCM、Qwen3.5 PCM、Qwen3.5 Opus | 先用一条短样本确认服务端接收的 Opus 封装/分帧，再比较少量短句、长句与静音。分别记录实际上传字节、首段、定稿和明显误译；三组用于区分模型变化与压缩影响，不宣称旧模型质量较差或 Opus 无损 |
| 2 板端可信连接 | 先验证 DNS、CA/域名/时间条件与 TLS，再验证 WSS 会话 | 与电脑对照无依赖，可独立先做。TLS 探测先不用云密钥；可信链路成立后才发鉴权。需要正例及证书/身份不匹配被拒绝的证据；只收到 CONNECT、配置应答或 auth=0 均不算通过 |
| 3 按选定编码测试持续传输 | 普通 AT 客户端在音频负载下上行，并接收模拟的增量/定稿事件 | 以第 1 步实测消息尺寸和发送节奏为依据，检查字节/序号、最长停顿、音频队列峰值及 UI 响应。1 分钟正确后再测 30 分钟；目标平均带宽留至少 50% 规划余量，暂停/拥塞不能造成队列无限增长或悄悄丢音频。加密链路另测，LAN 数字不替代 TLS 数字 |
| 4 板上预存音频的云端上屏 | 预编码短音频随程序放在板上，由板卡独立发送并显示真实译文 | 依赖第 1–3 步。先不用实时编码器和麦克风，电脑不代理 API。补本组真实译文的字形，核对增量、定稿、暂停、清空和旧会话隔离；用户验收实际屏幕。该步只证明文件闭环，不证明实时采样 |
| 5 实时编码与真实采音 | 先用已知 PCM 测 PS 编码，再接音频模块 | Opus 方案选定后才移植编码器，测编码时间、CPU、缓冲与显示/触摸并行负载。模块到货后核对电平/协议/时钟/引脚，采样先旁路；已知输入可回放、无掉样/持续削波后才评价识别质量 |
| 6 独立启动与整机验收 | 脱离电脑供电/运行，配置网络并持续出字幕 | 补网络与凭据配置、启动介质、完整中文覆盖和字体许可；在已确认目标及授权内写启动介质。分别验收冷启动、断网恢复、至少 2 小时真实负载及端到端时延；间歇暗屏/供电问题不得直接归为已解决 |

Opus 的候选起点为 16 kHz 单声道、24 kbit/s：音频本体约 3000 B/s，Base64 约 4000 B/s；JSON、分帧、WebSocket 和 TLS 仍需另计，以上是算术预算，尚无本项目编码/带宽实测。官方只确认 Qwen3.5 可用 `opus`，不能由此推定具体包封装、3.8 可用同字段或板端编码开销，见 [百炼客户端事件](https://help.aliyun.com/zh/model-studio/live-translator-client-events)。

板端 TLS 需另行核实当前固件与证书配置：官方 ESP-AT 对 `auth_mode=0` 定义为不认证、`2` 为加载 CA 验证服务端；官方命令存在不等于当前模块已具备合适的 CA 或身份校验能力，见 [ESP8266 TLS 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html#at-cipsslcconf-query-set-ssl-clients)。若固件/证书条件不满足，先形成保留模块并修固件、或替换网络模块的成本与实现范围比较；不通过关闭验证或直接投入 PS 自制 TLS 栈绕过评估。

每个瓶颈先安排最多两轮有明确假设的短对照；仍无进展时汇总证据并重选路线，不持续轮换发送间隔、重复下载和要求用户点屏。仅电脑实验无需板端重载；PL/PS/显示未变化时沿用其原证据范围，集成版仍按实际改动补对应验证。失败的透传保留诊断入口，当前不为其安排长测。替代模型/编码仍是建议；已授权实施按阶段验证后自动继续。
