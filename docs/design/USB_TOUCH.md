# Z7-Nano 裸机 USB 触摸

## 范围与事实入口

2026-10-13：用户已将屏幕 TOUCH 接到新板 USB-A Host。板级事实由 [source.json](../../hardware/z7_nano/source.json)维护；其中 `ULPI_MIO` 保存区间端点，生成器展开为全部 MIO。显示验收与 USB 验收分别记录，不能由屏幕已亮推定触摸已工作。

先执行 M4-0 控制器与 PHY 探测，再执行设备/配置/HID 描述符、原始触摸报告、屏幕坐标和按钮事件。USB3320 的 [原厂寄存器表](https://ww1.microchip.com/downloads/en/DeviceDoc/00001792E.pdf)说明 VID/PID 与 scratch 寄存器；[UG585 ULPI viewport](https://docs.amd.com/r/en-US/ug585-zynq-7000-SoC-TRM/ULPI-PHY-Viewport)提供控制器访问机制。

## M4-0 控制器与 PHY

[配置生成](../../scripts/generate_z7_nano_usb_config.tcl)只产生 USB0 的 PS 参考配置，不执行或下载硬件。[USB 派生输入](../../scripts/prepare_z7_nano_usb.py)只提取 USB MIO 设置；不运行这份完整 `ps7_init.tcl`，不重设 DDR、PLL 或 PS–PL 接口。

Vivado 2026.1 本次生成结果自动清除了 USB0 的 reset 选择，证据见 [实际属性](../../output/z7_nano/usb/config/properties.txt)。探测程序根据板级来源，独立将 PHY reset 配成同一 1.8 V bank 的 GPIO，用 bank1 masked DATA 和 DIRM/OEN 产生低有效复位，不以生成配置中没有生效的选项宣称已驱动。

[探测程序](../../software/z7_nano_usb/probe.c)在 CPU1 的低 OCM 运行；[链接范围](../../software/z7_nano_usb/link.ld)为入口 `0x1000`、报告 `0xE000`、栈顶 `0xFFF0`。CPU0 的动态 UI、字体、快照与双帧缓冲保持原区域。探测不启动 USB DMA，也不复位整个 PS。

检查内容为控制器 capability、PHY `0424:0007`、scratch 写读、Host 模式及端口状态。2026-10-13 已实测通过，证据见 [probe_run.json](../../output/z7_nano/usb/probe_run.json)。`PORTSC=0x80001803` 检测到设备连接且无 active over-current；CPU0 继续运行。寄存器探测不代表已获取屏幕设备描述符或触摸坐标。

首版在复位后的第一笔寄存器读取收到 `0x01`，后续身份读取正常；仅移动 Host 设置或显式 wake 没有消除异常。初始化改为先完成 scratch 写读校验，再读取身份，与 [Linux ULPI 初始化顺序](https://github.com/torvalds/linux/blob/master/drivers/usb/common/ulpi.c)一致；诊断见 [scratch-first 记录](../../output/z7_nano/usb/first_ulpi.json)。程序已先驱动 RESETB，scratch 根据原厂表恢复默认 0，测试后写回 0。保留严格身份与写读检查，没有加入忽略错误或循环重试。尚未证明首笔异常的内部硬件机制。

## 操作入口与后续方案

在项目根目录、machine Python 中执行已有入口；命令不是新的执行授权：

```powershell
python -B scripts/run_z7_nano_usb.py build-probe
python -B scripts/run_z7_nano_usb.py run-probe
```

[run_z7_nano_usb](../../scripts/run_z7_nano_usb.py)检查新板当前运行的 ARM 程序及同一次配置的完整帧比对，使用实时 PL 身份、同一硬件操作锁和产物快照。实际结果位于 `output/z7_nano/usb/`，中间文件在 `tmp/z7_nano/`。

屏幕仅由 TOUCH USB 供电后，启动依赖调整为显示平台/ARM 启动 → 当前帧比对 → USB PHY/Host 电源 → 实屏确认。USB 供电必须先于实屏验收；不以旧配置的用户验收挡住供电，也不将旧验收改写成新配置已实屏通过。当前程序、完整像素、PL 身份和线缆保护仍保留。

## M4-1 设备枚举与 HID 描述符

Host 实现复用 [TinyUSB](https://github.com/hathach/tinyusb) 0.21.0 的 EHCI 与 HID，添加 Zynq 控制器适配；原始提交为 `dae3f9a366bfcddbf9dcf1b48d7500286a849539`。所用子集和上下游哈希见 [UPSTREAM](../../software/third_party/tinyusb/UPSTREAM.json)，保留 [MIT LICENSE](../../software/third_party/tinyusb/LICENSE)。本项目修改 EHCI 的五处硬件等待以增加超时，并增加下述 Zynq 端口复位门禁；描述符和传输调度逻辑沿用该版本。

[Host 应用](../../software/z7_nano_usb/host.c)在 CPU1 低 OCM 工作，保存设备、配置、HID 报告描述符与最近 16 条原始报告，不直接驱动 UI。[适配](../../software/z7_nano_usb/usb_board.c)使用已有全局计时器，轮询 EHCI 事件，不启用 GIC 或重置 CPU0/DDR。2026-10-13 实际枚举与 120 秒观察通过，见 [Host 记录](../../output/z7_nano/usb/host_run.json)。屏幕 USB 身份为 `1a86:e5e3`，full-speed、一个 HID 接口、端点 `0x82`，最大包 64 字节，报告描述符 390 字节。2026-10-20 当前英文候选的间歇零报告及复位顺序对照见下节，不能由历史通过推定当前可用。

首轮固件在组装紧凑格式的端点描述符时执行未对齐写入，CPU1 进入数据异常；ARM 编译加入 `-mno-unaligned-access` 后控制请求正常。随后双 USB 连接仍出现真实断开事件。用户发现 POWER USB 接电脑也能传触摸，断开该线后同一固件枚举成功；当前接线由 [迁移专题](BOARD_MIGRATION.md#当前接线与实施方向)维护。该对照支持双 Host 连接造成干扰的判断，屏幕内部电路并未核实，不能声称两个口的数据线已确认直接并联。

[真实触摸记录](../../output/z7_nano/usb/touch_capture.json)保存 120 秒内收到的 686 条报告总数，以及最后 16 条中的按下和松开。用户确认画圈、中心点击及单 USB 供电正常。旧环形捕获未保留完整画圈轨迹，不据此声明四角或拖动已验收。当前显示仍推进、欠载和格式错误为零，见 [取样](../../output/z7_nano/display/sample.json)。

USB 使用 DMA；停止 CPU1 并不等于 DMA 已停止。[停止函数](../../scripts/z7_nano_usb_stop.tcl)在覆盖 USB 的 OCM 或重新配置显示平台前关闭并确认 USB0 Halt。[枚举入口](../../scripts/z7_nano_usb_host.tcl)结束和失败时也执行停止确认；CPU0 显示继续运行。

```powershell
python -B scripts/run_z7_nano_usb_host.py build
python -B scripts/run_z7_nano_usb_host.py enumerate
```

实际尝试位于 `output/z7_nano/usb/host_attempts/`。捕获由 [描述符检查](../../scripts/inspect_z7_nano_usb.py)解释，USB 设备身份不能用板上 PHY 身份替代。

`enumerate --observe-seconds 120` 已执行；默认观察 10 秒，允许 1..300 秒。运行前冻结接线记录；只有观察、停止 CPU1、确认 USB DMA Halt 与捕获均通过才更新成功记录，取消或超时在硬件锁内清理。失败寄存器在关闭控制器之前记录。

## M4-2 坐标与点击边沿

依据 [USB-IF HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf)和[Usage Tables](https://www.usb.org/sites/default/files/hut1_7.pdf)，[实际描述符解析](../../scripts/inspect_z7_nano_touch.py)得到报告 ID 1、44 字节、五个接触槽、X/Y 逻辑范围 0..4096；不能用 USB 最大包长 64 当成报告长度。[格式生成](../../scripts/prepare_z7_nano_touch.py)从已实测描述符产生严格匹配的 [touch_format.h](../../output/z7_nano/usb/touch_format.h)，不加入未知屏幕的猜测兼容。

[解码器](../../software/z7_nano_usb/touch_input.c)将坐标映射到 1024×600，重复按住报告只产生一次按下，松开产生一次 tap；多指输入取消点击，直到全部松开。按下和松开必须落在同一按钮内才能执行后续 UI 控制。M4-2 暂不控制 UI，先实测四角与中心。

[共享接口](../../software/z7_nano_usb/touch_link.h)位于低 OCM `0xD000`、256 字节，保存触摸状态和八项 tap 队列。状态以序号和内存屏障发布；队列满时计入丢弃数量，不覆盖尚未消费的 tap。链接器限制 BSS、Host 报告、描述符和栈的边界。[电脑侧检查](../../output/z7_nano/usb/touch_input_tests.json)覆盖真实报告及边界、多人触摸、队列场景。

首轮五点采集收到 598 条报告后出现断开；用户确认黑屏后主动拔插了 USB，见 [该次失败记录](../../output/z7_nano/usb/host_attempts/20261013T103813780881Z/failure.json)。已确认 DMA 停止、CPU0 仍运行；该尝试不作为五点或固件稳定性验收。保持接线后的新采集另行验证，不能混用两次报告。

保持接线后的 [五点结果](../../output/z7_nano/usb/touch_corners.json)已通过：依次为左上 `(41,35)`、右上 `(977,29)`、右下 `(978,547)`、左下 `(29,551)`、中心 `(489,277)`；恰好五次按下和五次松开，240 秒、384 条报告，无断开或丢弃。用户按该顺序操作并回复“好了”。证据绑定校准版 CPU1 ELF；这是方向/区域验证，不是逐像素精度或按钮验收。

## M4-3 真实按钮接入

[UI 消费者](../../software/subtitle_ui/ui_touch.c)使用原有 [UI 控制模型](../../software/subtitle_ui/ui_model.h)，响应开始/暂停/继续、清空、原文开关及字号。按下与松开必须在同一有效按钮内；跨按钮拖动、按钮外点击、设置占位和模型拒绝的控制只计为 ignored。没有新建 UI 状态机。

运行时 CPU1 独占整个 `0xD000` 接口，CPU0 独占 `0x1E040`、64 字节 feedback；ACK 的 generation 必须等于当前生产者且消费位置在已发布范围内。CPU1 才更新自己的 tap_read，避免重启时旧 ACK 污染新队列。CPU0 启动时在 CPU1 已停止的前提下跳过旧采集点击；每个发布块有独立序号和内存屏障。字号/原文动作沿用现有演示计时调整，界面可在控制改变后提前刷新。

[电脑检查](../../output/z7_nano/usb/ui_touch_tests.json)、两核 ARM 编译和[独立复核](../../output/board_migration/review_ui_touch.json)已通过；该复核只放行受控按钮测试。后续真实控件结果见下方本次实屏验收。枚举结束还会停止 Host，不能把短时测试入口当作持续运行产品。

2026-10-13 新版 ARM UI 已启动，[同次配置完整帧](../../output/z7_nano/display/capture.json)零差异，PHY 重探测及 HID 枚举通过。用户在按钮测试开始后报告“特别暗的黑屏能看到大概是 UI”，并主动拔插；[本次失败记录](../../output/z7_nano/usb/host_attempts/20261013T131201345531Z/failure.json)为 error 17、零报告、七项控制计数均为零，CPU1/DMA 已停止，CPU0 继续运行。之后 [显示计数](../../output/z7_nano/display/sample.json)继续推进，欠载/格式/程序错误均为零，不能据此宣称实际背光已正常。

用户报告开发板由电脑 USB 扩展坞的板载 JTAG 口供电。[原理图电源页](../../tmp/board_migration/schematic-page-15.png)显示 JTAG/UART USB 的 5 V 经二极管汇入板电源。屏幕、板卡和新接 ESP 共享供电路径；实际 VBUS 电压、电流和屏幕内部 USB 拓扑仍未测量。独立屏幕电源对照未执行；用户随后明确要求先重试触摸，不能把独立电源当作已获结果。POWER 口接电脑曾干扰触摸。

JTAG 信息发现存在异步时序：抓屏在 CPU 暂停前曾因缺少 cable 元数据失败；在连接后加入与其他入口一致的 1 秒等待，保留严格线缆检查，重做抓屏通过。显示采样同样保留该等待。八次真实控制的检查入口为 [check_z7_nano_touch_buttons](../../scripts/check_z7_nano_touch_buttons.py)，目前未执行。

### 触摸启动后的暗屏重现（待定位）

2026-10-13 用户纠正症状：启动触摸实验后，屏幕中间先出现黑线，然后逐渐变暗；并确认全过程没有动接线。[该次失败记录](../../output/z7_nano/usb/host_attempts/20261013T132402298070Z/failure.json)为枚举成功后约 66 秒自行断开，error 17、零 HID 报告、零已接受控制；不能归因到人工拔插或 UI 按钮。Host/DMA 已停止，CPU0 继续运行。[故障后的完整帧](../../output/z7_nano/usb/host_attempts/20261013T132402298070Z/display_capture_after_fault.json)614400 像素零差异，抓取图像无异常黑线；[刷新取样](../../output/z7_nano/usb/host_attempts/20261013T132402298070Z/display_sample_after_fault.json)欠载、格式和程序错误均为零。以上是数字画面证据，不等于实屏或背光正常。

故障前 PORTSC 的供电位为 1，故障快照为 0；清理后再次读取已恢复为 1。三次停机读取均得到 PHY OTG Control `0x27`、Interrupt Status `0x14`，即 CPEN 被请求启用，但内部 VbusValid 为 0，见[第一份原始读取](../../output/z7_nano/usb/power_inspections/20261013T133709450583Z/result.json)及失败记录引用的另外两份。依据 [USB3320 数据手册](https://ww1.microchip.com/downloads/en/DeviceDoc/00001792E.pdf)的 5.6.3/7.1.1.7/7.1.1.10，这是供电有效性异常的线索，不是电压测量，也未证明电流不足或软件根因。[USB 页](../../tmp/board_migration/schematic-page-09.png)中的 TPS2051B OC 仅上拉，没有接到 PHY/CPU；不能以端口过流位为 0 排除该开关限流。

用户要求再运行一次同样实验。为记录触发先后，枚举入口增加可选 `--power-trace`：初始化期间只读控制器，枚举后每秒记录控制器、Function Control、CPEN 请求和 VbusValid；ULPI 只发寄存器读/唤醒命令，不写电源或复位配置。[独立复核](../../output/board_migration/review_usb_power_trace.json)的八项 mock 通过；[加载段比较](../../output/board_migration/power_trace_elf_comparison.json)确认触摸固件的加载内容与失败尝试相同，整个 ELF 哈希因非加载部分不同而变化。CPU0 UI、PL 和显示时序未改。

### 本次实屏控件验收

`enumerate --observe-seconds 120 --power-trace` 已执行：[本次冻结运行](../../output/z7_nano/usb/host_attempts/20261013T135143830934Z/validated_host_run.json)收到 344 条报告、22 次完整点击，22 项队列全部消费、无丢弃、无断开。用户自由点击后确认“画面正常，触摸正常，我几乎把所有按钮都按了，UI也有正确的对应变化”。[控件与实屏验收](../../output/z7_nano/usb/touch_controls_acceptance.json)绑定本次 ELF、配置、队列与用户确认。

七种有效控制均有执行：开始 1、暂停 4、继续 2、清空 3、字号减 2、字号增 2、原文开关 3；17 项接受与 5 项 ignored 合计 22。该轮按有效控件覆盖和队列账目验收，没有执行固定八步序列，也没有确定每一次 ignored 的具体理由。设置仍为占位。

本轮没有复现黑线/渐暗。91 次枚举后供电采样同样得到 Function Control `0x45`、OTG Control `0x27`、Interrupt Status `0x14`，PORTSC 供电位始终为 1；原始 [CSV](../../output/z7_nano/usb/host_attempts/20261013T135143830934Z/power_trace.csv)已保存。因此 VbusValid 单独为 0 不能区分正常与故障，供电不足和软件根因都未证明；采样带来的读/唤醒及时序影响也未排除。间歇暗屏仍未解决。

短时测试完成后 CPU1/DMA 已停止，CPU0 显示继续。持续运行采用下方单独验收的常驻入口。

## 2026-10-20 英文候选的零报告对照

旧版与新编译 Host 都曾收到真实触摸报告并改变字号状态，之后同一程序再次启动却出现枚举正常、心跳推进、零原始报告。失败 QH 对应 FS/EP2/64 字节，S-mask=`01`、C-mask=`1c`、直连 HubAddr=0；overlay token `0x00408981` 表示 Active、ERR、CErr=2，SplitXState 实际为 0。不能把它误解为已证明卡在 complete-split，也不能只按版本归因。

[AMD EN247 第16页 AR47540](https://docs.amd.com/api/khub/documents/nvT6Go3MRJkASZYoi2lamA/content)要求端口复位的 ULPI 后处理完成前不启用 USBCMD.RS，以避免首个 SOF 损坏。现有 EHCI 初始化先启 RS，随后复位端口，缺少这项规避。板级适配现同步执行清 RS → 确认 HCH → 置 PR → 有限等待 PR 读回 0 → 立即恢复 RS；不跨 TinyUSB 的 50 ms 异步回调保持停止。改动只由 `Z7_USB_RESET_GUARD` 在本板启用，原上游哈希保持，局部修改哈希单独更新。

实际函数的寄存器顺序与三处超时回归通过，首次上板两次复位均完成 HCH/PR 门禁，最后一次耗时 55 ms；但随后用户再次反馈无触摸，原始报告仍为零，普通停机也仍超时。因此这项时序规避不是当前故障的已证实根因或完整修复，不能据此放行可靠性。核对当前 generation 后已停止 CPU1，并单独复位 USB0 确认 DMA Halt；CPU0 保持暂停和最后译文。

同一 TOUCH 数据线临时只接电脑一个 Host，用户确认触摸可用，随后接回 FPGA。新的当前 Host 收到 112 条报告，用户确认保留的真实中文随 A−/A+ 改变；接着正常停止、DMA Halt、重启均通过，再收到 172 条报告，继续/暂停及字号输入有实际记录。此对照支持设备/数据线能工作，但电脑与板卡供电路径不同，未据此排除板端电气因素，也未证明官方复位门禁已消除原间歇故障。当前证据入口见 [英文阶段](../context/ENGLISH.md#当前阶段触摸控制与时延界面2026-10-20)。

## 常驻触摸与断电恢复

[常驻入口](../../scripts/run_z7_nano_usb_live.py)的 start 只加载 CPU1，sample 正常路径只取样，stop 确认 CPU1 停止及 USB DMA Halt，CPU0 显示继续。每次启动发布新的 generation；取样/停止要求与当前程序、线缆和 generation 匹配，不接管未知实例。[工具复核](../../output/board_migration/review_usb_live.json)的 33 项隔离检查通过。

2026-10-15 补充：同一线缆完成新的受保护 PL 配置后，旧 program 绑定的活动记录可进入重新验证；仍须满足当前构建/应用/PHY 身份、CPU1 实际 Stopped 和 USB DMA Halt，不能只凭 program 编号变化就覆写运行核。同一次 program、不同或未知线缆的活动/未确认记录继续拒绝重复启动。五项隔离测试与实际恢复见 [本次复核](../../output/z7_nano/usb/restart_review.json)及 [实屏确认](../../output/z7_nano/usb/restore_20261015.json)。

2026-10-13 的 [生命周期记录](../../output/z7_nano/usb/live_lifecycle.json)按 start → sample → stop → restart → sample 实测通过。两次 generation 不同，停止时确认 DMA Halt，重启后继续接收与消费触摸。最后取样为 344 条报告、16 次点击、零丢弃；用户确认“画面正常，按钮仍有正确响应”。此证据不等于热插拔、长期整机或独立启动验收。

2026-10-14 用户确认重新上电、全部接线不变；[只读探测与上电事实](../../output/z7_nano/usb/power_cycle_20261014.json)表明上一临时会话已结束。已重新配置同一 PL 位流、启动同一 CPU0/CPU1 固件，614400 像素零差异、显示与 Host 进度继续、显示错误为零，见[本次恢复](../../output/z7_nano/usb/latest_restore.json)。用户重新点击主按钮和 A+ 后确认“画面正常，按钮有正确响应”。旧会话记录保留原日期及范围；本次使用 JTAG，断电后仍需重新加载，未改写 QSPI/SD。
