# 译声：Zynq 英中语音字幕终端

目标是连接 Wi-Fi 后独立进行语音识别、英中翻译、HDMI 显示和 USB 触摸操作。现阶段为 Z7-Nano 7010 的 JTAG 易失原型，仍需要电脑完成下载、运行期凭据与开发调试，尚未实现独立上电启动。

真实链路为 AN831/WM8731 麦克风 → PL I2S/16 kHz PCM → Cortex-A9 Opus/Ogg → ARM 可信 TLS → ESP8266 TCP/Wi-Fi → 百炼 → 双语字幕。CPU0 绘制 DDR 双缓冲，CPU1 执行 USB Host；采用裸机 C，未迁移 Linux/LVGL。硬件及接线见 [MAP](docs/context/MAP.md)，接口分工见 [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md)。

## 已有能力与限制

- 顶部菜单支持 English → Chinese / Chinese → English，实际麦克风、触摸、三档字幕字号和清晰画面已有用户短验收。
- 产品使用 `--text-only-response`，保留 PL 本地回环；L1/T1 使用与 UI 一致的抗锯齿卡片。本地耳机听感仍需单独验收。
- 仅字幕固定样本通过 30 分钟观察；真实麦克风仍存在**定稿队列阻塞**与 `UNEXPECTED_ASR_ERROR`。两句能显示不等于历史已正常，不宣布长期可靠。[历史版真实麦克风复测](docs/validation/history-mic-20261021/README.md)已确认两句预览正常但未入历史，暂停/继续后新增真实记录；旧版复现分别保留。
- 用户本轮要求暂缓定稿排障，先实现**独立历史记录页：左英文、右中文、滑动和页内清空**。历史页已通过离线回归、ARM 构建、示例/空态/正式主界面整帧比对，以及真实 USB 滑动、翻页、返回验收；页内确认清空已通过板端命令检查，正式麦克风版已恢复连云；行为见 [UI_SPEC](docs/design/UI_SPEC.md#板端历史记录页2026-10-21)；实际进度、运行身份和下一步唯一由 [NOW](docs/context/NOW.md)维护。
- 下一阶段由队友开发独立设置页及真实参数控制，优先核实输出音量；当前设置按钮只有外观。交接入口见[设置页规范](docs/design/UI_SPEC.md#板端设置页下一阶段2026-10-21)，本次只提交计划和测试证据。
- 断电会丢失应用和运行期配置；持久历史、启动镜像、可信上电时间/熵、长期稳定性及代表性语音质量未放行。未写 Flash/SD 或 ESP 固件。

## 使用与开发入口

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
