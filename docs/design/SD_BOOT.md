# SD 卡独立启动

上电后不接电脑，板子从 TF 卡自行加载 FPGA、两颗 CPU 的程序、板上 Qwen 资产和运行期配置，联网后可直接使用。JTAG 流程保持不变：固件只有在 SLCR `BOOT_MODE` 为 SD 时才执行本页描述的动作。实测与边界见[验证](../validation/sd-boot-20261026/README.md)，决定见 [D25](../context/DECISIONS.md#d25-sd-卡独立启动2026-10-26)，安全取舍见 [R34](../context/RISKS.md)。

## 使用

1. TF 卡（FAT32，SDHC/SDXC）根目录放 `BOOT.BIN`、`qwen.tqw`、`qwen.tok` 与 `z7cfg.bin`；前三个由 `scripts/write_sd_card.py` 写入并回读核对。
2. J1 启动跳线（4 针，丝印 JTAG/QSPI/SD）的跳线帽放到最右侧 SD；回到 JTAG 开发时放回最左侧 JTAG。
3. 音频模块、屏幕 HDMI/TOUCH、ESP8266 接好后上电。`BOOT.BIN` 约 5 MB，不再含 Qwen；界面出来约 5 秒后触摸可用，联网随后完成；之后固件在后台从卡上读取并校验板上 Qwen，见下文“板上模型后台加载”。

2026-10-27 实测（当前版本）：上电到应用启动 2.0 秒（FSBL 0.32 秒、跳板 0.54 秒，其余是应用启动代码），再约 0.8 秒出画面；模型约 20 秒读完，会话进行中应用 60 秒就绪。同日早些的版本分别为 5.5 秒与 128 秒，旧版约 412 MB、含 Qwen 的 `BOOT.BIN` 为 28.8 秒且之后还要复核约 3 分钟。见[验证](../validation/sd-model-20261027/README.md)。

切跳线需要拔下音频模块时，务必在上电前插回并接好 5 V；否则 WM8731 不应答或晶振不起振，识别没有输入。

## 启动链路

| 阶段 | 做什么 | 来源 |
| --- | --- | --- |
| BootROM | 读 TF 卡 `BOOT.BIN`，运行 FSBL | 芯片固化 |
| FSBL | `ps7_init`（与 JTAG 相同，另开 SD0 MIO40–45、UART0 MIO14–15）、配置 PL 位流、把分区装入 DDR；交接前由钩子把 `z7cfg.bin` 复制到 OCM `0x2F100` | [平台](../../scripts/build_sd_boot_platform.tcl)、[FSBL](../../scripts/build_sd_boot_fsbl.py)、[钩子](../../software/sd_boot/fsbl_hooks.c) |
| 跳板 | 打开指令缓存（数据访问仍不经缓存），按字校验装载包 CRC，按 xsdb `dow` 方式放置 CPU0/CPU1 各段（有数据的复制，其余清零），在 OCM `0x2F000` 写启动信息（第 2 版含跳板起止计时），关回缓存后跳到 CPU0 `0x10000` | [stub.c](../../software/sd_boot/stub.c)、[握手定义](../../software/sd_boot/sd_boot.h) |
| CPU0 应用 | 先给屏幕 USB 供电（复刻 JTAG 加载器的 `usb_port_power`），显示就绪且通电满 5 秒后唤醒 CPU1；网络空闲时应用配置；之后从卡上后台读取 Qwen | [sd_boot_runtime.c](../../software/network_ui/sd_boot_runtime.c)、[sd_model.c](../../software/network_ui/sd_model.c) |
| CPU1 | 原触摸主机程序，与 JTAG 流程相同 | `software/z7_nano_usb` |

FSBL 自身占用 OCM 0x0–0x1C8D0，与 CPU0 启动代码（0x10000）和 CPU1 程序（0x1000）重叠，所以两者先作为装载包进 DDR，再由跳板放置。

| DDR / OCM | 内容 |
| --- | --- |
| `0x04000000` | Qwen Q6 权重（401,637,888 字节），固件启动稳定后从卡上 `qwen.tqw` 读入，边读边做 SHA256 |
| `0x1C000000` | 分词器（5,277,796 字节），从 `qwen.tok` 读入，同上；最后一个不满 512 字节的扇区多写的 412 字节落在推理区 `0x1C800000` 之前的空隙 |
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

## 板上模型后台加载（2026-10-27）

决定见 [D26](../context/DECISIONS.md#d26-sd-启动后台加载板上模型2026-10-27)。以前 FSBL 把 Qwen 当作 `BOOT.BIN` 分区读进 DDR，屏幕要等它读完才亮。现在 Qwen 是卡上的两个普通文件，由 CPU0 在屏幕、触摸和运行期配置都完成后（`sd_boot_settled`）自己读：

- 文件：FAT32 根目录的 8.3 短名 `QWEN.TOK`、`QWEN.TQW`，大小必须等于固件固定的分词器/权重长度。支持 MBR 分区或整卡格式化；文件可以不连续，按簇链读取。只支持 SDHC/SDXC（块地址），≤2 GB 的 SDSC 卡不支持。
- 读取：沿用 FSBL 已选中、4 位高速的卡，不重新初始化。每次 CMD18 最多读 4 MiB 连续簇，ADMA2 直接写进固定窗口；完成后按地址作废 L1 行（L2 已关）。先读分词器，再读权重。
- 校验：已读入的部分在服务循环里按 2 KiB 分片做 SHA256，会话运行时每次约 400 µs，空闲时约 600 µs（开机后云端会话自动开始，所以实际按前者；纯哈希实测约 15 MB/s）；两份都与固件固定摘要一致才就绪，然后才允许引擎初始化。加载期间锁住电脑 UDP 上传入口；失败时解锁，仍可从电脑上传。
- 界面：**设置 → 模型与本地声纹** 在引擎按钮右侧显示“等待启动完成 / 加载中 xx% + 进度条 / 已就绪 / 文件缺失或损坏 / 读取失败 / 校验失败 / 初始化失败”。引擎第一次可用时，任意页面底部弹出约 4 秒的“本地模型已就绪”通知。本地模式下主页面在就绪前显示“本地模型正在准备”。
- 诊断：`sd_model_diag[10]` = 魔数 `SDM1`、状态、出错时的 SD 中断状态、开始/就绪毫秒、已读字节、已校验字节、读命令数、校验耗时 µs、校验分片数。
- SD 启动时不再运行 JTAG 热重载用的 DDR 复核（`remote_llm_restore_tick`）；JTAG 流程不变。

## 术语表（2026-10-27）

决定见 [D27](../context/DECISIONS.md#d27-功能整理调试开关面对面对话术语表删除旧功能2026-10-27)。卡根目录可选放一个 `glossary.txt`，固件读模型文件时一并读入，作为百炼的热词（`translation.corpus.phrases`）随每次翻译会话发送，让人名、产品名、专业词按指定译法翻译。

- 格式：UTF-8 纯文本（记事本默认即可，可带 BOM），每行 `中文 = English`，在第一个 `=` 处分开，两边空白忽略；空行和 `#` 开头的行跳过。缺 `=`、某一边为空、编码不对或含控制字符的行跳过。
- 方向：中译英时以中文为键、英文为值；英译中时反过来。
- 容量：文件不超过 4 KB，超过整份不用。会话里热词部分最多约 1200 字节，按行序装入，装不下的行跳过；中文词条一般能放 30–40 条。设置页“术语表 N 条”显示实际生效的条数，没有文件时显示“术语表：未加载”。
- 写入：示例见 `assets/glossary.example.txt`。可直接在资源管理器里复制到卡根目录，或用 `scripts/write_sd_card.py <卡根目录> --boot-only --glossary <文件>` 写入并回读核对（超过 4 KB 会拒绝）。
- 只在 SD 启动且带 `--tinyqwen` 的构建里读取；改了文件要重新上电。纯本地模式的板上 Qwen 不使用术语表。
- 宿主测试：`scripts/test_glossary.py` 检查解析、转义、两个方向和容量上限，生成的会话消息逐条按 JSON 解析。

## 构建

全部用 Vivado/Vitis 2026.1，命令见 [RUNBOOK](../context/RUNBOOK.md#sd-卡独立启动)。应用需带 `--sd-boot`（JTAG 下行为不变，可同一 ELF 两用）；带 `--tinyqwen` 时同时编入卡上模型加载器，`build_sd_boot.py` 拒绝打包不含加载器的旧应用。`BOOT.BIN` 不含凭据与 Qwen；`scripts/write_sd_card.py` 写 `BOOT.BIN`、`qwen.tok`、`qwen.tqw` 并逐个回读核对 SHA256（只换程序时加 `--boot-only`）。`z7cfg.bin` 由 `scripts/make_sd_config.py` 在用户输入后直接写到卡上，不在电脑留副本。
