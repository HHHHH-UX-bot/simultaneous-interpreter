# PL 网口电脑画面与控制

## 范围与实现

用户2026-10-22将PL网口接到电脑，要求电脑看到板载屏幕效果，并能用鼠标操作设置等功能，授权自动编译和JTAG加载。本功能使用现有板端C渲染器的真实帧缓冲，不在电脑另外维护一套设置或翻译状态。译音继续由WM8731输出，云连接沿用ESP8266。

硬件依据为用户提供的 `Z7-NANO_R21.pdf` 第5、11页：U30 RTL8211F、PHY地址1、Bank35、3.3V RGMII；网口与已接音频引脚不冲突。引脚由 `hardware/z7_nano/pl_ethernet.xdc` 维护。AXI Ethernet Lite位于 `0x40e00000`，通过PG160 GMII/RGMII桥接PHY；本配置只协商100BASE-T全双工，桥接管理地址8。显示50/250MHz保留，主MMCM新增200MHz供桥接时钟。

`build_z7_nano_audio.tcl` 的 `remote-ui` 配置生成独立平台，USR_ACCESS为 `0x5a374e34`。旧平台文件保留。Etherlite的MII寄存器改为与桥接相连后不再强制IOB；其载波状态两级同步器核实为C_SENSE_SYNC_1/2，仅第一级D入口作异步约束，第二级仍接受时序检查。引脚、电压、音频CDC、时序和DRC分别核验，不以放宽全部时钟关系求通过。

## 数据与交互

- 板端冻结最近完成的RGB画面，分时复制、无损RLE编码，两个编码缓冲交替。下一幅图在电脑确认上一幅完整接收后生成，避免下载中途被覆盖。
- IPv4/ARP/ICMP/UDP通过专用网线工作；每次服务最多处理两个RX缓冲，不阻塞等待发送。画面只包含RGB像素，不提供任意内存读取。
- UDP端口4177、板端地址 `192.168.1.50/24`。电脑本轮有线地址为 `192.168.1.102/24`，保留原网卡配置。该接口不新增默认网关或更改WLAN。
- 协议 `Z7R1` 包含本次连接随机cookie、序号、帧ID、偏移和界面上下文；上下文含epoch及设置/历史/语言菜单状态。重复点击请求只执行一次，旧页面上下文拒绝，超界坐标拒绝。
- 电脑点击/拖动走 `ui_touch_remote` 与实体触摸共用的命中检测、UI命令和历史滚动函数。板端应用后才返回ACK，方向切换仍走原云连接生命周期。实体CPU1触摸保持独立运行。
- Python本地服务仅监听 `127.0.0.1:8787`，浏览器显示无损PNG并映射指针坐标。图像和界面上下文用同一次快照返回；POST限制来源与长度。cookie是会话隔离，不是对不可信局域网的强身份认证。

新增DDR保留区 `0x03000000..0x03ffffff`，帧缓存及历史导出快照共8,734,240字节，约8.33MiB，与字体、TLS、译音和原帧缓冲分离。没有动态申请无限图像缓存。

## 历史另存为

电脑画面下方选择CSV、TXT或JSON，点击“导出历史 · 另存为…”后选择目录、文件名。支持浏览器原生文件保存接口；不支持时由本机Python的Tk另存为窗口选择位置，覆盖已有文件须在窗口确认。取消不会写文件。服务端不接受HTTP传来的任意保存路径。

导出通过协议op4/5读取板上独立冻结的定稿历史，按最旧到最新排列；包含原文、译文、方向，JSON还保留会话/片段/修订和丢弃计数。快照在同一UI循环中复制，下载期间继续采音和翻译；不导出预览文本。最多保存当前RAM保留的64条，已经滚出或断电丢失的历史无法追溯。CSV使用UTF-8 BOM，兼容Excel，公式起始字符加文本前缀；TXT、JSON保留原文。导出后的文件独立保存在电脑上。

## 构建与运行

准备字体、显示头及音频仿真沿用原入口；`scripts/build_tts.ps1 -RebuildFpga` 生成本配置及ARM/CPU1。单独平台入口为 `scripts/build_remote_display.py`；ARM增加 `--remote-display --touch --arm-tls`，模式为zhen或enzh。加载器根据应用元数据选择匹配平台并核对来源、ELF/位流/初始化及JTAG身份。

电脑端运行 `打开电脑画面.cmd`，或 `scripts/remote_display_server.py --local-ip <电脑有线IPv4>`，浏览器打开 `http://127.0.0.1:8787`。启动脚本自动使用到板端地址的本机路由。鼠标点击对应按钮；历史页可拖动或滚轮滚动。电脑网页关闭后不停止板端翻译，空闲连接超时后停止画面编码。

原生检查由 `scripts/validate_remote_display.py --gcc <Windows-GCC>` 执行，覆盖真实C编码/IPv4边界/控制去重、Python解码、历史快照/三格式/指定路径保存及共用鼠标命中检测。实际结果见[本轮验证](../validation/remote-display-20261022/README.md)。

官方接口依据：[PG135 Etherlite](https://docs.amd.com/r/en-US/pg135-axi-ethernetlite/Introduction)、[PG160 v4.0（2018）](https://docs.amd.com/api/khub/documents/fEsfqJT7_MyQMrEZhzjcVA/content)。本功能为直连可信网线原型，不将cookie当作加密认证；未增加音频向电脑输出、网口云上传、持久启动或无限历史存储。
