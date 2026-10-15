# 对象与位置映射

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
| `hardware/z7_nano/`、`output/z7_nano/` | 新板权威配置、独立构建/下载与 PS 验证产物 | 2026-10-13 建立；方法见 BOARD_MIGRATION，当前验收见 NOW |
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
| 音频模块 | 尚未到货；型号、输入类型、数字协议、采样率和电平均待核实 | 用户当前说明；不得假定是 I2S 或直接分配引脚 |
| “FPGA20引脚” | 含义待核实 | 可能指连接器针号或接口称呼；不能直接认定为信号线 |
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
