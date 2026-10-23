# 声纹与板上离线模式

## 使用

正式板上应用仍默认云端，使用现有百炼实时翻译/译音链路。点屏幕 **设置 → 模型与声纹**，或在电脑实际同步画面中点同一位置：

1. **云端 · 自由对话**：原有中英双向字幕与译音；需运行期 Wi-Fi 和 API 凭据。
2. **本地 · 12 个短句**：Cortex-A9 本地处理，不上传语音；顶部语言菜单选择英译中或中译英。说完后留约半秒停顿。未匹配时显示提示，保留上一条有效字幕。
3. **登记说话人 1–4**：在运行状态点相应登记位，用自己的声音说约 3 秒有效语音，进度到 100% 才替换该位的声纹。30 秒未收够会超时。暂停、换方向、换模式或云会话重建会取消未完成的采集；已完成登记保留到清除或断电。
4. 主界面显示当前声纹状态。未登记、相似度不足、多个登记位难区分或语音太短均显示未知/过短。登记位编号是用户自行对应的人；尚无姓名键盘、真人身份数据库或活体检测。结果不作为身份认证。

电脑运行根目录 `打开电脑画面.cmd`，地址 `http://127.0.0.1:8787`。板端绘制实际像素，电脑鼠标走实体触摸相同命中路径，所以模式、登记进度和身份显示共用状态。声纹显示针对当前有效音频，不在未对齐云端延迟的历史条目上补写人物标签。

## 离线短句范围

声学输入为 16 kHz 单声道，整数 FFT512 / 20 个 Mel 滤波带 / 12 个非零阶倒谱系数，25 ms 窗、20 ms 步进。VAD 后做有界 DTW，按短句归并三个语速模板，再执行绝对距离和第一/第二候选差距拒绝。模板有 38,916 个 int16 数值，约 76 KiB；它是小型模板声学模型，不是神经网络或开放式大模型。

| 中文 | English |
|---|---|
| 你好。 | Hello. |
| 谢谢。 | Thank you. |
| 请再说一遍。 | Please say that again. |
| 请说慢一点。 | Please speak slowly. |
| 你叫什么名字？ | What is your name? |
| 很高兴认识你。 | Nice to meet you. |
| 我需要帮助。 | I need help. |
| 洗手间在哪里？ | Where is the bathroom? |
| 这个多少钱？ | How much does this cost? |
| 我们现在开始。 | Let us begin now. |
| 请等一下。 | Please wait a moment. |
| 再见。 | Goodbye. |

识别成功后使用该表的对应译文及 24 kHz、8-bit PCM 短句音库，经原有 DAC 音量/静音/速度控制输出。固定对应与预合成音库不能扩展为任意文本翻译或通用 TTS。模型从 Windows Huihui/Zira 合成语音生成，尚未用用户真人语音适配；实际口音、距离、麦克风频响会影响命中率。6 秒以上连续有效声音的本地短句候选会拒绝，避免无界缓存和长话误标为短句。

## 板上声纹算法

每段有效语音的 MFCC 均值与标准差构成 24 维统计向量。每位登记收集约 150 帧，四位模型共 96 个统计值，存在 RAM。识别至少需要 40 帧有效语音，候选余弦相似度显示为整数分数；当前保守门限为 98/100，第一/第二候选差至少 2。阈值只经过合成样本筛查，没有真人 FAR/FRR 标定，宁可未知，也不宣称专业声纹验证质量。

云模式在已有 Opus 输入帧旁提取特征，不另抢 PL FIFO；本地模式由唯一消费者读取 PL FIFO。切换先结束云采集及译音，再改变新会话，局部音频和待登记声纹按 epoch 隔离。设置/历史仍共用原有 UI。模型字段增加 44 字节，状态/快照为 135848/135912 字节；历史条目 ABI 和导出格式保持原定义。

字体新增标签超过原 2 MiB，ARM TLS 链接中字体区调整为 `0x01700000..0x019fffff` 共 3 MiB；它在 FB1 加保护区之后，且止于云区之前。正式代码/音库约 1.62 MB，仍在原 2 MiB 应用区；链接器继续检查各区边界。

## 构建与加载

- `scripts/build_local_ai.ps1`：生成模型、字库、正式 ARM 应用和 CPU1。`-RebuildFpga` 先运行音频仿真，再完整构建 Vivado 2018.3 网口平台。`-Test` 生成独立语速/库外样本，执行本机 C 检查。
- `启动译声.cmd`：重新 JTAG 下载正式应用并隐藏输入 Wi-Fi/API 凭据。
- `启动离线短句.cmd`：无需凭据，重新下载后直接开始本地短句模式。
- `重载并复用当前云配置.cmd`：仅从本板既有运行期邮箱读取 API 配置，Wi-Fi 邮箱已擦除时在进程内复用 Windows 当前连接的 Wi-Fi 配置。凭据不写文件或参数。断电后没有有效 API 邮箱时请使用前两项。
- 构建产物与 load、CPU1、输入/位流/初始化哈希在各自目录绑定。`scripts/verify_local_board.py` 自动加载独立合成自测、读取公开计数、比较实板画面、测试电脑点击，最后恢复正式麦克风和运行配置。测试过程的私有配置仅在父进程内存持有。

现有 `build_tts.ps1` / `load_tts.py` 属旧构建入口；本轮优先使用 local_ai 入口。JTAG 下载断电失效，屏幕供电仍由下载流程开启 USB0 PORTSC.PP；本轮不写 QSPI、SD 或 ESP 固件。

## SpeechBrain 电脑对照

用户提供的 [SpeakerRecognition 官方接口](https://speechbrain.readthedocs.io/en/latest/API/speechbrain.inference.speaker.html)依赖 PyTorch；本板运行裸机 C，因此本轮没有把 ECAPA 冒充为板上算法。电脑额外建立项目隔离环境 `output/local_ai/speechbrain_env/`，模型为 [speechbrain/spkrec-ecapa-voxceleb](https://huggingface.co/speechbrain/spkrec-ecapa-voxceleb)，192 维嵌入，模型卡标注 Apache-2.0；已保存固定修订、文件哈希、包清单与实际测试结果。

在项目根目录用该环境的 `Scripts/python.exe` 运行 `scripts/speechbrain_compare.py`：

```powershell
# 两个 WAV 的声纹验证，WAV 为单声道 16-bit PCM，支持重采样。
python scripts/speechbrain_compare.py verify --reference reference.wav --audio probe.wav
# 按名字登记特征，显式保存嵌入到项目 output 下；不复制原始音频。
python scripts/speechbrain_compare.py enroll --name 张三 --audio reference.wav
python scripts/speechbrain_compare.py identify --audio probe.wav
```

这些是独立电脑文件工具，不读取板卡麦克风、不改变板端编号、不把推理转发到云端；没有将该较大 PyTorch 模型部署在 Zynq 上。`bootstrap_speechbrain.py` 使用官方 PyPI JSON 与 SHA256 核验的 wheel，安装范围限项目虚拟环境；本机首次 pip 索引等待失败已通过直接下载 wheel 解决。

## 验证边界

分项实证见[本轮验证](../validation/local-ai-20261022/README.md)。120 个合成正例、8 个库外句、两个合成声音和像素一致性，均不替代真人登记/误识率、真人短句可用率、听感或长时间端到端验收。原有云端长句/历史故障记录仍保留原范围。
