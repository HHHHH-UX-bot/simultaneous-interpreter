# 旧阶段进展与风险快照

归档日期：2026-10-21，来源为整理前 NOW 与 RISKS 的旧阶段段落。以下“当前/下一步/本轮”仅属于各原日期和构建，不表示现在的板态。不能据此执行旧板命令或模块供电建议；接线以 [现行 MAP](../MAP.md) 和板级 JSON 为准。最新目标、已知故障及任务顺序只由 [NOW](../NOW.md)维护。

## 原阶段进展

下面保留此前各阶段的原有记录，日期和验收范围不外推到当前候选。

## 当前目标与范围

长期目标是形成语音进入板卡、Wi-Fi 直接调用云端语音翻译、中文字幕上屏、触摸交互的独立产品，使用时不依赖电脑。2026-10-14 用户选择阿里百炼一体方案，见 [D13](../DECISIONS.md#d13-云端语音翻译与独立终端)和[CLOUD_SPEECH](../../design/CLOUD_SPEECH.md)；旧系统方案保留历史范围。

2026-10-16 用户新增近期目标：麦克风 → 音频模块 → 板卡 → 耳机实时回环，并把延迟时间实时显示在 HDMI 屏幕上，已有画面及功能不动；接线事实、时延分层定义与分阶段计划见 [AUDIO_LOOPBACK](../../design/AUDIO_LOOPBACK.md)，决策见 [D15](../DECISIONS.md#d15-音频实时回环与时延显示)。2026-10-17 已完成 PL 实现定稿（WM8731 作 I2S 主）、Vivado 2018.3 构建、下载与板级取证：I2C 11 条写入有真实 ACK，但模块未产生任何 BCLK/LRCLK/ADCDAT；同时发现模块 J3 与开发板 JP2 的电源/地针文档有误（J3-1 是 GND、JP2-12/30 是 GND）。当前卡在硬件供电/共地，详见 [AUDIO_LOOPBACK §10](../../design/AUDIO_LOOPBACK.md#10-板级取证2026-10-17)。

当前推进至 [C1-7a ARM 自主可信 TLS](../../design/CLOUD_SPEECH.md#c1-7a-arm-自主可信-tls)。实现、本机验证、代码复核和 ARM 构建通过；修正版已下载，画面与常驻触摸获用户验收。首次入网通知误判已修正并恢复 AT，但修正版的 TLS 身份对照尚未重跑。2026-10-16 用户因额度要求整理、同步 GitHub，本轮停止硬件实验；当前屏幕是演示字幕，证据见 [本阶段](../../../output/z7_nano/tls_boot/stage.json)。

2026-10-13 用户更换为 Z7-Nano 7010，并要求移植现有工程，见 [D9](../DECISIONS.md#d9-换为-z7-nano-7010-并继续产品迁移)。[板级迁移](../../design/BOARD_MIGRATION.md)已取得显示、PS/DDR、动态中文、USB 触摸与基本网络的分项证据；当前实施缺口集中在板端云连接、连续音频、完整字库及独立启动。旧 RevB 的验收仅属于旧板；电脑 P5 文件推理及接口证据可保留原范围。

此前 project-context-docs 建档交付已完成。用户随后明确要求按阶段计划继续；旧板 P2b/W 阶段已验收，新板独立重验。临时下载与 Git 上传分别依据用户实施指令和 D12，不依据文档本身；未改写 QSPI/SD 或 ESP 固件。

旧板 P2b-3 的模拟中文 UI、控制与长期运行，W4-0/W4-1 的自主 ESP 网络分别通过原范围。2026-10-12 电脑模型与文件接口通过原范围；真实音频和触摸尚未完成。历史入口见下方，不能用旧板记录表示新板已通过。

## 当前进展

新板 M0/M1 已通过：原理图/HDMI/LED 约束与正确 SDK 7010/16 位 DDR 来源已核对，JTAG 为单个 `xc7z010`、JTAG 启动模式。静态 UI 构建的引脚、生成时钟、时序及 DRC 通过；临时 JTAG 配置后，用户确认画面正常、清楚稳定。证据见 [新板构建](../../../output/z7_nano/static/build.json)、[实现检查](../../../output/z7_nano/static/implementation.json)、[下载与实屏验收](../../../output/z7_nano/program.json)、[代码复核](../../../output/board_migration/review_static.json)、[运行器检查](../../../output/board_migration/runner_validation.json)。

M2 已实测通过：4 MiB 区域的多种数据模式与高地址抽样，共 5242948 次字检查，缓存/MMU 关闭；见 [DDR 实测](../../../output/z7_nano/ddr/run.json)及[复核](../../../output/board_migration/review_ddr.json)。这不是全容量内存资格测试。

M3 新板双缓冲平台与 ARM UI 已通过，见 [构建](../../../output/z7_nano/display/build.json)、[实现检查](../../../output/z7_nano/display/implementation.json)、[ARM 程序](../../../output/z7_nano/display/app_build.json)、[配置](../../../output/z7_nano/display/program.json)、[实际运行](../../../output/z7_nano/display/run.json)、[复核](../../../output/board_migration/review_display.json)。实际抓取 614400 个像素与电脑同状态重建零差异，见 [帧比对](../../../output/z7_nano/display/capture.json)；抓屏后恢复运行，刷新继续且欠载/格式/程序错误均为零，见 [后续取样](../../../output/z7_nano/display/sample.json)。用户确认“画面清楚正常”，见 [实屏验收](../../../output/z7_nano/display/user_acceptance.json)。

M4-0 USB 控制器/PHY 已实测通过：`0424:0007`、scratch 写读 0x5A、Host 模式 3，端口检测到连接；CPU0 继续运行、CPU1 探测后停止。见 [实测](../../../output/z7_nano/usb/probe_run.json)和 [USB_TOUCH](../../design/USB_TOUCH.md)。PHY 身份与屏幕 USB 设备身份分别验证。

M4-1 裸机 Host 已实测枚举屏幕 `1a86:e5e3`，120 秒稳定接收 686 条 44 字节 HID 报告，见 [运行](../../../output/z7_nano/usb/host_run.json)及[真实按下/松开](../../../output/z7_nano/usb/touch_capture.json)。用户已断开会向电脑传触摸的 POWER USB，单 TOUCH USB 供电与显示正常；接线事实见迁移专题。USB DMA 的停机确认已接入枚举、PHY 重探测和显示重配置入口。

M4-2 已实测通过：[四角/中心](../../../output/z7_nano/usb/touch_corners.json)均在对应区域，恰好 5 次按下和 5 次松开；保持接线的 240 秒内收到 384 条报告，无断开、无丢弃点击。该证据绑定校准版 CPU1，尚不代表按钮已通过。

M4-3 七类真实控件、队列核账及常驻 start/sample/stop/restart/sample 已通过；用户确认持续触摸与 UI 响应正常，见 [实屏控件验收](../../../output/z7_nano/usb/touch_controls_acceptance.json)和[常驻生命周期](../../../output/z7_nano/usb/live_lifecycle.json)。2026-10-14 用户确认板卡重新上电且接线不变，已恢复同一显示/触摸固件，整帧比对和持续取样通过；本次实屏确认“画面正常，按钮有正确响应”，见 [本次恢复](../../../output/z7_nano/usb/latest_restore.json)。此前无拔插时的黑线/渐暗仍未定位，对照由 [USB_TOUCH](../../design/USB_TOUCH.md#触摸启动后的暗屏重现待定位)维护。

M5-0 新板 UART/自主 AT 已通过，[封存的阶段证据](../../../output/z7_nano/wifi/uart_stage.json)绑定 Z7N3 配置、AT/回显/固件响应、零串口错误、整帧及持续触摸。M5-1 的 [封存网络阶段](../../../output/z7_nano/wifi/network_stage.json)绑定三句已知字幕、精确历史、三种异常拒绝、断线重连、整帧零差异和常驻触摸；用户确认“显示正确，清楚稳定”。该历史程序最后一句及三条定稿不是模型或麦克风输出，不代表当前高速程序的运行状态。方法由 [M5](../../design/BOARD_MIGRATION.md#m5-新板-uart-与自主-at)维护。

M5-2/C0 高速串口阶段已按其范围通过，见 [封存阶段](../../../output/z7_nano/uart_921600/stage.json)：完整断电后自动协商及屏幕断开时暖重载均通过；模块查询 930232、FPGA 分频 54，帧错误/溢出为零。同版 614400 像素比对零差异，USB generation 与 CPU0 消费者匹配，用户确认画面与按钮正常。此处为封存基线；当前联网进展见 C1-1。此前一次 DAP AHB 事务错误经完整断电恢复，根因仍未定位，见 RISKS R16。实现由 [高速 UART](../../design/BOARD_MIGRATION.md#m5-2-921600-串口)维护。

C1-0 固定加密配置查询已完成并取得本版实屏验收：五项实际应答、匹配程序的完整帧比对、高速 UART 和常驻触摸已封存，见 [本阶段](../../../output/z7_nano/capabilities/stage.json)。用户确认画面正常、按钮响应正确。实际查询结果和方法由 [CLOUD_SPEECH](../../design/CLOUD_SPEECH.md#c1-0-固定加密配置查询)维护；连续音频传输、可信 TLS/WSS 和云端语音仍未放行。当前界面仍是演示内容。

C1-1 完成串口/绘制调度调整及 60 秒逐包上传：数据正确，但容量尚不足；用户确认同版画面和按钮正常。随后两档速率的透传对照均出现载荷损坏，未放行；最新低速对照已退出 AT 并关闭 TCP。实际结果与范围由 [CLOUD_SPEECH](../../design/CLOUD_SPEECH.md#c1-1-连续上传与串口调度)维护，尚未执行 30 分钟负载，未放行板端音频。

C2-PC 已完成同一公开音频的 Qwen3.8 PCM、Qwen3.5 PCM、Qwen3.5 Ogg/Opus 三组对照，包封装、最终文本及实际上传帧负载已核对，见 [云端结果与边界](../../design/CLOUD_SPEECH.md#c2-pc-受控音频云端对照)。Ogg/Opus 可进入传输原型，不是板端编码、代表性质量或麦克风验收；屏幕仍为演示字幕。

2026-10-15 用户指定 CYEA 并恢复实施。板卡临时配置已丢失，已重新加载原位流和相同 ARM 固件，整帧比较通过，常驻触摸重新启动；用户确认画面正常、主按钮和 A+ 正确响应，见 [本次恢复](../../../output/z7_nano/usb/restore_20261015.json)。已修复同板新配置被旧 Host 记录阻挡的接续问题，保留实际 CPU1 停止与 DMA Halt 检查，见 [限定复核](../../../output/z7_nano/usb/restart_review.json)。

C1-2 已完成 CYEA 关联、DNS、时间同步、原 CA 备份及 GlobalSign R3 安装/回读，无 Key 可信 TLS 对照通过。[C1-3](../../design/CLOUD_SPEECH.md#c1-3-esp8266-云端-wss-会话)进一步实测通过 ESP8266 到百炼的 WSS 鉴权/中文纯文本会话配置。随后 JTAG 逐字节音频诊断因发送严重落后而未通过，模块已恢复 AT、连接关闭、MUX0/auth2 和零 UART 错误；不再重复该音频路径。后续 [C1-4](../../design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)已通过 ARM 自主调度的 9.025 秒 Ogg/Opus 加密文件收发，得到完整转写与中文定稿，收尾释放/关闭均确认、UART 错误为零。首轮镜像发布交接问题已定位修复，同版重复执行通过。WSS 控制和云事件解析仍在电脑，屏幕仍为演示字幕。短测程序和实屏验收见 [本版](../../../output/z7_nano/stream/user_acceptance.json)。后续 512 KiB 程序的一分钟上传在中途 JTAG 轮询时溢出；断电恢复后，延后观测的同音频对照通过，752 帧全部确认，UART 错误为零，用户确认画面与按钮正常。范围与两轮证据由 [一分钟持续对照](../../design/CLOUD_SPEECH.md#一分钟持续对照)维护，不把单次通过当作长期修复。[C 端事件解码](../../design/CLOUD_SPEECH.md#c1-5-c-端云事件解码)独立阶段完成本机验证及 ARM 对象编译；后续集成与板上验收见下方 C1-6。

C1-6 的实现、离线验证、ARM 编译和独立复核见 [离线阶段](../../../output/z7_nano/cloud_live/offline_stage.json)；后续同一程序的 TLS、短文件真实字幕、实际像素及用户验收见 [板上阶段](../../../output/z7_nano/cloud_live/board_stage.json)。具体数值和能力边界统一由 [C1-6 专题](../../design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕)维护，仍依赖电脑初始化云会话与预装音频。

P5c-0 DeepSeek 独立文本已实测：17 项离线验证和独立复核通过，操作者隐藏输入密钥后，一条公开英语参考文本取得完整中文，耗时 6.297 秒；见 [MODEL_BACKEND](../../design/MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译)。尚未接入 ASR/HTTP 或上屏，人工译文质量与流式首字时延待验收；参考译文的三个缺字已在 C1-6 补齐；本次云文件实屏覆盖范围以 C1-6 为准，不等于 DeepSeek 上屏验收。历史选择由 [D11](../DECISIONS.md#d11-中文翻译优先使用-deepseek-api)维护，当前主线为 D13 百炼。

用户授权维护 Git 和上传关联 GitHub 账号。首次 241 个源码/文档文件已提交并推送私有仓库，远程 main 与本地一致；后续上传核验由 [Git 结果](../../../output/github/publish.json)维护，仓库映射见 [MAP](../MAP.md#git-与-github)。

电脑 P5a/P5b 的文件推理与 HTTP 接口保持 2026-10-12 原验证范围，见 [MODEL_BACKEND](../../design/MODEL_BACKEND.md)。旧 RevB 的分阶段验收、显示诊断及接续过程已归档到 [历史基线](../history/revb_baseline_2026-10-12.md)；不是新板当前运行状态。

## 阻塞与待核实

动态中文当前为有界演示字库，不能显示任意真实译文；最终字体覆盖及再分发许可未核清。新板七类 USB 控件和常驻 Host 生命周期已通过上述范围；间歇暗屏、热插拔和整机长期运行尚未验收，设置按钮仅视觉占位。范围与数据边界见 [DYNAMIC_UI](../../design/DYNAMIC_UI.md)。

尚需核实以下事项：

- 音频模块：2026-10-16 用户确认主控为 **WM8731** 并提供 J3→JP2→FPGA 接线表。按 Z7-NANO_R21.pdf 第 5/13 页逐针核对：JP2 为 CON40A（2x20）排针、走 Bank35（`GPIO2_5P/5N`、`6P/6N`、`7P/7N`、`8P`），H16/H17 为 MRCC 时钟脚。**2026-10-17 更正电源/地针**：JP2 11=`VCC_5V`、29=`VCC_3V3`、12/30=GND；模块 J3-1=GND、2=`VCC5V`、37/38=GND、39/40=`D3V3`（旧记录把 J3 1/2 都记为 VCC5V、把 JP2 12/30 记为电源针，均错误）。旧说法“FPGA20引脚”不再使用。第一版英语转中文已在 DECISIONS D8 确认。
- 音频引脚占用已核对：当前板 HDMI 在 Bank34（`U18/U19`、`V20/W20`、`T20/U20`、`N20/P20`，见 [source.json](../../../hardware/z7_nano/source.json)），与音频 7 个 Bank35 信号**不重叠**，无需改脚；旧板 [hdmi.xdc](../../../constraints/hdmi.xdc) 的“冲突”判断已在 R17 更正。模块原理图 AN831.pdf 也已核对：`U11=WM8731SEFL` + `Y3=12.288 MHz 晶振`（不需 FPGA 供 MCLK）、`U8=LT1117-3.3`、J3-1=GND、J3-2=`VCC5V`、J3-37/38=GND、J3-39/40=`D3V3`，信号针 3–9 与接线表一致。**上板前置变为 I2C 配置**：需先写 R7 主模式、R8 采样率、R6/R4/R5/R2/R3 与 R9 使能，模块才会输出 BCLK/LRCLK（见 R18）。[AUDIO_LOOPBACK](../../design/AUDIO_LOOPBACK.md)
- 音频模块供电（2026-10-17 用户万用表）：开发板 JP2-11 的 5 V 在屏幕带电时约 4.3 V、拔屏 4.6 V；AN831 的 D3V3(J3-39/40) 与 codec 的 VCC3V3 是两个网络，codec 3.3 V 只由板上 LT1117-3.3 从 5 V 产生，4.3 V 输入处在压差边缘。**待实测模块 VCC3V3**（U8 OUT/TAP 或 C39 正端对模块 GND）才能判断是供电余量还是模块晶振问题，见 RISKS R23。
- 新板 USB 枚举、真实输入、四角坐标与 UI 控制已通过各自范围；完整 VBUS 电压/电流测量尚未做。
- 触摸启动与暗屏的触发顺序、USB 供电实际电压/电流和屏幕内部电路仍待定位；最新诊断及当前接线由 USB_TOUCH/迁移专题维护。独立屏幕电源对照未执行，不先行认定电流不足。
- 当前使用裸机 USB Host/HID；独立上电启动介质与产品启动方式尚未决定。新板最小 PS/DDR 实测结果由 JTAG/OCM 获取。
- 真实 ASR/MT 的代表性质量、人工译文与完整时延。公开模型、隔离运行环境和受控文件推理已有证据，边界与后续缺口由 [MODEL_BACKEND](../../design/MODEL_BACKEND.md) 维护。参考译文的三个缺字已补齐并离线渲染通过；仍是有限字库，新程序的画面、动态栈水位和持续收发待板测。

NAS 资料位置已核清，原件与缓存哈希一致，来源见 [MAP](../MAP.md#原理图来源与可访问性)。没有把旧路径失效误判为 NAS 关机。

## 云端同声传译（进行中）

用户 2026-10-17 给出百炼云工作空间端点与 Key，要求板端麦克风实时同传、HDMI 只显示中文译文、右上角显示传译延迟。电脑侧已按仓库既有协议验证通过（见 [pc_protocol.json](../../../output/cloud_speech/pc_protocol.json)）：`wss://ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com/api-ws/v1/realtime?model=qwen3.8-livetranslate-flash-realtime`，`session.update` 设 `translation.zh` + 纯文本，20 ms 16 kHz PCM `input_audio_buffer.append`，中文首字 2.19 s、全文 13.27 s，事件类型齐全。PL 侧新增 ESP8266 UART 邮箱（0x43c30000，ID 0x57494631 已由 PS 读通）与麦克风 PCM 抽头（0x43c20080/0x43c20084，48 kHz 计数已验证）；**ESP 模块已实测打通（2026-10-17 21:20）**：真实波特率 **115200**（分频 434），`AT` 返回 `OK`；此前误判是因为读串口寄存器时有效位应为 bit31（写成了 bit8）且按 921600 采样。预热一个哑字节后首字符不再损坏。下一步：按 115200 配置应用、写入热点 OnePlus12、跑通联网/TLS/WSS。

## 下一步

- 音频实时回环**已通**（2026-10-17）：PL 侧按 **WM8731 作 I2S 时钟主** 定稿——软 I2C 初始化（R4=0x015 麦克风+DAC、R7=0x042 主模式 I2S 16-bit、R8=0x000 normal 48 kHz）、`i2s_loopback.v` 从机回环（2 帧 FIFO）+ L1 时间戳、右上角 `L1 nnnn us` 叠加；六个自检 testbench 通过，Vivado 2018.3 构建（setup/hold 7.419/0.032 ns）与下载通过。**根因与解决**：模块 3.3 V 由板上 LT1117-3.3 从 J3-2 的 5 V 产生（J3-39/40 的 D3V3 与 codec 的 VCC3V3 是两个网络，接它不给 codec 供电）；开发板 USB 5 V 被屏幕负载拖到 4.3 V，处在 LT1117 压差边缘，导致 codec 能应答 I2C 但晶振不启动。用户改用充电宝主供电、排针 5V 直供模块后一切正常。**实测证据**：[program.json](../../../output/z7_nano/audio_loopback/program.json)：`live=1`、`codec_found=1`、`regs_written=11`、`bclk_alive=1`、引脚计数 BCLK 48696307 / ADCLRC 763781 / DADLRC 766854 / ADCDAT 3928603、`occ=2`、`over=0/under=0`、`L1=192 cycles = 62 µs`、min/max=0/62；显示 `format_errors=0`、整帧 614400、`display_ok=1`。细节见 [AUDIO_LOOPBACK §10](../../design/AUDIO_LOOPBACK.md#10-板级取证2026-10-17)。**屏幕供电**：屏由开发板 PS USB0 主机端口 VBUS 供电（PORTSC1.PP），只有 CPU1 USB host 固件或等价初始化才会打开；`scripts/z7_nano_audio.tcl run` 已内置该初始化（`USB0_PORT_POWER PP=1`），烧录后屏幕即有电。**下一步**：用户耳机听感验收、屏上 `L1 0062 us` 实屏确认、持续运行（≥30 min 溢出统计）与 L3 声学时延；当前是 L1 数字域读数，不等于声学端到端。

音频回环主线（用户 2026-10-16 指定）：下一步是用项目许可的 Vivado 跑 [A 阶段](../../context/RUNBOOK.md#a-音频实时回环与时延显示后续)的构建（`scripts/run_audio_loopback.py build`），再按模块接线并依次确认 I2C ACK、BCLK 出现、耳机出声、屏上读数；A0 的剩余核对（模块 I2C 地址、供电/GND、丝印方向）在接线时完成。文档里的步骤不构成执行授权。

接续云端主线先核对实际板卡/程序状态，按 [C1-7a](../../design/CLOUD_SPEECH.md#c1-7a-arm-自主可信-tls)重跑 ARM TLS 正确/错误/正确身份及关闭对照；本版显示与触摸已经用户确认。TLS 通过后再做 ARM WSS Upgrade 与会话配置，并接回同一程序的预装文件发送/云事件消费；随后分别验证长时、真实采音、完整字库和独立启动。音频模块的型号与逐针映射已核对（引脚与当前板显示不重叠），MCLK 来源、电平与 JP2 丝印方向待确认，见 [AUDIO_LOOPBACK](../../design/AUDIO_LOOPBACK.md)；精简实现要求见 [AGENTS](../../../AGENTS.md#实现与换板约定)。

2026-10-14 的额度暂停点保留为 [历史接续记录](../../../output/z7_nano/upload/pause.json)，现已被本次继续指令解除。Git 维护授权继续有效；本轮源码、限定测试与进度在核对后同步，远端核验由 [Git 结果](../../../output/github/publish.json)维护。

## 证据入口

- 动态中文：[实现与方法](../../design/DYNAMIC_UI.md)、[字库清单](../../../output/subtitle_ui/assets_manifest.json)、[字库验证](../../../output/subtitle_ui/assets_validation.json)、[模型测试](../../../output/subtitle_ui/model_tests.json)、[演示计时](../../../output/subtitle_ui/demo_tests.json)、[电脑渲染](../../../output/subtitle_ui/host_validation.json)、[ARM 构建](../../../output/subtitle_ui/build.json)、[实际捕获](../../../output/subtitle_ui/latest_capture.json)、[整帧比对](../../../output/subtitle_ui/pixel_comparison.json)。最新板端执行共用 framebuffer 的独立尝试；图形测试用户验收仍绑定原图形应用。
- 自主 AT：[PS_WIFI](../../design/PS_WIFI.md)、[地址与 UART 仿真](../../../output/ps_wifi/simulation.json)、[独立平台构建](../../../output/ps_wifi/build.json)、[AT 主机异常检查](../../../output/ps_wifi/probe_host_tests.json)、[ARM 构建](../../../output/ps_wifi/build-app.json)。实际证据：[配置](../../../output/ps_wifi/program.json)、[运行](../../../output/ps_wifi/run.json)、[自主 AT](../../../output/ps_wifi/at_probe.json)、[用户实屏](../../../output/ps_wifi/user_acceptance.json)、[平台评审](../../../output/ps_wifi/review/review.json)、[软件评审](../../../output/ps_wifi/review/runtime_review.json)。配置、AT 与实屏分别通过；不是持续网络/音频验收。
- 自主网络字幕：[协议/诊断/方法](../../design/NETWORK_UI.md)、[关联](../../../output/network_ui/connection.json)、[完整试验](../../../output/network_ui/pilot.json)、[用户实屏](../../../output/network_ui/user_acceptance.json)、[当前修复评审](../../../output/network_ui/review/alignment_review.json)、[当前工具评审](../../../output/network_ui/review/frontend_review.json)。这些证据绑定同一网络应用与执行；不构成音频、ASR/MT 或触摸验收。

- 电脑真实模型：[实现/HTTP 合同/边界](../../design/MODEL_BACKEND.md)、[运行环境](../../../output/model_backend/runtime/validation.json)、[模型清单](../../../output/model_backend/models/manifest.json)、[真实文件结果](../../../output/model_backend/file_baseline.json)、[输入检查](../../../output/model_backend/input_validation.json)、[接口试验](../../../output/model_backend/service/pilot.json)、[当前接口评审](../../../output/model_backend/service/review/followup_review.json)、[VAD 专项评审](../../../output/model_backend/review/vad_followup_review.json)、[演示字体覆盖](../../../output/model_backend/display_coverage.json)。文件/接口证据不替代真实麦克风、板端访问或模型上屏。

- PS/DDR：[实现与操作](../../design/PS_BRINGUP.md)、[原厂配置来源](../../../hardware/zynq_mini_revb/source.json)、[构建](../../../output/ps_bringup/build.json)、[只读探测](../../../output/ps_bringup/probe.json)、[最近一次真实执行](../../../output/ps_bringup/run.json)。前两次独立结果由 PS_BRINGUP 路由；这些最小 DDR 检查自身未改写 PL、QSPI 或 SD。
- DDR 显示：[实现与方法](../../design/FRAMEBUFFER.md)、[完整像素仿真](../../../output/framebuffer/simulation.json)、[生成复位集成回归](../../../output/framebuffer/reset_simulation.json)、[当前构建](../../../output/framebuffer/build.json)、[应用构建](../../../output/framebuffer/build-app.json)、[最近一次配置](../../../output/framebuffer/program.json)、[最近一次实际运行](../../../output/framebuffer/run.json)、[用户实屏验收](../../../output/framebuffer/user_acceptance.json)、[独立评审](../../../output/framebuffer/review.md)。构建、配置与实屏分别记录；[首次失败](../../../output/framebuffer/attempts/20261011T055048Z_5ccd7bbb/result.json)作为已定位问题保留。

- 已确认选择：[DECISIONS](../DECISIONS.md)；实际设备与资料核对：[MAP](../MAP.md)。
- Wi-Fi：[接口与实现](../../design/WIFI_LINK.md)、[仿真](../../../output/wifi/simulation.json)、[构建](../../../output/wifi/build.json)、[JTAG 配置](../../../output/wifi/hardware/program_result.json)；操作入口：[run_wifi.py](../../../scripts/run_wifi.py)、[wifi_link.py](../../../scripts/wifi_link.py)。
- 网络实测：[纠正接线后 AT](../../../output/wifi/hardware/probe_link.json)、[Wi-Fi/TCP 双向结果](../../../output/wifi/hardware/connect_link.json)；历史诊断：[纠正接线前无 AT 响应](../../../output/wifi/hardware/probe_before_wiring_correction.json)、[当时寄存器快照](../../../output/wifi/hardware/uart_no_response_snapshot.json)。
- 主机脚本验证：[七项协议测试及隐藏输入检查](../../../output/wifi/host_tests.json)，源码哈希记录包含本次控制台输入处理修正。
- 显示诊断：[恢复原静态位流](../../../output/wifi/hardware/restore_static_result.json)、[显示对照记录](../../../output/wifi/diagnostics/display_acceptance.json)。集成位流的启动稳定性不能沿用旧实屏验收结论。
- 显示实现比较：[定点结果](../../../output/wifi/diagnostics/display_implementation_comparison.json)；[再次恢复原静态版](../../../output/wifi/hardware/restore_static_repeat_result.json)。
- 当前板端身份与静默计数：[只读快照](../../../output/wifi/diagnostics/current_mailbox_snapshot.json)；Wi-Fi 代码与诊断：[独立评审记录](../../../output/wifi/review/review.md)。
- 静态画面：[量化预览](../../../output/static_ui/ui_quantized.png)、[资产及整图还原清单](../../../output/static_ui/asset_manifest.json)、[实现说明](../../design/STATIC_DEMO.md)。
- PL 验证：[仿真结果与源码哈希](../../../output/fpga_static/simulation_result.json)、[逐像素记录](../../../output/fpga_static/logs/simulate_tb_static_ui_video.log)、[串行接收记录](../../../output/fpga_static/logs/simulate_tb_static_ui_top.log)。
- 实现证据：[构建结果](../../../output/fpga_static/build_result.json)、[位流](../../../output/fpga_static/static_ui_top.bit)、[资源](../../../output/fpga_static/reports/utilization.rpt)、[时序](../../../output/fpga_static/reports/timing.rpt)、[DRC](../../../output/fpga_static/reports/drc.rpt)。
- 原静态硬件证据：[JTAG 识别](../../../output/fpga_static/hardware/probe_result.json)、[最近一次原静态版下载](../../../output/fpga_static/hardware/program_result.json)；2026-10-10 用户反馈为“正常显示，文字和布局清楚”，仅验收原静态画面。最近下载记录已由 2026-10-11 的对照恢复操作更新，不能将其日期视为旧验收日期。
- 定点复核：[独立评审与修正](../../../output/fpga_static/review/review.md)、[失败重试记录保护检查](../../../output/fpga_static/review/program_failure_guard_test.json)。
- [详细方案](../../design/SYSTEM_PLAN.md)、[UI 规范](../../design/UI_SPEC.md)、[接口契约](../../design/INTERFACES.md)。
- [当前交互原型](../../../output/ui/index.html)；阶段方法：[RUNBOOK](../RUNBOOK.md)。
- 浏览器证据：[交互验证结果](../../../output/playwright/ui_validation.json)，记录当前 HTML 的 SHA-256、17 项通过结果、主要按钮尺寸和关键文字对比度；未出现 JavaScript 错误或外部资源请求。使用既有 machine 环境的 Playwright 和本机 Edge，没有安装浏览器或新依赖。
- 目视证据：[就绪画面](../../../output/playwright/ui-ready.png)、[字幕与记录](../../../output/playwright/ui-subtitles.png)、[设置](../../../output/playwright/ui-settings.png)、[长句](../../../output/playwright/ui-long-text.png)。模拟输入的检查不构成 USB 触摸、实际采样或模型验收。
- 文档证据：[直接链接与职责入口检查](../../../output/verification/documentation_validation.json)，范围为本次改动的 Markdown、评审记录、原型本地导航和直接引用目标，没有扩展审计同级工程或 NAS 内容。
- Wi-Fi 文档补充：[本阶段直接链接检查](../../../output/verification/wifi_documentation_validation.json)，检查九份改动文档及其直接目标，不扩展审计资料库。
- 产品接续建档：[本次文档检查](../../../output/verification/product_handoff_documentation_validation.json)，检查八份修改文档、沿用规则和直接链接；不包含业务实现、构建、烧录或业务测试。

## 云端同传收尾清单（2026-10-18 状态）

2026-10-18 用户在场完成多轮**中文语音→中文（中译中）→ HDMI 实时字幕→ T1 延迟**联调，并大幅优化延迟：字幕改为**边生成边显示**（`response.text.delta` 逐字上屏，定稿再进历史记录），会话 VAD 改为 `server_vad, silence_duration_ms=500`；用户实测从开口到首字约 **1.5–3.5 s**，连续多句不再卡死，屏幕正常。证据 [云端板端阶段](../../../output/cloud_app/status.json)，抓帧 [capture3/screen.png](../../../output/cloud_app/capture3/screen.png)。

本日完成的关键修正：
- **字库准确性**：28 px 字幕字体扩展为 GB2312 一级常用字（3858 字，`ui_assets` 上限 2 MiB 内 95.9%）；演示标签/示例仍用小集合；`generate_subtitle_assets.py` 去掉 fontTools 依赖，自带 TTC/`cmap` 解析，`--extend-size 28` 生成；`net_glyph_available` 只校验 28 px 字幕字体；“最近记录”/原文行也改用 28 px 字体，不再出现方框乱码。
- **稳定性**：单个无法解析/不匹配的服务端事件不再终止会话；`response.done` 为 `cancelled/incomplete` 属正常（用户重新开口会取消上一响应），只丢弃该句；配对槽位可回收；流式 `AT+CIPSEND` 超时/ERROR 改为丢帧+发 `AT` 探活（30 s 上限），不再整会话 fault；网络 fault 时主动停流并把 T1 置为无效（显示 `----`）。
- **T1 准确性**：按音频时间轴测量——每个上行 append 记录流位置与本地时间，`speech_started.audio_start_ms` 映射回采集时刻，T1 为“开口→首字上屏”的真实延迟；`response.done` 重置窗口。
- **中译中**：会话 `translation.language=zh`，屏幕标签改为“中文 → 中文”，字幕只显示译文；后续再按功能拆分独立工程（英译中/中译英/英译英）。

遗留（按优先级）：
1. **上行带宽是当前瓶颈**：实测 ESP8266 单连接 TLS 上行约 13–14 kB/s，而 16 kHz PCM/Base64 实时需约 44 kB/s，导致部分语音帧被丢、长句/多句时偶发“说了但没识别”（`cloud_report` 仍正常，`ignored` 增加）。用户已感觉“延迟好很多”，但要商业级连续识别需要：音频压缩（Opus，qwen3.5-livetranslate 文档支持 `input_audio_format=opus`/`sample_rate=8000`，需在 A9 上实现编码或换方案）或更高吞吐链路。下一步先做短对照，不靠单次现象定论。
2. **HDMI 画面横向循环错位（间歇）**：背景内容循环平移、overlay 位置正常；多次重烧后可恢复正常，尚未定位启动相位根因；属 VDMA/视频启动竞争候选，需采集卡或示波器级诊断。
3. `AT+CIPSEND` 极端超时后 ESP 仍可能停在等数据状态，只能整板断电恢复；新代码已把常见发送超时改为可恢复。
4. API Key 仍只在运行期经 JTAG 写入 `.cloud_config`（0x1E200），未入库；凭据脚本在 `tmp/cloud/`（Git 忽略）。

## 下一阶段：Opus + qwen3.5（2026-10-18 决定）

用户选择“方案 1”：改用 `qwen3.5-livetranslate-flash-realtime` 的 Opus 低码率上行，解决 13–14 kB/s 上行与 44 kB/s 需求的缺口。开工前已把完整工程备份到桌面 `simultaneous-interpreter-zhzh-20261018`（含 `.git`，HEAD `b350714`，171 MB，与工作目录逐字节一致），并完成可行性验证：

- `third_party/opus` 已引入 libopus v1.4（BSD-3-Clause，`source.json` 记录 commit 与逐文件 sha256），固定点配置 `FIXED_POINT + DISABLE_FLOAT_API + VAR_ARRAYS`。
- 主机（MinGW x64）编译 128 个 C 文件并跑通编码→解码回环（16 kHz 单声道、20 ms 帧、16 kbit/s，VBR 平均约 6 B/帧，100/100 帧解码有效）。
- Cortex-A9 `-Os` 交叉编译通过；编码器+解码器 `.text` 合计约 215 KB。

关键前置（详见 [AUDIO_COMPRESSION](../../design/AUDIO_COMPRESSION.md)）：`start.S` 目前关闭 MMU/Cache、栈在 OCM 顶端，Opus 需要可缓存 DDR 执行与更大的栈。因此下一步先做 MMU/Cache 与代码/栈迁移（保持显示/音频/网络回归），再接入编码器并切 qwen3.5、把 `session.update` 改为 opus/16000/server_vad 500；PCM 路径保留为回退。

## 2026-10-18 下午：MMU/Cache + Opus + Ogg 已上板

已完成（详见 [AUDIO_COMPRESSION](../../design/AUDIO_COMPRESSION.md#2026-10-18-实施结果mmucache--opus--ogg--qwen35)）：

- MMU/Cache 启动路径与布局迁移完成：应用/rodata/data 在 `0x01C00000` 可缓存 DDR、栈 128 KB 在 `0x01CE0000`，OCM section 与邮箱保持非缓存；framebuffer/cloud_final/snapshot/guard 在发布前 `cache_clean_range()`。显示、音频回环、ESP/WSS 回归全部通过；渲染耗时可从约 246 ms 降到 15–18 ms。
- libopus v1.4 固定点编进 APP_DDR（约 270 KB），16 kHz/20 ms/16 kbit·s⁻¹ CBR；**qwen3.5 的 `input_audio_format=opus` 需要 Ogg Opus 容器**，已实现 `ogg_opus.c` 并按 10 包（200 ms）/页发送，主机工具与可用参考实现逐字节一致。
- 板端实测：qwen3.5 会话建立成功，Ogg 头页后 5 页/秒、上行 **约 3.4 kB/s**，8 秒连续无服务端错误、`cloud_report phase=1/error=0`、FIFO 无溢出。
- qwen3.5 事件适配：`response.text.text` 累计式流式文本已接入（替代 3.8 的 `response.text.delta`），`response.text.done` 仍作定稿；用真实服务端事件 JSON 做了本机解析验证。

待办：板端真实语音（中译中）多句连续、长时静默后首句、T1、30 分钟稳定性——需要用户在场说话；PC 侧已用 `test_en.wav` 验证同一 Ogg 流/会话配置可被识别并返回中文译文。

## 2026-10-18 晚：中译中改显 ASR 原文，完成无麦克风自测

- **中译中字幕口径**：源=目标语种时 qwen3.5 返回的是改写而非原文（“老师好”→“大家好”），不可用；新增 `Z7_ASR_SUBTITLE` 编译开关，字幕改为流式 ASR 原文（`text`+`stash` 累计），ASR `completed` 即提交，译文事件忽略。见 [AUDIO_COMPRESSION](../../design/AUDIO_COMPRESSION.md#2026-10-18-晚中译中显示-asr-原文t1-口径修正无麦克风自测)。
- **T1 口径修正**：`speech_started.audio_start_ms` 是服务端自己的音频位置，长静音后会与板端发送位置脱节（实测板端已发 44.2 s、服务端报 14.4 s），原音频时间轴映射会漂移并钳到 9999。改为 **T1 = speech_started 事件 → 首条 ASR 文本**（服务端 VAD 触发到首字），跨多句/长静音稳定。
- **`input_audio_buffer.clear` 在 server_vad 下返回错误**（`Error clear input audio buffer: server_vad is enabled.`），已移除；长静音本身没问题（PC 40 s 静音、板端 30 s 静音自测后下一轮仍正常识别并提交）。
- **无麦克风自测工具**：`scripts/generate_selftest_audio.py`（Windows 中文 TTS）+ `build_cloud_app.py --selftest` → `cloud_app_selftest.elf`，以“14.58 s 中文语音 + 30 s 静音”循环驱动整条链路；自测每周期 4 句全部 committed、云 phase=1/error=0。
- **板端当前状态**：自测固件运行中做 30 分钟连续测试；之后恢复麦克风固件并重新配置 qwen3.5，等用户真人语音验收。ESP 偶发不响应 AT（本次通过 JTAG 发 `+++`/`AT+RST` 并重烧恢复），已知硬故障仍需断电。




## 原风险记录（已由现行表修订）

方案核验日期：2026-10-10；Wi-Fi 和显示回归补充核验：2026-10-11；电脑模型/文件接口缺口核验：2026-10-12。2026-10-15 已恢复显示/触摸并执行 CYEA/TLS 预检；当前云端路径的风险见 R10/R15。2026-10-16 补充核验：用户提供的音频模块接线表与仓库内 HDMI 约束的球号对照（R1/R2/R17）。只覆盖相关接口和实现缺口，系统性能目标见 SYSTEM_PLAN。

| 编号 | 有证据的问题或缺口 | 保护措施与放行条件 |
|---|---|---|
| R1 音频模块板级事实未全核实 | WM8731 型号与逐针映射已核对（Z7-NANO_R21.pdf 第 5/13 页）；但模块的 MCLK 来源、供电电压/电流、I/O 电平与 Bank35 VCCO 一致性、模拟前端与采样率均未核实 | 接线前先按模块原理图/实物核对 MCLK 与供电：WM8731 需要 MCLK 才能工作，而本表未列 MCLK 线，模块若不自带振荡器则无法出音。参考 [SOURCES](../../design/SOURCES.md) 与 [AUDIO_LOOPBACK](../../design/AUDIO_LOOPBACK.md)，映射见 [MAP](../MAP.md#音频模块连接) |
| R2 排针针号与方向 | JP2 的球号与网络名已按原理图核对，但针号是逻辑针号，实物 1 号脚方向未经实物确认 | 以板上丝印与 1 号脚标记核对后再计数；区分排针序号、板上丝印、FPGA 封装球号和 HDL 端口。核对结论写回 [audio_module.json](../../../hardware/z7_nano/audio_module.json) |
| R3 触摸接入与供电 | 新板七类控件和常驻启动/取样/停止/重启已通过各自实屏范围；此前无拔插时出现黑线、渐暗和自行断开，见 [USB_TOUCH](../../design/USB_TOUCH.md#触摸启动后的暗屏重现待定位)。POWER USB 接电脑曾传触摸并干扰枚举 | 间歇暗屏仍未定位；实际 VBUS 电压/电流、屏幕内部拓扑及长期整机/热插拔待核实。正常/故障都出现的状态位不能独自定因，不把一次成功当成修复 |
| R4 动态 UI 验收与字符覆盖 | 板上中文演示及像素比对保留原范围；C1-6 字库补到 340 字，仅为受控文件子集，新云事件路径的受控短文件已获实屏验收，任意中文仍未覆盖 | 云事件提交前复用已有字形查询，缺字报告 `0x01000000 OR codepoint` 并保留 UI；本机回归通过，不把子集当作任意中文支持，方法见 [C1-6](../../design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕) |
| R5 独立产品启动未实测 | 新板裸机显示、USB 触摸和 ESP 网络已有各自证据；当前仍由 JTAG 加载，断电丢失 RAM 程序 | 当前板卡入口由 BOARD_MIGRATION 维护；启动介质、独立上电与凭据配置按 CLOUD_SPEECH C5 验收，不把 JTAG 成功当作脱离电脑的产品启动 |
| R6 模型泛化与完整时延未验收 | 受控英语文件已真实识别/翻译；噪声幻觉已有复现与 VAD 对照，但真实录音、口音、领域词汇、人工译文及流式效果未验收，证据见 MODEL_BACKEND | 固定代表性音频和术语集，再记录识别、翻译、峰值内存、P50/P95 时延及 RTF；单文件 WER 或单噪声修复不代表产品质量 |
| R7 草稿与断线一致性 | 流式结果可能修订、乱序、延迟或在清空后到达 | 使用 session/segment/revision；最终历史只收定稿，丢弃旧会话/旧版本结果，断线标出音频缺口 |
| R8 音频处理误伤语音 | DSP 参数、采样时钟与幅度未验证 | 先旁路采集建立基线，再逐项启用滤波；保留可回放样本与饱和/溢出计数，降采样必须先抗混叠 |
| R9 素材与模型许可 | 原型依赖/模型已锁定并保留公开来源；最终分发许可及系统字体再分发仍待核实，来源由 MODEL_BACKEND/DYNAMIC_UI 维护 | 原型执行不等于产品分发授权；产品化前核对全部许可与可替代素材，后续获取依赖/模型仍依当次授权 |
| R10 语音数据与云端凭据 | 电脑已通过可信 TLS/WSS 处理公开合成英语；当前板卡在 CYEA 下安装正确 CA 后，无 Key 可信 TLS 的正例/错误身份拒绝/正例复测已通过，见 [C1-2](../../design/CLOUD_SPEECH.md#c1-2-cyea-与板端-tls-预检) | 受控文件的可信 WSS、ARM 云事件及上屏已通过 C1-6；C1-7a 的 ARM 自主 TLS 已实现但物理身份对照待重跑；独立凭据管理与真实采音仍待完成；密钥不进入源码/参数/证据。CA 更新保留同板备份，限定公开分区并回读；auth0 诊断不能作为安全放行 |
| R11 资料位置变化（已核清） | 旧路径未包含“参考文档”层；当前原件与缓存 SHA-256 一致 | 使用 MAP 的当前相对资料入口；实物版本仍须在接线前核对 |
| R12 ESP 串口接反（已纠正并复测） | 用户确认先前 RXD/TXD 接反；纠正后 AT、固件查询、Wi-Fi 和 TCP 通过，证据见 NOW | 后续按 MAP 的方向接线；此前无响应仅保留为历史证据。供电瞬态和持续吞吐尚未验收 |
| R13 屏幕启动后间歇性显示异常（恢复方法已验证） | 仅拔屏幕供电重启时曾随机抖动；供电与 HDMI 都拔下约 10 秒，再先 HDMI 后供电，用户确认三次均稳定，详见 NOW | 使用 RUNBOOK 的已验证恢复顺序；具体电源/锁定根因与长期稳定性未确认，不把三次结果归因到 U15 或当成永久硬件修复 |
| R14 无线凭据与调试通道 | AT 命令可能回显凭据，临时 TCP 服务可能在异常时残留 | 私有进程管道、禁止原始串口日志及命令行密码；确认 ATE0 后关联；临时服务关闭必须取得应答，失败不宣布联网验证通过 |
| R16 重新下载后的 PS 调试事务错误 | 高速 UART 首次实测通过后，一次重新配置后的启动在 CPU 初始化前报告 DAP AHB 事务错误；完整断电后恢复，屏幕断开时的冷启动和暖重载均通过，随后屏幕/常驻触摸恢复正常 | 根因仍未定位，不归因于波特率或宣布永久修复；CPU 目标不可见时不跳过身份保护，当前证据见 [NOW](../NOW.md) |
| R15 长时连续音频尚未放行 | 一分钟延后 JTAG 观测的 Opus/TLS 收发通过且 UART 零错误；中途观测的一轮发生 RX 溢出，根因未完全确定；30 分钟未验证，旧透传损坏保持未放行 | 两轮证据与范围由 [一分钟持续对照](../../design/CLOUD_SPEECH.md#一分钟持续对照)维护；C1-6 已通过 ARM 边消费的短文件，仍须另测长时及控制回复路径，不绕过错误或用单轮结果宣布长期修复 |
| R17 引脚冲突（已核对，不成立） | 2026-10-16 曾据 [hdmi.xdc](../../../constraints/hdmi.xdc)（标注 ZYNQ_MINI_REVB，旧板）判断音频球号与已验收 HDMI 重叠；按 [source.json](../../../hardware/z7_nano/source.json) 与 Z7-NANO_R21.pdf 核对后确认：当前板 TMDS 在 Bank34（U18/U19、V20/W20、T20/U20、N20/P20），音频 7 个信号在 Bank35 的 JP2，**不重叠** | 结论已写回 [audio_module.json](../../../hardware/z7_nano/audio_module.json)。教训：不得把旧板 xdc 当作当前板引脚事实；换板后引脚以 source.json 为唯一板级记录 |
| R18 模块时钟与 I2C 配置（已解决） | 模块自带 12.288 MHz 晶振；PL 侧 I2C 初始化（0x1A 探测 + 11 条写入）与 I2S 从机回环经六个 testbench、构建与下载验证。2026-10-17 晚改用稳定 5 V 供电后：`live=1`、`bclk_alive=1`、四路音频引脚计数均活跃、`L1=62 µs`、`over/under=0`，回环打通，证据 [program.json](../../../output/z7_nano/audio_loopback/program.json) | 仍待用户耳机听感与实屏确认、≥30 min 持续运行与 L3 声学时延；`L1` 是数字域测量点，不等于声学延迟 |
| R19 音频位流构建与下载（已完成） | Vivado 2018.3 完整构建 BLOCK DESIGN + 综合/实现/位流通过（setup/hold = 7.289/0.044 ns），`scripts/z7_nano_audio.tcl run` 已完成 PL 配置、PS7 初始化、应用启动与寄存器读取；显示侧一次通过（0 欠载/0 格式错误），后续两次运行出现单次视频格式错误（应用 fail(21)，待定位是否与模块供电/共地噪声有关） | 构建与下载已不再是缺口；显示格式错误与音频供电需分别取证，见 R21/R22 |
| R21 音频模块供电/共地文档错误（已发现） | 2026-10-17 复核厂商文档与原理图：模块 J3-1=GND、2=VCC5V、37/38=GND、39/40=D3V3；开发板 JP2 11=VCC_5V、29=VCC_3V3、12/30=GND。旧文档把 J3 1/2 都记为 VCC5V、把 JP2 12/30 记为电源针，可能误导实际接线 | 已更正 [audio_module.json](../../../hardware/z7_nano/audio_module.json)、[MAP](../MAP.md#音频模块连接) 与 [AUDIO_LOOPBACK](../../design/AUDIO_LOOPBACK.md#2-接口事实)；接线前必须按更正后的针号核对，GND 必须共地，否则 codec 可寄生取电（I2C 应答）但晶振/BCLK 不启动 |
| R22 显示启动的单次格式错误（随供电改善消失） | 2026-10-17 在 4.3 V 5 V 供电下，两次运行出现 `format_errors=1` 触发应用 `fail(21)`；换成充电宝稳定 5 V 后同一位置流运行 `format_errors=0`、整帧 614400、`display_ok=1` | 已不再复现；若换回弱供电/整机长期运行再出现，按 VDMA/fsync 与供电两条线分别定位 |
| R23 模块供电余量不足（已确认并解决） | 开发板 USB 5 V 被屏幕负载拖到 4.3 V（拔屏 4.6 V），低于 LT1117-3.3 正常压差需求；codec 能应答 I2C 但晶振不启动，四路音频引脚零跳变。用户改用充电宝主供电 + 排针 5 V 直供模块 J3-2 后，`bclk_alive=1`、回环 62 µs、显示 `format_errors=0`（此前的显示格式错误也一并消失，说明是供电噪声） | 后续整机若再回到单 USB 供电，需确认 5 V 不低于约 4.6 V 或给模块单独稳压；模块 J3-39/40 的 D3V3 与 codec 的 VCC3V3 不是同一网络，不能靠接 3.3 V 给它供电 |
| R24 板端实时音频上行带宽（已解决，待真人语音验收） | PCM 16 kHz/Base64 需约 44 kB/s，而 ESP8266 单连接 TLS 只有约 13–14 kB/s（CIPSEND 2 KB×约 7 次/秒），曾导致多句/长句丢语音。已改为 libopus 固定点 16 kbit/s 编码并按 200 ms Ogg Opus 页发送（10 包/页），板端实测上行约 3.4 kB/s（自测连续 30 min 仍在观察），qwen3.5 会话与流式发送正常（cloud phase=1/error=0）。 | 关键协议：qwen3.5 的 opus 输入必须是 **Ogg Opus 容器**，裸 Opus 包无效（已用 PC 参考验证）。中译中字幕已改为 ASR 原文（`Z7_ASR_SUBTITLE`），不再用同语种“译文”。剩余验收：用户真实语音多句连续、长时静默后首句、30 min 稳定性；见 [AUDIO_COMPRESSION](../../design/AUDIO_COMPRESSION.md#2026-10-18-晚中译中显示-asr-原文t1-口径修正无麦克风自测)。 |
| R25 云端 VAD 收段与 T1 口径（已解决） | 2026-10-18：`server_vad` + `silence_duration_ms=500` 多句连续提交正常；ASR 模式下收到 `transcription.completed` 即提交。`speech_started.audio_start_ms` 在长静音后与板端音频位置脱节（实测服务端 14.4 s vs 板端 44.2 s），T1 改为“speech_started 事件→首条 ASR 文本”。`input_audio_buffer.clear` 在 server_vad 下非法（服务端报错），长静音不需要 clear。 | 真人语音复测 T1 与多句连续性；见 [AUDIO_COMPRESSION](../../design/AUDIO_COMPRESSION.md#2026-10-18-晚中译中显示-asr-原文t1-口径修正无麦克风自测)。 |
| R26 HDMI 画面横向循环错位（间歇，未解决） | 背景画面在屏上循环平移、PL overlay 的 `L1/T1` 位置正常；DDR framebuffer 与 VDMA 寄存器均正确。多次重烧/断电后可消失又复现，说明是启动相位竞争而非固定偏移；改“停机配置再启动”会让首帧 format_ok 失败（error 21） | 需采集卡或示波器级诊断 VDMA/视频启动相位；未修复前以重烧临时恢复，不算整机验收。证据 [云端板端阶段](../../../output/cloud_app/status.json) |
| R27 ESP8266 偶发不响应 AT（已缓解，待复测） | 2026-10-18 一次长时间运行后 ESP 完全不再回 AT（网络 phase=6、error=29、UART 仅 96 B），历史上偶发“说话没反应”可能同源。已加自动恢复：保存最近一次 SSID/口令，phase=6 且错误为 29/32/34/43–47 时退避 5 s 重建设置（最多 6 次）。另一次 error=43（TLS 建链失败）在重烧后立即恢复，说明多为瞬时网络问题而非 ESP 死机。 | 复测自恢复是否覆盖真实现场；若 ESP 彻底死机（AT 无任何回显）仍需断电或重烧；后续可考虑 ESP 复位 GPIO/看门狗，需硬件确认 EN/RST 引脚。 |

英语候选补充（2026-10-20）：英译英首轮缺少 Pong、麦克风初轮远端 TLS 告警仍保留；最终采音修正版的真实英语识别/中文实屏和有界链路已取得证据，没有据此认定历史掉线根因全部修复。当前证据与后续验收由 [ENGLISH](../ENGLISH.md#当前证据和边界)维护。

连接提示阶段再次记录同类 TLS 告警并恢复；后续逐段指纹已将数据不一致缩到 PL 内部串行发送之后，具体根因仍未定位，见 [当前诊断](../ENGLISH.md#tls-传输诊断进行中2026-10-20)。提示功能或单轮无故障不能放行长时链路。

传输对照补充（2026-10-20）：单端实体 UART 三组通过；接回 ESP 后，无 TLS 的透传也复现数据替换，降到平均 8 kB/s 的长测仍失败。1024 字节、逐块等待 SEND OK 的独立诊断通过 1 MiB 完整比较；这是替代路径的证据，尚不是产品修复或 ESP 电气接收的完整资格验证。产品候选未上板，用户要求断电收尾；当前板态及下一步只按 [NOW](../NOW.md) 与 [TCP 对照](../../validation/esp-tcp-20261020/README.md)接续。

分块产品补充（2026-10-21）：真实 ARM TLS、五分钟固定样本、快速暂停/继续及真实麦克风/字号短验收通过。随后长测发生定稿停更、上传继续、TLS/UART 零错误，后来自行恢复；长时未通过，不能只检查连接计数。单次诊断重现停更，查询时 ESP 缓冲为 0、期间新通知 0，不能据此直接归因云端或宣称已修复。OnePlus12、声学听感、可信上电时间/熵、凭据配置和独立启动仍未放行，当前状态见 [实机验证](../ENGLISH.md#分块-tls-实机验证2026-10-21)。

方案和模拟 UI 可独立使用；硬件阶段按对应放行条件推进，不通过假数据宣称解决任何硬件或模型缺口。

换板补充核验：2026-10-13。`01_FPGA_demo.zip` 的 Lab9 HDMI 导出虽然放在 7010 目录，内部声明 `7z020/-2`、32 位 DDR 和 1 GiB，不能作为新板初始化来源。后续 SDK Hello World 导出声明 `7z010/-1`、16 位 DDR 和 512 MiB；仍需实际 DDR 与显示验收。新板基线与来源统一见 [BOARD_MIGRATION](../../design/BOARD_MIGRATION.md)。
