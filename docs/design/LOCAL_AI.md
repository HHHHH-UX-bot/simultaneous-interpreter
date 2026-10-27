# 旧离线短句与 ARM 模板声纹（已删除）

2026-10-22 的桌面副本曾提供“本地 · 12 个短句”模式：Cortex-A9 上的整数 MFCC（FFT512、20 个 Mel 带、12 阶倒谱）+ 有界 DTW 模板匹配，识别 12 个固定中英短句并播放预合成音库；同一构建用 MFCC 统计向量做 ARM 模板声纹。2026-10-27 按用户选择删除（[D27](../context/DECISIONS.md#d27-功能整理调试开关面对面对话术语表删除旧功能2026-10-27)），一并删去 `--local-ai` 构建选项、`local_ai.c`、`tiny_features.*`、短句表与音库生成脚本、`build_local_ai.ps1`、`load_local_ai.py`、`verify_local_board.py` 以及根目录 `启动离线短句.cmd`、`启动译声.cmd`、`重载并复用当前云配置.cmd`。最后包含它们的提交为本地标签 `archive/pre-cleanup-20261027`（`024f934`）。

现行功能：本地模式是云端 ASR → 板上 Qwen → 云端 TTS，见 [LOCAL_VOICE_LLM](LOCAL_VOICE_LLM.md)；声纹是 PL 内 MFCC + CNN，见 [LOCAL_SPEAKER_CNN](LOCAL_SPEAKER_CNN.md)。`local_ai.h` 仍保留，作为本地模式控制器（`llm_ui_bridge.c`）的接口。

当时的实测与边界见[验证](../validation/local-ai-20261022/README.md)，只代表那一版。

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
