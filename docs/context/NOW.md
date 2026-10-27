# 当前状态

## 当前：SD 卡独立启动（2026-10-26 起，10-27 后台加载模型）

用户要求从实用性出发做“脱离电脑独立启动”，范围见 [D25](DECISIONS.md#d25-sd-卡独立启动2026-10-26)；10-27 改为屏幕先亮、本地模型后台加载，见 [D26](DECISIONS.md#d26-sd-启动后台加载板上模型2026-10-27)。设计见 [SD_BOOT](../design/SD_BOOT.md)。

10-27 又按用户要求做了一轮功能整理（[D27](DECISIONS.md#d27-功能整理调试开关面对面对话术语表删除旧功能2026-10-27)）：新增调试信息开关（默认隐藏延迟卡片）、面对面对话、TF 卡术语表；删除旧 12 短句离线识别和产品不编译的传输分支。用户上板确认三项新功能与识别正常，见[验证](../validation/features-20261027/README.md)。

- 板载：TF 卡 `BOOT.BIN` SHA256 `34c3eb9bca502aed326b338f2e7b743d6d53566708bb5c20f3b8f47702c745c6`（约 5 MB：FSBL、PL 位流 `97ddda29…`、跳板、CPU0/CPU1 装载包），`qwen.tqw`、`qwen.tok`（固件固定摘要），示例 `glossary.txt`（5 条），用户生成的 `z7cfg.bin`。CPU0 应用带 `--sd-boot --tinyqwen`，ELF SHA256 `b5a198f148a19cb0ca970f8fc1bd8ed52dbc5584ecd6294162721d9e80742b82`，JTAG 下行为不变。J1 跳线在 SD。板子另有供电线（推测连电脑的 USB），只拔主电源不会重启。
- 10-27 后台加载那一版的冷启动（JTAG 只读计时）：上电到应用启动 2.0 秒（同日首版 5.5 秒，旧版含 Qwen 的 `BOOT.BIN` 为 28.8 秒），之后约 0.8 秒出画面。模型在应用启动 5.5 秒后开始读卡，60 秒校验完成、引擎就绪，就绪通知约 4 秒后撤下；期间无视频欠载、麦克风无丢样本。首版时用户确认触摸、识别正常，本地模式出过译文。见[验证](../validation/sd-model-20261027/README.md)。
- 同次测到两次 ESP8266 发送 30 秒无应答（网络错误 27），PL UART 无溢出或帧错；用户怀疑接线松动，云端模式也出现过。本地模式遇网络错误后不会自行恢复，用户本轮选择不处理，见 [R35](RISKS.md)。
- 10-26 第 3 次冷启动与历史页成段版（各自的 `BOOT.BIN` 含 Qwen）的结果见 [SD 启动验证](../validation/sd-boot-20261026/README.md)、[成段验证](../validation/history-paragraph-20261026/README.md)。
- 仍是 RAM：设置、历史和声纹登记断电丢失。凭据明文在卡上；TLS 时间以配置文件生成时刻为基准。见 [R34](RISKS.md)。
- 回到 JTAG 开发：J1 跳线放回 JTAG，再按下方入口加载。

## 前轮：PR #7 合并与 2026.1 重建（2026-10-26）

队友 [PR #7](https://github.com/HHHHH-UX-bot/simultaneous-interpreter/pull/7) 已按用户指示合并为 `e38fbe6`，内容与 PR head `791da60` 相同；其中 15 个文件的 LF→CRLF 行尾改写原样保留。本机只装了 Vivado/Vitis 2026.1，按用户“如果冲突就用2026的”重建，范围见 [D24](DECISIONS.md#d24-合并-pr-7本机以-vivadovitis-20261-重建2026-10-26)。

- 当时板载（JTAG 易失，已由上节 SD 启动替换）：PL 为 `output/z7_nano/remote_display/`，Vivado 2026.1，USR_ACCESS `0x5a374e35`，位流 SHA256 `97ddda29282ba4189b3a1cfaa04aff3ba850e1a8f01899d27b52a6312a31fd80`，setup/hold 0.040/0.017 ns。运行中的应用 ELF SHA256 `78b7c93eb9863c7cab927bf8796aa5ba16601facdfb64be5a90f6050b6136cc9`（ARM GCC 13.4.0，Selftest=false），CPU1 触摸主机在运行。
- 构建目录 `output/network_history/microphone/` 已在仓库卫生改动后重建，11 个装载段与运行中的 ELF 逐字节相同，只有调试信息不同；原部署 ELF 留在本机 `output/merge_pr7_20261026/deployed_app/`。
- 自动检查：完整加载与显示报告，屏幕 USB 供电且检测到触摸设备连接，声纹加速器模型校验和 MFCC/CNN 周期，Qwen 资产的板上 SHA 复核与引擎初始化，主界面 614,400 像素与本机渲染零差异；五项主机回归和 48→16 kHz 抽取仿真通过。
- 用户现场短验收（凭据由用户在终端输入）：板屏显示 CYEA 并就绪；云端中译英、英译中均有字幕和译音；历史页、设置页及“模型与声纹”页触摸正常；云端 ASR → 板上 Qwen → 云端 TTS 有译文和语音；FPGA 声纹登记说话人 1 后能识别。均为单次短测。
- 未验证：电脑画面、历史导出、网页文字翻译和网口 UDP 上传（网线/电脑 IP 未配置，用户选择跳过），以及逐档听感、声纹拒识、板上 Qwen 耗时和长期稳定性。
- 这次 Qwen 资产经 JTAG 写入 DDR 再热重启 CPU0（约 23 分钟）；整板断电后需要重新加载，网口链路可用时应走 UDP 上传（约 1 分钟）。
- 结果与边界见[本轮验证](../validation/merge-pr7-2026-20261026/README.md)。

## 功能现状

| 功能 | 现状 | 权威专题 |
| --- | --- | --- |
| 云端实时同传 | 麦克风 → PL I2S → Opus/Ogg → ARM TLS → ESP8266 → 百炼 LiveTranslate → 双语字幕与 DAC 译音；中英双向菜单；L1/T1/T2 延迟卡片由设置页“调试信息”打开（默认隐藏） | [CLOUD_SPEECH](../design/CLOUD_SPEECH.md)、[长句验证](../validation/long-speech-20261022/README.md) |
| 设置页 | 播报开关、数字音量/静音、0.8/1.0/1.2x 语速、字号、显示原文、翻译方向；偏好只存 RAM | [TTS_SETTINGS](../design/TTS_SETTINGS.md) |
| 历史记录 | 最多 64 条定稿，连续定稿自动合并成段，左英文右中文，可滑动和页内清空；断电丢失 | [UI_SPEC](../design/UI_SPEC.md#板端历史记录页2026-10-21) |
| 板上 Qwen 翻译 | 云端 ASR → 板上 Qwen2.5-0.5B Q6 → 云端 TTS；队友记录单句模型完成约 22–24 秒，不是实时同传 | [LOCAL_VOICE_LLM](../design/LOCAL_VOICE_LLM.md)、[BOARD_LLM](../design/BOARD_LLM.md) |
| FPGA 声纹 | PL 内 MFCC + CNN，四个 RAM 登记位，实验性识别，不能用于身份认证；登记时暂停翻译并关闭上传 | [LOCAL_SPEAKER_CNN](../design/LOCAL_SPEAKER_CNN.md) |
| 面对面对话 | 语言菜单第三项；上下分屏，上半屏旋转 180° 给对面的人，两人轮流点说话按钮切方向 | [UI_SPEC](../design/UI_SPEC.md#调试信息开关与面对面对话2026-10-27) |
| 术语表 | TF 卡 `glossary.txt` 作为百炼热词随翻译会话发送，约 30–40 条中文词条；设置页显示生效条数 | [SD_BOOT](../design/SD_BOOT.md#术语表2026-10-27) |
| 电脑画面与导出 | PL 网口直连，电脑看板端实际画面并用鼠标操作；历史导出 CSV/TXT/JSON，并附完整原文/译文；显示当前 Wi-Fi 名称 | [REMOTE_DISPLAY](../design/REMOTE_DISPLAY.md) |

## 未解决与边界

- 真人麦克风的定稿队列阻塞（R28）和云端 `UNEXPECTED_ASR_ERROR`（R29）只做过局部修正，不宣称长期可靠；PR #1 审查遗留项见 R32，2026.1 与 2018.3 构建分叉见 R33。全部见 [RISKS](RISKS.md)。
- 可从 TF 卡独立启动（程序、Qwen 资产与凭据都在卡上）；设置、历史和声纹登记仍在 RAM，断电丢失。未写 QSPI Flash 或 ESP 固件。
- 10-21 至 10-23 各轮的详细过程、当时的板载身份和测量数据已归档到[历史记录](history/progress-20261021-20261023.md)。

## 使用入口

- 独立使用：TF 卡放 `BOOT.BIN`、`qwen.tqw`、`qwen.tok` 与 `z7cfg.bin`，J1 跳到 SD，接好音频模块后上电；本地模型约 1 分钟后可用，进度在 **设置 → 模型与本地声纹**。改动后提交前运行 `python -B scripts/run_host_tests.py`。生成与更新卡内容见 [RUNBOOK](RUNBOOK.md#sd-卡独立启动)。
- 根目录 `启动FPGA声纹.cmd`：重新 JTAG 加载当前声纹/网络名称固件并输入运行期 Wi-Fi 和百炼配置；整板断电后用 `断电后启动本地LLM.cmd`。这些入口默认调用 SDK 2018.3 的 xsct/hw_server；只有 2026.1 的机器需改用 `scripts/load_tts.py --xsct <Vitis 2026.1 xsdb>`，再用 `scripts/configure_cloud_runtime.py --xsdb <同一 xsdb>` 输入凭据，并先启动 2026.1 的 hw_server。
- 根目录 `打开电脑画面.cmd`：启动本机服务并打开 `http://127.0.0.1:8787`，点击/拖动操作同步到板端；下方导出按钮可选 CSV/TXT/JSON 和保存位置。该文件写死了队友电脑的 Python 路径，本机需改用 `machine` 环境运行 `scripts/start_remote_display.py`。
- 构建：`scripts/build_cloud_app.py` 现在由 `hardware/z7_nano/source.json` 直接生成显示头文件，干净克隆不再需要手工拷贝 `output/display/framebuffer_hw.h`；声纹 PL 平台与仿真参数见 [LOCAL_SPEAKER_CNN](../design/LOCAL_SPEAKER_CNN.md)，2026.1 下的做法见[本轮验证](../validation/merge-pr7-2026-20261026/README.md)。
- 新旧构建与符号不能混用；调试或换固件前先查实际板态和加载身份。源码、日志、工程备份不含密钥、无线密码或真人原始音频。

入口规则见[AGENTS](../../AGENTS.md)，工具/目录见[MAP](MAP.md)、[RUNBOOK](RUNBOOK.md)，代码路由见[PROJECT_INDEX](../../PROJECT_INDEX.md)。
