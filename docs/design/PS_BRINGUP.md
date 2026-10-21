# PS 与 DDR 最小验证

适用范围：本文 PS 初始化与验证源码针对旧 ZYNQ_MINI_REVB；当前 Z7-Nano 使用其独立 source.json/PS 初始化和 BOARD_MIGRATION 入口。DDR 检查会改写内存，不得在当前云应用/显示/USB DMA 运行时直接执行旧 bring-up 命令。

## 范围与实现

本阶段在保留当前 PL 位流的情况下，通过 XSDB 临时运行 Cortex-A9 核 0 的裸机检查程序；不生成启动镜像，不写 QSPI/SD。长期操作规则仍以 [AGENTS](../../AGENTS.md) 为准，当前结论以 [NOW](../context/NOW.md) 为准。

初始化数据来自 NAS 中同型号 `arm_01_uart_hello_world` 的 2018.3 硬件导出。器件、PS 参考时钟、DDR 型号及总线宽度已与 RevB 原理图核对；参数和文件哈希的权威记录是 [source.json](../../hardware/zynq_mini_revb/source.json)，原厂数据见 [ps7_init.tcl](../../hardware/zynq_mini_revb/ps7_init.tcl)。这里只复用寄存器初始化数据，没有将旧 HDF 当作 2026.1 软件平台。后续新增 PS/PL 接口须重新生成匹配的硬件导出与初始化数据。

[start.S](../../software/ps_bringup/start.S)、[main.c](../../software/ps_bringup/main.c)、[link.ld](../../software/ps_bringup/link.ld) 不依赖 BSP 或第三方运行库。代码位于低地址 OCM bank 1：`0x00010000–0x0001FFFF`；结果区为 `0x0001E000` 的 13 个字，栈与结果区隔离。复位后明确关闭 MMU、L1 数据/指令缓存、分支预测和高地址异常向量，异常向量位于程序入口。

DDR 主测试区为 `[0x01000000,0x01400000)`：walking ones/zeros、四种全区数据模式与地址派生模式。另用 `0x00100000`、`0x0FF00000`、`0x10000000`、`0x1FFF0000` 检查四个区域的独立性。每次共 5,242,948 次读取核对。这不是全部 512 MiB 的逐字节测试，也不代表温度、电源或长期 DDR 可靠性已验收。

该区域与后续 [DDR 图像缓冲](FRAMEBUFFER.md#数据与软件布局) 重叠。显示 DMA 活动时不得运行此测试或重新初始化 DDR；先依照显示阶段流程停止并复位 DMA。同一操作锁也不能替代这项设备状态检查。

## 入口与恢复

[run_ps_bringup.py](../../scripts/run_ps_bringup.py) 提供 `build`、只读 `probe`、`run` 和失败后的 `cleanup`。工具路径显式传入，使用现有 Vitis ARM GCC 与 XSDB。命令格式如下，尖括号是需要替换的安装路径；这些模板不构成自动执行授权：

```powershell
python scripts/run_ps_bringup.py build --gcc '<ARM GCC exe>'
python scripts/run_ps_bringup.py probe --xsdb '<Vitis xsdb.bat>'
python scripts/run_ps_bringup.py run --xsdb '<Vitis xsdb.bat>'
```

实际已执行结果见下方证据。本次连接既有 `localhost:3121`，限定到已识别的 JTAG 线缆；不结束硬件服务器或用户 GUI。运行时先幂等停止两个核，再确认 L2 未启用，执行有界初始化，只复位核 0，下载独立 ELF 快照。CPU 发布结果前后执行 DSB；主机有界轮询，停机后重新读取完整结果，核验签名、阶段、计数、范围和 SCTLR。

同一驱动的操作互斥，启动硬件子进程前持久化未收尾门闩。超时或取消保留输出，只终止本次子进程树，再用独立有界连接确认两核停止。原进程树未确认退出时，单独看到 CPU 停止不能解除门闩，须先诊断原进程及其子进程，不能直接重试初始化。正常结束时两核均停止；恢复原显示可按 [RUNBOOK](../context/RUNBOOK.md#hdmi-屏幕启动与异常恢复) 操作，原静态/Wi-Fi 位流保留。

## 已取得证据

核验日期：2026-10-11；范围为以下实际执行记录。

- [构建](../../output/ps_bringup/build.json)：ARM GCC 13.4.0，无编译警告，ELF 布局与输入哈希检查通过。
- [只读探测](../../output/ps_bringup/probe.json)：PS 可访问，实测启动寄存器低三位为 1（QSPI）；这不证明已有启动内容或缓存状态。
- [第一次 CPU/DDR 检查](../../output/ps_bringup/attempts/20261011T045429Z_a75aaf83/result.json)、[第二次检查](../../output/ps_bringup/attempts/20261011T045453Z_ea850087/result.json)：分别独立初始化、下载、运行，均通过。最新摘要见 [run.json](../../output/ps_bringup/run.json)。
- [修正调试器状态缓存后的第三次检查](../../output/ps_bringup/attempts/20261011T054301Z_5bae14c1/result.json)同样通过；实时 `state` 确认停止，避免缓存属性引发重复 stop。范围仍是上述限定区域，未新增长期可靠性结论。
- [主机失败路径测试](../../tests/test_ps_driver.py)：八项检查已执行通过；不连接板卡。结果见 [host_checks.json](../../output/ps_bringup/host_checks.json)。
- [独立代码评审](../../output/ps_bringup/review.md)：停机重试、结果发布、快照哈希、进程清理和向量前提的 Important 问题已修正，之后才运行真实硬件。

后续 DDR 帧缓冲、PL 连续取帧、中文渲染、PS 自主联网、USB 触摸和音频能力均须各自验证，不能从本阶段通过推断。

## 技术依据

调试行为按现有 2026.1 XSDB 本地帮助及 AMD [XSDB 指南](https://docs.amd.com/r/en-US/ug1725-xsdb-reference-guide/Running-an-Application-in-Non-Interactive-Mode) 核对。`rst -processor` 的范围见 [官方命令说明](https://docs.amd.com/r/en-US/ug1725-xsdb-reference-guide/rst)。初始化参数使用板卡原厂资料；不从通用教程猜 DDR 时序。
