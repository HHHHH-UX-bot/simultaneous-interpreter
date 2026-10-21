# 技术资料与适用范围

范围更新（2026-10-21）：下表保留原检索日期与所支持的结论，本轮没有重新联网核验这些网页。当前板级事实以 Z7-Nano source.json/audio_module.json 为准，LVGL/本地模型等旧资料不表示已采用；现行实现与故障证据见 [NOW](../context/NOW.md)。

核验日期：2026-10-10；用于方案选型，不代表相关软件已安装、驱动已启用或模型已运行。板卡与屏幕的本地证据入口见 [MAP](../context/MAP.md)，避免把通用平台文档当成 RevB 引脚表。

| 来源 | 本轮核对的内容 | 使用边界 |
|---|---|---|
| [AMD Zynq 7000 产品表](https://www.amd.com/en/products/adaptive-socs-and-fpgas/soc/zynq-7000.html) | Z-7010 双核 Cortex-A9、PL 逻辑/BRAM/DSP 与 PS 外设 | 不是板上 CPU 频率、内存或模型性能实测 |
| [PetaLinux UG1144 2026.1 Introduction](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Introduction) | Zynq-7000 支持，工具集计划在 2026.2 弃用 | 长期路线必须评估迁移，不能凭新名称认定板卡支持 |
| [PetaLinux Installation Requirements](https://docs.amd.com/r/en-US/ug1144-petalinux-tools-reference-guide/Installation-Requirements) | Linux 主机支持表、2026.1 必须匹配 Vivado 2026.1 导出 | 不默认现有 Windows、WSL/VM 或旧 BSP 可用 |
| [AXI VDMA PG020 Overview](https://docs.amd.com/r/en-US/pg020_axi_vdma/Overview) | 系统内存读帧到 AXI4-Stream 的职责 | 页面正文抓取未稳定；本轮检索摘要支持上述用途，实际寄存器/配置应再查原文 |
| [LVGL Linux 集成](https://lvgl.io/docs/open/integration/embedded_linux) | fbdev、DRM/KMS、evdev 与软件渲染选择 | 输入和显示驱动独立；应用库不凭空创建板上显示驱动 |
| [LVGL 官方 Linux 参考工程](https://github.com/lvgl/lv_port_linux) | CMake/Ninja 构建与多种显示后端入口 | 未下载工程或锁定版本；只参考架构 |
| [faster-whisper 上游](https://github.com/SYSTRAN/faster-whisper) | 推理后端、量化与附加流式项目 | 他人 CPU/GPU 跑分不能作为本机或 Zynq 保证 |
| [Whisper 上游说明](https://github.com/openai/whisper#command-line-usage) | translate 的英语目标与转写区别 | 本项目中文目标另设文本翻译阶段，不调用云端 API |
| [OPUS-MT 英中模型卡](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh) | 英语到中文、Apache-2.0 标注 | 仅在源语言匹配时作为候选；质量、版本依赖和最终许可需随实际部署复核 |
| [Anthropic frontend-design SKILL](https://github.com/anthropics/skills/blob/main/skills/frontend-design/SKILL.md) | 视觉方向、token、布局比较、截图自查和具体界面文案 | 已读短小原文，作为 UI 设计参考，不安装包、不覆盖项目规则 |
| [Google Live Transcribe](https://support.google.com/accessibility/android/answer/9158064) | 字号、语言入口、历史与持续字幕阅读 | 仅借鉴交互；本产品暂停语义与其“只保持阅读画面”不同 |
| [Android 可访问性](https://developer.android.com/guide/topics/ui/accessibility/apps) | 触控区域、文字对比与控件说明 | dp 不等于本屏硬件像素；本产品采用更大逻辑触控目标并待实屏确认 |
| [W3C 目标尺寸解释](https://www.w3.org/WAI/WCAG22/Understanding/target-size-enhanced.html) | 粗指针、触控误差和目标面积 | CSS px 与硬件像素不同，不据此宣布产品整体 WCAG 合规 |
| [LVGL Button 文档](https://lvgl.io/docs/open/widgets/button) | checked/disabled/focused 等状态与事件 | 将设计映射到原生控件，实际版本与平台待验证 |
| [Cirrus Logic WM8731 编解码器](https://www.cirrus.com/products/wm8731/) | I2S 数字音频接口、主/从模式与 BCLK/ADCLRC/DACLRC 方向、MCLK 主时钟需求、I2C/SPI 控制口 | 本轮未取得数据手册原始 PDF 正文（第三方镜像返回 403）：MCLK 频率档位、寄存器完整默认值仍须查原文；芯片手册结论不等于模块板级实现。镜像线索：[alldatasheet](https://www.alldatasheet.co.nz/html-pdf/473988/WOLFSON/WM8731_09/4524/15/WM8731_09.html) |
| [WM8731 开源驱动（Teensy Audio control_wm8731.cpp）](https://raw.githubusercontent.com/PaulStoffregen/Audio/master/control_wm8731.cpp) | 对照确认：I2C 从地址 `0x1A`/`0x1B`、寄存器编号（R0/R1 输入音量、R2/R3 耳机音量、R4 模拟通路、R5 数字通路、R6 掉电、R7 接口格式、R8 采样率、R9 ACTIVE、R15 复位）、`R7=0x42` 为主模式而 `0x02` 为从模式、`R9=1` 使能接口 | 第三方实现，只用于交叉核对寄存器用法；最终以 Cirrus 数据手册与板上实测为准。不下载、不编译该工程 |

较旧的板卡参考工程应按源码与硬件配置迁移，不混用旧 XSA/bitstream 与新工具的产物。长期记录版本时写入具体对象和验证范围，避免只记“最新版”。
