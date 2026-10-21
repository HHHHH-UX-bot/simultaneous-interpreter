# 字幕停更：输出模态单变量对照

2026-10-21，Z7-Nano 7010、ESP8266 1 Mbaud、CYEA。仅字幕固定英文样本的 30 分钟板测已通过；随后另行修正本地 DAC 选择、加载真实麦克风版，用户确认识别、字号和画面正常。未写启动介质或 ESP 固件。

## 假设与对照边界

[同日原候选](../arm-packets-20261021/README.md)在文本和译音同时回传时两次发生定稿停更；其中一次无重连自行恢复，另一次单次查询发现 ESP 缓冲为 0。这不能直接证明是云端或译音导致。

本次保持模型、Opus 输入、固定样本、30 秒静音间隔、VAD、译音音色/回合参数、512 包队列、UART、TLS 和 USB/显示不变，只将 `session.modalities` 从 `["text","audio"]` 改为 `["text"]`。译音消费者仍编入，但预期不收到音频事件。此前观测约 15 秒一轮，本次保持相同节奏，不同时改变观察频率。对照不是随机交叉试验，不能单凭一次正常就断言服务端根因。

该配置由显式 `--text-only-response` 开启，不改变默认构建。`speech` 构建仍保留，目的是使上传和播放代码不因对照而被一起替换。接口依据为[百炼官方客户端事件](https://www.alibabacloud.com/help/zh/model-studio/live-translator-client-events)中的 Qwen3.5 `modalities`。

观察期间用户确认[初版字幕优先并保留本地回环](../../context/DECISIONS.md#d16-初版以字幕为主保留本地回环)。这属于产品范围决定，不是提前给长测或停更根因放行。

## 固定样本长测：仅改变云端输出模态

- 实际板态：同一 cable，DONE/EOS=1、CRC=0、USR_ACCESS=0x5a375458；沿当前普通 AT、TCP 关闭和 CPU1 停止的暖加载流程切换。
- 默认与文本配置的真实 C 测试各通过 14600 项检查、500 条 UI 定稿；文本版使用 `response.text.text` / `response.text.done` 的 `text` 字段。两份实际 session 除 `modalities` 外逐字段相同。
- 文本固定样本 ARM 构建、实际暖加载、云端就绪和 CPU1 常驻启动通过。持续观察同时核对上传音频时长、原文/译文/提交数、TLS/UART/丢页，以及译音事件确实为零。
- 实际首末快照跨度 **1830.1 秒**、新增 **174 条双语定稿**，最长采样定稿间隔 **30.5 秒**；始终同一条 TLS 连接，UART/TLS/上传丢页为零，返回音频事件为零。实际计数与音频时间线见 [result](result.json)、[observation](observation.json)。
- 这次观察使用 ELF `8214ea7f3385986b40830b7026f14b87073aba81e4346eaa25c46d261559203d`；播放代码仍对应基线 `7a04d1e`，**尚未包含下节 DAC 修正**。来源由 [selftest_build](selftest_build.json)、[selftest_load](selftest_load.json)绑定，不能要求该历史播放哈希匹配修正后的文件。
- 会话单变量和默认加载映像比较见 [config_comparison](config_comparison.json)；它属于 DAC 修改前的对照，不是当前麦克风映像等同于旧固件的声明。

## 单独修正本地输出选择

停止请求云端译音并不会自动恢复本地回环：旧 `speech_playback_init/tick` 仍把 `audio_regs 0x9c` 位 0 写成 1，选择译音 DAC。长测完成后另行修改字幕构建，使该位在初始化、空闲、完成和停止后始终为 0，选择现有 PL 的麦克风回环。默认译音构建仍为 1；没有改动 RTL、引脚或重新构建 PL。

新测试在旧代码下复现 `expected=0 actual=1`，修正后默认/文本两种模式通过，分别见 [修改前](playback_before.json)、[修改后](playback_after.json)。默认完整播放回归通过 WS/JSON/Base64、PCM 顺序、环形缓冲绕回、抖动/预填充和短句尾部，见 [playback_full](playback_full.json)。最终默认/文本英语事件/UI 测试各 14600 项、500 次定稿通过，见 [native_audio](native_audio.json)、[native_text](native_text.json)。没有将旧的全套模式报告当作本次重跑。

## 当前真实麦克风和数字回环

修正后麦克风 ELF 为 `0661bbcd5a92209ede2a6fbcf4e883ec98eab3c7dad39f295b1e45d7fb885740`，见 [microphone_build](microphone_build.json)、[microphone_load](microphone_load.json)。暖加载前确认云链路 idle、普通 AT、无 TCP、CPU1 停止且 USB DMA Halt；加载后整帧 614400 像素零差异，见 [microphone_frame](microphone_frame.json)。随后恢复 CPU1 常驻触摸并直连云端，公开计数见 [microphone_ready](microphone_ready.json)、[microphone_touch_live](microphone_touch_live.json)。

用户说参考句、出现新原文和译文后点 A+，确认“识别、字号和画面都正常”，见 [user_acceptance](user_acceptance.json)。只读数字寄存器检查发现 DAC 选择位 0、ADC/DAC 计数增加、溢出/欠载为零、BCLK 3.072 MHz、L1 为 62 µs，见 [loopback_hardware](loopback_hardware.json)。这不是本轮耳机听感、声学时延或真人长期运行验收。字幕版仍编入原播放缓冲，未声称完成内存裁剪；既有 PL 回环无需新增硬件资源，用户允许资源不足时取消的条件尚未触发。

## 放行边界与接续

固定样本定稿超过 150 秒不更新则停止并保存现场；本次达到至少 1800 秒首末跨度并持续产出有效定稿才判通过。[观察脚本](observe.py.txt)与 [stage](stage.json)冻结本次方法和指纹；[回环探测脚本](check_loopback.py.txt)只读公共寄存器，不读麦克风载荷。两个源码阶段分别保存，不能把长测扩大为 DAC 修正后的麦克风整机长测。

仍待真实麦克风连续多句/长静默、耳机听感、其他网络及独立启动；云端译音停更根因未确认。未添加经验性自动重试，也不把“停止请求译音”称为厂商故障修复。接续和当前所有者见 [NOW](../../context/NOW.md)。
