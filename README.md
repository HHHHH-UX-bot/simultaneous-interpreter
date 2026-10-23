# 译声：Zynq 双向语音传译终端

本副本已部署 **FPGA MFCC + CNN 本地声纹**，同时保留真实 Qwen2.5-0.5B-Instruct 板上翻译。当前入口为 `启动FPGA声纹.cmd`；整板断电后用 `断电后启动本地LLM.cmd`。电脑实际板屏为 `http://127.0.0.1:8787`，文字输入为 `http://127.0.0.1:8790`。使用、实板证据和模型限制见 [本地声纹](docs/design/LOCAL_SPEAKER_CNN.md)，当前加载身份见 [NOW](docs/context/NOW.md)。

在 **设置 → 模型与本地声纹** 登记说话人 1–4，连续说话约 4–6 秒，进度完成后识别编号或显示未知。MFCC 和 CNN 前向计算在 FPGA，模板匹配在板上 ARM，声纹不调用云 API；登记期间关闭翻译云连接，完成后恢复。普通翻译的 STT/TTS 仍使用云端，本地模式的翻译使用板上 Qwen。当前使用真实麦克风固件 `output/network_history/microphone/`，Selftest=false，测试注入默认关闭。旧短句/SpeechBrain 对照仅保留历史，不是本副本当前运行能力；旧 `启动离线短句.cmd`、`启动译声.cmd` 等入口不适用本声纹平台。

目标是连接 Wi-Fi 后独立进行语音识别、英中翻译、HDMI 显示和 USB 触摸操作。现阶段为 Z7-Nano 7010 的 JTAG 易失原型，仍需要电脑完成下载、运行期凭据与开发调试，尚未实现独立上电启动。

真实链路为 AN831/WM8731 麦克风 → PL I2S/16 kHz PCM → Cortex-A9 Opus/Ogg → ARM 可信 TLS → ESP8266 TCP/Wi-Fi → 百炼 → 双语字幕与译文合成语音 → WM8731 DAC。CPU0 绘制 DDR 双缓冲，CPU1 执行 USB Host；采用裸机 C，未迁移 Linux/LVGL。硬件及接线见 [MAP](docs/context/MAP.md)，接口分工见 [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md)。

## 已有能力与限制

- 顶部菜单支持 English → Chinese / Chinese → English，实际麦克风、触摸、三档字幕字号和清晰画面已有用户短验收。
- 当前桌面工程请求目标文本和合成语音，DAC 输出译音；支持中英双向切换，并以原有卡片风格显示 L1/T1/T2 和播报状态。用户已确认听到译音、音量正常；完整声学时延仍需单独测量。
- 仅字幕固定样本通过 30 分钟观察；真实麦克风仍存在**定稿队列阻塞**与 `UNEXPECTED_ASR_ERROR`。两句能显示不等于历史已正常，不宣布长期可靠。[历史版真实麦克风复测](docs/validation/history-mic-20261021/README.md)已确认两句预览正常但未入历史，暂停/继续后新增真实记录；旧版复现分别保留。
- 原阶段已完成**独立历史记录页：左英文、右中文、滑动和页内清空**，取得其离线、整帧比对及真实USB滑动/翻页/返回证据。新译音阶段局部修正迟到预览、空定稿和response.done最终输出处理；[本轮验证](docs/validation/tts-settings-20261021/README.md)列明实际范围，长期可靠性仍单独验证。行为见 [UI_SPEC](docs/design/UI_SPEC.md#板端历史记录页2026-10-21)，运行身份由 [NOW](docs/context/NOW.md)维护。
- 独立设置页已提供播报、数字音量/静音、播放速度、字号、原文与方向控制；见[译音与设置专题](docs/design/TTS_SETTINGS.md)。桌面工程可使用 `启动译声.cmd` 重新加载，运行期凭据仍需重新输入。
- 断电会丢失应用和运行期配置；持久历史、启动镜像、可信上电时间/熵、长期稳定性及代表性语音质量未放行。未写 Flash/SD 或 ESP 固件。

连续长句已做6句、每句25–29字的两轮真实云链路自动测试，并修复IPD空行累积故障；当前采用2Mbaud和Opus O2，完整数字及保留限制见[长句验证](docs/validation/long-speech-20261022/README.md)。

## 使用与开发入口

PL网口直连电脑后，运行根目录 `打开电脑画面.cmd`，或打开本机 `http://127.0.0.1:8787`（服务已启动时）。电脑显示板端真实画面，可点击设置、调整参数、拖动历史；下方“导出历史 · 另存为…”支持CSV/TXT/JSON并选择保存位置。最多导出板上当前保留的64条定稿。三种格式均在末尾追加完整原文和完整译文，并按会话、方向和段落长度整理。板屏各页及两个电脑网页均显示实际 Wi-Fi 名称，当前连接时显示“当前网络名称：CYEA”。网络、构建与验证见[电脑画面与导出](docs/design/REMOTE_DISPLAY.md)。

先读 [AGENTS](AGENTS.md)，再按 [PROJECT_INDEX](PROJECT_INDEX.md)查对应章节。板卡构建与受控启动看 [RUNBOOK](docs/context/RUNBOOK.md#当前产品构建与运行范围2026-10-21)，不要直接照旧 RevB 命令操作新板；实际加载身份必须与位流、PS 初始化和 ELF 一致。凭据仅通过已有私有运行期输入，不提交源码或日志。

| 对象 | 入口与范围 |
| --- | --- |
| 当前产品与规划 | [NOW](docs/context/NOW.md)、[SYSTEM_PLAN](docs/design/SYSTEM_PLAN.md) |
| UI 与真实 USB 触摸 | [UI_SPEC](docs/design/UI_SPEC.md)、[USB_TOUCH](docs/design/USB_TOUCH.md) |
| 新板配置、HDMI、音频与供电 | [BOARD_MIGRATION](docs/design/BOARD_MIGRATION.md)、[AUDIO_LOOPBACK](docs/design/AUDIO_LOOPBACK.md)、[MAP](docs/context/MAP.md) |
| 当前英语/双向云译 | [ENGLISH](docs/context/ENGLISH.md)、[CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md)、[AUDIO_COMPRESSION](docs/design/AUDIO_COMPRESSION.md) |
| 独立中文 ASR / 中译英译音候选 | [ZHZH](docs/context/ZHZH.md)、[ZHEN](docs/context/ZHEN.md)，保留各自构建与证据范围 |
| 早期 HTML 原型 | 本机生成的 [output/ui/index.html](output/ui/index.html)，内容是模拟，不能当作板端实现 |
| 旧板静态/动态显示与局域网文本 | [STATIC_DEMO](docs/design/STATIC_DEMO.md)、[DYNAMIC_UI](docs/design/DYNAMIC_UI.md)、[NETWORK_UI](docs/design/NETWORK_UI.md)，仅属原阶段 |
| PS/DDR 与显示基础 | [PS_BRINGUP](docs/design/PS_BRINGUP.md)、[FRAMEBUFFER](docs/design/FRAMEBUFFER.md)，当前板独立配置由迁移专题路由 |
| 电脑模型/文件接口与 DeepSeek 对照 | [MODEL_BACKEND](docs/design/MODEL_BACKEND.md)，不在当前产品运行主链路 |
| 原始决策、风险与历史 | [DECISIONS](docs/context/DECISIONS.md)、[RISKS](docs/context/RISKS.md)、[history](docs/context/history/README.md) |

`output/`、`tmp/`、厂商资料、二进制、模型缓存和安装字体不随 Git 上传。文档中的这些路径是本机生成材料入口，克隆后应按对应方法重建；可共享的阶段摘要与公开证据在 `docs/validation/`。同级 HDMI/LED 是独立工程，本轮不修改。Git 流程见 [RUNBOOK](docs/context/RUNBOOK.md#git-维护)。
