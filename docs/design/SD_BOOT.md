# SD 卡独立启动

上电后不接电脑，板子从 TF 卡自行加载 FPGA、两颗 CPU 的程序、板上 Qwen 资产和运行期配置，联网后可直接使用。JTAG 流程保持不变：固件只有在 SLCR `BOOT_MODE` 为 SD 时才执行本页描述的动作。实测与边界见[验证](../validation/sd-boot-20261026/README.md)，决定见 [D25](../context/DECISIONS.md#d25-sd-卡独立启动2026-10-26)，安全取舍见 [R34](../context/RISKS.md)。

## 使用

1. TF 卡（FAT32）根目录放 `BOOT.BIN` 与 `z7cfg.bin`。
2. J1 启动跳线（4 针，丝印 JTAG/QSPI/SD）的跳线帽放到最右侧 SD；回到 JTAG 开发时放回最左侧 JTAG。
3. 音频模块、屏幕 HDMI/TOUCH、ESP8266 接好后上电。FSBL 读约 412 MB 的 `BOOT.BIN` 需要 30–60 秒，期间屏幕黑；界面出来约 5 秒后触摸可用，联网随后完成；板上 Qwen 再用约 3 分钟复核权重。

切跳线需要拔下音频模块时，务必在上电前插回并接好 5 V；否则 WM8731 不应答或晶振不起振，识别没有输入。

## 启动链路

| 阶段 | 做什么 | 来源 |
| --- | --- | --- |
| BootROM | 读 TF 卡 `BOOT.BIN`，运行 FSBL | 芯片固化 |
| FSBL | `ps7_init`（与 JTAG 相同，另开 SD0 MIO40–45、UART0 MIO14–15）、配置 PL 位流、把分区装入 DDR；交接前由钩子把 `z7cfg.bin` 复制到 OCM `0x2F100` | [平台](../../scripts/build_sd_boot_platform.tcl)、[FSBL](../../scripts/build_sd_boot_fsbl.py)、[钩子](../../software/sd_boot/fsbl_hooks.c) |
| 跳板 | 校验装载包 CRC，按 xsdb `dow` 方式放置 CPU0/CPU1 各段（有数据的复制，其余清零），在 OCM `0x2F000` 写启动信息，跳到 CPU0 `0x10000` | [stub.c](../../software/sd_boot/stub.c)、[握手定义](../../software/sd_boot/sd_boot.h) |
| CPU0 应用 | 先给屏幕 USB 供电（复刻 JTAG 加载器的 `usb_port_power`），显示就绪且通电满 5 秒后唤醒 CPU1；网络空闲时应用配置 | [sd_boot_runtime.c](../../software/network_ui/sd_boot_runtime.c) |
| CPU1 | 原触摸主机程序，与 JTAG 流程相同 | `software/z7_nano_usb` |

FSBL 自身占用 OCM 0x0–0x1C8D0，与 CPU0 启动代码（0x10000）和 CPU1 程序（0x1000）重叠，所以两者先作为装载包进 DDR，再由跳板放置。

| DDR / OCM | 内容 |
| --- | --- |
| `0x04000000` | Qwen Q6 权重（401,637,888 字节），固件开机后自行全量 SHA256 复核 |
| `0x1C000000` | 分词器（5,277,796 字节），同上 |
| `0x1F800000` | 跳板程序 |
| `0x1F900000` | 装载包（CPU0 11 段 + CPU1 2 段，约 2.7 MB，另清零约 10.7 MB） |
| OCM `0x2F000` / `0x2F100` | 启动信息 / 配置文件副本（固件应用后清零） |

## JTAG 流程里由电脑做、SD 启动由固件补上的事

| 电脑原先做的 | SD 启动时 |
| --- | --- |
| 加载器 `usb_port_power`：PHY 复位、控制器复位、主机模式、端口供电 | CPU0 在显示启动前执行同一序列。缺这一步时 CPU1 的 ULPI 首次写超时（错误 6）；屏幕若在 HDMI 之后才通电并立刻被枚举，触摸屏不发报告 |
| `run_cloud_touch.py start` 下载并启动 CPU1 | 跳板放置 CPU1 程序；CPU0 在通电满 5 秒、显示就绪后写 `0xFFFFFFF0` 并 `sev` |
| `configure_cloud_runtime.py` 写网络/云配置、时间与 64 字节随机数 | FSBL 钩子读 `z7cfg.bin`；固件校验 CRC 后写入同样的邮箱。随机数 = SHA-256(文件种子 ‖ 2048 个实时麦克风采样与计时器值)；时间基准为文件生成时刻 |

TLS 只在 SD 启动时忽略证书的“尚未生效”标志（板子时钟落后于真实时间）；证书链、域名和过期检查不变。

## 构建

全部用 Vivado/Vitis 2026.1，命令见 [RUNBOOK](../context/RUNBOOK.md#sd-卡独立启动)。应用需带 `--sd-boot`（JTAG 下行为不变，可同一 ELF 两用）。`BOOT.BIN` 不含凭据；`z7cfg.bin` 由 `scripts/make_sd_config.py` 在用户输入后直接写到卡上，不在电脑留副本。
