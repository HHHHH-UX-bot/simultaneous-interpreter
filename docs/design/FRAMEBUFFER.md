# PS/DDR 可更新显示

适用范围：本文是旧 RevB P2b-2 显示测试与无缓存基线。当前 Z7-Nano 的配置看 BOARD_MIGRATION，云应用已启用 MMU/Cache 并在发布帧/快照前清理缓存；不能将下方“关闭 Cache/尚无自主网络”当作当前固件事实。DMA 停机、帧发布和实体身份保护继续有效。

## 阶段与边界

本专题负责 P2b-2 的 CPU 写像素、PL 连续取帧与翻页。当前实际结果见 [NOW](../context/NOW.md)；彩条、棋盘格和移动块是显示测试，不能作为中文渲染、翻译、触摸或音频能力的证据。沿用已验收的 HDMI 引脚、50 MHz 像素时钟、250 MHz 串行时钟和 1024×600 时序。

数据路径：Cortex-A9 核 0 → DDR 中两幅 XRGB8888 图像 → PS HP0 → VDMA MM2S → AXI Stream → [framebuffer_video](../../rtl/framebuffer_video.v) → 原 HDMI 编码与串行输出。[framebuffer_top](../../rtl/framebuffer_top.v) 在本阶段把 ESP TX 保持为串口空闲高电平；当前显示设计尚未接入自主网络数据。

## 数据与软件布局

| 对象 | 配置 |
|---|---|
| 图像 | 1024×600；每像素 4 字节，内存字为 `0x00RRGGBB`；stride 4096 字节 |
| 两幅图像 | `0x01000000`、`0x01400000`；每幅 2,457,600 字节 |
| VDMA 控制 | `0x43C00000`；MM2S、两个 frame store、64 位内存口、32 位 stream |
| 显示控制 | `0x43C10000`；ID 为 `0x46564231`，寄存器定义见 RTL 与 [main.c](../../software/framebuffer/main.c) |
| 程序、栈、报告 | 低地址 OCM；沿用最小 PS 链接布局，报告为 `0x0001E000` 起 16 个字 |

显示应用保持 MMU/L1 数据与指令缓存关闭；启用缓存须另行实现和验证同步，不能直接沿用当前结论。只绘制未显示的缓冲，在发布 DMA 地址前执行 DSB，等待 VDMA park 当前读帧索引改变后才复用旧缓冲。显示报告以序列号和 DSB 发布，主机只接受完整一致的快照，并检查 CPU 仍运行和计数继续前进。

最小 DDR 检查会改写上述图像范围并重新初始化 PS。**不得在显示 DMA 工作时执行 `run_ps_bringup.py run`**；先按对应阶段的停机流程关闭显示、停止并复位 VDMA。PS 与显示驱动共用操作锁，锁不替代设备当前状态检查。

## 构建和操作入口

[prepare_framebuffer](../../scripts/prepare_framebuffer.py) 从通过核查的原厂参数生成当前 PS 配置；[build_framebuffer](../../scripts/build_framebuffer.tcl) 生成匹配的 PS/PL 系统、位流、XSA 与 2026.1 `ps7_init.tcl`。XSA 不含位流；交付位流和初始化文件分别核对哈希，没有将旧 HDF 当作新平台。

[run_framebuffer](../../scripts/run_framebuffer.py) 提供 `build`、`build-app`、`program`、`run`、只读 `probe/sample` 和 `cleanup`。使用 machine Python；Vivado/XSDB/编译器可显式传入，观察时的 Vivado 默认指向已核验安装。以下为方法模板，操作授权仍由当前用户指令决定：

```powershell
python scripts/simulate_framebuffer_reset.py --vivado '<Vivado vivado.bat>'
python scripts/run_framebuffer.py build --vivado '<Vivado vivado.bat>'
python scripts/run_framebuffer.py build-app --gcc '<ARM GCC exe>'
python scripts/run_framebuffer.py program --vivado '<Vivado vivado.bat>' --xsdb '<Vitis xsdb.bat>'
python scripts/run_framebuffer.py run --xsdb '<Vitis xsdb.bat>'
python scripts/run_framebuffer.py sample --xsdb '<Vitis xsdb.bat>'
```

构建要求当前像素/AXI 仿真与生成复位 IP 的集成仿真均通过。每次硬件操作保存独立输入快照，固定到已核验的构建哈希。一次成功 PL 配置只允许一次 PS 初始化/启动；重启应用前必须重新配置 PL。再次配置前先停止两个 CPU 核，关闭显示，再停止并复位 VDMA，确认 DMA 不再访问 DDR。失败或取消保留门闩与日志，不能凭历史 PASS 直接重试。

`cleanup` 只确认 CPU 停止，不自动证明 DMA 已停止。新会话中调试器的目标属性可能缓存旧状态，停机操作使用实时 `state` 核对；不能依据缓存的 Running 再次无条件 `stop`。

恢复纯 PL 静态/Wi-Fi 基线前也须先停止并复位当前显示 DMA，不能在 DMA 活动时直接调用旧静态下载命令。若板端状态未知，应先恢复并取得新证据，不以 CPU 单独停止替代 DMA 停止。

## 验证与已定位问题

核验日期：2026-10-11；范围限于本阶段列出的输入、构建与执行记录。

- [像素与 AXI 仿真](../../output/framebuffer/simulation.json)：两幅完整图像共 1,228,800 像素；另检查欠载、错误行末、下一帧恢复、AXI 独立 AW/W 和响应等待、关闭显示时不再启动额外 DMA 帧。
- [复位集成仿真](../../output/framebuffer/reset_simulation.json)：使用实际生成的 `proc_sys_reset`，先复现低电平辅助输入持续复位，再检查正确空闲电平、AXI ID/尺寸读取、外部复位重入与失锁恢复。[测试源码](../../sim/tb_framebuffer_reset.v)、[执行入口](../../scripts/simulate_framebuffer_reset.py)。
- [当前构建](../../output/framebuffer/build.json)、[应用构建](../../output/framebuffer/build-app.json)、[配置](../../output/framebuffer/program.json)、[启动](../../output/framebuffer/run.json)分别记录实际状态；以各记录的当前输入哈希与状态为准。

首个显示启动在应用下载/运行前读取 `0x43C10000` 超时，随后调试访问出现 DAP/APB 错误。原因已定位为新增设计把 `C_AUX_RESET_HIGH=0` 的辅助复位接到常量 0，使 DMA 和显示控制一直复位；修复将其接到常量 1，并在设计生成后断言极性及接线。此前单独模块仿真未覆盖生成复位路径，集成回归已补充。初始失败保留在 [独立尝试](../../output/framebuffer/attempts/20261011T055048Z_5ccd7bbb/result.json)。

本次针对这幅已知错误位流执行了限定恢复，见 [一次性记录](../../output/framebuffer/reset_repair_consumption.json)：修复 PL 配置成功仍保留 CPU unknown，独立停机检查未通过，未继续初始化 DDR 或启动应用。用户随后完成开发板断电恢复；新的 CPU 停机与实时 FPGA `DONE=0` 探测通过，见 [冷复位接续证据](../../output/framebuffer/cold_reset_reentry.json)。这是本次故障处理，不是通用绕过门闩的方法。

修复后的 [第一次板上启动](../../output/framebuffer/attempts/20261011T061423Z_a8274d31/result.json)和[第二次启动](../../output/framebuffer/attempts/20261011T061617Z_d94a45d3/result.json)均通过。第二次之前的 [配置记录](../../output/framebuffer/attempts/20261011T061441Z_eba464e3/result.json)与[实际 quiesce 日志](../../output/framebuffer/logs/20261011T061441Z_eba464e3/quiesce.log)确认先停核、关闭显示、停止与复位 VDMA，再重新下载。完整帧、翻页和连续进度均通过，错误计数为 0；实现 setup/hold 余量分别为 11.481/0.021 ns，资源为 1827 LUT、2500 寄存器、7.5 个 BRAM36 等效容量。权威数字仍以构建报告和各独立执行记录为准。

日常冷启动识别已加入 [probe_framebuffer](../../scripts/probe_framebuffer.tcl)：每次 program/run/sample 在任何 PL MMIO 前读取真实 DONE/EOS/CRC/USR_ACCESS；DONE=0 时只单独停两核后配置，不读取不存在的 PL 外设；匹配的已配置帧缓冲才进入 quiesce，未知配置拒绝。`current_pl.json` 只保留历史配置身份。一次性修复与故障冷复位脚本已经消费，不作为日常入口；新增冷路径的当前测试范围以检查记录为准，不能声称本轮再次完成实体断电试验。

## 实屏检查与下一阶段

测试画面应为上半屏八条彩条、下半屏棋盘格、白边框，以及约半秒更新一次的橙色方块。屏幕启动采用 [已验证恢复顺序](../context/RUNBOOK.md#hdmi-屏幕启动与异常恢复)：开发板与 ESP 保持供电，屏幕供电与 HDMI 同时断开约 10 秒，再先接 HDMI、后接屏幕供电。真实 RGB 顺序、清晰度和无明显撕裂由用户确认。

2026-10-11 用户确认“清楚稳定，图案和移动方块正常”，[实屏验收](../../output/framebuffer/user_acceptance.json)绑定当时图形应用的独立配置与运行，P2b-2 通过。用户后来恢复实施；P2b-3 中文应用、操作及证据由 [DYNAMIC_UI](DYNAMIC_UI.md) 与 NOW 维护。共享目录中的最新 `build-app/program/run` 会随所选应用更新，不能把新应用记录解释为旧图形验收。真实触摸、音频与 ASR/MT 仍按 RUNBOOK 单独验证。

VDMA 寄存器和提交顺序依据 AMD [Programming Sequence](https://docs.amd.com/r/en-US/pg020_axi_vdma/Programming-Sequence) 与 [Park Pointer Register](https://docs.amd.com/r/en-US/pg020_axi_vdma/PARK_PTR_REG-Park-Pointer-Register-Offset-28h)。
