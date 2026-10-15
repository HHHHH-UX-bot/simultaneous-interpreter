# 项目索引

本项目设计一款以 Zynq 为采集和交互终端、将外语语音转换为中文字幕的产品。当前范围与证据以 [NOW](docs/context/NOW.md) 为准。

索引是条件路由，不是必读清单；先定位当前问题对应章节，信息足够即停止。

| 任务或问题 | 权威入口 | 职责 |
|---|---|---|
| 确认适用规则与阶段验证要求 | [AGENTS](AGENTS.md) | 长期协作与读写规则 |
| 了解用途、打开原型 | [README](README.md) | 使用入口 |
| 接续工作、查进展与下一步 | [NOW](docs/context/NOW.md) | 当前状态与验证证据 |
| 查硬件、工具、资料及目录关系 | [MAP](docs/context/MAP.md) | 事实、位置映射及来源 |
| 移植或操作当前 Z7-Nano 板卡 | [BOARD_MIGRATION](docs/design/BOARD_MIGRATION.md) | 新板资料、板级入口、迁移顺序及验收范围；旧 RevB 入口不用于新板 |
| 验证新板 USB Host 与触摸 | [USB_TOUCH](docs/design/USB_TOUCH.md) | 控制器/PHY、坐标、按钮、常驻生命周期与断电恢复 |
| 连接新板 ESP8266 | [新板接线](docs/design/BOARD_MIGRATION.md#esp8266-新板接线)、[新板 UART](docs/design/BOARD_MIGRATION.md#m5-新板-uart-与自主-at) | JP1 针位、UART/AT 入口及后续网络验证 |
| 理解系统分工、选型、资源和时延预算 | [SYSTEM_PLAN](docs/design/SYSTEM_PLAN.md) | 产品与系统方案 |
| 接入云端语音翻译、脱离电脑运行 | [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md) | 已选百炼方案、官方接口来源、凭据准备、音频带宽与分阶段接入 |
| 复盘产品瓶颈、选择后续验证顺序 | [2026-10-15 计划](docs/design/CLOUD_SPEECH.md#2026-10-15-复盘后的验证顺序建议)、[当前系统主线](docs/design/SYSTEM_PLAN.md#当前产品主线与实施取舍) | 建议、依赖、停止条件与预存音频闭环；不是恢复实验或已确认换型 |
| 将新板 UART 切换至 921600 | [高速 UART](docs/design/BOARD_MIGRATION.md#m5-2-921600-串口) | 临时波特率协商、冷启动与暖重载、验证入口和放行边界 |
| 查询当前 ESP 固件的加密配置 | [C1-0](docs/design/CLOUD_SPEECH.md#c1-0-固定加密配置查询)、[操作](docs/context/RUNBOOK.md#c-云端语音接入) | 固定查询、实际应答与后续 TLS/带宽边界 |
| 检查 CYEA、板端 TLS 与客户端 CA | [C1-2](docs/design/CLOUD_SPEECH.md#c1-2-cyea-与板端-tls-预检)、[操作](docs/context/RUNBOOK.md#c-云端语音接入) | 无 Key 预检、证书来源、备份/更新及安全握手边界 |
| 验证 ESP8266 到百炼的 WSS 会话 | [C1-3](docs/design/CLOUD_SPEECH.md#c1-3-esp8266-云端-wss-会话)、[工具](scripts/probe_z7_nano_wss.py) | 经 ESP 真实鉴权/配置，无音频，尚非 ARM 自主客户端 |
| 验证 ARM 调度的 ESP 加密音频收发 | [C1-4](docs/design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)、[文件客户端](scripts/pilot_z7_nano_cloud_file.py) | 预编码文件、PS CIPSEND/IPD、公开内存与安全交接；WSS 控制/解析仍在电脑 |
| 开发 ARM 云事件消费与真实字幕 | [C1-5](docs/design/CLOUD_SPEECH.md#c1-5-c-端云事件解码)、[C 解码](software/network_ui/cloud_events.c)、[验证](scripts/validate_cloud_events.py) | 本机/ARM 对象证据、DDR/栈与后续 WebSocket/UI 接入；尚未在板运行 |
| 测试百炼实时语音翻译接口或 PCM/Opus 负载 | [C2-PC](docs/design/CLOUD_SPEECH.md#c2-pc-受控音频云端对照)、[客户端](scripts/run_bailian_file.py) | 同样本三组对照、Ogg 封装、定稿和实际帧字节，尚非 ARM 自主云端链路 |
| 测量新板连续上传与透传 | [C1-1](docs/design/CLOUD_SPEECH.md#c1-1-连续上传与串口调度)、[逐包测量](scripts/measure_z7_nano_upload.py)、[透传测量](scripts/measure_z7_nano_passthrough.py) | 已知数据块核账、时长、有效带宽与退出清理；不替代音频/TLS 验收 |
| 调整屏幕布局、按钮、字幕和异常交互 | [UI_SPEC](docs/design/UI_SPEC.md) | 界面与行为规范 |
| 修改或重新下载纯 PL 静态 UI | [STATIC_DEMO](docs/design/STATIC_DEMO.md)、[run_static_ui](scripts/run_static_ui.ps1) | 静态像素结构、资产与阶段入口 |
| 处理屏幕无画面、抖动和上电顺序 | [RUNBOOK 屏幕恢复](docs/context/RUNBOOK.md#hdmi-屏幕启动与异常恢复) | 本设备已验证的恢复流程，实际结论见 NOW |
| 调试 ESP8266、UART 与局域网 | [WIFI_LINK](docs/design/WIFI_LINK.md)、[RUNBOOK W](docs/context/RUNBOOK.md#w-esp8266-串口与联网) | 寄存器、运行入口、凭据边界与验证顺序 |
| 实施 PS 自主串口与联网接管 | [PS_WIFI](docs/design/PS_WIFI.md)、[run_ps_wifi](scripts/run_ps_wifi.py) | 独立平台、PS 事务所有权、公共状态和跨平台保护 |
| PS 通过 Wi-Fi 接收模拟字幕 | [NETWORK_UI](docs/design/NETWORK_UI.md)、[run_network_ui](scripts/run_network_ui.py) | 实际文本协议、易失配置、公共观察、LAN 测试及恢复边界 |
| 运行或接续英语识别/中文翻译与电脑服务 | [MODEL_BACKEND](docs/design/MODEL_BACKEND.md) | 隔离运行环境、固定模型、真实文件证据、HTTP 接口、噪声修复及放行边界 |
| 接入 DeepSeek 中文翻译 | [P5c-0](docs/design/MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译) | 官方接口、隐藏密钥输入、文本实测及尚未接通的环节 |
| 维护 Git 与上传 GitHub | [MAP](docs/context/MAP.md#git-与-github)、[RUNBOOK](docs/context/RUNBOOK.md#git-维护) | 账号/仓库映射、分支、提交范围与远程核验 |
| 实现采样、网络、结果更新接口 | [INTERFACES](docs/design/INTERFACES.md) | 拟议数据契约 |
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
