# Z7-Nano 7010 板级迁移

迁移范围更新（2026-10-21）：新板显示、真实触摸、采音/Opus、ARM TLS 与麦克风双向字幕已有分项实测。当前 UART 为 1,000,000 baud/分频 50；下方 M5-2 的 921600 是历史阶段，不混用。当前功能缺口与 UI 历史页由 [NOW](../context/NOW.md)维护；迁移成功不代表独立启动或长期稳定。

## 范围与来源

核验日期：2026-10-13。目标为用户更正后的 `Z7-NanoR20-7010-2018.3` 资料包对应板卡；原 RevB 配置、源码和产物保留。实际进度统一由 [NOW](../context/NOW.md) 维护。

只读取资料中的原理图、HDMI/LED 约束及 SDK Hello World PS 导出，没有解压整个资料库。原理图文件名为 `Z7-NANO_R21.pdf`，所读电路页标题栏为 Rev 2.0。关键材料与哈希见 [材料索引](../../output/board_migration/material_inventory.json)；原 FPGA Lab9 PS 导出与目标不符，已排除。

原理图第 2/5/7/9/12 页分别用于 PS MIO、PL I/O、DDR、USB 和 HDMI 电路核对。厂商 [Z7-Nano 用户手册](https://fpga-docs.microphase.cn/zh-cn/latest/DEV_BOARD/Z7-NANO/Z7-NANO_Reference_Manual.html)提供交叉依据；网页中的闪存容量表存在单位不一致，未采用它推定容量。

新板封装、引脚和 PS 参数的唯一板级记录为 [source.json](../../hardware/z7_nano/source.json)。[prepare_z7_nano](../../scripts/prepare_z7_nano.py)从它生成构建输入，不独立维护第二套引脚表。SDK 原厂初始化保存在 [vendor_ps7_init.tcl](../../hardware/z7_nano/vendor_ps7_init.tcl)，这里只复用配置数据，不加载旧 HDF 中的位流。

## 当前接线与实施方向

- 用户确认：板卡唯一 USB Host → 屏幕 TOUCH micro-USB；HDMI → 同一屏幕；JTAG → 电脑。2026-10-13 用户发现 POWER USB 接电脑时触摸会操作电脑，已断开屏幕 POWER USB，仅保留一台 USB Host；当前连接见 [connection.json](../../output/z7_nano/usb/connection.json)。屏幕内部 USB 拓扑及 VBUS 电压、电流尚未测量。
- 屏幕沿用 KD070V02，逻辑分辨率 1024×600。新板上电后的显示、触摸枚举与坐标分别实测。
- 新板为 USB3320 ULPI PHY，连接 PS USB0 的 MIO28..39，PHY 复位在 MIO46；VBUS 由 PHY CPEN 控制 TPS2051B。资料核对不等于已经开启 Host 或测过电压。
- 优先继续现有 PS 裸机 UI、PL HDMI 和局域网电脑模型；先验证 USB Host 控制器，再枚举 HID 和实现触摸。AMD [UG585 USB 章节](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/USB-Host-Device-and-OTG-Controller)说明 PS 的 Host/Device 能力，不能据此宣称现有 BSP 已提供可用触摸驱动。
- 新板网络路线已由用户选择接 ESP8266；针位分配与待确认状态见下节。已有两路 Ethernet 的硬件资料，本轮未操作其接口。

## ESP8266 新板接线

2026-10-13 用户在网线连接问题中要求提供外接引脚图以接 ESP8266。已按原理图第 5/13 页选择 JP1 的两个空闲 Bank34 I/O，电平为 3.3 V；针位分配的权威来源为 [esp8266.json](../../hardware/z7_nano/esp8266.json)，[接线图](../../output/wiring/esp8266-jp1.png)由该文件生成。图是逻辑针号，实物朝向必须由板上 1 号标记确认。旧 RevB 的针号不能用于定位此连接器。

用户随后确认“已按图接好并恢复连接”，记录在上述接线文件；这是用户报告，尚未电气测量。该文件维护接线事实，UART 当前实现与验证由下方 M5 节维护。触摸 UI 已通过相应范围，之后依次验证新 UART、模块响应、关联和电脑服务，凭据不写入文档或图。

## 迁移阶段

| 阶段 | 内容 | 放行证据 |
|---|---|---|
| M0 | 新板原理图/约束、正确 PS 来源、JTAG 身份 | 来源与差异核对；只读探测确认为单个 7010 |
| M1 | 独立新板约束与纯 PL 静态 UI | 构建、实际 I/O、生成时钟、时序/DRC；JTAG 配置；用户实屏确认 |
| M2 | 7010/16 位 DDR 的 PS 平台 | 新导出、初始化与 OCM/DDR 实测；不使用被排除的 Lab9 导出 |
| M3 | 复用双缓冲和动态中文 UI | 实际帧缓冲/控制状态与新板实屏验收 |
| M4 | [USB Host 与触摸](USB_TOUCH.md) | PHY/端口、设备与 HID 描述符、坐标、触摸按钮分别验证 |
| M5 | 新板网络到电脑模型 | 选择实际连通的网络口；真实文件结果和中文字库上屏验证 |

每个阶段验证后才推进依赖阶段。没有音频模块信息时不分配音频引脚；不写 QSPI/SD，不把临时 JTAG 原型当成独立开机产品。

## 操作入口

已建立 [run_z7_nano](../../scripts/run_z7_nano.py)。以下在项目根目录、machine Python 中执行；命令记录不是新的执行授权：

```powershell
python -B scripts/run_z7_nano.py build-static
python -B scripts/run_z7_nano.py probe
python -B scripts/run_z7_nano.py program-static
python -B scripts/run_z7_nano.py build-ddr
python -B scripts/run_z7_nano.py test-ddr
python -B scripts/run_z7_nano_display.py build
python -B scripts/run_z7_nano_display.py build-app
python -B scripts/run_z7_nano_display.py program
python -B scripts/run_z7_nano_display.py run
python -B scripts/run_z7_nano_display.py sample
python -B scripts/run_z7_nano_display.py capture
```

静态构建、只读探测、静态下载、DDR 构建及实测已执行。M3 的 [独立显示入口](../../scripts/run_z7_nano_display.py)已完成新板平台及 ARM UI 编译；实际运行和实屏状态以 NOW 为准。支持 `--vivado`、`--xsdb`、`--gcc` 指定现有工具，像素比对可用 `--native-gcc-directory` 指向既有 native 编译器的依赖目录，不安装新依赖。

静态 UI 使用 [新板包装顶层](../../rtl/z7_nano_static_top.v)和原有显示逻辑；一个低有效 PL LED 显示心跳，没有输出旧板的 HDMI enable 或 ESP 信号。已核对 11 个共享 HDL/资产输入仍匹配原仿真与资产证据，新的引脚、时钟及包装由本轮构建检查；这不构成 USB 或 DDR 验收。

下载限定到已识别的新板线缆和唯一 7010，检查当前构建与位流快照哈希；硬件动作共用 PS 操作锁，超时或取消只停止本次工具进程树。结果在 `output/z7_nano/`，中间文件在 `tmp/z7_nano/`。旧 `run_static_ui`、`run_framebuffer`、`run_network_ui` 的板级身份保护和产物保持原范围，不用于新板。

USB Host 启用后，TOUCH 线也可能为屏幕供电；屏幕内部供电隔离待核实。若后续需要完整重启屏幕，应核对 POWER、HDMI 和 TOUCH 三条连接是否均已断开，不能直接把旧板的两线断电恢复当作新板已验证的方法。

## PS 双缓冲与动态 UI

新板显示平台保持原 `framebuffer_video`、50/250 MHz 时钟、XRGB8888 双缓冲和 UI 状态/字体接口，使用 [新顶层](../../rtl/z7_nano_framebuffer_top.v)与正确新板 PS 参数。派生配置见 [configuration.json](../../output/z7_nano/display/configuration.json)；构建见 [build.json](../../output/z7_nano/display/build.json)、[实现检查](../../output/z7_nano/display/implementation.json)，ARM UI 与保留内存范围见 [app_build.json](../../output/z7_nano/display/app_build.json)。共享 host/字体验证仅在当前源码哈希仍匹配时复用，不复用旧板的运行结果。

静态与 DDR 显示分别使用 Z7N1/Z7N2 配置标识。每次 PS 初始化必须对应一次未消费的新 PL 配置，并验证显示寄存器的复位状态。重新配置已运行的显示平台时先停止 CPU 和 VDMA、等待传输完成；PS–PL 接口全隔离时不访问 PL MMIO，部分启用则停止并定位。`sample` 不初始化 PS；`capture` 暂停 CPU 绘图、让 DMA 留在完整帧上，取出实际 DDR 像素后恢复 CPU，再与同一状态的电脑渲染逐像素比较。

M3 显示基线使用预设字幕，单独的 M3 验收不覆盖 USB Host 或电脑模型；后续触摸结果见 [USB_TOUCH](USB_TOUCH.md)。不能由 UI 演示推定实时翻译已经完成。

## M5 新板 UART 与自主 AT

新 [UART 平台](../../rtl/z7_nano_wifi_top.v)沿用 M3 的 HDMI、PS/DDR、VDMA 与复位，在同一 50 MHz 时钟接入已有 [PS UART 包装](../../rtl/ps_wifi_uart.v)。地址 `0x43c20000`、复位分频 434、8N1；M5-0/M5-1 的历史验收使用 115200，当前高速设置见 M5-2。寄存器合同见 [WIFI_LINK](WIFI_LINK.md#axi-lite-寄存器)。新板约束从前述接线记录生成，保留 UART 的慢边沿、8 mA 驱动、接收上拉和异步约束。引脚/时序/DRC 独立核对，不沿用旧板硬件验收。

构建和 ARM 程序独立保存在 `output/z7_nano/wifi/`，平台标识为 Z7N3（`0x5a374e33`）；M3 保留 Z7N2。CPU0 复用 [自主 AT 入口](../../software/ps_wifi/main.c)、原 UI 与触摸消费者，只执行 AT、关闭回显及查询固件；不提交无线凭据。保留 CPU0 `0x1e040` 触摸反馈、`0x1e100` 公共 AT 状态与 CPU1 低 OCM 的分工，不更换触摸固件。

[新板入口](../../scripts/run_z7_nano_wifi.py)复用显示执行器的配置、停机和像素检查，旧板入口不变。USB PHY 和常驻工具的 `--platform wifi` 只选择此新板配置；默认 display 仍要求 Z7N2，选定平台、当前程序、线缆或 generation 不匹配即停止。重新配置前先停止已管理的 Host，确认 USB/VDMA 停止；不同平台不能以旧运行记录代替新验收。

2026-10-14 的 [M5-0 阶段](../../output/z7_nano/wifi/uart_stage.json)已通过：构建 WNS 12.005 ns、WHS 0.037 ns，引脚/时钟/DRC 及 [复核](../../output/board_migration/review_wifi.json)通过；实际 CPU0 发出 22 字节 AT 指令并收到 209 字节，AT/回显关闭/固件检查均通过，串口溢出和帧错误为零。当前同配置的整帧、常驻触摸与实屏已确认。以下是本次从 Z7N2 首次切换的已执行顺序，不是后续每轮重新执行的清单。

```powershell
python -B scripts/run_z7_nano_wifi.py build
python -B scripts/run_z7_nano_wifi.py build-app
python -B scripts/run_z7_nano_usb_live.py stop
python -B scripts/run_z7_nano_wifi.py program
python -B scripts/run_z7_nano_wifi.py run
python -B scripts/run_z7_nano_wifi.py capture
python -B scripts/inspect_z7_nano_at.py
python -B scripts/run_z7_nano_usb.py run-probe --platform wifi
python -B scripts/run_z7_nano_usb_live.py start --platform wifi
python -B scripts/run_z7_nano_usb_live.py sample --platform wifi
```

自主 AT、整帧、常驻触摸与实际屏幕分别放行后才进入关联/电脑服务。M5-0 已封存，后续网络应用见下节；本阶段不写 QSPI/SD 或 ESP 固件。

### M5-1 网络软件与局域网字幕（已验收）

[网络入口](../../scripts/run_z7_nano_network.py)复用同一 Z7N3 位流，选择已有 [网络状态机](../../software/network_ui/network.c)、[字幕协议](NETWORK_UI.md#文本试验协议)及真实触摸消费者。只新增 `0x1e140` 公共网络状态和 `0x1e180` 私有易失配置；ARM 构建、OCM 布局及实际启动通过。`output/z7_nano/wifi/` 的 ARM 构建槽随所选软件更新，M5-0 的原 ELF/构建/实测已封存至上述阶段记录，不覆盖旧 RevB 产物。

新板 [状态观察](../../scripts/inspect_z7_nano_network.py)、[私有配置提交](../../scripts/connect_z7_nano_network.py)及[局域网字幕试验](../../scripts/pilot_z7_nano_network.py)经 [复核](../../output/board_migration/review_network_z7.json)和实测通过；不调用旧板硬件入口。网络应用启动后，PHY/常驻工具选择 `--platform network`。凭据只经隐藏标准输入、进程管道和临时 RAM 进入固件，不落盘、不打印原始工具输出；60 秒是接受关联结果的观察窗口，外部工具各有独立超时，不宣称总退出时间严格为 60 秒。

2026-10-14 [M5-1 阶段](../../output/z7_nano/wifi/network_stage.json)实际关联后取得模块地址，电脑经 TCP 8266 发送三句已知字幕；不支持字形、旧 epoch、重复请求分别拒绝，重连保留两条定稿并继续第三条。板端当前文本和全部历史与发送内容精确一致，614400 像素比对零差异。常驻触摸未丢弃点击；用户确认启动后的按钮正确响应，以及最终“显示正确，清楚稳定”。这些是已知文本网络验收，不代表语音、ASR、云翻译或任意中文字库已接通。

本次已执行网络 `program → run → capture → public sample`，随后 `run-probe/start/sample --platform network`、隐藏输入关联、[字幕试验](../../scripts/pilot_z7_nano_network.py)及实屏确认。关联记录绑定本次配置和 ELF；断电后不可复用旧 running/连接文件。当前电脑与模块须处于同一可达局域网。

### M5-2 921600 串口

用户在 2026-10-14 指定改为 921600、8N1，见 [D14](../context/DECISIONS.md#d14-新板串口使用-921600)。沿用同一 Z7N3 位流与引脚，不重新综合；新板网络入口通过 `-DZ7_UART_921600` 选择启动协商。50 MHz 分频 54 的实际速率约 925926，比目标高 0.469%。旧板及独立 AT 入口继续采用原 434 分频。

[AT 任务](../../software/ps_wifi/wifi_at_probe.c)在模块默认 115200 时发送 `AT+UART_CUR=921600,8,1,0,0`，等待发送完成及保护间隔后，确认 UART 空闲/高电平再切分频。暖重载仅在原速率超时后尝试已知高档，不遍历未知速率；重新执行 AT、关闭回显、验证回显、查询固件和 UART 配置后才放行。只用 CUR，不写 UART_DEF、固件或 Flash。[网络入口](../../software/network_ui/main.c)在启动验收前不消费网络，验收后持续处理串口故障并暂停 UI。

公共 AT ABI 仍为 64 字节；在高速 profile 下 word14 是实际分频，word15 是模块查询返回的实际波特率。原 22 字节验证序列增加 UART_CUR 查询后为 36 字节；原 M5-0/M5-1 的证据仍保留其旧程序/速率范围。

2026-10-14 [M5-2/C0 阶段](../../output/z7_nano/uart_921600/stage.json)已封存并通过其范围：模块查询为 930232，FPGA 分频 54（约 925926），两端差约 0.46%，帧错误和溢出均为零；完整断电后自动协商和屏幕断开时暖重载均通过，随后屏幕/常驻触摸恢复。当前程序 614400 像素比对零差异，用户确认“画面正常，按钮响应正确”。模块返回实际分频速率，不能把设置参数当作精确测得的线速。之前一次 DAP 错误恢复不代表根因已修复，见 RISKS R16。[本机验证](../../output/z7_nano/uart_921600/host_validation.json)、[串行时序仿真](../../output/z7_nano/uart_921600/wire_validation.json)与[独立复核](../../output/z7_nano/uart_921600/review.json)各保留原范围；短 AT 验收不代表持续音频、TLS 或云端已经通过。

```powershell
python -B -X utf8 scripts/validate_z7_nano_uart_baud.py --wire
python -B -X utf8 scripts/run_z7_nano_network.py build-app
```

[只读速率观察](../../scripts/inspect_z7_nano_baud.py)须在匹配的网络程序实际启动后运行。重新配置仍按现有停止 Host、确认 USB/VDMA 停机、下载、恢复常驻触摸的顺序；持续音频与 TLS/WSS 另见 [CLOUD_SPEECH](CLOUD_SPEECH.md#分阶段放行)。
