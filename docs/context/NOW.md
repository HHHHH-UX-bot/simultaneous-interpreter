# 当前状态

## 当前目标与范围

长期目标是形成语音进入板卡、Wi-Fi 直接调用云端语音翻译、中文字幕上屏、触摸交互的独立产品，使用时不依赖电脑。2026-10-14 用户选择阿里百炼一体方案，见 [D13](DECISIONS.md#d13-云端语音翻译与独立终端)和[CLOUD_SPEECH](../design/CLOUD_SPEECH.md)；旧系统方案保留历史范围。

2026-10-13 用户更换为 Z7-Nano 7010，并要求移植现有工程，见 [D9](DECISIONS.md#d9-换为-z7-nano-7010-并继续产品迁移)。当前优先完成 [板级迁移](../design/BOARD_MIGRATION.md)：资料/目标身份 → 新板静态 HDMI → PS/DDR → 动态中文 UI → USB Host/触摸 → 网络与模型结果。旧 RevB 的 PS/DDR、显示、ESP 与 W4-1 验收仅属于旧板；电脑 P5 文件推理及接口证据可保留原范围。

此前 project-context-docs 建档交付已完成。用户随后明确要求按阶段计划继续；旧板 P2b/W 阶段已验收，新板独立重验。临时下载与 Git 上传分别依据用户实施指令和 D12，不依据文档本身；未改写 QSPI/SD 或 ESP 固件。

旧板 P2b-3 的模拟中文 UI、控制与长期运行，W4-0/W4-1 的自主 ESP 网络分别通过原范围。2026-10-12 电脑模型与文件接口通过原范围；真实音频和触摸尚未完成。历史入口见下方，不能用旧板记录表示新板已通过。

## 当前进展

新板 M0/M1 已通过：原理图/HDMI/LED 约束与正确 SDK 7010/16 位 DDR 来源已核对，JTAG 为单个 `xc7z010`、JTAG 启动模式。静态 UI 构建的引脚、生成时钟、时序及 DRC 通过；临时 JTAG 配置后，用户确认画面正常、清楚稳定。证据见 [新板构建](../../output/z7_nano/static/build.json)、[实现检查](../../output/z7_nano/static/implementation.json)、[下载与实屏验收](../../output/z7_nano/program.json)、[代码复核](../../output/board_migration/review_static.json)、[运行器检查](../../output/board_migration/runner_validation.json)。

M2 已实测通过：4 MiB 区域的多种数据模式与高地址抽样，共 5242948 次字检查，缓存/MMU 关闭；见 [DDR 实测](../../output/z7_nano/ddr/run.json)及[复核](../../output/board_migration/review_ddr.json)。这不是全容量内存资格测试。

M3 新板双缓冲平台与 ARM UI 已通过，见 [构建](../../output/z7_nano/display/build.json)、[实现检查](../../output/z7_nano/display/implementation.json)、[ARM 程序](../../output/z7_nano/display/app_build.json)、[配置](../../output/z7_nano/display/program.json)、[实际运行](../../output/z7_nano/display/run.json)、[复核](../../output/board_migration/review_display.json)。实际抓取 614400 个像素与电脑同状态重建零差异，见 [帧比对](../../output/z7_nano/display/capture.json)；抓屏后恢复运行，刷新继续且欠载/格式/程序错误均为零，见 [后续取样](../../output/z7_nano/display/sample.json)。用户确认“画面清楚正常”，见 [实屏验收](../../output/z7_nano/display/user_acceptance.json)。

M4-0 USB 控制器/PHY 已实测通过：`0424:0007`、scratch 写读 0x5A、Host 模式 3，端口检测到连接；CPU0 继续运行、CPU1 探测后停止。见 [实测](../../output/z7_nano/usb/probe_run.json)和 [USB_TOUCH](../design/USB_TOUCH.md)。PHY 身份与屏幕 USB 设备身份分别验证。

M4-1 裸机 Host 已实测枚举屏幕 `1a86:e5e3`，120 秒稳定接收 686 条 44 字节 HID 报告，见 [运行](../../output/z7_nano/usb/host_run.json)及[真实按下/松开](../../output/z7_nano/usb/touch_capture.json)。用户已断开会向电脑传触摸的 POWER USB，单 TOUCH USB 供电与显示正常；接线事实见迁移专题。USB DMA 的停机确认已接入枚举、PHY 重探测和显示重配置入口。

M4-2 已实测通过：[四角/中心](../../output/z7_nano/usb/touch_corners.json)均在对应区域，恰好 5 次按下和 5 次松开；保持接线的 240 秒内收到 384 条报告，无断开、无丢弃点击。该证据绑定校准版 CPU1，尚不代表按钮已通过。

M4-3 七类真实控件、队列核账及常驻 start/sample/stop/restart/sample 已通过；用户确认持续触摸与 UI 响应正常，见 [实屏控件验收](../../output/z7_nano/usb/touch_controls_acceptance.json)和[常驻生命周期](../../output/z7_nano/usb/live_lifecycle.json)。2026-10-14 用户确认板卡重新上电且接线不变，已恢复同一显示/触摸固件，整帧比对和持续取样通过；本次实屏确认“画面正常，按钮有正确响应”，见 [本次恢复](../../output/z7_nano/usb/latest_restore.json)。此前无拔插时的黑线/渐暗仍未定位，对照由 [USB_TOUCH](../design/USB_TOUCH.md#触摸启动后的暗屏重现待定位)维护。

M5-0 新板 UART/自主 AT 已通过，[封存的阶段证据](../../output/z7_nano/wifi/uart_stage.json)绑定 Z7N3 配置、AT/回显/固件响应、零串口错误、整帧及持续触摸。M5-1 的 [封存网络阶段](../../output/z7_nano/wifi/network_stage.json)绑定三句已知字幕、精确历史、三种异常拒绝、断线重连、整帧零差异和常驻触摸；用户确认“显示正确，清楚稳定”。该历史程序最后一句及三条定稿不是模型或麦克风输出，不代表当前高速程序的运行状态。方法由 [M5](../design/BOARD_MIGRATION.md#m5-新板-uart-与自主-at)维护。

M5-2/C0 高速串口阶段已按其范围通过，见 [封存阶段](../../output/z7_nano/uart_921600/stage.json)：完整断电后自动协商及屏幕断开时暖重载均通过；模块查询 930232、FPGA 分频 54，帧错误/溢出为零。当前程序的 614400 像素比对零差异，屏幕重新接回后常驻 USB 已恢复，generation 与 CPU0 消费者匹配。用户确认“画面正常，按钮响应正确”。当前 RAM 是高速网络原型，字幕仍为模拟内容，未提交无线关联配置或云端音频。此前一次 DAP AHB 事务错误经完整断电恢复，根因仍未定位，见 RISKS R16。实现由 [高速 UART](../design/BOARD_MIGRATION.md#m5-2-921600-串口)维护。

P5c-0 DeepSeek 独立文本已实测：17 项离线验证和独立复核通过，操作者隐藏输入密钥后，一条公开英语参考文本取得完整中文，耗时 6.297 秒；见 [MODEL_BACKEND](../design/MODEL_BACKEND.md#p5c-0-deepseek-独立文本翻译)。尚未接入 ASR/HTTP 或上屏，缺对应字形，人工译文质量与流式首字时延待验收。用户优先选择由 [D11](DECISIONS.md#d11-中文翻译优先使用-deepseek-api)维护。

用户授权维护 Git 和上传关联 GitHub 账号。首次 241 个源码/文档文件已提交并推送私有仓库，远程 main 与本地一致；后续上传核验由 [Git 结果](../../output/github/publish.json)维护，仓库映射见 [MAP](MAP.md#git-与-github)。

电脑 P5a/P5b 的文件推理与 HTTP 接口保持 2026-10-12 原验证范围，见 [MODEL_BACKEND](../design/MODEL_BACKEND.md)。旧 RevB 的分阶段验收、显示诊断及接续过程已归档到 [历史基线](history/revb_baseline_2026-10-12.md)；不是新板当前运行状态。

## 阻塞与待核实

动态中文当前为有界演示字库，不能显示任意真实译文；最终字体覆盖及再分发许可未核清。新板七类 USB 控件和常驻 Host 生命周期已通过上述范围；间歇暗屏、热插拔和整机长期运行尚未验收，设置按钮仅视觉占位。范围与数据边界见 [DYNAMIC_UI](../design/DYNAMIC_UI.md)。

尚需核实以下事项：

- 音频模块型号、协议、电平及“FPGA20引脚”的含义。最后一次音频到货确认是 2026-10-11 的“尚未到货”；新板音频连接未确认。第一版英语转中文已在 DECISIONS D8 确认。
- 新板 USB 枚举、真实输入、四角坐标与 UI 控制已通过各自范围；完整 VBUS 电压/电流测量尚未做。
- 触摸启动与暗屏的触发顺序、USB 供电实际电压/电流和屏幕内部电路仍待定位；最新诊断及当前接线由 USB_TOUCH/迁移专题维护。独立屏幕电源对照未执行，不先行认定电流不足。
- 当前使用裸机 USB Host/HID；独立上电启动介质与产品启动方式尚未决定。新板最小 PS/DDR 实测结果由 JTAG/OCM 获取。
- 真实 ASR/MT 的代表性质量、人工译文与完整时延。公开模型、隔离运行环境和受控文件推理已有证据，边界、噪声修复及后续缺口由 [MODEL_BACKEND](../design/MODEL_BACKEND.md) 维护。实际译文有 3 个字超出现有演示字体，需独立补字/字体更新与画面核验。

NAS 资料位置已核清，原件与缓存哈希一致，来源见 [MAP](MAP.md#原理图来源与可访问性)。没有把旧路径失效误判为 NAS 关机。

## 下一步

维护并上传本阶段源码/文档，然后推进 CLOUD_SPEECH C1：先查询所装 AT 固件的加密能力，再测持续传输与可信 TLS/WSS。电脑当前连接手机热点，后续测试网络选择待用户答复；不自行切换电脑网络。用户有阿里云账号，北京地域 Key/Workspace ID 仍待确认。原本地 ASR 接 DeepSeek 草稿已停止；字库、真实采样、间歇暗屏及独立上电启动继续各自验收。

## 证据入口

- 动态中文：[实现与方法](../design/DYNAMIC_UI.md)、[字库清单](../../output/subtitle_ui/assets_manifest.json)、[字库验证](../../output/subtitle_ui/assets_validation.json)、[模型测试](../../output/subtitle_ui/model_tests.json)、[演示计时](../../output/subtitle_ui/demo_tests.json)、[电脑渲染](../../output/subtitle_ui/host_validation.json)、[ARM 构建](../../output/subtitle_ui/build.json)、[实际捕获](../../output/subtitle_ui/latest_capture.json)、[整帧比对](../../output/subtitle_ui/pixel_comparison.json)。最新板端执行共用 framebuffer 的独立尝试；图形测试用户验收仍绑定原图形应用。
- 自主 AT：[PS_WIFI](../design/PS_WIFI.md)、[地址与 UART 仿真](../../output/ps_wifi/simulation.json)、[独立平台构建](../../output/ps_wifi/build.json)、[AT 主机异常检查](../../output/ps_wifi/probe_host_tests.json)、[ARM 构建](../../output/ps_wifi/build-app.json)。实际证据：[配置](../../output/ps_wifi/program.json)、[运行](../../output/ps_wifi/run.json)、[自主 AT](../../output/ps_wifi/at_probe.json)、[用户实屏](../../output/ps_wifi/user_acceptance.json)、[平台评审](../../output/ps_wifi/review/review.json)、[软件评审](../../output/ps_wifi/review/runtime_review.json)。配置、AT 与实屏分别通过；不是持续网络/音频验收。
- 自主网络字幕：[协议/诊断/方法](../design/NETWORK_UI.md)、[关联](../../output/network_ui/connection.json)、[完整试验](../../output/network_ui/pilot.json)、[用户实屏](../../output/network_ui/user_acceptance.json)、[当前修复评审](../../output/network_ui/review/alignment_review.json)、[当前工具评审](../../output/network_ui/review/frontend_review.json)。这些证据绑定同一网络应用与执行；不构成音频、ASR/MT 或触摸验收。

- 电脑真实模型：[实现/HTTP 合同/边界](../design/MODEL_BACKEND.md)、[运行环境](../../output/model_backend/runtime/validation.json)、[模型清单](../../output/model_backend/models/manifest.json)、[真实文件结果](../../output/model_backend/file_baseline.json)、[输入检查](../../output/model_backend/input_validation.json)、[接口试验](../../output/model_backend/service/pilot.json)、[当前接口评审](../../output/model_backend/service/review/followup_review.json)、[VAD 专项评审](../../output/model_backend/review/vad_followup_review.json)、[演示字体覆盖](../../output/model_backend/display_coverage.json)。文件/接口证据不替代真实麦克风、板端访问或模型上屏。

- PS/DDR：[实现与操作](../design/PS_BRINGUP.md)、[原厂配置来源](../../hardware/zynq_mini_revb/source.json)、[构建](../../output/ps_bringup/build.json)、[只读探测](../../output/ps_bringup/probe.json)、[最近一次真实执行](../../output/ps_bringup/run.json)。前两次独立结果由 PS_BRINGUP 路由；这些最小 DDR 检查自身未改写 PL、QSPI 或 SD。
- DDR 显示：[实现与方法](../design/FRAMEBUFFER.md)、[完整像素仿真](../../output/framebuffer/simulation.json)、[生成复位集成回归](../../output/framebuffer/reset_simulation.json)、[当前构建](../../output/framebuffer/build.json)、[应用构建](../../output/framebuffer/build-app.json)、[最近一次配置](../../output/framebuffer/program.json)、[最近一次实际运行](../../output/framebuffer/run.json)、[用户实屏验收](../../output/framebuffer/user_acceptance.json)、[独立评审](../../output/framebuffer/review.md)。构建、配置与实屏分别记录；[首次失败](../../output/framebuffer/attempts/20261011T055048Z_5ccd7bbb/result.json)作为已定位问题保留。

- 已确认选择：[DECISIONS](DECISIONS.md)；实际设备与资料核对：[MAP](MAP.md)。
- Wi-Fi：[接口与实现](../design/WIFI_LINK.md)、[仿真](../../output/wifi/simulation.json)、[构建](../../output/wifi/build.json)、[JTAG 配置](../../output/wifi/hardware/program_result.json)；操作入口：[run_wifi.py](../../scripts/run_wifi.py)、[wifi_link.py](../../scripts/wifi_link.py)。
- 网络实测：[纠正接线后 AT](../../output/wifi/hardware/probe_link.json)、[Wi-Fi/TCP 双向结果](../../output/wifi/hardware/connect_link.json)；历史诊断：[纠正接线前无 AT 响应](../../output/wifi/hardware/probe_before_wiring_correction.json)、[当时寄存器快照](../../output/wifi/hardware/uart_no_response_snapshot.json)。
- 主机脚本验证：[七项协议测试及隐藏输入检查](../../output/wifi/host_tests.json)，源码哈希记录包含本次控制台输入处理修正。
- 显示诊断：[恢复原静态位流](../../output/wifi/hardware/restore_static_result.json)、[显示对照记录](../../output/wifi/diagnostics/display_acceptance.json)。集成位流的启动稳定性不能沿用旧实屏验收结论。
- 显示实现比较：[定点结果](../../output/wifi/diagnostics/display_implementation_comparison.json)；[再次恢复原静态版](../../output/wifi/hardware/restore_static_repeat_result.json)。
- 当前板端身份与静默计数：[只读快照](../../output/wifi/diagnostics/current_mailbox_snapshot.json)；Wi-Fi 代码与诊断：[独立评审记录](../../output/wifi/review/review.md)。
- 静态画面：[量化预览](../../output/static_ui/ui_quantized.png)、[资产及整图还原清单](../../output/static_ui/asset_manifest.json)、[实现说明](../design/STATIC_DEMO.md)。
- PL 验证：[仿真结果与源码哈希](../../output/fpga_static/simulation_result.json)、[逐像素记录](../../output/fpga_static/logs/simulate_tb_static_ui_video.log)、[串行接收记录](../../output/fpga_static/logs/simulate_tb_static_ui_top.log)。
- 实现证据：[构建结果](../../output/fpga_static/build_result.json)、[位流](../../output/fpga_static/static_ui_top.bit)、[资源](../../output/fpga_static/reports/utilization.rpt)、[时序](../../output/fpga_static/reports/timing.rpt)、[DRC](../../output/fpga_static/reports/drc.rpt)。
- 原静态硬件证据：[JTAG 识别](../../output/fpga_static/hardware/probe_result.json)、[最近一次原静态版下载](../../output/fpga_static/hardware/program_result.json)；2026-10-10 用户反馈为“正常显示，文字和布局清楚”，仅验收原静态画面。最近下载记录已由 2026-10-11 的对照恢复操作更新，不能将其日期视为旧验收日期。
- 定点复核：[独立评审与修正](../../output/fpga_static/review/review.md)、[失败重试记录保护检查](../../output/fpga_static/review/program_failure_guard_test.json)。
- [详细方案](../design/SYSTEM_PLAN.md)、[UI 规范](../design/UI_SPEC.md)、[接口契约](../design/INTERFACES.md)。
- [当前交互原型](../../output/ui/index.html)；阶段方法：[RUNBOOK](RUNBOOK.md)。
- 浏览器证据：[交互验证结果](../../output/playwright/ui_validation.json)，记录当前 HTML 的 SHA-256、17 项通过结果、主要按钮尺寸和关键文字对比度；未出现 JavaScript 错误或外部资源请求。使用既有 machine 环境的 Playwright 和本机 Edge，没有安装浏览器或新依赖。
- 目视证据：[就绪画面](../../output/playwright/ui-ready.png)、[字幕与记录](../../output/playwright/ui-subtitles.png)、[设置](../../output/playwright/ui-settings.png)、[长句](../../output/playwright/ui-long-text.png)。模拟输入的检查不构成 USB 触摸、实际采样或模型验收。
- 文档证据：[直接链接与职责入口检查](../../output/verification/documentation_validation.json)，范围为本次改动的 Markdown、评审记录、原型本地导航和直接引用目标，没有扩展审计同级工程或 NAS 内容。
- Wi-Fi 文档补充：[本阶段直接链接检查](../../output/verification/wifi_documentation_validation.json)，检查九份改动文档及其直接目标，不扩展审计资料库。
- 产品接续建档：[本次文档检查](../../output/verification/product_handoff_documentation_validation.json)，检查八份修改文档、沿用规则和直接链接；不包含业务实现、构建、烧录或业务测试。
