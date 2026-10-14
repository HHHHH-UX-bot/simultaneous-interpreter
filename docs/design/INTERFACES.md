# 接口契约草案

草案日期：2026-10-10；接续说明更新：2026-10-12。本文定义拟议产品接口，以便采样、电脑后端和 UI 独立开发；这里的音频/会话/字幕合同尚未实现。已实现的 UART/TCP 诊断由 [WIFI_LINK](WIFI_LINK.md)、自主网络文本由 [NETWORK_UI](NETWORK_UI.md)、电脑 HTTP 文件接口由 [MODEL_BACKEND](MODEL_BACKEND.md) 定义；它们与本草案分别维护。实施前根据 [MAP](../context/MAP.md) 和 [SYSTEM_PLAN](SYSTEM_PLAN.md) 确认合同版本。

## 1. 模块到 PL 的接线合同

必须先取得：型号/资料版本、输入是麦克风还是 Line-in、供电、各引脚逻辑电平、连接器方向、数字协议、主从关系、采样率、样本位宽、时隙宽度、有效边沿、通道位置、配置寄存器与复位要求。

每根线的最终记录应包含 `功能 → 模块针号 → 板连接器针号 → 板网名 → FPGA 封装球号 → 方向 → IOSTANDARD`。当前这些值全部待核实，不建立猜测性 XDC。物理针 20 与 FPGA 球号或 HDL 端口不是同一概念，来源见 MAP。

## 2. PL 到 PS 的音频接口

建议归一化数据为单声道 `s16le`，采样率 16000 Hz。保持样本连续性；跨域使用异步 FIFO，不用简单的寄存器同步器传多位音频。

PL 音频输出可采用 AXI4-Stream + AXI DMA S2MM，寄存器采用 AXI4-Lite。音频 DMA 与视频 VDMA 是不同用途，不混用名称。DMA 描述符、对齐、缓冲大小与中断节流在驱动阶段确认。

建议只读状态包括：累计样本数、FIFO 溢出、采样帧错误、DMA 错误、峰值、饱和样本计数；可写控制包括 enable、reset、DSP bypass 和已核实格式选择。硬件状态复位必须定义是否清零计数，不能与清空屏幕记录暗中绑定。

## 3. 终端与电脑通信

建议使用一条持久 WebSocket：JSON 控制/结果消息，二进制 PCM 音频。私有开发网络可先评估 WS，正式部署按局域网威胁模型与证书能力选 WSS；不把公网服务地址或密钥写入仓库。当前端点仅逻辑名 `LAN_TRANSLATION_BACKEND`，没有部署地址。

ESP8266 的 AT、关联与 TCP 回环已有历史实证，PS 自主 AT 和网络模拟字幕已通过对应验收，使用独立 [NETWORK_UI](NETWORK_UI.md) 文本 TCP 协议。当前电脑有界 HTTP 文件接口见 MODEL_BACKEND；板卡主动访问它尚未取证。上述 WebSocket 合同仍未实现。PS 是唯一 AT/UART 事务所有者，调试 JTAG 只启动和观察、提交一次运行时配置。连续音频数据率先按 [带宽预算](SYSTEM_PLAN.md#8-资源带宽与延迟预算) 验证，不能以短包或文本结果上屏替代。

### 会话控制

| 消息 | 方向 | 关键字段与含义 |
|---|---|---|
| `hello` / `capabilities` | 双向 | 协议版本、支持语言、采样格式；不匹配明确拒绝 |
| `session.start` | 板→电脑 | UUID session_id、source_language、target_language=zh-CN、audio_format |
| `session.ready` | 电脑→板 | 配置接受并可收音频；收到后才显示后端准备就绪 |
| `session.pause` / `session.paused` | 双向 | 停止收发新音频，取消未定稿结果，保留历史 |
| `session.stop` / `session.stopped` | 双向 | 结束当前会话与处理任务 |
| `heartbeat` | 双向 | 单调时间/计数，不以心跳取代真实音频数据状态 |
| `audio.gap` | 板→电脑 | 起始/结束样本索引与原因，模型重置上下文，界面标中断 |
| `error` | 电脑→板 | 可枚举 code、范围、可重试标志和短文案，不返回秘密 |

### 音频二进制帧候选格式

Header 固定 48 字节、小端；以下仅为 v1 候选，不是已经运行的网络抓包格式：

| 偏移（B） | 字段 | 大小 | 含义 |
|---|---|---|---|
| 0 | magic | 4 B | ASCII `AUD1` |
| 4 | protocol_version | u16 | 1 |
| 6 | flags | u16 | 已定义标志，未知位拒绝 |
| 8 | session UUID | 16 B | 原始 UUID 字节，字节序以 RFC UUID 约定固定 |
| 24 | packet_seq | u32 | 包序号，每个 session 从 0 增加 |
| 28 | sample_count | u32 | 当前包中的 PCM 样本数 |
| 32 | first_sample_index | u64 | 会话内第一个样本索引，用于发现缺口 |
| 40 | capture_monotonic_us | u64 | 板端采集单调时间，用于板端总时延 |
| 48 | PCM payload | 2 × sample_count B | 默认每 20 ms 为 320 样本、640 B |

采样率/声道由会话配置确定；收端核对 payload 长度。过大帧、未知会话、重复/倒退包或缺口都需显式处理；WebSocket 自身保持同一连接内消息顺序，但异步模型任务仍可能乱序。

### 识别与翻译结果

```json
{
  "type": "translation.final",
  "protocol_version": 1,
  "session_id": "demo-session",
  "segment_id": 3,
  "revision": 2,
  "source_revision": 4,
  "source_language": "en",
  "target_language": "zh-CN",
  "source_text": "Please check the interface before connecting the module.",
  "text": "连接模块之前，请先核对接口。",
  "audio_start_sample": 16000,
  "audio_end_sample": 32000,
  "final": true
}
```

该示例仅表达字段，`demo-session` 不是正式 UUID。结果类型建议有 `asr.partial`、`asr.final`、`translation.partial`、`translation.final`；第一版可以关闭中文 partial。分别定义原文与译文 revision，翻译必须指向 source_revision。时间边界用样本索引，避免不同机器墙钟误差。

拒绝超限文本（建议上限 4096 UTF-8 字节/段）、无效编码、未知类型和旧 session。显示文本不解释为 HTML、格式指令或代码。当前原型的 DOM 使用纯文本，不从后台文本注入 markup。

## 4. UI 状态与结果归属

会话运行状态与处理阶段独立：会话 `idle/running/paused/blocked`；阶段 `listening/recognizing/translating/final`；音频设备、后端连接和输入过载另有独立状态。这样不会在“正在翻译”时误停实际连续采集。

| 行为 | 一致性规则 |
|---|---|
| ASR 草稿更新 | 只更新当前段原文；不追加历史 |
| 中文 partial（可选） | 必须标生成中，旧 revision 不覆盖新 revision |
| 中文 final | 关联当前原文版本；按段去重、按音频次序显示，只追加一次历史 |
| 用户暂停 | 停止新采样/发送，取消当前未定稿，保留最后中文；恢复开启新的音频连续区间 |
| 清空 | 用户确认后取消未定稿、换 session_id、清除本次记录；旧返回不能污染新会话 |
| 改源语言 | 暂停当前会话后应用；清楚提示将开启新会话；只提供后端声明可用语言 |
| 断线/缺失音频 | 保留已定稿文字，停止无限积压，显示短原因；恢复后由用户明确继续 |
| 过载 | 警告削波，不自行伪造增益控制；模拟中的恢复只是清除演示故障 |

调度上允许下一段识别与上一段翻译并行，但必须限流、按段提交且丢弃无效版本。第一版可以先单段流水线，测吞吐后决定是否并行。

## 5. 显示与触摸接口

UI 通过 LVGL 的显示 flush/完成回调访问板端显示驱动；明确定义实际支持的像素格式、stride、缓冲 ownership、Cache 同步和翻页完成事件。当前原型 CSS 绘制不是这一接口的实现。

触摸使用 Linux input/evdev 抽象后提供坐标与按下/松开事件。优先按设备标识寻找节点，而不是固定 `/dev/input/event0`。将原始绝对坐标范围映射到 1024×600，独立核对旋转、X/Y 交换及反向；第一版取一根主触点，避免多点触摸重复触发按钮。

## 6. 日志与度量

日志包含阶段、错误码、计数和延迟，不默认记完整音频或全文字幕。需要回放采样时明确设置调试录音开关并保存于专用输出目录。用户清空与实际持久化策略保持一致，不能只清屏却继续无限存储。

端到端时延用板端采集单调时间到板端可见提交时间；电脑推理耗时在电脑本地计算。跨机器时间未同步时不相减。记录 P50/P95、段长、RTF、缺口/溢出、显存峰值及 UI 响应，分别标注模拟与真实数据。
