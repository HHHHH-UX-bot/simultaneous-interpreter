# 英语识别与中文翻译

本地 P5a/P5b 核验日期为 2026-10-12，范围为 CPU 文件推理、有界 HTTP 接口及直接证据。P5a 受控英语文件已完成真实 ASR/MT；P5b 文件接口的 33 项真实检查与 58 项独立回归保持原源码范围。2026-10-14 新增 P5c-0 DeepSeek 独立文本实测，见下节。代表性录音、人工译文质量、性能分布和流式效果尚未验收。语言范围见 [D8](../context/DECISIONS.md#d8-第一版英语转中文)，历史文本选择见 [D11](../context/DECISIONS.md#d11-中文翻译优先使用-deepseek-api)，当前云端语音选择见 [D13](../context/DECISIONS.md#d13-云端语音翻译与独立终端)；硬件到货和当前板端状态由 [NOW](../context/NOW.md)维护。

本文维护已经执行的电脑模型试验与对照入口。2026-10-14 用户改为最终“板卡 Wi-Fi → 云端语音翻译 → 中文字幕”的独立终端，当前方案由 [CLOUD_SPEECH](CLOUD_SPEECH.md)维护。现有 [NETWORK_UI](NETWORK_UI.md) 原型仍由电脑连接 ESP 的临时 TCP 服务；该证据不代表板卡主动调用云端或连续上传音频。

## P5c-0 DeepSeek 独立文本翻译

当前采用官方 `deepseek-flash`，2026-10-14 官方对应 DeepSeek V4.1 Flash；地址 `https://api.deepseek.com/chat/completions`，显式 `thinking.type=disabled`、温度 0、最多 384 输出 tokens、非流式单次请求，无自动重试。[官方模型](https://api-docs.deepseek.com/quick_start/pricing/)、[请求合同](https://api-docs.deepseek.com/zh-cn/api/create-chat-completion/)。后端别名可能更新，本次记录实际请求与返回模型名，不将 API 别名视作固定权重修订。

实现为 [DeepSeekTranslator](../../software/model_backend/deepseek_translation.py)和[单句入口](../../scripts/run_deepseek_translation.py)。文本沿用板端 255 码点/768 UTF-8 字节上限，超限或截断返回失败；不截断译文、不改成模拟文案。响应最大 64 KiB，官方 HTTPS 连接后使用剩余等待窗口中断停滞响应，系统 DNS 不宣称硬实时截止。

2026-10-14 [17 项离线验证](../../output/model_backend/deepseek/offline_validation.json)及[独立复核](../../output/model_backend/deepseek/review.json)通过。操作者通过本机隐藏对话框输入密钥，已对公开参考英语执行一次真实 API 调用：[单句结果](../../output/model_backend/deepseek/latest_text.json)。完整译文保留，返回耗时 6.297 秒，使用 117 tokens；人工译文验收仍为 pending。相同英语旧本地 MT 记录为 0.555 秒，来自 2026-10-12，不是同时对照或时延分布，不能据此承诺 Flash 更快。

入口只允许隐藏对话框，避免终端明文回退；密钥不写文件、命令行、模型提示或原始错误。JSON 解析后拦截密钥回显，失败只记录固定原因/HTTP 状态。每个程序进程需要独立输入，当前没有持久密钥管理。已执行入口如下：

```powershell
python -B -X utf8 scripts/run_deepseek_translation.py --key-dialog
```

本阶段只测英语文本到中文：旧 ASR/HTTP 服务仍是本地版本，未接入此适配器，未把 API 结果发送板卡。该译文还缺两个字形，见 [覆盖检查](../../output/model_backend/deepseek/display_coverage.json)。本地 ASR 接 DeepSeek 的未提交草稿已停止并保留在 tmp；后续按照云端一体方案推进，不把此历史试验作为最终运行依赖。任意中文覆盖仍是独立任务。

## 当前可用电脑

已核对本机 CPU、内存、显卡、驱动、machine 环境包及 torch 的 CUDA 状态，唯一记录在 [environment.json](../../output/model_backend/environment.json)。服务运行时监听地址和进程身份以 [runtime.json](../../output/model_backend/service/runtime.json) 为准；地址可能随 DHCP 改变，启动时显式传入。

当前复用 machine 的 CPU 版 Torch，CUDA 不可用。33 个依赖安装于 `output/model_backend/runtime/site-packages/` 与 `runtime/compat/`，仅由本项目进程调整导入路径；没有覆盖原环境的 Torch。针对原环境 sklearn 命名空间不完整的问题，兼容依赖也置于项目内。版本、路径与导入结果唯一见 [运行环境验证](../../output/model_backend/runtime/validation.json)，准确 wheel/版本/官方 PyPI 哈希见 [基础锁](../../software/model_backend/requirements-runtime.lock)、[兼容锁](../../software/model_backend/requirements-compat.lock)和 [包来源](../../output/model_backend/runtime/package_lock.json)。

## 已执行的模型链路

1. 用明确标注来源的英语测试 WAV 验证文件格式，第一轮固定 16 kHz、单声道、16 位 PCM。合成语音测试不能替代麦克风采样、噪声、口音或真实会话质量验证。
2. ASR 试验采用 `Systran/faster-whisper-small.en`，强制英语 transcribe、CPU int8、8 线程；其来源是 OpenAI `small.en` 的 CTranslate2 转换。[发布者模型卡](https://huggingface.co/Systran/faster-whisper-small.en)。Whisper translate 输出英语，英语转中文使用独立 MT。[Whisper 官方说明](https://github.com/openai/whisper)
3. MT 试验采用 `Helsinki-NLP/opus-mt-en-zh`，CPU float32，句首显式标记 `>>cmn_Hans<<`；直接加载 tokenizer/model，禁用远程 Python 代码与联网补文件。权重以 `weights_only=True` 读取，并显式选择已核验的权重格式。[发布者模型卡](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)
4. 两个仓库均固定 40 位提交修订，只获取规定文件；13 个文件的上游 Git blob/LFS 哈希与本地 SHA-256 通过核对。修订、大小、文件清单及来源唯一见 [模型清单](../../output/model_backend/models/manifest.json)。加载前重新检查完整 ASR/MT 配置、文件选择、路径、大小及哈希，拒绝额外权重、索引或 adapter 文件；不会只检查清单的一部分。
5. 当前还使用 faster-whisper 1.2.1 已安装官方 wheel 中的 Silero VAD v6；加载前核对项目内位置、版本和固定文件哈希。配置与实际 VAD 资产记录在文件/服务证据中，没有另行下载 VAD 模型。

上述是已执行的原型试验选型，没有形成用户确认的最终模型方案。ASR 发布者标示 MIT，MT 标示 Apache-2.0；正式分发仍需核对完整依赖/素材许可并保留来源。当前字体许可缺口按 [RISKS R9](../context/RISKS.md) 处理。

## 受控文件证据与噪声修复

输入为 Microsoft Zira 合成的 9.025 秒英语 WAV，格式为 16 kHz、单声道、16 位 PCM：[音频](../../output/model_backend/fixtures/english_reference.wav)、[来源/参考文本/格式](../../output/model_backend/fixtures/reference.json)。原文、实际译文、逐段识别、WER、ASR/MT 耗时、RTF 与源码/权重/音频绑定见 [当前文件结果](../../output/model_backend/file_baseline.json)。WER 为 0，模型输出保留原样；`HumanTranslationAcceptance` 仍为 pending。模型构建耗时不包含 Python 与库导入的全部冷启动成本。

第一次 HTTP 试验的确定性轻微白噪声被误识别为 “Thank you.”；[原失败记录](../../output/model_backend/attempts/20261012T043558Z_before_vad/service_pilot.json)保留 failed。对同一音频直接重现后，VAD 检出零个人声区间，启用过滤的 ASR 输出为空；定位证据见 [单变量对照](../../output/model_backend/diagnostics/noise_vad_probe.json)。当前使用显式默认 VAD 参数；零 PCM 仍走单独的数字静音短路。该修复和单个噪声样本不等于真实房间噪声、口音、背景音乐或 VAD 效果已验收。

文件入口为 [run_model_file](../../scripts/run_model_file.py)，边界入口为 [validate_model_backend](../../scripts/validate_model_backend.py)。输入只接受 1 个样本到 30 秒的对应 WAV，文件总长不超过 964096 字节，拒绝空输入、截断、错误声道/采样率/位宽及超限；18 项检查见 [输入检查](../../output/model_backend/input_validation.json)。文件阶段独立评审见 [原文件评审](../../output/model_backend/review/review.json)、[VAD 专项评审](../../output/model_backend/review/vad_followup_review.json)；各记录保留其对应版本，后续 RIFF/HTTP 修复由下方当前接口评审覆盖。

## 电脑 HTTP 文件接口

实现入口：[service](../../software/model_backend/service.py)、[run_translation_service](../../scripts/run_translation_service.py)。验证入口：[validate_translation_service](../../scripts/validate_translation_service.py)，它生成仅在内存中的 API key，通过私有管道启动自己拥有的服务，使用测试文件验证后关闭服务。实际监听/请求/关闭证据见 [33 项接口试验](../../output/model_backend/service/pilot.json)、[当前独立评审](../../output/model_backend/service/review/followup_review.json)和 [58 项回归](../../output/model_backend/service/review/adversarial_after_http_fixes.json)。服务当前已停止，端口关闭；本机请求的通过不能宣称 ESP 已访问此端口。

| 操作 | 输入和结果 |
|---|---|
| `GET /v1/health` | 返回 ready、固定音频格式与上限、CPU、忙碌状态及完成数量；不返回音频、文本或 API key |
| `POST /v1/interpret` | 请求体为完整 WAV；返回实际英语原文、简体中文、no_speech、段/修订身份与处理耗时 |

POST 必须携带 `Content-Type: audio/wav`、准确 `Content-Length`、运行时 `X-API-Key`、`X-Session-ID`（1..48 位字母/数字/下划线/连字符）、正 uint32 `X-Segment-ID` 和 `X-Revision`。会话/段/修订是本接口的标识，不等同于板端 UI epoch；字段合同由本节维护，不复制拟议 WebSocket 正式接口。

服务只绑定一个显式私网/环回 IPv4，客户端 IP 必须在 allowlist；API key 经隐藏输入或父进程私有管道提供，不出现在参数、文件或请求日志中。原型 HTTP 适用于受信局域网，不当作公网部署。最多 4 个请求线程、1 个推理任务，忙时返回 429，不积压音频。最多 8 个会话，每个保留最近完成结果用于同身份同音频重试；冲突音频和更旧段/修订返回 409，第九个会话要求重启本轮原型服务。音频短时缓冲，结果缓存仅在内存；默认不保存实际用户录音或字幕。验证器仅保存明示来源的合成测试结果。

异常 RIFF 块曾触发标准库 RuntimeError、使请求无状态断开；当前统一为 400 `invalid_wav`。单次接收超时也不足以约束持续慢速输入：当前请求头总限时 5 秒，认证后请求体总限时 15 秒，到期中断 socket 读取并释放线程；完整请求体收到后立即撤销输入计时器，较长推理不受该计时器影响。原失败保留在 [修复前回归](../../output/model_backend/service/review/adversarial_before_http_fixes.json)，不重写为通过。

推理忙返回 429；四个连接槽位都占用时可能返回 503。独立评审同时记录 Windows 在关闭尚有未读字节的饱和连接时，客户端可能看到传输断开；后续客户端须把这类传输失败纳入有界重试，不能只处理 HTTP 状态码。尚未实现的 ESP 客户端不据此宣称恢复能力通过。

启动前在项目目录选择 machine Python 路径为 `$PythonPath`，并核对电脑/模块当前 IP。如下是已实现的操作入口，不构成新的执行授权：

```powershell
& $PythonPath -B -X utf8 scripts/run_translation_service.py --bind <电脑局域网IP> --allow-peer <电脑局域网IP> --allow-peer <ESP当前IP>
```

该交互入口隐藏读取 64 位十六进制 API key；Ctrl+C 关闭。父进程管道模式在 STOP 或管道关闭时自动停止，避免中断后留下无主服务。本轮没有调整路由器、防火墙或全局代理。

## 上屏与完整阶段的剩余条件

当前 337 字演示子集不覆盖实际译文中的“将、提、者”；[覆盖核对](../../output/model_backend/display_coverage.json)只验证当前真实文件译文。接模型前需独立补足字体或实现有界字形更新，重新核验字体与板端画面，不能把真实译文替换成模拟文案。

板卡主动连接电脑尚未取证；连续音频仍受 [SYSTEM_PLAN 带宽预算](SYSTEM_PLAN.md#8-资源带宽与延迟预算)限制，不能用文件接口通过替代 ESP/UART 吞吐、音频采样和端到端时延。真实麦克风与触摸阶段分别见 [RUNBOOK](../context/RUNBOOK.md)。

## 放行证据

本轮八份上下文/导航文档及直接引用目标的检查见 [文档验证](../../output/verification/model_backend_documentation_validation.json)；只检查本次改动与关联链接，没有扩展到全项目或资料库审计。

- 记录实际版本、权重来源和哈希、所用 CPU/GPU、输入格式、音频时长与预热方式。
- 用公开/用户允许的代表性英语音频和参考文本评估识别，人工核对中文意思；单个正确示例不代表产品质量。
- 分别记录 ASR、MT、网络/上屏耗时以及 RTF；区分文件测试、流式模拟和真正连续采样。
- 不将音频上传到云端；默认不保存用户语音。测试文件和模型产物集中在明确的 output 子目录，中间材料放 tmp。
- 字库覆盖、网络/音频带宽和软件故障恢复分别通过后，才开始板卡音频到真实译文的联合验收。
