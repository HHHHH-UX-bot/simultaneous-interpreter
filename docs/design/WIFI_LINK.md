# ESP8266 联网验证

适用范围：本文 W0–W3 的 JTAG→UART、115200 缺省及旧 RevB 引脚是配件诊断基线。当前产品为 Z7-Nano CPU0→PL UART（1 Mbaud）→ESP TCP→ARM TLS/WSS，不需要电脑作翻译中继；接线/运行身份看 [MAP](../context/MAP.md)/[NOW](../context/NOW.md)。

日期：2026-10-11。用户已连接 ATK-MW8266D（板载 ATK-ESP-01），授权继续串口、Wi-Fi 与局域网数据收发。接线与电气来源以 [MAP](../context/MAP.md#esp8266-连接) 为准，阶段结果在 [NOW](../context/NOW.md) 汇总。

## 当前结构

电脑控制脚本 → JTAG-to-AXI → PL UART FIFO → ESP8266 → Wi-Fi → 局域网电脑。

这是 W0–W3 配件接入阶段的调试通道，尚未由 PS 自主接管，也未传输音频或真实识别/翻译结果。该阶段沿用静态画面；后来的 PS 动态中文显示独立由 [DYNAMIC_UI](DYNAMIC_UI.md) 实现，当前装载情况见 NOW。后续可由 PS 接管同一寄存器合同，不将已完成的动态演示等同于网络字幕。

[wifi_ui_top](../../rtl/wifi_ui_top.v) 复用既有显示、时钟和 TMDS 子模块；UART 与 JTAG-to-AXI 使用缓冲后的 50 MHz 像素时钟，复位受外部复位、MMCM 锁定和上电延迟控制。芯片时钟输入只进入一个 IBUF。

收发 FIFO 各 4096 字节、同步 BRAM 读出。UART 使用两级输入同步，8N1，默认分频 434（名义 115200 波特）。分频只能在收发空闲且没有待处理起始位时修改。尚未完成持续音频带宽验收。

## AXI-Lite 寄存器

32 位、按字对齐；非法地址和非法分频写返回 SLVERR。AW/W 分别握手，B/R 响应支持背压。TX 写只取低字节，遵守 WSTRB[0]。

| 地址 | 名称 | 语义 |
|---|---|---|
| 0x00 | ID | 只读 0x57494631 |
| 0x04 | STATUS | bit0 RX 非空；bit1 TX 有空间；bit2 TX 忙，包含 FIFO/预取/PHY；bit3 RX 忙；bit8 同步后 RX 电平；bit9 TX 电平 |
| 0x08 | DIVIDER | 每位周期数，16..65535；忙或起始位待处理时拒绝修改 |
| 0x0C | CONTROL | 一拍控制脉冲，读为 0；具体见下文 |
| 0x10 | TX_DATA | 低 8 位入队，满时返回 SLVERR |
| 0x14 | RX_DATA | 弹出一个字节；bit31=1 表示有效，低 8 位为数据；空时返回 0 |
| 0x18 / 0x1C | RX_COUNT / TX_COUNT | FIFO 排队字节数，不含预取和在途字节 |
| 0x20 / 0x24 | RX_OVERFLOW / FRAMING_ERROR | 接收溢出及错误停止位计数 |
| 0x28 / 0x2C | TX_STARTED / RX_RECEIVED | PHY 开始发送数、正确接收帧数；TX_STARTED 不等于停止位已发送完毕 |

CONTROL：bit0 丢弃 RX 队列；bit1 丢弃 TX 队列与预取字节，已开始的帧继续；bit2 复位/中止 PHY 并丢弃预取字节，保留剩余 TX FIFO；bit3 清计数。通常空闲时写 0xF 初始化。控制脉冲与 FIFO/PHY 握手互斥。判断发送排空使用 STATUS bit2，不能只看 TX_COUNT。

## 操作与证据边界

[run_wifi.py](../../scripts/run_wifi.py) 按 simulate → build → probe → program 执行，显式给出 --vivado 启动器，使用 machine Python。仿真前后校验 RTL、约束、构建参数和测试哈希；构建、下载按依赖证据放行。旧显示证据只有在所有记录输入仍匹配时复用。

[wifi_link.py](../../scripts/wifi_link.py) 的 probe 检测 AT 波特率与版本；connect 从交互或 --credentials-stdin 获取凭据。依次验证 AT、关闭回显、易失 Wi-Fi 参数、请求的接入点与 STA 地址，再核对双向 TCP 数据。--subnet 指定当前局域网范围。

探测记录每档分频接受情况、发送/接收数、帧错误和溢出；115200 下另有带保护时间的 +++ 退出透传检查。没有收到 AT 应答时，这些计数只反映 FPGA 内部状态，不能证明实际排针波形、模块供电或固件正常。

TCP 测试临时启用 ESP 的 8266 端口，由电脑主动连接；载荷含随机标记、全部 256 字节值和中文 UTF-8。往返一致并确认临时服务关闭后才通过。这个结果不是翻译服务或音频吞吐验收。CIPSEND 必须收到实际 > 提示符才发送数据，不能用提前到达的 OK 代替。

Wi-Fi 密码不放入源码、位流、命令行、文档或原始串口日志。私有 Vivado 子进程关闭 trace/log/journal，UART 数据只经过进程管道；结果只保存状态、版本、IP、计数、摘要和时间。路由器后台仅在定位实际网络问题时按当前授权读取；不改写 QSPI/SD 或 ESP 固件。

使用 --credentials-stdin 时，管道直接读取 JSON；交互控制台使用隐藏输入，防止凭据被终端回显。关联前必须确认 ATE0 生效。验证结果与实际固件保存在 [connect_link](../../output/wifi/hardware/connect_link.json)，阶段结论以 NOW 为准。

## W4 接管边界

P2b-3 已完成控件、实屏及实际长期更新验证，当前按 [RUNBOOK W4](../context/RUNBOOK.md#w4-板端自主网络与音频传输带宽后续) 接管。新平台构建、自主 AT 软件与操作方法见 [PS_WIFI](PS_WIFI.md)，实际硬件结果以 NOW 为准。以下边界仍适用；关联、网络字幕和持续音频负载不是构建成功自动获得的能力：

1. 当前 UART 的寄存器解码比较完整地址，而 JTAG 阶段使用零起始偏移；PS 地址空间必须由包装层转换为外设局部偏移，并拒绝保留地址，不能直接把绝对 AXI 地址送入旧解码器。
2. 原 UART 使用 50 MHz、434 分频；[显示系统连接](../../scripts/build_framebuffer.tcl) 同样将 GP0/HP0、VDMA 和显示控制接到 50 MHz 像素时钟。优先复用该时钟、分频及正确的外设复位，避免为接管 UART 引入额外时钟域。实际启动先核对收发空闲并清理队列/错误，再进行 AT；新集成位流仍须单独验证。
3. 新平台应有独立的实时配置身份、构建和初始化快照，使旧显示位流不能被误当成带 UART 的平台并读取不存在的 MMIO。停机与 VDMA 保护继续共用，原显示和 Wi-Fi基线不覆盖。
4. PS 成为 UART/FIFO/AT 唯一事务所有者；JTAG 只访问定义好的状态/配置邮箱。AT、回显关闭、固件查询、易失关联及 TCP 按顺序分别取证。旧关联记录不能证明模块断电后仍连接；凭据仅经私有运行时输入进入 RAM，不写 ELF、文件、命令行或原始回显日志。
5. 先传明确标注的模拟结果，验证 UTF-8、长度、epoch/segment/revision 与旧消息隔离。当前字库是子集；入站未知字形应在应用前拒绝并反馈，不能让未受控消息触发绘制失败。真实任意中文覆盖与字体许可仍需后续补足。
6. 115200、8N1 每方向理论约 11,520 字节/秒；4096 字节 RX FIFO 在持续满速输入下约 356 毫秒填满。绘制期间也需考虑收包服务间隔及有界流控；抓屏暂停 CPU 时不能任由对端持续灌入。网络文本测试可用逐包确认限制在途负载，不能以此代替连续 PCM 带宽验收。

前两项现已有独立平台包装/软件初始化及构建产物。第一轮放行仍是 PS 自主取得 AT 响应，再进入关联和模拟结果上屏，不同时改动 HDMI 时序、模块供电与串口速度。

## 参考

- AMD [JTAG-to-AXI Master](https://docs.amd.com/r/en-US/ug908-vivado-programming-debugging/JTAG-to-AXI-Master)、[create_hw_axi_txn](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/create_hw_axi_txn)、[run_hw_axi](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/run_hw_axi)：无处理器 PL 设计可由 JTAG 发起 AXI 事务；不以吞掉错误的 quiet 调用判断成功。
- Espressif [Wi-Fi AT 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.3.0.0_esp8266/AT_Command_Set/Wi-Fi_AT_Commands.html)、[TCP/IP AT 命令](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/TCP-IP_AT_Commands.html)：兼容性由实际模块版本和响应确认。
