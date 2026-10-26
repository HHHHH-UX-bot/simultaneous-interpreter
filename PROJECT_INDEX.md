# 项目索引

本项目以 Zynq 为采集和交互终端，进行中英文双向字幕与语音传译。当前范围与证据以 [NOW](docs/context/NOW.md) 为准。

索引是条件路由，不是必读清单；先定位当前问题对应章节，信息足够即停止。

| 任务或问题 | 权威入口 | 职责 |
|---|---|---|
| 脱离电脑从 TF 卡启动、更新卡内容或排查自启问题 | [SD_BOOT](docs/design/SD_BOOT.md)、[操作](docs/context/RUNBOOK.md#sd-卡独立启动)、[验证](docs/validation/sd-boot-20261026/README.md) | 启动链路、FSBL 钩子与跳板、固件补做的 USB 预上电/CPU1/配置、卡内容生成与安全取舍 |
| 在只有 Vivado/Vitis 2026.1 的机器上重建、烧录当前版本 | [本轮验证](docs/validation/merge-pr7-2026-20261026/README.md)、[D24](docs/context/DECISIONS.md#d24-合并-pr-7本机以-vivadovitis-20261-重建2026-10-26)、[NOW 使用入口](docs/context/NOW.md#使用入口) | 2026.1 适配点、JTAG 预置 Qwen 资产、实板检查与未验证范围 |
| 声纹登记、云端/板上离线切换与 SpeechBrain 对照 | [LOCAL_AI](docs/design/LOCAL_AI.md)、[验证](docs/validation/local-ai-20261022/README.md) | Cortex-A9 整数 MFCC/DTW、12 个固定短句、四位内存声纹、实际触摸/电脑画面与独立 PC ECAPA |
| PL网口电脑画面、鼠标控制、历史另存为 | [REMOTE_DISPLAY](docs/design/REMOTE_DISPLAY.md)、[本轮验证](docs/validation/remote-display-20261022/README.md) | 实际帧缓冲无损传输、共用触摸命中、CSV/TXT/JSON定稿导出、2018.3平台及实机证据 |
| 优化连续长句的字幕/译音、上传与下行缓冲 | [长句验证](docs/validation/long-speech-20261022/README.md)、[前轮延迟验证](docs/validation/tts-latency-20261021/README.md)、[TTS_SETTINGS](docs/design/TTS_SETTINGS.md) | 六句自动实测、2Mbaud、IPD空行修复、Opus O2、计时取舍与真人长句验收边界 |
| 修改设置页、译音、真实输出音量及 T2 | [译音与设置](docs/design/TTS_SETTINGS.md)、[设置页规范](docs/design/UI_SPEC.md#板端设置页2026-10-21)、[NOW](docs/context/NOW.md) | 当前设置、DAC 增益/静音/速度、云端文本与译音、延迟定义、构建与证据边界 |
| 开发或验收独立历史记录页、英中对照与滑动 | [UI_SPEC](docs/design/UI_SPEC.md#板端历史记录页2026-10-21)、[验证](docs/validation/history-ui-20261021/README.md)、[成段](docs/validation/history-paragraph-20261026/README.md)、[NOW](docs/context/NOW.md) | 64条英中对照与自动成段、真实C滚动/手势、清空语义、内存及分项板测范围 |
| 核对真实麦克风异常及局部修正 | [本轮译音验证](docs/validation/tts-settings-20261021/README.md)、[历史版复测](docs/validation/history-mic-20261021/README.md)、[RISKS](docs/context/RISKS.md) | 迟到预览、空定稿和response.done最终译文的修正证据；原故障、局部修复与长期可靠性分开 |
| 使用顶部英中双向翻译菜单或修改其生命周期 | [UI_SPEC](docs/design/UI_SPEC.md#板端翻译方向切换2026-10-21)、[验证](docs/validation/language-picker-20261021/README.md) | 两项菜单、实际语言切换、旧会话隔离及验收边界 |
| 接续英译中、英译英，核对桌面候选和当前板测 | [ENGLISH](docs/context/ENGLISH.md) | 共享模式、修正、真实验证与未放行项 |
| 使用或修改中文译英字幕、译音、Zynq TLS | [ZHEN](docs/context/ZHEN.md) | 当前能力、故障修复、板测证据、代码和操作入口 |
| 使用中译中独立识别、共享 TLS 或核对当前板端版本 | [ZHZH](docs/context/ZHZH.md) | 中文 ASR 行为、当前使用方式、两种证据来源及验收边界 |
| 确认适用规则与阶段验证要求 | [AGENTS](AGENTS.md) | 长期协作与读写规则 |
| 了解用途、打开原型 | [README](README.md) | 使用入口 |
| 接续工作、查进展与下一步 | [NOW](docs/context/NOW.md) | 当前状态与验证证据 |
| 查硬件、工具、资料及目录关系 | [MAP](docs/context/MAP.md) | 事实、位置映射及来源 |
| 移植或操作当前 Z7-Nano 板卡 | [BOARD_MIGRATION](docs/design/BOARD_MIGRATION.md) | 新板资料、板级入口、迁移顺序及验收范围；旧 RevB 入口不用于新板 |
| 验证新板 USB Host 与触摸 | [USB_TOUCH](docs/design/USB_TOUCH.md) | 控制器/PHY、坐标、按钮、常驻生命周期与断电恢复 |
| 连接新板 ESP8266 | [新板接线](docs/design/BOARD_MIGRATION.md#esp8266-新板接线)、[新板 UART](docs/design/BOARD_MIGRATION.md#m5-新板-uart-与自主-at) | JP1 针位、UART/AT 入口及后续网络验证 |
| 接音频模块、做实时回环与延迟显示 | [AUDIO_LOOPBACK](docs/design/AUDIO_LOOPBACK.md)、[接线与球号](docs/context/MAP.md#音频模块连接)、[A 阶段操作](docs/context/RUNBOOK.md#a-音频实时回环与时延显示后续) | 需求、实现、寄存器、板级取证；回环已通（5 V 供电余量是根因），听感/L3 待验收（§2/§10） |
| 核对音频引脚占用与新板显示是否冲突 | [风险 R17/R18](docs/context/RISKS.md)、[接线事实](hardware/z7_nano/audio_module.json) | 已核对不重叠（Bank35 JP2 vs Bank34 HDMI）；残留项为 MCLK、电平与丝印方向 |
| 调整时延读数在屏幕上的呈现 | [UI_SPEC 时延读数](docs/design/UI_SPEC.md#时延读数)、[PS 渲染](software/subtitle_ui/ui_render.c) | 当前为抗锯齿 L1/T1 卡片；旧 PL 像素文字层已关闭，数字测量仍由 PL 提供 |
| 查看/修改音频回环实现与寄存器映射 | [AUDIO_LOOPBACK 已实现](docs/design/AUDIO_LOOPBACK.md#7-已实现2026-10-17)、[寄存器文件](rtl/audio_regs.v)、[回环](rtl/i2s_loopback.v) | 文件职责、AXI `0x43c20000` 寄存器表、L1 定义与仿真证据 |
| 跑音频仿真或构建音频位流 | [仿真/构建运行器](scripts/run_audio_loopback.py)、[构建脚本](scripts/build_z7_nano_audio.tcl)、[A 阶段操作](docs/context/RUNBOOK.md#a-音频实时回环与时延显示后续) | simulate 六个 testbench 通过；Vivado 2018.3 构建+下载通过；板级回环已通（稳定 5 V 供电后 live=1、L1=62 µs），见 AUDIO_LOOPBACK §10 |
| 理解系统分工、选型、资源和时延预算 | [SYSTEM_PLAN](docs/design/SYSTEM_PLAN.md) | 产品与系统方案 |
| 接入云端语音翻译、脱离电脑运行 | [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md) | 已选百炼方案、官方接口来源、凭据准备、音频带宽与分阶段接入 |
| 低带宽实时音频（Opus、qwen3.5、Cache/MMU） | [AUDIO_COMPRESSION](docs/design/AUDIO_COMPRESSION.md) | 上行 13–14 kB/s 瓶颈、libopus v1.4 供应商化与主机/ARM 可行性验证、MMU/Cache 前置与实施步骤 |
| 复盘产品瓶颈、选择后续验证顺序 | [NOW](docs/context/NOW.md)、[当前系统主线](docs/design/SYSTEM_PLAN.md#当前产品主线与实施取舍) | 译音/设置已实现，长期可靠性与独立启动仍待验证；2026-10-15 的预存音频顺序仅为历史计划 |
| 追溯 921600 阶段或核对当前 1 Mbaud | [BOARD_MIGRATION](docs/design/BOARD_MIGRATION.md)、[MAP](docs/context/MAP.md) | 921600 为旧阶段；当前字幕候选为 1,000,000 baud/分频 50，不能混用旧参数 |
| 查询当前 ESP 固件的加密配置 | [C1-0](docs/design/CLOUD_SPEECH.md#c1-0-固定加密配置查询)、[操作](docs/context/RUNBOOK.md#c-云端语音接入) | 固定查询、实际应答与后续 TLS/带宽边界 |
| 检查 CYEA、板端 TLS 与客户端 CA | [C1-2](docs/design/CLOUD_SPEECH.md#c1-2-cyea-与板端-tls-预检)、[操作](docs/context/RUNBOOK.md#c-云端语音接入) | 无 Key 预检、证书来源、备份/更新及安全握手边界 |
| 验证 ESP8266 到百炼的 WSS 会话 | [C1-3](docs/design/CLOUD_SPEECH.md#c1-3-esp8266-云端-wss-会话)、[工具](scripts/probe_z7_nano_wss.py) | 经 ESP 真实鉴权/配置，无音频，尚非 ARM 自主客户端 |
| 验证 ARM 调度的 ESP 加密音频收发 | [C1-4](docs/design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)、[文件客户端](scripts/pilot_z7_nano_cloud_file.py) | 预编码文件、PS CIPSEND/IPD、公开内存与安全交接；WSS 控制/解析仍在电脑 |
| 开发 ARM 自主 TLS 与会话 | [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md)、[ENGLISH](docs/context/ENGLISH.md) | ARM TLS/WSS 与真实麦克风已经接通；历史 C1-7a 不是当前未完成项，长期故障和独立启动仍待验收 |
| 开发 ARM 云事件消费与真实字幕 | [实现](software/network_ui/cloud_live.c)、[本轮验证](docs/validation/tts-settings-20261021/README.md) | 原文/译文预览、合法定稿与明确空结果分别处理；旧故障和新局部修正不能扩展为长期放行 |
| 测试百炼实时语音翻译接口或 PCM/Opus 负载 | [C2-PC](docs/design/CLOUD_SPEECH.md#c2-pc-受控音频云端对照)、[客户端](scripts/run_bailian_file.py) | 同样本三组对照、Ogg 封装、定稿和实际帧字节，尚非 ARM 自主云端链路 |
| 测量新板连续上传与透传 | [C1-1](docs/design/CLOUD_SPEECH.md#c1-1-连续上传与串口调度)、[逐包测量](scripts/measure_z7_nano_upload.py)、[透传测量](scripts/measure_z7_nano_passthrough.py) | 已知数据块核账、时长、有效带宽与退出清理；不替代音频/TLS 验收 |
| 核对 UART 波形、ESP 透传与分块发送 | [当前实机验证](docs/context/ENGLISH.md#分块-tls-实机验证2026-10-21)、[单端采样](docs/validation/uart-wire-20261020/README.md)、[TCP 对照](docs/validation/esp-tcp-20261020/README.md)、[产品板测](docs/validation/arm-packets-20261021/README.md) | 历史译音停更未定位；当前字幕版固定样本长测与麦克风短验收见[新证据](docs/validation/text-response-20261021/README.md)，不混用两阶段范围 |
| 调整屏幕布局、按钮、字幕和异常交互 | [UI_SPEC](docs/design/UI_SPEC.md) | 界面与行为规范 |
| 修改或重新下载纯 PL 静态 UI | [STATIC_DEMO](docs/design/STATIC_DEMO.md)、[run_static_ui](scripts/run_static_ui.ps1) | 静态像素结构、资产与阶段入口 |
| 处理屏幕无画面、抖动和上电顺序 | [RUNBOOK 屏幕恢复](docs/context/RUNBOOK.md#hdmi-屏幕启动与异常恢复) | 本设备已验证的恢复流程，实际结论见 NOW |
| 调试 ESP8266、UART 与局域网 | [WIFI_LINK](docs/design/WIFI_LINK.md)、[RUNBOOK W](docs/context/RUNBOOK.md#w-esp8266-串口与联网) | 寄存器、运行入口、凭据边界与验证顺序 |
| 实施 PS 自主串口与联网接管 | [PS_WIFI](docs/design/PS_WIFI.md)、[run_ps_wifi](scripts/run_ps_wifi.py) | 独立平台、PS 事务所有权、公共状态和跨平台保护 |
| PS 通过 Wi-Fi 接收模拟字幕 | [NETWORK_UI](docs/design/NETWORK_UI.md)、[run_network_ui](scripts/run_network_ui.py) | 实际文本协议、易失配置、公共观察、LAN 测试及恢复边界 |
| 运行或接续英语识别/中文翻译与电脑服务 | [MODEL_BACKEND](docs/design/MODEL_BACKEND.md) | 隔离运行环境、固定模型、真实文件证据、HTTP 接口、噪声修复及放行边界 |
| 接入 DeepSeek 中文翻译 | [P5c-0](docs/design/MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译) | 官方接口、隐藏密钥输入、文本实测及尚未接通的环节 |
| 维护 Git、上传 GitHub，审核或合并队友 PR | [MAP](docs/context/MAP.md#git-与-github)、[RUNBOOK](docs/context/RUNBOOK.md#git-维护)、[PR 审核](docs/context/RUNBOOK.md#队友-pr-审核与合并) | 账号/fork 映射、分支、提交范围、PR 审核与远程核验；各 PR 当前状态见 NOW |
| 实现采样、网络、结果更新接口 | [INTERFACES](docs/design/INTERFACES.md) | 区分旧 LAN 草案与当前实际 C/Opus/WSS/UI 合同 |
| 接续动态字幕与板端自主运行 | [RUNBOOK P2b](docs/context/RUNBOOK.md#p2b-ps-实时中文与模拟-ui后续)、[W4](docs/context/RUNBOOK.md#w4-板端自主网络与音频传输带宽后续) | 子阶段、依赖和放行条件；实际进度见 NOW |
| 修改、启动或观察板上动态中文 UI | [DYNAMIC_UI](docs/design/DYNAMIC_UI.md)、[run_subtitle_ui](scripts/run_subtitle_ui.py) | 有界模型、字库、绘制、JTAG 控制和实际像素比对 |
| 运行或维护最小 PS/DDR 检查 | [PS_BRINGUP](docs/design/PS_BRINGUP.md)、[run_ps_bringup](scripts/run_ps_bringup.py) | 初始化来源、OCM/DDR 范围、受控下载与失败恢复 |
| 维护 DDR 显示、翻页和显示 DMA | [FRAMEBUFFER](docs/design/FRAMEBUFFER.md)、[run_framebuffer](scripts/run_framebuffer.py) | 图像/地址布局、PS/PL 对应关系、复位与受控启动 |
| 执行某阶段、判断能否进入下一阶段 | [RUNBOOK](docs/context/RUNBOOK.md) | 前置条件与验收方法 |
| 追溯已确认的选择 | [DECISIONS](docs/context/DECISIONS.md) | 决策、理由及范围 |
| 定位风险或必要资料缺口 | [RISKS](docs/context/RISKS.md) | 风险与保护措施 |
| 核对技术建议的外部依据 | [SOURCES](docs/design/SOURCES.md) | 官方与上游资料、适用范围 |
| 追溯有价值的历史记录 | [history](docs/context/history/README.md) | 历史索引 |
| 体验当前界面原型 | [界面原型](output/ui/index.html) | 离线模拟交互 |

旧 RevB 的引脚、时序和历史验收通过 MAP 路由；Z7-Nano 配置由迁移专题路由到独立板级来源。未实际建立的源码目录和命令不作为实现入口。
