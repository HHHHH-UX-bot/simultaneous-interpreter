# 云端语音翻译与独立终端

方案核验日期：2026-10-15，范围为官方接口、电脑受控文件对照、ESP 可信连接及 PS 调度的加密音频短测；完整 ARM 云客户端与真实采音尚未完成。选择来源见 [D13](../context/DECISIONS.md#d13-云端语音翻译与独立终端)，实际进展由 [NOW](../context/NOW.md)维护。

## 已选方案与替代项

| 路线 | 适合本产品的能力 | 状态 |
|---|---|---|
| 阿里百炼 `qwen3.8-livetranslate-flash-realtime` | 音频流输入、翻译文本及原文增量；北京地域 WSS，可仅返回文本 | 用户选择优先验证；[官方接口](https://help.aliyun.com/zh/model-studio/qwen3-5-livetranslate-flash-realtime) |
| 腾讯云实时语音翻译 | WebSocket 同时返回原文、译文与句子完成状态，英语到中文；可选 `hunyuan-translation-lite` | 备用接口，未接入；[官方文档](https://cloud.tencent.com/document/product/1093/127565) |
| 火山引擎豆包语音同传 | 中英互译，S2T 流式音频到译文 | 备用模型，未接入；[官方简介](https://docs.volcengine.com/docs/DoubaoVoice/ProductOverview-3?lang=zh) |
| 云端 STT + DeepSeek | 例如 Qwen3-ASR 实时识别后再调用文本翻译，两个服务分别测延迟 | 当前不优先；[ASR 文档](https://help.aliyun.com/zh/model-studio/qwen3-asr-flash-realtime)，历史文本实测见 [MODEL_BACKEND](MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译) |

以上是接入适配性的选择，不是同条件速度、质量或费用排名。官方延迟宣传不能替代本产品实测；此次没有购买服务或代办云账号设置。

串口 C0 的执行证据由 [高速 UART](BOARD_MIGRATION.md#m5-2-921600-串口)路由；C1-0、C1-2/3/4 分别取得固定查询、可信 TLS/WSS 及加密音频短测证据；持续带宽尚未放行，电脑云端对照见 C2-PC。当前状态统一见 NOW，不把本文件的计划当成放行记录。

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

2026-10-14 官方接口另列出 Qwen3.5 支持 PCM/Opus，采样率支持 8/16 kHz；当前 Qwen3.8 文档默认输入是 16 kHz PCM，其会话字段不同，不能直接套用旧字段。见 [客户端事件](https://help.aliyun.com/zh/model-studio/live-translator-client-events)。2026-10-15 已验证电脑 Ogg/Opus 编码、包封装和同样本接口对照，见 [C2-PC](#c2-pc-受控音频云端对照)。它可进入候选传输验证；板端编码开销及代表性质量仍未知，不把原型试验写作用户已确认替换模型。

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

此无音频阶段证明经 ESP 的云端鉴权与会话配置，不代表 ARM 自主客户端、真实语音翻译或实时吞吐通过。

其后的 [JTAG 音频诊断](../../output/z7_nano/tls/audio_diagnostic.json)未通过：5 次 Ogg append 共 1711 B 后，发送进度已落后 95.397 s，后续发送未获确认，没有完整译文。此路径逐字节经 JTAG 操作 UART，不能据此衡量 PS 自主发送或 ESP 的持续容量；不再重复这条音频发送路径，临时诊断源码已归入该尝试，正式 WSS 工具仍只做无音频会话。随后 [恢复检查](../../output/z7_nano/tls/audio_recovery.json)取得新的回显关闭 GMR 应答，确认 AT 模式、关闭连接、恢复 MUX0/auth2，UART 错误为零；未要求用户断电或重新下载。

后续 PS 自主 CIPSEND 的实际短测见 [C1-4](#c1-4-ps-调度的加密音频短测)。真实译文上屏、实时编码器、麦克风和代表性质量分别验证。

## C1-4 PS 调度的加密音频短测

2026-10-15 [80 ms 基线](../../output/z7_nano/stream/attempts/20261015T100317800974Z/result.json)通过：同一段 9.025 秒公开英语先在电脑编码为 Ogg/Opus，经一次 DDR 装载和完整回读后，由 ARM 自主按时执行普通 CIPSEND、接收 IPD；ESP8266 在 CYEA 上承担 auth2 TLS。电脑仍负责 WSS 建连/会话配置与云事件解析，没有新增局域网代理服务。

| 观察 | 实测结果与范围 |
|---|---|
| 音频与发送 | 115 个掩码 WebSocket 帧、62419 B，全部取得 SEND OK；PS 发送用时 10.081 s |
| 调度 | 最大相对计划排队落后 1.361 s；这是 9 秒文件短测，尚非持续实时容量 |
| 云端返回 | 完整英语转写、中文定稿及 session.finished，非空完成文本才算通过 |
| 电脑观察时间 | 首段译文 1.500 s、全部完成 11.375 s；含 JTAG 轮询与取回，不能当模型单独时延 |
| 收尾 | PS 释放串口、TCP 关闭、MUX0 恢复均确认；UART 溢出/帧错误及网络错误为零 |

同一个运行中的程序另做 [160 ms 对照](../../output/z7_nano/stream/packetization_comparison.json)：每八页一次 append，取得逐字一致的原文和中文，58 帧、57762 B，发送 9.441 s，最大落后 1.366 s，零 UART 错误且完整收尾。相对上表，包数约减半、帧字节少 7.46%，但最大落后未改善；首段/完成的电脑观测为 1.812/11.562 s。各一轮短测不足以证明时延改进或归因 AT 开销，不继续盲调更大分组；客户端默认保留四页，八页通过显式参数用于对照。

真实中文定稿为“在我们开始之前，请确保每个人都能听清演讲者的声音。我们会留一点时间在演讲结束后回答问题。”。当前屏幕仍是演示字幕，不把本次文件结果记作真实译文上屏、ARM 完整自主云客户端、麦克风或长时稳定性验收。

[首轮](../../output/z7_nano/stream/attempts/20261015T095254331007Z/result.json)虽已得到完整译文，但关闭命令未确认，保留 failed。定向 RAM 桩复现了交接问题：stream 状态立即报告释放，而 network 公开镜像每 200 ms 更新，旧镜像使工具拒绝清理。工具改为读取一致的 phase/request/ack，phase3 等待网络镜像归零；无须重烧板卡。随后同一程序重复执行本次短测通过，不以旧失败记录替代新证据。

实现入口为 [stream_pilot.c](../../software/network_ui/stream_pilot.c)、[文件客户端](../../scripts/pilot_z7_nano_cloud_file.py)和 [WSS 控制](../../scripts/z7_nano_wss_link.py)。复用已有 UART/AT 状态机，不新增堆分配、传输重试或任务框架。默认每四个 Ogg 页组成一次 append，约 80 ms 一包；此样本发送负载约 6.9 kB/s，不能将原来每页一次 append 的电脑对照字节率直接套用。

当前一分钟对照程序将发送/接收区各扩至 512 KiB；已通过的 9 秒短测原布局随 [短测阶段封存](../../output/z7_nano/stream/stages/short_20261015/stage.json)保留，不能用新构建覆盖历史证据。当前布局如下；其他 UI、字体与快照地址继续由 [DYNAMIC_UI](DYNAMIC_UI.md#字符状态与内存边界)维护：

| 区域 | 地址与大小 | 用途 |
|---|---|---|
| stream 公开状态 | OCM `0x0001E280`，64 B | sequence、请求/确认、发送/接收计账及错误 |
| 预装发送文件 | DDR `0x01C00000`，512 KiB | STF1 头和有界 due_ms/size/掩码帧；不含 Key |
| 接收留存 | DDR `0x01C80000`，512 KiB | PS 按长度剔除 IPD 后保存 TCP 字节，供电脑批量取回；两区均不越过 `0x01D00000` |

文件只在开始前校验一遍；发送帧至多 2048 B，接收达到容量即报错，不静默覆盖。request1 启动、request2 安全释放、request3 在已释放后复位，允许短测复用当前程序。未确认的 CIPSEND/PS 所有权写入公开状态标记，后续正常 AT 入口拒绝盲发命令；只有获得明确恢复证据才能解除，不靠重载 CPU 假定 ESP 也已复位。

[短测本机验证](../../output/z7_nano/stream/stages/short_20261015/C_validation.json)154 项及 [C 复核](../../output/z7_nano/stream/review.json)、[主机交接复核](../../output/z7_nano/stream/frontend_review.json)通过各自范围。本版 614400 像素比对零差异，常驻触摸恢复，用户在音频测试前确认画面及主按钮/A+ 正常，见 [实屏验收](../../output/z7_nano/stream/user_acceptance.json)；这不是音频负载期间的触摸时延测量。

### 一分钟持续对照

2026-10-15 [第一轮一分钟](../../output/z7_nano/stream/attempts/20261015T103034229412Z/result.json)未通过。公开参考英语循环/裁剪为 60 秒，仅用于连续负载；默认四页分组的 STF1 文件为 419414 B、752 帧，预装完整回读耗时 200.594 s，预装已移至 TLS 建连前以避免空闲会话超时。PS 在第 635 帧、累计发送 349539 B 时报告 UART RX 溢出：首个错误样本 overflow336、framing0、network_error32；接收留存 130845 B，未达到 512 KiB。最大计划落后 2590 ms；已收到多句转写/译文，但没有 session.finished，不能计为持续上传成功。源文件、构建、失败结果及限定复核已 [按本轮封存](../../output/z7_nano/stream/stages/minute_polling_20261015/stage.json)。

用户在该轮上传期间确认“画面正常，按钮响应正确”，见 [一分钟实屏反馈](../../output/z7_nano/stream/minute_user_acceptance.json)，它不替代失败的云端链路验收。板卡随后重新上电，原临时配置丢失；恢复中在未配置 PL 上读取 MMIO 触发调试超时，已停止该访问，并经用户完整断电、重新配置和新鲜 AT/GMR/关闭连接/MUX0 读回恢复，见 [恢复证据](../../output/z7_nano/stream/cold_restart_recovery.json)。后续先确认 live FPGA 配置，再读取 PL 外设。

[延后观测对照](../../output/z7_nano/stream/attempts/20261015T132109928210Z/result.json)通过：使用同一 ARM/PL、同一 60 秒音频及默认分组，仅启用 `--defer-observation`，发送开始后到音频结束 5 秒内不做 JTAG 轮询/取回，随后批量取回 PS 留存。752 帧、413386 B 全部 SEND OK，PS 用时 60011 ms，接收 163401 B，最大落后 453 ms，溢出/帧错误/网络错误为零；完整转写、中文定稿及 session.finished、释放/TCP 关闭/MUX0 均确认。用户在上传期间确认画面和按钮正常，见 [实屏反馈](../../output/z7_nano/stream/deferred_user_acceptance.json)；结束后的常驻 Host 取样仍通过。阶段已 [封存](../../output/z7_nano/stream/minute_stage.json)。

[两轮比较](../../output/z7_nano/stream/minute_comparison.json)支持 JTAG 观测扰动这一候选，但两轮之间曾完整断电，且各只有一次，不能据此宣布唯一根因或长期修复。延后模式的 104.922 秒首段/完成时间是电脑批量取回时间，不是云响应时延；预装回读耗时 203.828 秒也不属于实时发送。现有发送优先分支和 UI 翻页等待仍是待验证候选，不通过扩大 FIFO 或放宽错误检查隐去故障。固定预装文件只用于受控一分钟，后续 30 分钟应在 ARM 边消费事件后验证，不简单扩大 RAM 留存。

## C1-5 C 端云事件解码

已新增 [cloud_events.c](../../software/network_ui/cloud_events.c)和 [接口](../../software/network_ui/cloud_events.h)：对一条完成重组的 JSON 文本，提取英语定稿、中文文本、response 完成状态、会话结束与错误码；其余事件忽略。输入上限 4096 B、128 token、文本 255 个 Unicode scalar，保留 item_id/response_id，处理 UTF-8 与 JSON Unicode 转义，无堆分配。JSON 分词沿用上游 [jsmn v1.1.0](https://github.com/zserge/jsmn/tree/v1.1.0)，固定提交与未改动源码哈希见 [来源](../../third_party/jsmn/source.json)，保留 [MIT 许可](../../third_party/jsmn/LICENSE)。它是可信 API 消息的定向提取器，不承诺完整 JSON 语法校验。

[验证](../../output/z7_nano/cloud_events/validation.json)覆盖 25 项定向边界及 1929 条与 Python JSON 结果的格式对照，包含已完成一分钟会话提取的 627 条公开事件和 [固定样例](../../tests/fixtures/cloud_events_reference.jsonl)的键序/Unicode 变体。C1-6 扩展了 `conversation.item.created` 的 `previous_item_id` 提取，重新从同一次已完成的公开会话读取关联字段；[新样例清单](../../output/z7_nano/cloud_events/linked_fixture.json)仅保留允许的字段，没有新调用 API、保存原始串口或私人音频。仓库保留 16 条固定样例，可独立复现 48 种格式对照；完整会话样例保存在本机 output，不随源码上传。当前对象代码 4893 B；完整集成编译与板测范围见 C1-6。

[最初独立复核](../../output/z7_nano/cloud_events/review.json)发现并修复了等价 JSON 转义的字段比较问题；字段名、type/status 统一复用 Unicode 解码比较，相关重复字段拒绝。该版 37 个聚焦回归和 4573 B 对象已由 [C1-5 阶段](../../output/z7_nano/cloud_events/stage.json)保留；本次集成的复核另见 C1-6，不把旧哈希当成当前通过。

`cloud_event_parse` 自身静态栈帧仍为 2608 B。旧 OCM 布局无法直接容纳集成；C1-6 已分离 DDR 代码与有界运行区，并完成新 ELF 的静态布局检查。对象编译和链接均不计作板上执行。

## C1-6 ARM 云事件到字幕

2026-10-15 完成实现、离线验证和 ARM 编译，当时按用户要求暂缓板测；[离线阶段](../../output/z7_nano/cloud_live/offline_stage.json)保留该历史范围。2026-10-16 接线恢复后，同一 ELF 已完成真实云文件消费和 HDMI 用户验收，当前阶段通过；[板上阶段](../../output/z7_nano/cloud_live/board_stage.json)绑定同一次配置、程序和证据。

[cloud_frame.c](../../software/network_ui/cloud_frame.c)以单个固定上下文适配上游 [wslay 1.1.1](https://github.com/tatsuhiro-t/wslay/tree/release-1.1.1)帧处理，固定提交、逐文件哈希及许可见 [来源](../../third_party/wslay/source.json)和 [COPYING](../../third_party/wslay/COPYING)，上游文件未改动。[cloud_wire.c](../../software/network_ui/cloud_wire.c)重组最大 4096 B 文本，支持分片间插入控制帧；同批未回复 PING 合并为最新 PONG，CLOSE 优先。控制回复使用电脑经私有管道一次提供的 32 枚新随机掩码；没有堆分配、无限队列或整段会话 RX 留存。

[cloud_live.c](../../software/network_ui/cloud_live.c)通过服务端 `previous_item_id` 把译文 item 关联原文 item，保留 item/response ID；该含义由 [百炼服务端事件](https://help.aliyun.com/zh/model-studio/live-translator-server-events)与本次真实返回共同核实。原文与译文最终事件可以先后不同，只有对应 `response.done` 为 completed 才提交现有 UI 模型。最多四组待配对和最近四组完成 ID；不承诺无限去重或任意事件重排，同一 response 按 `text.done → response.done` 顺序消费。暂停或 epoch 变化丢弃待结果，不回填到恢复/清空后的会话。

提交前复用现有网络协议的字形查询，未知字符报告 `0x01000000 OR codepoint`，不改 UI、不把显示任务带入 fail(32)。本轮只为实际参考译文补“将、提、者”，字体从 337 增至 340 字，实际资产 782290 B；不是完整中文支持。显式 stop 清旧错误并撤下 consumer，下次文件任务总先 stop，避免旧错误阻挡重启或把旧 capture 路径的数据截走。成功文件任务仍保留最后字幕；主按钮目前是本地暂停/继续，不等于启动新云连接。

复用已验收 Z7N3 位流，不改 PL 或共用显示构建器。新链接器在原允许 DDR 范围中分区：

| 分区 | 地址与容量 | 当前 ELF |
|---|---|---|
| 字形/文本 | `0x01800000` 起，至云代码前上限 1.5 MiB | 782290 B |
| 云代码 | `0x01980000..0x01A00000` | 11712 B |
| 原 UI 双快照 | `0x01B00000` 起，不越过 `0x01B08000` | 22896 B |
| 云运行区 | `0x01B08000..0x01B0E000` | 21536 B |
| 最后一组公开原译 | `0x01B0E000`，2052 B | sequence + 两组 UTF-32[256] |
| 控制掩码池 | `0x01B0FF00`，128 B | 32 × 4 B |
| 公开云状态 | OCM `0x0001E2C0`，64 B | sequence/request/ack、阶段、错误与计数 |

现有 TX/RX 文件区地址保留；新 consumer 模式只使用 TX 预装文件，TCP 返回即时消费。OCM 栈范围 `0x1E300..0x1FFF0` 为 7408 B；GCC 静态帧与已识别的渲染回调嵌套控制回复路径合计 5976 B。帧发送函数自身 4144 B，与 JSON 解码的 2608 B 不同时存活；这不是完整所有路径证明或动态栈水位。原 libgcc 枚举宽度链接警告仍存在，编译未失败；本轮没有更改工具链。

[本机验证](../../output/z7_nano/cloud_live/host_validation.json)通过 29 项检查：627 条真实公开事件回放形成 14 组定稿，实际 C 状态进入既有渲染器后零缺字、零裁切，见 [离线渲染](../../output/z7_nano/cloud_live/native_cloud_caption.png)。另执行真实 Tcl RPC 循环的模拟目标检查 4 项、Python 传输/交接检查 8 项，原发送器 241 项回归通过。独立审查修复控制回复、重复原文、stop/旧报告、consumer 切换、缺字与空白成功判据，12 组定点复核通过，无未解决 Critical/Important，见 [本阶段复核](../../output/z7_nano/cloud_live/stages/offline_20261015T144257685619Z/review.json)。这些都不是板上、TLS 吞吐、触摸或 HDMI 验收。

### 2026-10-16 板上短文件验证

首次恢复时 ESP8266 部分线未接，AT 超时、RX 为零，TLS 预检没有进入鉴权或音频发送。用户接好四线后，停 USB DMA、重新下载同一程序并恢复显示/触摸，AT/GMR 正常；没有为接线问题修改软件。无 Key TLS 预检完成可信正例、错误身份拒绝、正例复测，见板上阶段内固定记录。

使用 `--onboard-ui` 上传 9.025 秒公开英语文件，经 ESP8266 可信 TLS/WSS 调用 `qwen3.5-livetranslate-flash-realtime`。ARM 接收 91 条消息，原文/译文各两次定稿，实际提交两组字幕；115 帧发送 62419 B，接收 23502 B，UART 溢出与帧错误均为零。PS 调度发送用时 9036 ms、最大调度滞后 335 ms；电脑延后观察到完成的 14.75 秒不代表云端首字或翻译时延。预编码帧装载及回读另耗 29.219 秒，不计入实时发送。

最后原文为 “We will leave a little time for questions after the presentation.”，中文为“我们会留一点时间在演讲结束后回答问题。”。用户核对实际 HDMI 中央文字及右侧定稿数 2，确认“显示正确，清楚稳定”。板上捕获与本机渲染比较 614400 像素零差异、零缺字和裁切。试验前主按钮/A+ 已获用户验收，结束后 CPU0 与常驻 USB Host 均运行；没有将此记录扩展为上传期间实际触摸按钮验收。

本次实际栈写水位在 7408 B 栈底的 4096 B 标记区观测到 2860 B 未触及前缀，写入深度为 4548 B。本次收到 PING 为零，该观测不是所有调用路径的 SP 上界，也不替代长时压力验证。任务已释放 PS、关闭 TCP 并恢复 MUX0，最终真实译文保留在屏上。固定结果、像素图、栈观测和两项用户验收均由板上阶段清单路由并记录哈希。

C1-6 验收版本边界：电脑仍负责 TLS/WSS 初始化、预编码音频及控制掩码熵，340 字字库仅覆盖受控子集，部分 UI 状态文案仍为演示占位。当前阶段完成，不代表完整 ARM 云客户端、真实采音、30 分钟或独立启动完成。下一阶段建议先移入 ARM 建连与会话控制，明确设备凭据/熵来源和会话生命周期，再验证短文件，随后才测长时与真实采音；操作由 [RUNBOOK](../context/RUNBOOK.md#c-云端语音接入)维护。

## C1-7a ARM 自主可信 TLS

2026-10-16 已完成实现、本机验证、独立代码复核和 ARM 构建；修正版已下载，614400 像素比对零差异，常驻触摸恢复，用户确认画面和主按钮/A+“一切正常”。用户随后因额度要求整理并同步，**修正版 TLS 身份对照尚未重跑**，本阶段未宣告物理 TLS 通过。固定程序、验证和当前验收范围见 [阶段清单](../../output/z7_nano/tls_boot/stage.json)。屏幕当前为演示字幕，C1-6 的真实云译仍属于其固定 ELF。

实现复用现有网络非阻塞 AT 引擎，覆盖 Wi-Fi 关联、DNS 查询、SNTP、auth2 回读、SNI/CN 与 SSL 建连。私有配置 action3 启动，reserved0 为正确身份、reserved1 仅作 wrong.invalid 对照；TLS ready 为网络 phase8。action4 从明确终态关闭 link0 并恢复 MUX0，结束为 phase0。TLS 所有权期间拒绝新的 LAN/查询请求；超时或串口错误不假装释放。凭据由 ARM 消费后擦除，目前仍经电脑私有管道写运行期 Wi-Fi 配置，没有独立配置存储。

命令语义参考 [Espressif ESP8266 ESP-AT 文档](https://docs.espressif.com/projects/esp-at/en/release-v2.1.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)，实际支持须由本模块对照确认。SNTP 初始时钟不进入证书验证，查询按 1 秒间隔、有 15 秒期限；不退回 auth0。本机检查覆盖原 LAN、校时、错误身份、auth0 回读、CONNECT 后同批 CLOSED、关闭失败、定时器回绕和重关联通知，工具模拟检查另覆盖正常退出、外层超时杀进程树及内层超时不能借用上一轮关闭证据。

首次板测停在 JOIN 的 phase6/error34，还未发出 TLS 建连，也未使用 API Key。重关联 WIFI DISCONNECT 的本机注入复现此错误，修正为 JOIN 完成后才按掉线处理，ready 重启通知仍立即失败。原失败尝试保留；受控重载后取得全新 AT/echo/GMR/UART 配置、空 FIFO 和空闲所有者证明，才归档该次不确定标记，见阶段内 recovery 入口。修正版只完成新的显示/触摸基线，不能以恢复记录替代 TLS 重跑。

独立复核已修正 TLS 所有权、同批 CONNECT/CLOSED、XSDB 进程树和跨轮释放证明问题，无未解决 Critical/Important。新增代码令 OCM 代码/数据达到 0x1D7EC，超过已有 0x1D000 边界；将 network_tick 放入既有 0x01980000 云代码 DDR 区后，__bss_end 为 0x1C15C，云代码区 0x4464 B，链接保护通过。没有缩减栈、移动邮箱或放宽容量断言，原 libgcc 枚举宽度警告仍存在。

接续顺序：先核对实际板卡状态，恢复本版显示/常驻触摸（若已断电须受控重新配置），运行 ARM 正确身份→错误身份拒绝→正确身份的 TLS 对照并确认三次关闭；通过后再做 C1-7b 的 WSS Upgrade、session.update 与现有音频/字幕链路交接。运行期 Key、掩码熵、完整字库、真实采音、长时和独立启动仍未完成，不把电脑提供的控制掩码当作独立产品随机源。

## C2-PC 受控音频云端对照

2026-10-14 电脑通过实际 WLAN 直连北京地域服务，以实时节奏发送已有的 9.025 秒公开合成英语 PCM。API 返回了完整英语原文、中文译文及完成事件，见 [本次文件结果](../../output/cloud_speech/attempts/20261014T111342583658Z/result.json)；音频来源由 [MODEL_BACKEND](MODEL_BACKEND.md#受控文件证据与噪声修复)维护，客户端由 [run_bailian_file](../../scripts/run_bailian_file.py)实现。

首段译文在发送开始后 2.203 秒到达，定稿在 9.734 秒到达，比最后一块输入晚 0.719 秒。这是单次受控文件对照，未验证真实麦克风、代表性质量或板卡独立云端链路，未输出译音。客户端默认验证证书与主机名、绕过代理并绑定指定 WLAN 地址；完成事件才计入定稿，未完成增量不进入最终结果，异步失败会收回发送任务。五个事件/任务生命周期用例见 [独立复核](../../output/cloud_speech/review.json)。

2026-10-15 在 CYEA 上以同一 9.025 秒文件完成三组新对照，见 [编码对照](../../output/cloud_speech/codec_comparison.json)。PCM 每 20 ms 一块；Opus 为 16 kHz、单声道、24 kbit/s CBR，通过已有隔离环境中的 PyAV/libopus 编码，Ogg 页目标 20 ms，每次 append 一页。

| 模型 / 输入 | 首段译文 / 全部完成 | 音频 append 的 WebSocket 帧字节率 |
|---|---|---|
| Qwen3.8 / PCM | 2.156 s / 9.750 s | 47858.61 B/s |
| Qwen3.5 / PCM | 0.703 s / 9.515 s | 47858.61 B/s |
| Qwen3.5 / Ogg-Opus | 0.672 s / 9.469 s | 11109.81 B/s |

Opus 与同模型 PCM 的最终英语原文、中文译文逐字一致；此样本上传帧字节少 76.79%。统计包含音频 Base64、JSON 和客户端 WebSocket 掩码帧，不包含建连/结束消息及 TCP/TLS 开销，不是实际线路吞吐。单次时延和相同文本不能证明代表性质量、无损或模型总体更优。

先前“裸 Opus 每包一次 append”虽获 `input_audio_format=opus` 确认，却未返回语音/翻译事件并超时；它已保留在对照证据中，实现仅保留实际通过的 Ogg 路径。Ogg 数据为 39903 B，独立重组解码恰为 9.025 秒，含末帧和 pre-skip；10 项针对模型事件、权威定稿及任务回收的离线用例通过，见 [本次复核](../../output/cloud_speech/codec_review.json)。默认客户端仍为 Qwen3.8 PCM，Opus 是当前传输试验候选；下一步复用已编码文件验证 ESP 的真实加密数据链路，暂不移植实时编码器。

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
| 1 电脑编码与模型对照 | 同一公开英语样本比较 Qwen3.8 PCM、Qwen3.5 PCM、Qwen3.5 Opus | 已通过一条短样本的 Ogg 分帧和三组对照，足以进入传输原型；记录实际上传字节、首段和定稿。代表性短句、长句、静音及噪声质量在产品选型前补充，不阻挡链路实现，不宣称 Opus 无损 |
| 2 板端可信连接 | 先验证 DNS、CA/域名/时间条件与 TLS，再验证 WSS 会话 | 与电脑对照无依赖，可独立先做。TLS 探测先不用云密钥；可信链路成立后才发鉴权。需要正例及证书/身份不匹配被拒绝的证据；只收到 CONNECT、配置应答或 auth=0 均不算通过 |
| 3 按选定编码测试持续传输 | 普通 AT 客户端在音频负载下上行，并接收模拟的增量/定稿事件 | 以第 1 步实测消息尺寸和发送节奏为依据，检查字节/序号、最长停顿、音频队列峰值及 UI 响应。1 分钟正确后再测 30 分钟；目标平均带宽留至少 50% 规划余量，暂停/拥塞不能造成队列无限增长或悄悄丢音频。加密链路另测，LAN 数字不替代 TLS 数字 |
| 4 板上预存音频的云端上屏 | 预编码短音频随程序放在板上，由板卡独立发送并显示真实译文 | 依赖第 1–3 步。先不用实时编码器和麦克风，电脑不代理 API。补本组真实译文的字形，核对增量、定稿、暂停、清空和旧会话隔离；用户验收实际屏幕。该步只证明文件闭环，不证明实时采样 |
| 5 实时编码与真实采音 | 先用已知 PCM 测 PS 编码，再接音频模块 | Opus 方案选定后才移植编码器，测编码时间、CPU、缓冲与显示/触摸并行负载。模块到货后核对电平/协议/时钟/引脚，采样先旁路；已知输入可回放、无掉样/持续削波后才评价识别质量 |
| 6 独立启动与整机验收 | 脱离电脑供电/运行，配置网络并持续出字幕 | 补网络与凭据配置、启动介质、完整中文覆盖和字体许可；在已确认目标及授权内写启动介质。分别验收冷启动、断网恢复、至少 2 小时真实负载及端到端时延；间歇暗屏/供电问题不得直接归为已解决 |

Opus 候选的纯音频算术预算为 3000 B/s、Base64 约 4000 B/s；实际 Ogg 页和每条 JSON 的开销见 [C2-PC](#c2-pc-受控音频云端对照)，传输容量按实际消息节奏衡量。官方只确认 Qwen3.5 可用 `opus`，不能由此推定 3.8 可用同字段或板端编码开销，见 [百炼客户端事件](https://help.aliyun.com/zh/model-studio/live-translator-client-events)。

板端 TLS 需另行核实当前固件与证书配置：官方 ESP-AT 对 `auth_mode=0` 定义为不认证、`2` 为加载 CA 验证服务端；官方命令存在不等于当前模块已具备合适的 CA 或身份校验能力，见 [ESP8266 TLS 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html#at-cipsslcconf-query-set-ssl-clients)。若固件/证书条件不满足，先形成保留模块并修固件、或替换网络模块的成本与实现范围比较；不通过关闭验证或直接投入 PS 自制 TLS 栈绕过评估。

每个瓶颈先安排最多两轮有明确假设的短对照；仍无进展时汇总证据并重选路线，不持续轮换发送间隔、重复下载和要求用户点屏。仅电脑实验无需板端重载；PL/PS/显示未变化时沿用其原证据范围，集成版仍按实际改动补对应验证。失败的透传保留诊断入口，当前不为其安排长测。替代模型/编码仍是建议；已授权实施按阶段验证后自动继续。
