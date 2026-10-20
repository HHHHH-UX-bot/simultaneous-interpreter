# 对象与位置映射

## 2026-10-20 中译中补充

两种模式共用 `software/network_ui/` 和 `third_party/mbedtls/`，中译中显式构建入口和产物分目录见 [ZHZH](ZHZH.md)。实际加载副本和本 PR 检出结果分开存于 `docs/validation/zhzh-20261020/board_*` 与 `checkout_*`；不将本地保存版的原位流当作本轮新 FPGA 构建。当前板上已按此前授权切换中译中，本次只保存和提交。

## 2026-10-20 中文译英补充

中文译英模式、内存/认证与音频代码映射见 [ZHEN](ZHEN.md#操作和代码入口)；当前固件验证范围见该专题，不沿用下方旧阶段产物。

## 项目边界

本项目根目录记为 `./`，与 `../hdmi/`、`../led/` 同级。它们是独立工程，本轮未修改。外部板卡资料库只读取所需资料，不整库复制。

| 当前目录 | 职责 | 状态 |
|---|---|---|
| 根目录三个 Markdown 入口 | 规则、导航、基本用途 | 已建立 |
| `docs/context/` | 当前状态、映射、验收、决定、风险 | 已建立 |
| `docs/design/` | 系统方案、UI 规范、接口与来源 | 已建立 |
| `output/ui/` | 可直接打开的离线界面交付 | 已建立 |
| `output/verification/` | 文档检查结果 | 仅保存本轮验证 |
| `output/playwright/` | 界面检查结果与截图 | 验证时按需建立 |
| `rtl/`、`constraints/`、`sim/` | PL 静态画面、UART/FIFO/AXI、仿真及引脚约束 | 已建立，P2a/W0 输入 |
| `scripts/` | 静态资产、构建/JTAG、ESP AT 与局域网验证入口 | 已建立 |
| `output/static_ui/` | 静态截图、调色板、小块 ROM 数据和资产清单 | 已生成并核对 |
| `output/fpga_static/` | 静态版 Vivado 工程、位流、报告、日志与硬件证据 | 按阶段产生，状态见 NOW |
| `output/wifi/` | Wi-Fi 集成位流、串口/联网结果、评审和显示诊断 | 按阶段产生，状态见 NOW |
| `software/ps_bringup/`、`hardware/zynq_mini_revb/` | 最小 PS/DDR 程序与有来源的初始化数据 | 已建立；范围见 PS_BRINGUP |
| `output/ps_bringup/` | 本次 PS 编译、探测、独立执行快照与结果 | 按阶段产生，状态见 NOW |
| `software/framebuffer/`、`output/framebuffer/` | DDR 显示应用及匹配 PS/PL 构建、复位仿真、独立硬件尝试 | 已建立；布局与入口见 FRAMEBUFFER，实际状态见 NOW |
| `software/subtitle_ui/`、`assets/subtitle_demo.json`、`output/subtitle_ui/` | 板端中文状态/绘制/计时演示、字体输入、构建、实际抓屏和控制证据 | 已建立；职责与专用地址见 DYNAMIC_UI，阶段状态见 NOW |
| `software/ps_wifi/`、`rtl/ps_wifi*`、`output/ps_wifi/` | PS 自主 AT 软件、新显示/UART 平台及独立证据 | 已建立；地址与所有权由 PS_WIFI 维护；不覆盖旧显示或 Wi-Fi 产物 |
| `software/network_ui/`、`output/network_ui/` | 自主网络模拟字幕软件、独立应用与阶段证据 | 已建立；具体协议与操作由 NETWORK_UI 维护；物理运行所有者见 `output/ps_runtime/active_app.json` |
| `software/model_backend/`、`output/model_backend/` | 电脑 CPU ASR/MT 与独立云文本翻译的对照试验、依赖锁、文件接口证据 | 已建立；由 MODEL_BACKEND 维护；最终云语音方案由 CLOUD_SPEECH 维护，不是最终电脑运行依赖 |
| `software/network_ui/stream_pilot.*`、`scripts/pilot_z7_nano_cloud_file.py`、`output/z7_nano/stream/` | ARM 定时加密音频文件短测、公开状态与取证 | 内存地址、范围与方法见 [C1-4](../design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)；不属于已完成自主云客户端 |
| `software/network_ui/cloud_events.*`、`third_party/jsmn/`、`output/z7_nano/cloud_events/` | 有界 C 事件解码、固定上游来源及公开样例验证 | [C1-5](../design/CLOUD_SPEECH.md#c1-5-c-端云事件解码)维护解码范围，集成见 C1-6 |
| `software/network_ui/cloud_frame.c`、`cloud_wire.*`、`cloud_live.*`、`third_party/wslay/`、`output/z7_nano/cloud_live/` | 帧重组、原译 ID 配对、字幕消费与离线/板上阶段证据 | [C1-6](../design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕)维护内存分区、栈与边界；C1-6 固定 ELF 的短文件云译上屏已验收 |
| `software/network_ui/network.c`、`scripts/probe_z7_nano_tls_boot.py`、`output/z7_nano/tls_boot/` | ARM 非阻塞 TLS、私有 Wi-Fi 配置与身份对照工具 | [C1-7a](../design/CLOUD_SPEECH.md#c1-7a-arm-自主可信-tls)维护状态/边界；network_tick 进入既有 DDR 云代码区，修正版 TLS 板测待重跑 |
| `software/network_ui/cloud_client.*`、`scripts/build_cloud_app.py`、`output/cloud_app/` | 板端自主 WSS 客户端（握手、session.update、PCM append、传译延迟写入 audio_regs 0x88）、云端应用构建/烧录/抓帧证据 | [C1-8](../design/CLOUD_SPEECH.md#c1-8-板端实时云同传集成2026-10-17-夜)维护；构建入口 `python -B scripts/build_cloud_app.py`，烧录用 `scripts/z7_nano_audio.tcl run` |
| `hardware/z7_nano/`、`output/z7_nano/` | 新板权威配置、独立构建/下载与 PS 验证产物 | 2026-10-13 建立；方法见 BOARD_MIGRATION，当前验收见 NOW |
| `rtl/i2c_master.v`、`rtl/wm8731_init.v`、`rtl/i2s_loopback.v`、`rtl/audio_latency.v`、`rtl/audio_regs.v`、`rtl/audio_overlay.v`、`rtl/z7_nano_audio_top.v`、`scripts/build_z7_nano_audio.tcl`、`scripts/audio_synth_ooc.tcl`、`scripts/run_audio_loopback.py`、`output/z7_nano/audio_loopback/`、`output/z7_nano/audio_ooc/` | 音频回环 RTL（I2C 初始化、I2S 从机回环、L1 时延、屏幕读数叠加）、BD/位流构建、OOC 综合检查与仿真/构建证据 | 2026-10-17 建立；寄存器映射 AXI `0x43c20000`、验收边界见 [AUDIO_LOOPBACK](../design/AUDIO_LOOPBACK.md#7-已实现2026-10-17)；位流与实屏证据未产生 |
| `tmp/` | 必要中间脚本与选定原理图页 | 已建立，不作为长期规则来源 |

最小 PS 源码入口见 [PS_BRINGUP](../design/PS_BRINGUP.md)；电脑模型试验见 [MODEL_BACKEND](../design/MODEL_BACKEND.md)，当前云语音计划见 [CLOUD_SPEECH](../design/CLOUD_SPEECH.md)。真实音频尚未接入，已验收的新板 USB 触摸入口由迁移专题路由。静态入口为 [static_ui_top](../../rtl/static_ui_top.v)、[static_ui_video](../../rtl/static_ui_video.v) 和 [run_static_ui](../../scripts/run_static_ui.ps1)。

DDR 显示的配置、地址和运行入口由 [FRAMEBUFFER](../design/FRAMEBUFFER.md) 维护；同级工程的静态时序与引脚继续沿用，未改写资料原件。

动态中文 UI 由 [DYNAMIC_UI](../design/DYNAMIC_UI.md) 维护；它与图形测试共用显示驱动、构建目录内所选 ELF、运行记录和 PS 操作锁。当前应用由实际 active_app 与独立运行快照核对，不从目录名推断。硬件观察依赖本机 `127.0.0.1:3121` 的硬件调试服务；本轮连接恢复记录见 [debug_server](../../output/subtitle_ui/debug_server.json)，不在缺少监听时误判 CPU 挂起。

## 硬件与连接事实

2026-10-13 当前目标改为 Z7-Nano 7010。新板唯一权威配置与当前接线入口见 [迁移专题](../design/BOARD_MIGRATION.md#当前接线与实施方向)；下面的 RevB 原理图、CAM1、USB Type-C 和 ESP 引脚只属于旧板，不能用于新板接线。用户已断开屏幕到电脑的 POWER USB，USB Host 与触摸的实际验证状态由 [USB_TOUCH](../design/USB_TOUCH.md)维护。

核验日期：2026-10-10；范围为用户确认、已有项目文档、缓存原理图相关页及 P2a JTAG 识别。接口电气与真实外设功能未新增测量。

| 对象 | 事实或状态 | 来源与限制 |
|---|---|---|
| 开发板、器件、屏幕、既有 HDMI 显示 | 沿用同级 HDMI 项目的对象 | [其 MAP](../../../hdmi/docs/context/MAP.md#对象与证据)、[显示验收](../../../hdmi/docs/context/NOW.md#当前进展)；旧结果不替代新项目验收 |
| 音频模块 | WM8731 主控模块；J3→JP2→Bank35 的 7 根信号已与原理图逐针对上（含 MRCC 时钟脚与电源针）；模块 MCLK 来源、供电/电平与实物丝印方向待核实 | 用户说明 + Z7-NANO_R21.pdf 第 5/13 页；映射见[音频模块连接](#音频模块连接)与 [audio_module.json](../../hardware/z7_nano/audio_module.json) |
| “FPGA20引脚” | 2026-10-16 的音频接线表改用明确的 FPGA 球号与 JP2 针号，旧说法不再引用；球号本身仍待与原理图核对 | 用户接线表；区分排针序号、板上丝印、FPGA 封装球号和 HDL 端口，见 R2 |
| 扩展连接器 CAM1 | 原理图为 40 针双排，提供 34 个 PL I/O；物理针号 20、21 为 3.3 V，40 为 5 V，1、19、22 为 GND | 原理图第 9 页；[所读页](../../tmp/schematic-page-09.png)。核对实物方向后才能用针号 |
| DDR | 原理图一颗 MT41J256M16、16 位数据总线；配置声明 512 MiB | 第 6 页，[所读页](../../tmp/schematic-page-06.png)；2026-10-11 最小初始化及限定区域检查两次通过，范围见 [PS_BRINGUP](../design/PS_BRINGUP.md)。未全容量逐字节测试，Linux 可用内存未测 |
| USB 数据口 | 原理图 P3 连接 USB3320 ULPI PHY，并有 `USBCP_EN` 控制 VBUS；CC1/CC2 接到 `USB_ID` | 第 10 页，[所读页](../../tmp/schematic-page-10.png)；这不证明任意 Type-C 线能进入 Host 模式 |
| 触摸线 | 旧板阶段未接入；2026-10-13 用户确认新板 Host 到 TOUCH 已接线，枚举/坐标待核实 | 当前新板连接由迁移专题维护；屏幕 TOUCH 是数据口 |
| 网口 | 原理图有 PS、PL 两套 RTL8211E 千兆网口电路；方案优先 PS 网口 | 原理图第 7 页文字定位；实物对应接口、链路、驱动与地址未验收 |
| 启动存储 | 原理图含 W25Q128 QSPI 和 SD 接口 | 第 8 页文字定位；可用 SD 卡、内容、启动开关和启动模式待核实 |
| 调试连接 | 本轮识别到一个 JTAG 目标，器件 xc7z010；串口此前未连接 | [当前探测证据](../../output/fpga_static/hardware/probe_result.json)；Linux 初次启动建议补串口 |
| 开发电脑 | 原局域网模型试验已有记录；最终产品使用时不依赖电脑 | 电脑试验证据见 MODEL_BACKEND；最终云语音方向见 DECISIONS D13 |

## ESP8266 连接

核验日期：2026-10-11。用户确认 ATK-MW8266D（板载 ATK-ESP-01）由板上 3.3 V 供电，并接板上 GND。供电能力和瞬态电压没有仪表测量，运行稳定性另行验证。

用户补充先前 RXD/TXD 接反，现已纠正为下表方向。纠正后真实 AT 应答及局域网 TCP 双向收发通过，运行版本与动态地址以 [联网证据](../../output/wifi/hardware/connect_link.json) 为准；本次使用 115200、8N1，固件报告 Bin version 2.2.1（ATK-MW8266）。租约地址不是固定配置，后续运行重新查询。

上述联网证据属于 W1–W3 的 JTAG/UART 历史测试；当前 PS 自主网络的动态地址及执行身份由 [NETWORK_UI](../design/NETWORK_UI.md#分阶段操作与证据)路由到独立运行结果，不沿用旧租约作为当前地址。

| Zynq 侧 | 方向 | 模块侧 | 依据 |
|---|---|---|---|
| U15 / FPGA_GPIO_11N_34 | 输出，UART TX | RXD | 用户接线；原理图第 5/9 页 |
| W15 / FPGA_GPIO_10N_34 | 输入，UART RX | TXD | 用户接线；原理图第 5/9 页 |
| 板上 3.3 V / GND | 供电与参考 | VCC / GND | 用户确认；原理图第 2 页标明 Bank34=3.3 V |

约束入口：[wifi_uart.xdc](../../constraints/wifi_uart.xdc)。目视材料：[Bank34 引脚](../../tmp/wifi/schematic-bank34.png)、[PL 供电](../../tmp/wifi/schematic-pl-power.png)。不根据其他品牌 ESP-01 外观推定 ATK 额外控制脚状态。

新增 Wi-Fi 源码位于 rtl、sim 和 scripts；产物及证据放在 output/wifi，中间文件放在 tmp/wifi。实现与接口见 [WIFI_LINK](../design/WIFI_LINK.md)。

## 音频模块连接

核验日期：2026-10-16；范围：用户提供的接线表、模块主控说明（WM8731）与 Z7-NANO_R21.pdf 第 5/13 页逐针核对。未做电气测量、未核对实物丝印方向、本轮未接线。

用户 2026-10-16 提供的接线（已与原理图逐针对上）如下，机器可读副本见 [audio_module.json](../../hardware/z7_nano/audio_module.json)：

| J3 脚 | 模块信号 | JP2 脚 | 板级网络 | FPGA 球号 | Bank35 引脚名 | 方向 |
|---|---|---|---|---|---|---|
| 3 | VW_I2C_SCLK | 15 | GPIO2_6P | B19 | IO_L2P_T0_AD8P_35 | FPGA → 模块 |
| 4 | VM_I2C_SDAT | 16 | GPIO2_6N | A20 | IO_L2N_T0_AD8N_35 | 双向 |
| 5 | VM_DACDAT | 17 | GPIO2_7P | C20 | IO_L1P_T0_AD0P_35 | FPGA → 模块 |
| 6 | VM_BCLK | 13 | GPIO2_5P | H16 | IO_L13P_T2_MRCC_35 | 模块 → FPGA |
| 7 | VM_ADCDAT | 18 | GPIO2_7N | B20 | IO_L1N_T0_AD0N_35 | 模块 → FPGA |
| 8 | VM_DADLRC | 14 | GPIO2_5N | H17 | IO_L13N_T2_MRCC_35 | 模块 → FPGA |
| 9 | VM_ADCLRC | 19 | GPIO2_8P | D19 | IO_L4P_T0_35 | 模块 → FPGA |
| 10 | NC | — | — | — | — | 悬空 |

- JP2 = CON40A（2x20），在 Bank35，网络前缀 `GPIO2_`；同页 JP1 为 GPIO1（ESP8266 所用）。JP2 电源/地针（2026-10-17 按第 13 页复核）：11 = `VCC_5V`、29 = `VCC_3V3`、**12 与 30 = GND**；此前记为 11/12=VCC_5V、29/30=VCC_3V3 是错误的。
- 针号是用户提供的逻辑针号；实物 1 号脚方向与丝印尚未核对，不能直接按图计数（同 ESP8266 的 JP1 约定）。
- **引脚占用已核对：与当前板显示不冲突**。当前板 TMDS 由 [source.json](../../hardware/z7_nano/source.json) 的 `Pins` 定为 Bank34 的 `U18/U19`、`V20/W20`、`T20/U20`、`N20/P20`；音频 7 个信号全在 Bank35 的 JP2 上。仓库内 [hdmi.xdc](../../constraints/hdmi.xdc) 标注 ZYNQ_MINI_REVB（旧板），不是当前板显示约束，不能据此认定冲突。见 [RISKS](RISKS.md) R17 与 [AUDIO_LOOPBACK](../design/AUDIO_LOOPBACK.md#3-引脚占用核对已核实与当前板显示不冲突)。
- 接口事实：模块主控为 WM8731（用户说明），工作为 I2S 时钟主（BCLK、DADLRC、ADCLRC 由模块输出），PL 只能作从机；采集与播放各有独立 LRCLK；控制走 I2C；H16/H17 是 MRCC 时钟脚。实现已按此方向（R7=0x042 主模式），见 [AUDIO_LOOPBACK](../design/AUDIO_LOOPBACK.md#7-已实现2026-10-17)。
- 板级补充核对（2026-10-16，Z7-NANO_R21.pdf 全 15 页）：①开发板没有音频 codec 或音频时钟，音频全部经 JP2 外接模块；②Bank34 与 Bank35 均由 `VCC_3V3` 供电，PL 侧为 3.3 V 逻辑；③板载 24C32 EEPROM 的 I2C 网络位于 Bank34 网络块（与 `HDMI1_*`、`GPIO1_*` 同块），不占用 JP2 的音频脚。
- 模块补充核对（2026-10-17 复核 AN831.pdf 原理图 + 黑金模块实验文档第 5 页）：`U11=WM8731SEFL`、`Y3=12.288 MHz 晶振`（不需要 FPGA 供 MCLK）、`U8=LT1117-3.3`、`J10/J11/J12=3.5 mm 插座`、`J3=CONN PCB 20x2-S`。**J3 电源/地针更正**：1 = **GND**、2 = **VCC5V**、37/38 = **GND**、39/40 = **D3V3**（此前把 1/2 都记为 VCC5V 是错误的；该错误可能误导模块供电接线）。信号针 3–9 与上表一致（用户已确认实物方向）。**BCLK/LRCLK 需先经 I2C 配置 R7 主模式与 R9 使能后才输出**，模块未引出 MCLK/CLKOUT。
- JP2 编号规则（同页核对）：奇数为 P、偶数为 N（pin1=GPIO2_0P，pin2=GPIO2_0N，依此类推，遇电源位列跳号）；11=`VCC_5V`、12=GND、29=`VCC_3V3`、30=GND。
- 对接注意：JP2 与模块 J3 都是 2x20，但**针号不一一对应，不能用整排排线直连**（例：J3 3→JP2 15、J3 6→JP2 13），必须按上表逐根跳线。**GND 用 JP2 第 12 或 30 针**（2026-10-17 更正；此前误判 JP2 没有 GND 针）。模块供电：J3 1/2 接 JP2 11（5 V，经模块 LT1117-3.3），J3 39/40 接 JP2 29（3.3 V）。
- 正确供电接线（2026-10-17）：模块 J3-2（5 V）→ JP2-11（`VCC_5V`）**或** J3-39/40（3.3 V）→ JP2-29（`VCC_3V3`）；模块 GND（J3-1/37/38）→ JP2-12 或 30（GND）。只接 3.3 V 时模块板载 LT1117 空载，不影响 codec。

## 沿用而不复制的权威配置

HDMI 引脚、电气连接和当前已验收视频时序以 [HDMI MAP 的硬件与视频配置](../../../hdmi/docs/context/MAP.md#已核对的硬件与视频配置) 为唯一权威表。新项目选择显示方案时按需读取；不复制旧 bitstream 当成新实现。

P2a 选定复制了时钟、复位、TMDS 子模块、XDC 和编码黄金模型，各文件复制时核对哈希并保留原注释；同级源文件未修改。新的 [hdmi.xdc](../../constraints/hdmi.xdc) 是当前构建输入，沿用上述权威映射，构建时再次核对全部差分管脚。

Vivado/Vitis 版本与 BASIC 许可证历史证据见 [HDMI MAP 的工具与许可证](../../../hdmi/docs/context/MAP.md#工具与许可证)。Vivado/xsim 2026.1 已用于 PL 验证。2026-10-11 已使用现有 Vitis 2026.1 XSDB 与附带 ARM GCC 13.4.0 完成最小 PS/DDR 检查，执行证据见 [PS_BRINGUP](../design/PS_BRINGUP.md)。Linux 构建环境和 LVGL 未建立；项目内 CPU 模型运行环境及验证范围见 MODEL_BACKEND。

## 原理图来源与可访问性

外部资料逻辑入口为用户提供的 NAS 文件共享，当前相对目录是 `参考文档/zynq/`，目标文件为该目录下的 `01开发板硬件资料/ZYNQ_MINI_REVB原理图.pdf`。另有同级 `ZYNQ_MINI开发板/` 目录，本轮未读取。此前文档里的共享子路径少了“参考文档”这一层；NAS 共享本身当前可访问。

本轮先用同级 HDMI 项目 `tmp/pdfs/schematic.pdf` 保留副本，随后在当前 NAS 路径核对原件：均为 436523 字节，SHA-256 均为 `c5932dadeb5a1ba7483e5d4492f43c6642e9c0be41aa20e1676099272b48a6d6`。副本共 12 页。新增接口与资源结论主要依据第 4、6、7、8、9、10 页，其中第 6、9、10 页已渲染并目视核对；其他相关页用于关键词和电路名称定位。当前原件与缓存的字节一致性已确认，不代表板上器件或运行状态已经实测。

Linux 资料只做文件名定位：`02教程文档/LINUX篇/` 含 Ubuntu 虚拟机、PetaLinux 2018.3、Linaro 图形桌面和 USB_SLAVE 教程。未读教程全文或运行其镜像；这些文件名不证明新版本 BSP、USB Host 或本产品显示驱动已经可用。实际连接前还需核对实物版本、方向与模块电平。

## 外部技术资料

官方与上游链接、核验范围见 [SOURCES](../design/SOURCES.md)。使用接口或库前只读取对应章节，不自动遍历参考仓库。界面状态契约以 [INTERFACES](../design/INTERFACES.md) 为拟议权威入口。

## Git 与 GitHub

2026-10-14 已确认关联账号与本机现有 Git 登录均为 `HHHHH-UX-bot`，创建仓库 [simultaneous-interpreter](https://github.com/HHHHH-UX-bot/simultaneous-interpreter)时为私有。2026-10-15 GitHub API 核验时仓库为公开，本轮未修改可见性；变化时间与原因未核实。本项目独立 Git 根目录、分支 `main`，远程目标为该仓库；当前提交/远端核验见 [Git 结果](../../output/github/publish.json)，流程见 [RUNBOOK](RUNBOOK.md#git-维护)。

[忽略规则](../../.gitignore)将 tmp/output、运行模型/构建产物、密钥/许可证及安装字体留在本地。文档中的这些路径及同级旧 HDMI 工程是本机证据/资料入口，Git 仓库不携带所有运行缓存。[字节规则](../../.gitattributes)保留现有行尾，避免改变绑定源码字节的验证哈希。TinyUSB 许可随裁剪源码保留；本项目未另行声明全部材料可开源再分发。

2026-10-16 用户说明已将仓库 fork 到 `max133777/simultaneous-interpreter`，并要求本次改动以 PR 形式提交。该 fork 的远端地址、可见性、默认分支及与上游的关系尚未核验（待核实）。本轮核对发现：交给本次修改的本地副本是一个解压目录、不含 Git 元数据，不能直接在其中提交或推送。PR 的基线仓库、分支与合并方式以用户当次指令为准，流程见 [RUNBOOK](RUNBOOK.md#git-维护)。
