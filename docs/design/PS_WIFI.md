# PS 自主通信接管

适用范围：本文是旧 RevB W4 的自主 AT/模拟网络过渡平台；当前产品运行在 Z7-Nano 音频平台，UART/AT/TCP/TLS 均由运行中的 CPU0 拥有。操作按 [NOW](../context/NOW.md)/RUNBOOK，不能把本页旧平台和 ELF 覆盖当前应用。

核验日期：2026-10-11。本文负责 W4 新平台、PS 软件与调试保护的实际方法；原 UART 寄存器合同与 W0–W3 历史结果沿用 [WIFI_LINK](WIFI_LINK.md)。W4-0 自主 AT 与新平台实屏均已通过；W4-1 自主关联和网络模拟字幕及实屏已通过，后续网络协议/证据由 [NETWORK_UI](NETWORK_UI.md)维护，当前状态见 [NOW](../context/NOW.md)。

## 平台与边界

- 新平台入口为 [ps_wifi_top](../../rtl/ps_wifi_top.v)，独立输出在 `output/ps_wifi/`。已验收的显示及 JTAG UART 平台保留。
- HDMI 引脚、50/250 MHz 时钟、帧时序、PS/DDR、VDMA 和复位逻辑沿用显示基线。新 UART 同样使用 50 MHz 像素时钟及正确的外设复位；波特率分频仍为 434。
- 配置寄存器 USR_ACCESS 为 `0x46574e31`，原显示配置为 `0x46564231`。视频控制模块 ID 仍为原值；不能把视频 ID 当作新平台身份。
- UART PS 地址为 `0x43c20000`，窗口 64 KiB。[包装层](../../rtl/ps_wifi_uart.v) 将窗口内地址转换为原 UART 偏移；其他高位地址及保留/不对齐偏移拒绝。GP0 的第三个目标连接 UART，不引入新时钟域。
- CPU 是 UART 事务唯一所有者。JTAG 在本步骤只配置 PL、启动软件、读取公共状态及使用既有 UI 测试邮箱；不向 UART 逐字节代发 AT。

## 自主 AT 步骤

[wifi_at_probe](../../software/ps_wifi/wifi_at_probe.c) 是有界、无堆分配的协作任务，与已验收的中文字幕演示一起运行。依次发送 AT、ATE0、再次 AT、GMR；必须取得实际回复、确认第二次 AT 没有回显并取得固件版本响应。ATE0 的 OK 自身不是回显关闭证据。

发送前残留字节不会认证新事务；必须等待完整 TX FIFO/预取/PHY 空闲后再解释回复。过期观察、错误响应、过大响应、溢出或帧错误均停止通信步骤，显示任务保持独立。硬件时间使用无符号回绕安全的期限检查。本步骤不发送无线凭据，也不修改 ESP 固件、供电或启动介质。

公共状态位于 OCM `0x0001e100`，64 字节：magic `0x57505331`、有序序列、阶段、错误、AT/回显/固件标记及固件摘要、UART 收发/错误计数、运行次数和步骤。奇偶序列用 ARM 内存屏障保护。它紧接显示/UI 邮箱，未占用字体、快照或帧缓冲。完整 ARM 加载区域仍经 ELF 检查。

## 操作与保护

按以下顺序执行；工具位置由当前已核实安装环境提供，不将命令配置当作执行成功：

1. `build_ps_wifi.py simulate`：完整 PS 地址、独立串口线路解码、FIFO、背压和非法地址检查。
2. `build_ps_wifi.py build`：使用已通过的显示/实际复位回归及原厂 PS 来源，生成独立位流、XSA、初始化和时序报告。
3. `validate_ps_wifi_probe.py` 与隔离的 `test_ps_wifi_gates.py`：AT 成功/异常、回显新鲜度、期限和跨平台安全门闩。
4. `run_ps_wifi.py build-app`：验证已有中文模型/字体/演示的当前证据，构建 PS 自主 AT 程序。
5. `run_ps_wifi.py program` → `run`：每次先只读验证实际配置，再核对 CPU/DMA。已知旧显示平台可先正常停止 DMA；FPGA 未配置时单独停核；配置后严格要求新平台身份。PS 初始化仅消费一次成功配置令牌。
6. `ps_wifi_debug.py sample`：核对当前位流/ELF/独立运行快照，然后只读取公共状态。显示运行与 AT 成功分别记录；显示正常不能冒充 AT 通过。捕获仍仅读取帧缓冲与 UI 快照。

所有操作共用 `tmp/ps/operation.lock`。新平台的未确认 CPU/进程状态同时关闭显示和 PS bring-up 的旧入口；只读外部工具也在启动前持久记录未确认门闩，程序崩溃不会自动放行。只读进程确认退出后才恢复此前状态；修改性操作由实际启动/停机/清理结果放行。未知进程树不能靠一次停核绕过。原 UI 的成功运行在切换后成为历史记录，不重标为新平台验收。

## 证据与下一步骤

- [完整地址 UART 仿真](../../output/ps_wifi/simulation.json)、[独立平台构建](../../output/ps_wifi/build.json)、[时序](../../output/ps_wifi/reports/timing.rpt)：属于逻辑和构建证据，未自行证明实屏或 AT。
- [AT 主机异常检查](../../output/ps_wifi/probe_host_tests.json)、[ARM 构建](../../output/ps_wifi/build-app.json)：软件与布局证据，未自行证明真实模块响应。
- [JTAG 配置](../../output/ps_wifi/program.json)、[ARM 运行](../../output/ps_wifi/run.json)、[公共 AT 状态](../../output/ps_wifi/at_probe.json)、[用户实屏](../../output/ps_wifi/user_acceptance.json)：新平台先正常停止旧 DMA，再配置/启动；实际 CPU 发送 22 字节并取得 AT、回显关闭及固件应答，UART 错误为 0。用户确认画面清楚稳定、字幕持续更新。
- [平台评审](../../output/ps_wifi/review/review.json)、[软件与保护评审](../../output/ps_wifi/review/runtime_review.json)：逻辑、构建与隔离故障案例；不替代物理结果。

自主 AT 后的易失关联、局域网文本确认、有界 UTF-8/字库检查、模拟结果上屏及断线恢复已由 W4-1 独立验证，见 NETWORK_UI。真实音频及持续吞吐仍未验收；115200 下的音频带宽缺口按 [系统预算](SYSTEM_PLAN.md#8-资源带宽与延迟预算) 单独处理。

AT 数据接收和发送语义参考 [Espressif TCP/IP AT 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)；实际兼容性以模块回复为准。
