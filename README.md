# 译声：语音中文字幕终端

目标是音频模块经 Zynq 采集与必要前处理，通过 ESP8266/Wi-Fi 访问云端语音翻译服务，把中文字幕显示在 HDMI 屏幕上，并支持 USB 触摸交互。最终使用时不依赖电脑；当前选择阿里云百炼实时语音翻译一体方案，见 [DECISIONS](docs/context/DECISIONS.md#d13-云端语音翻译与独立终端)和[云端方案](docs/design/CLOUD_SPEECH.md)。

当前包含详细方案、离线交互原型、旧 RevB 基线，以及电脑端模型试验和 HTTP 文件接口。2026-10-13 起移植至 Z7-Nano 7010，新板已完成 PS/DDR、动态中文、七类 USB 触摸控件及常驻运行、ESP8266 自主关联与已知字幕网络验收；方法见 [迁移专题](docs/design/BOARD_MIGRATION.md)，当前证据见 [NOW](docs/context/NOW.md)。921600 串口及 ESP 可信 TLS/WSS 已分项验证，PS 已完成一次预编码一分钟音频收发并取得真实云译。[ARM 云事件到字幕路径](docs/design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕)已完成实现、离线验证、交叉编译及真实短文件上屏验收；[ARM 自主 TLS](docs/design/CLOUD_SPEECH.md#c1-7a-arm-自主可信-tls)已实现并通过本机检查、修正版显示/触摸验收，TLS 板测待重跑；完整云客户端、真实采音及独立启动仍待完成。

## 查看本轮成果

1. 浏览器直接打开 [界面原型](output/ui/index.html)，不需要安装依赖或启动服务器。
2. 点击“开始翻译”，体验原文逐步出现、中文字幕定稿、会话记录与暂停。
3. 试用字号、原文开关、设置、会话记录；页面下方的演示面板可模拟缺少音频、后端断开、输入过载、静音和长句。
4. 阅读 [系统详细方案](docs/design/SYSTEM_PLAN.md)、[UI 规范](docs/design/UI_SPEC.md) 和 [分阶段验收](docs/context/RUNBOOK.md)。
5. 板卡静态版已完成构建、JTAG 临时配置和用户实屏验收，文字与布局清楚；画面见 [量化预览](output/static_ui/ui_quantized.png)。构建与重新下载见 [RUNBOOK P2a](docs/context/RUNBOOK.md#p2a-纯-pl-静态-ui-演示)，技术结构见 [STATIC_DEMO](docs/design/STATIC_DEMO.md)。
6. ESP8266 的实现、寄存器及凭据处理见 [WIFI_LINK](docs/design/WIFI_LINK.md)，操作顺序见 [RUNBOOK W](docs/context/RUNBOOK.md#w-esp8266-串口与联网)。AT、Wi-Fi 和局域网双向数据已验证；当前阶段结果及显示稳定性以 NOW 为准。
7. PS/DDR 最小检查与双缓冲显示已完成真实运行；用户确认彩条、棋盘格和移动方块清楚稳定。来源与入口见 [PS_BRINGUP](docs/design/PS_BRINGUP.md)、[FRAMEBUFFER](docs/design/FRAMEBUFFER.md)。图形测试源码和独立执行快照保留为基线。
8. 新板支持预设中文文案、分页、三档字号、定稿历史、JTAG 测试控制及真实 USB 按钮；显示见 [DYNAMIC_UI](docs/design/DYNAMIC_UI.md)，触摸与常驻入口见 [USB_TOUCH](docs/design/USB_TOUCH.md)。部分界面文字仍是原演示占位；JTAG 加载断电后需重新下载。
9. 旧 RevB 网络应用能自主关联 ESP8266，并接收电脑通过 TCP 发送的有界模拟字幕和控制；运行入口与边界见 [NETWORK_UI](docs/design/NETWORK_UI.md)。该旧板验收不替代新板网络验证；电脑须处于可访问模块的局域网。
10. 电脑端已用合成英语音频完成真实识别与中文翻译；模型、文件证据、HTTP 接口和运行方法见 [MODEL_BACKEND](docs/design/MODEL_BACKEND.md)。现有板端演示尚未消费模型译文，单文件结果不代表麦克风或整机效果。
11. PS 自主调度的加密音频文件收发见 [C1-4](docs/design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)，该历史版本的 WSS 控制和返回解析在电脑。新增 [C1-6](docs/design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕)把有界事件消费与真实译文更新移入 ARM；2026-10-16 已通过同一程序的短文件真实云译、像素比对及用户实屏验收，电脑仍负责建连和预装音频。验证与后续入口见 [云接入操作](docs/context/RUNBOOK.md#c-云端语音接入)。

界面按目标屏幕的逻辑像素设计，窄窗口会等比例缩放。浏览器鼠标/键盘和板端真实 USB 触摸分别取证，当前板级能力由 NOW 路由。

## 接续开发

先读取 [AGENTS](AGENTS.md)，根据 [PROJECT_INDEX](PROJECT_INDEX.md) 定位需要的内容。接手任务从 NOW 确认范围、当前证据和下一步；不要把本轮方案中的操作步骤当成执行授权。

当前移植先复用现有 PS 裸机 UI 与 PL 视频链路，并独立验证新板 USB Host 和触摸 HID。Linux + LVGL 是早期方案中的可选建议，未安装或构建；是否需要更换软件栈以实际 USB/网络验证为依据。

已有 [HDMI 项目](../hdmi/README.md) 的 PL 显示链路已经验收。本项目保留它作为独立基线；DDR、中文渲染和 USB 触摸分别取证，真实音频及模型译文上屏仍须实现并验证。

用户已授权按阶段继续实施。当前优先执行 [换板迁移](docs/design/BOARD_MIGRATION.md)；旧板 P2b/W4 和电脑 P5 的证据保留其原范围。现有 JTAG 原型与短包联网不能当作独立产品启动或连续语音传输能力。

DeepSeek 独立英译中已取得一条真实结果，保留为文本翻译对照，见 [模型专题](docs/design/MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译)。电脑模型服务是开发试验入口，最终云端链路由 [CLOUD_SPEECH](docs/design/CLOUD_SPEECH.md)维护。源码仓库及本地产物边界见 [Git 映射](docs/context/MAP.md#git-与-github)；文档中的 output/tmp 和同级工程链接是本机材料入口，运行缓存不随源码仓库上传。
