# 音频实时回环与时延显示

核验日期：2026-10-16（新增）；同日补充核对：WM8731 型号、Z7-NANO_R21.pdf 全 15 页与 AN831.pdf 模块原理图、JP2 逐针映射、Bank35 引脚名与 MRCC、JP2 电源针、模块时钟与 I2C 配置要求。范围：接线事实、接口结论、时延测量定义与分阶段计划。本轮为文档轮次：没有接线、构建、烧录、采样、仪器测量或实屏验收，未执行项均已标注。

## 1. 需求与范围（用户已确认）

用户 2026-10-16 要求：麦克风接音频模块输入端，耳机接模块输出端，耳机实时听到麦克风的声音（本地实时回环）；并把延迟时间实时显示在 HDMI 屏幕上；**屏幕已有画面及功能不动**。

- 本文记录上述需求为已确认需求；实现、测量和验收状态见 [NOW](../context/NOW.md)，决策见 [D15](../context/DECISIONS.md#d15-音频实时回环与时延显示)。
- 本专题只覆盖本地音频路径（麦克风 → 模块 → PL → 模块 → 耳机）与延迟测量/显示；不涉及 Wi-Fi、云端语音、字幕模型和云端凭据。
- 回环与云端链路若同时运行，会共用 DDR/VDMA 带宽、显示刷新和 PL 资源；资源关系与优先级在实现前单独核对，不假定互不影响。

## 2. 接口事实

来源：用户 2026-10-16 提供的接线表（J3 → JP2 → FPGA 引脚 → 方向）与模块型号（WM8731），并按两份原理图核对：开发板 **Z7-NANO_R21.pdf**（SHA-256 `f977123c…0ce45`，与 [source.json](../../hardware/z7_nano/source.json) 记录一致）与模块 **AN831.pdf**（SHA-256 `60de5187…0235`）。结论见 [audio_module.json](../../hardware/z7_nano/audio_module.json)。

| J3 脚 | 模块信号 | JP2 脚 | 板级网络 | FPGA 球号 | Bank35 引脚名 | 方向 |
|---|---|---|---|---|---|---|
| 3 | VW_I2C_SCLK | 15 | GPIO2_6P | B19 | IO_L2P_T0_AD8P_35 | FPGA → 模块 |
| 4 | VM_I2C_SDAT | 16 | GPIO2_6N | A20 | IO_L2N_T0_AD8N_35 | 双向 |
| 5 | VM_DACDAT | 17 | GPIO2_7P | C20 | IO_L1P_T0_AD0P_35 | FPGA → 模块 |
| 6 | VM_BCLK | 13 | GPIO2_5P | H16 | IO_L13P_T2_**MRCC**_35 | 模块 → FPGA（配置为主模式后） |
| 7 | VM_ADCDAT | 18 | GPIO2_7N | B20 | IO_L1N_T0_AD0N_35 | 模块 → FPGA |
| 8 | VM_DADLRC | 14 | GPIO2_5N | H17 | IO_L13N_T2_**MRCC**_35 | 模块 → FPGA（配置为主模式后） |
| 9 | VM_ADCLRC | 19 | GPIO2_8P | D19 | IO_L4P_T0_35 | 模块 → FPGA |
| 10 | NC | — | — | — | — | 悬空 |

JP2 电源针（同页核对）：第 11/12 针 = `VCC_5V`，第 29/30 针 = `VCC_3V3`。JP2 是 CON40A（2x20）排针，位于 **Bank35**，网络前缀 `GPIO2_`；编号规则为奇数 P、偶数 N（遇电源位列跳号）。模块侧 J3 同为 2x20，**J3 的 3–9 针已与 JP2 的 13–19 针一一对上**；用户 2026-10-16 确认实物方向已核对。

接线要点（实施时必须遵守）：

- **两边虽然都是 2x20，但不能用整排排线直连**：J3 与 JP2 的针号并不一一对应（例：J3 3 → JP2 15、J3 6 → JP2 13、J3 7 → JP2 18），必须按上表逐根跳线；直连会把 I2C 时钟接到错误的球号上。
- **电源与地（2026-10-17 更正）**：模块 J3 的 1 针 = **GND**、2 针 = **VCC5V**、37/38 针 = **GND**、39/40 针 = **D3V3**（AN831.pdf 原理图与黑金模块实验文档第 5 页一致；此前把 1/2 都记为 VCC5V 是错误的，见 [audio_module.json](../../hardware/z7_nano/audio_module.json)）。开发板 JP2 的 11 针 = `VCC_5V`、29 针 = `VCC_3V3`、**12/30 针 = GND**（Z7-NANO_R21.pdf 第 13 页复核）。接法：J3-2 → JP2-11（或只接 J3-39/40 → JP2-29）；J3-1/37/38 → JP2-12 或 30。**模块 GND 必须与开发板共地**，否则 codec 只能经信号线寄生取电：I2C 仍可能应答，但 12.288 MHz 晶振与 BCLK 不会启动。

接口结论：

- **模块（WM8731）是 I2S 时钟主，但这一点取决于寄存器配置**：R7 的 MS 位置 1 后由模块输出 BCLK/ADCLRC/DACLRC，PL 只能作从机；WM8731 复位默认是从模式，因此**必须先用 I2C 配置**，否则 PL 收不到任何时钟（见 §5）。
- 采集与播放各有独立 LRCLK（ADCLRC / DACLRC），RTL 必须分别处理，不能只接一个 LRCLK 当通用声道时钟。
- 同时存在独立 ADC 与 DAC 数据线，因此可以只做数字域回环（ADCDAT → DACDAT），耳机通路的音量与模拟质量由模块自身负责。
- 控制口是 I2C（FPGA 输出 SCLK、SDAT 双向）；模块未引出 MCLK/CLKOUT 线。
- H16/H17 标注的 MRCC 与原理图一致（`IO_L13P/N_T2_MRCC_35`），可作为输入时钟脚使用。

开发板侧补充核对（Z7-NANO_R21.pdf 全 15 页：PS BANK500&501/502、PL BANK0&13、PL BANK34&35、ZYNQ Power、DDR3、PS ETHERNET×2、PS USB、PS UART SD、HDMI TX、GPIO、USB-JTAG、Power）：

- **开发板没有音频 codec，也没有音频时钟**（全页无音频电路；`SPK_R` 是 USB3320 的 ULPI 引脚名）→ 音频通路 100% 经 JP2 外接模块。
- Bank34 与 Bank35 都由 `VCC_3V3` 供电 → PL 侧是 **3.3 V 逻辑**，与模块 DBVDD/DCVDD 一致。
- 板载 24C32 EEPROM 的 I2C（`EEPROM_I2C_SCL/SDA`）位于 Bank34 网络块，与 `HDMI1_*`、`GPIO1_*` 同块 → **不占用 JP2 的音频脚**，不需要与模块 I2C 共用。

模块侧补充核对（AN831.pdf，模板标题栏日期 2015-01-28）：

- `U11 = WM8731SEFL`（WM8731-QFN28）、`Y3 = 12.288 MHz 晶振`（XT49M）、`U8 = LT1117-3.3`、`J10/J11/J12 = PJ-325`（3.5 mm 麦克风/线路输入与耳机输出）、`J3 = CONN PCB 20x2-S`。
- **模块自带 12.288 MHz 晶振 → 不需要 FPGA 提供 MCLK**；12.288 MHz ÷ 256 = 48 kHz（按 R8 配置）。
- J3 电源：1/2 针 = `VCC5V`（模块用 LT1117-3.3 生成 3.3 V），39/40 针 = `D3V3`；GND 以接地符号绘制、无文字标注，针位需目视/实物确认。
- 芯片引脚与网络对应：`BCLK`(U11 pin7)=VM_BCLK、`DACDAT`(8)=VM_DACDAT、`DACLRC`(9)=VM_DADLRC、`ADCDAT`(10)=VM_ADCDAT、`ADCLRC`(11)=VM_ADCLRC、`SCLK`(28)=VW_I2C_SCLK、`SDIN`(27)=VM_I2C_SDAT。

## 3. 引脚占用核对（已核实：与当前板显示不冲突）

已核实的证据链：

1. [source.json](../../hardware/z7_nano/source.json) 的 `Pins` 段是本项目对新板引脚的唯一板级记录（`scripts/prepare_z7_nano.py` 由它生成构建用 `board.tcl`/xdc）：当前板 TMDS = `U18/U19`（时钟对）、`V20/W20`、`T20/U20`、`N20/P20`，全部在 **Bank34**。
2. Z7-NANO_R21.pdf 第 4/11 页（PL BANK34&35、HDMI TX）与上述球号一致；音频 7 个信号在第 4 页归入 BANK35、在第 12 页归入 **JP2**。
3. 因此音频的 7 个球号与当前板 HDMI（Bank34）**没有任何重叠**，无需改脚；板载 EEPROM I2C 也在 Bank34，同样不占用。

需要纠正的历史判断：仓库里的 [hdmi.xdc](../../constraints/hdmi.xdc) 标注为 **ZYNQ_MINI_REVB（旧板）**，它把 H16/D19/C20/B19 用作 TMDS；该文件不是当前板的显示约束（当前板约束由 `output/z7_nano/config/hdmi.xdc` 生成，不在仓库内）。2026-10-16 之前据它提出的"音频引脚与已验收 HDMI 重叠"不成立，已在 [RISKS](../context/RISKS.md) R17 更正。

## 4. 时延测量定义

"延迟"必须按测量点分层，三层不能混用同一个数字（目前三层都未测得）：

| 层级 | 测量点 | 计划方法 | 用途 |
|---|---|---|---|
| L1 数字回环 | ADC 样本进入 PL，到同一系数样本出现在 DACDAT | PL 内样本计数/时间戳（BCLK 或系统时钟） | 回环最小延迟基线与逐次回归量 |
| L2 系统路径 | 采集入口到播放出口，含 FIFO/DDR/PS/重采样 | 两端单调计数器打点 | 评估加入软件处理后的代价 |
| L3 声学端到端 | 麦克风膜片到耳机声压 | 外部仪器或双通道录音测脉冲/包络偏移 | 用户实际可感知的延迟 |

显示规则：

- 屏幕上必须能看出所显示的是哪一层、哪个测量点；只有 L1/L2 时可显示"数字域"标记，不得让人误读为声学延迟。
- 未测得时显示"未测量"；不得用估算值、理论值或历史值冒充实测（沿用 [AGENTS](../../AGENTS.md#分阶段执行与验证) 对"仿真/构建/测量/验收"分别记录的要求）。
- 读数刷新不得阻塞视频扫描或改变现有已验收的布局、控件和交互；新增读数会改变像素，因此不能用旧的全帧零差异结论覆盖本轮实屏验收。界面约定（位置已定）见 [UI_SPEC 时延读数](UI_SPEC.md#时延读数)。

## 5. 方案建议（未确认、未实施）

- **第一步是 I2C 初始化 WM8731（必需，不是可选）**：模块自带晶振，但 BCLK/LRCLK 只在 R7 的 MS 位置 1 后输出，且 R9 的 ACTIVE 位置 1 才使能数字音频接口。缺少这一步的表现是"完全没有时钟、也没有声音"，容易被误判成接线错误。需要写的寄存器：R7（主模式 + I2S + 位宽）、R8（采样率/256fs）、R6（各功能块上电）、R4/R5（输入选择与 DAC 通路）、R2/R3（耳机音量、取消静音）、R9（使能）；I2C 从地址 0x1A 或 0x1B 取决于模块 CSB 接法，实测确认。实现方式建议 PL 软 I2C（复用 H16 附近的 B19/A20 两根线）或 PS 侧 I2C，二选一后再定。
- **最小延迟优先**：先在 PL 内做 ADCDAT → DACDAT 直通（同一 BCLK 域内的少量寄存器，或跨域异步 FIFO），不经 PS/DDR。这样 L1 与 L3 的差主要来自 codec 内部与模拟前端。
- **需要软件处理时**再引入 PS 路径：PL 采集 → AXI DMA → DDR → PS 播放，接口沿用 [INTERFACES](INTERFACES.md#2-pl-到-ps-的音频接口) 的 `s16le` 单声道约定；此路径的延迟按 L2 单独测量。
- **时钟与约束**：PL 是从机，BCLK/ADCLRC/DACLRC 都是**输入**，需对 BCLK 显式 `create_clock` 并配套 `set_input_delay`；H16/H17 已是 MRCC 脚，时钟资源可用。IOSTANDARD 必须与 Bank35 的 3.3 V 一致；RTL 侧按 [hdmi_clock.v](../../rtl/hdmi_clock.v)、[async_rst_sync.v](../../rtl/async_rst_sync.v) 的既有路线处理时钟与异步复位。
- 以上均为建议；未记录为用户已确认的技术决定，也未产生代码或约束。

## 6. 分阶段计划（本轮均未执行）

| 阶段 | 内容 | 前置 | 验收 | 状态 |
|---|---|---|---|---|
| A0 资料与引脚核对 | 板卡与模块原理图、逐针映射、时钟、供电、电平、引脚占用 | 用户提供两份原理图 | 可引用的接线结论落到 `hardware/z7_nano/audio_module.json` | **已完成**（2026-10-16；I2C 地址与 GND 针位待实测） |
| A1 约束核对 | BCLK 作输入时钟、MRCC、IOSTANDARD、驱动；不动现有显示约束 | A0 | 约束报告、DRC、时序无未约束路径 | 已实现（`hardware/z7_nano/source.json` 的 `AudioPins` → `output/z7_nano/config/audio.xdc`：`create_clock` 325.52 ns、输入/输出延迟、I2C false path、BCLK 与 MMCM 异步组）；**约束实际通过需构建验证** |
| A2 I2C 初始化 | PL 软 I2C 主机写 WM8731 寄存器；再用 ILA/示波器确认 BCLK 出现 | A1 | BCLK/LRCLK 实际存在，寄存器回读正确 | 已实现并仿真通过（探测 0x1A/回退 0x1B、11 条寄存器写入与顺序有断言）；板级未验证 |
| A3 PL 采集 | I2S 从机接收：位序、LRCLK 相位、位宽/槽宽；样本计数与溢出统计 | A2 | 仿真逐位对照；实采与已知输入比对 | 已实现并仿真通过（位精确，零溢出）；板级未验证 |
| A4 PL 回环 | ADCDAT → FIFO → DACDAT 直通 | A3 | ILA 观测，无掉样、无持续削波；耳机可听回环 | 已实现并仿真通过（位精确、零欠载、恒定 3 帧延迟）；耳机听感未验证 |
| A5 时延计数 | L1 时间戳与寄存器暴露 | A4 | 延迟可重复，数值与采样率一致 | 已实现并仿真通过（192 BCLK 周期）；板上实测未做 |
| A6 屏显 | 在既有画面上叠加只读读数，不改变既有布局/按钮 | A5 | 框外全帧比对 + 用户实屏确认 | 已实现并仿真通过（框外零改动、552 绘制像素）；实屏未验收 |
| A7 联调与验收 | 持续运行、耳机听感、L3 声学测量 | A6 | 用户确认可听回环与延迟读数可信；≥30 分钟无未解释溢出 | 未执行 |

操作步骤与前置条件的权威入口是 [RUNBOOK](../context/RUNBOOK.md#a-音频实时回环与时延显示后续)；本文不复制命令。

## 7. 已实现（2026-10-17）

本轮把 A1–A5 的 PL 实现写入代码并在本机仿真通过；构建位流、下载、接线、真实出声与实屏验收**均未执行**。

| 文件 | 职责 |
|---|---|
| [i2c_master.v](../../rtl/i2c_master.v) | 字节级 I2C 主机：100 kHz、开漏、START/STOP、逐字节 ACK 采样与 NACK 报错 |
| [wm8731_init.v](../../rtl/wm8731_init.v) | WM8731 初始化：先探测 0x1A、失败回退 0x1B；按 RESET→R8→R7（主模式）→R6→R4/R5→R0..R3→R9（ACTIVE）写入；九路 9 位寄存器值可由 AXI 影子寄存器改写后重发 |
| [i2s_loopback.v](../../rtl/i2s_loopback.v) | I2S 从机收发（posedge 采 ADCDAT，negedge 出 DACDAT，即 I2S 的一 bit 延迟）+ 2 帧 FIFO + 预填充门控 + 溢出/欠载计数 + L1 时间戳；BCLK 域与 50 MHz 域用 req/ack 握手快照跨域 |
| [audio_latency.v](../../rtl/audio_latency.v) | L1 的 BCLK 周期→微秒换算（Q8 定点乘加 + 饱和）与 min/max 记录 |
| [audio_regs.v](../../rtl/audio_regs.v) | AXI4-Lite 寄存器文件（基址 `0x43c20000`）：状态、L1、计数、影子寄存器与触发位 |
| [audio_overlay.v](../../rtl/audio_overlay.v) | 在既有画面右上角叠加 `L1 nnnn us`（5×7 字体 2× 放大），只改框内像素 |
| [z7_nano_audio_top.v](../../rtl/z7_nano_audio_top.v) | 顶层：沿用已验收的显示 BD，插入叠加与音频链路；I2C 走 IOBUF 开漏 |
| [build_z7_nano_audio.tcl](../../scripts/build_z7_nano_audio.tcl) | BD（PS7+VDMA+视频+音频第三 AXI 口）+ 综合实现位流，含引脚/时钟/DRC/时序检查 |
| [audio_synth_ooc.tcl](../../scripts/audio_synth_ooc.tcl) | 不依赖 BD 的逐模块 OOC 综合检查 |
| [run_audio_loopback.py](../../scripts/run_audio_loopback.py) | simulate/build 运行器与证据落盘 |

寄存器映射（AXI4-Lite，基址 `0x43c20000`）：

| 偏移 | 名称 | 说明 |
|---|---|---|
| 0x00 | ID | `0x41554431`（"AUD1"） |
| 0x04 | STATUS | [7:0] live / codec_found / addr_sel / init_busy / init_done / init_error / max_valid；[31:24] 已写寄存器数 |
| 0x08 | LATENCY_US | L1 微秒（未测到显示 0） |
| 0x0c | LATENCY_CYCLES | L1 原始 BCLK 周期数 |
| 0x10 / 0x14 | ADC_FRAMES / DAC_FRAMES | 采集/播出立体声帧计数 |
| 0x18 / 0x1c | OVERRUNS / UNDERRUNS | 溢出与欠载计数 |
| 0x20 | BCLK_CYCLES | BCLK 自由计数（可用于判断模块时钟是否存在） |
| 0x24 / 0x28 | MIN_US / MAX_US | 最小/最大 L1（`0xFFFFFFFF` 表示尚未测到） |
| 0x2c | CTRL | bit0 重发初始化、bit1 清 min/max、bit2 冻结屏显；上电默认 bit0=1 |
| 0x30..0x4c | R0,R1,R2,R3,R4,R5,R7,R8 | 9 位寄存器影子值（默认 R7=0x042 主模式、R4=0x010、R2/R3=0x079、R0/R1=0x017、R5/R8=0x000） |
| 0x50 / 0x54 | OCCUPANCY / BCLK_HZ | FIFO 占用帧数与名义 BCLK 频率（3072000） |

L1 定义（已实现）：每个立体声帧在**采集起点**取 BCLK 周期时间戳，播出时相减，得到"采集→播出"的真实延迟；它不是 FIFO 占用量的近似。仿真测得回环延迟 3 帧、L1 = 192 个 BCLK 周期，在 3.072 MHz 下为 62.5 µs。

仿真证据（[simulation.json](../../output/z7_nano/audio_loopback/simulation.json)，2026-10-17，本机 xsim）：

| 用例 | 覆盖 | 结果 |
|---|---|---|
| tb_i2c_wm8731 | I2C 位序与 ACK、0x1A 探测、0x1B 回退、无应答报错、11 条寄存器写入的内容与顺序（含 R7=0x042、R9=0x001 最后） | 通过 |
| tb_i2s_loopback | I2S 从机位对齐、位精确回环、恒定 3 帧延迟、占用=2、零溢出/欠载、L1=192 周期 | 通过 |
| tb_audio_latency | 周期→微秒换算（20/41/62/996/21247 µs）、live 门控、min/max 与清零 | 通过 |
| tb_audio_regs | ID、影子寄存器读写、CTRL 触发脉冲、状态字、只读寄存器、非法地址错误响应 | 通过 |
| tb_audio_overlay | 框外像素零改动、绘制像素仅在框内、数值变化改变图案、未测量时画横线 | 通过（"L1 0321 us" 552 像素） |

已执行的综合检查：逐模块 OOC 综合（本机 Vivado 2018.3，[audio_synth_ooc.tcl](../../scripts/audio_synth_ooc.tcl)）——六个模块 i2c_master / wm8731_init / audio_latency / i2s_loopback / audio_regs / audio_overlay 全部 0 error，报告在 `output/z7_nano/audio_ooc/reports/`，证据 [synthesis.json](../../output/z7_nano/audio_loopback/synthesis.json)。该检查不含 BD，不能替代完整构建。

仍未执行：含 BD 的完整综合/实现/位流（`build_z7_nano_audio.tcl` 按项目 2026.1 的 BD/IP 版本编写，见 [RUNBOOK](../context/RUNBOOK.md#a-音频实时回环与时延显示后续)）、下载到板、接线、真实 I2C 探测与出声、实屏验收与 L3 声学测量。

## 8. 证据与放行（板级尚未产生）

- 已有证据：`output/z7_nano/audio_loopback/{simulation.json,synthesis.json}`（仿真与 OOC 综合）与 `output/z7_nano/audio_ooc/`（综合日志与利用率报告）；板级证据尚未产生。
- 仿真通过、约束通过、构建通过、下载通过、I2C 配置成功、BCLK 实测、实际回环、听感验收和延迟数值分别记录，不能互相替代（[AGENTS](../../AGENTS.md#分阶段执行与验证)）。
- 只有取得 L1/L2 实测值后，才允许在屏幕或文档中出现具体延迟数字；L3 需要用户以听觉或仪器确认。

## 9. 待核实清单（板级）

- 模块 I2C 从地址（0x1A / 0x1B）与 MODE/CSB 的实际接法：代码会先探测 0x1A、失败回退 0x1B，实际结果要按板上 I2C ACK 记录。
- 是否需要写 R4/R5 之外的通路寄存器才能让麦克风出声；默认值（R4=0x010 线路输入）可能不适用于麦克风输入，必要时用 CTRL 影子寄存器改写后重发。
- 模块供电电流与 5 V 需求、GND 针位（原理图中以接地符号绘制）。
- 实际采样率与位宽（12.288 MHz 与 R8 档位）；若格式变化，audio.xdc 的 BCLK 周期与 `audio_latency` 的 BCLK_HZ 必须同步修改。
- 麦克风偏置/增益与耳机驱动能力；最大音量下的失真与削波。
- 读数在屏上的最终像素位置（实现为右上角 `L1 nnnn us`）与冻结显示观感，见 [UI_SPEC](UI_SPEC.md#时延读数)。
- 位流构建（需项目许可的 Vivado）与 BD 端口命名：`build_z7_nano_audio.tcl` 已对 `S_AXI_AUDIO_*` 端口名做显式检查，首次构建即是该检查的验证。

## 10. 板级取证（2026-10-17）

本轮把 A2–A6 的 PL 实现按 **WM8731 作 I2S 时钟主**（与 AN831 厂商例程一致）定稿，用 Vivado 2018.3 完成构建并下载。构建与仿真可重复，但**耳机仍无音频**，逐项证据如下。

- **构建/下载已通过**：`scripts/run_audio_loopback.py build`（Vivado 2018.3）PASS，位流 `output/z7_nano/audio/z7_nano_audio_top.bit`，setup/hold = 7.289/0.044 ns；`scripts/z7_nano_audio.tcl run` 完成 PL 配置、PS7 初始化、字幕应用启动与音频寄存器读取。显示侧一次通过（0 欠载/0 格式错误）。
- **I2C 正常且写入有应答**：`codec_found=1`、`addr_sel=0`（0x1A）、`regs_written=11`、`init_error=0`；总线空闲时 SDA/SCL 均为高（未卡死），因此 11 条写应答是真实 ACK，不是总线卡低造成的假应答。
- **寄存器值已修正**：R4=0x015（MICBOOST + INSEL=麦克风 + DACSEL，DAC 接通输出，麦克风进 ADC）；R7=0x042（MS=1 主模式 + I2S + 16 bit）；R8=0x000（Normal、256fs、48 kHz）。这些位定义按数据手册 Table 29/30 逐位核对，之前的 0x009/0x00E/0x001 会让 DAC 不接通、进入 USB 8 kHz 模式。
- **codec 未产生任何 I2S 时钟**：读取时 BCLK/ADCLRC/DADLRC/ADCDAT 的引脚跳变计数均为 0，BCLK 自由计数不增长，`live=0`。即 PL 侧没有收到模块的 BCLK/LRCLK，模块也未在 ADCDAT 上输出 ADC 数据。
- **WM8731 是只写器件**：数据手册第 46 页明确 “The WM8731/L is a write only device”，因此本轮新增的 I2C 回读（R4/R7/R8/R9）只能作为未来版本/其他 codec 的调试通道（`i2c_master` 的总线恢复 FSM 保留但默认不触发：实测总线空闲为高电平，未卡死）；对 WM8731 无法用回读证明写入内容，`rb_valid=0` 属预期。
- **已定位的接线问题**：模块 J3 的 1 针是 GND、2 针是 5V（此前文档误记为 1/2 都是 VCC5V），开发板 JP2 的 12/30 针是 GND（此前误记为 11/12=VCC_5V、29/30=VCC_3V3）。按旧文档接线会出现模块 GND 未共地或电源极性接错；此时 codec 可经信号线寄生取电而仍应答 I2C，但晶振/BCLK 不会启动，与本轮“I2C 正常、无任何时钟”的现象一致。
- **供电实测（2026-10-17 用户万用表）**：开发板 JP2-11 的 5 V 在屏幕带电时约 **4.3 V**、拔掉屏幕约 4.6 V；用户测 J3-39/40 的 D3V3 为 3.3 V。注意 AN831 原理图中 **D3V3 与 codec 的 VCC3V3 是两个网络**：codec 的 3.3 V 只由板上 U8(LT1117-3.3) 从 J3-2 的 5 V 产生，所以 J3-39/40 的 3.3 V 不证明 codec 已上电。LT1117-3.3 的压差约 1.0–1.1 V，4.3 V 输入对 3.3 V 输出只剩约 1.0 V 余量，处在压差边缘（轻载可勉强稳压，晶振启动的电流尖峰与 USB 5 V 纹波会使其掉出稳压区）。**必须实测模块 VCC3V3**（U8 的 OUT/TAP 或 C39 正端对模块 GND），而不是测 J3-39/40。
- **已解决（2026-10-17 晚）**：用户改用**充电宝主供电 + 排针 5 V 直供模块 J3-2** 后回环打通。实测：`live=1`、`bclk_alive=1`、BCLK 引脚计数 48696307、ADCLRC 763781、DADLRC 766854、ADCDAT 3928603、`occ=2`、`overruns=0`、`underruns=0`、`L1=192 BCLK = 62 µs`（min/max = 0/62），显示 `format_errors=0`、整帧 614400、`display_ok=1`；证据 [program.json](../../output/z7_nano/audio_loopback/program.json)。结论：**5 V 供电余量不足是根因**——开发板 USB 5 V 被屏幕拖到 4.3 V，低于 LT1117-3.3 的正常压差需求，codec 能应答 I2C 但晶振不启动；换成稳定 5 V 后晶振、BCLK、ADC/DAC、回环和屏显全部正常，之前的显示 `format_errors=1` 也不再出现。
- **实屏读数修正（2026-10-17）**：首次实屏发现屏幕有画面但看不到时延读数。用 JTAG 读回 DDR 帧缓冲（`FB_BASE0=0x01000000`, 1024x600, stride 4096）确认 overlay 框（x=896..1015, y=8..23）下方像素为白 `0xFFFFFF`/浅色 `0xE0F0EC`，而叠加文字是白色 `0xFFFFFF`——白字白底不可见。已将 `audio_overlay.v` 的 `TEXT_RGB` 默认改为深色墨字 `0x172A38`（与界面 INK 同色），并在 [UI_SPEC](UI_SPEC.md#时延读数) 记录该修正；浅色主题下深色字可读。
- **下一步（硬件）**：① 实测模块 VCC3V3：若 <3.2 V 或波动，换更稳的 5 V 输入（外接 5 V 电源、共地），或把开发板 3.3 V（JP2-29）直接接到模块 VCC3V3（C39 正端 / U8 OUT），绕过 LT1117；② 若 VCC3V3 是干净 3.3 V，则重点怀疑模块 12.288 MHz 晶振/模块硬件；③ 修好后重新上电再跑一次 `scripts/z7_nano_audio.tcl run`：预期 `AUDIO_PINS` 四路计数快速增大、`bclk_alive=1`、`live=1`、`occ=2`、`L1≈192 cycles ≈62 µs`。在此之前不能把“无音频”归因于软件。

## 相关入口

- 当前状态、阻塞与下一步：[NOW](../context/NOW.md)
- 已确认决策：[DECISIONS](../context/DECISIONS.md#d15-音频实时回环与时延显示)
- 硬件、接头与球号映射：[MAP](../context/MAP.md#音频模块连接)
- 风险与验证缺口：[RISKS](../context/RISKS.md)
- 操作与验收步骤：[RUNBOOK](../context/RUNBOOK.md#a-音频实时回环与时延显示后续)
- 系统方案中的音频接入草案：[SYSTEM_PLAN](SYSTEM_PLAN.md#5-音频接入与前处理设计)
- 接口契约（PL→PS 音频）：[INTERFACES](INTERFACES.md#2-pl-到-ps-的音频接口)
- 外部资料与适用范围：[SOURCES](SOURCES.md)
