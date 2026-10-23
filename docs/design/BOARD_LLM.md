# 板上 Qwen 本地部署

2026-10-23 已加入云端 STT/TTS 与真实麦克风，本地翻译保持板上计算。现行入口、延迟和验证见 [LOCAL_VOICE_LLM](LOCAL_VOICE_LLM.md)，以下保留模型结构与原始文本基线。

本工程将 Qwen2.5-0.5B-Instruct 的真实解码器和权重部署到 Z7-Nano 的 Cortex-A9。输入可以是新的中英文本或问答，没有短句字典。电脑通过直连网线提交文本和显示结果，不执行模型计算，也没有云端兜底。

## 使用

- 断电后：运行根目录 `断电后启动本地LLM.cmd`。它重新加载 FPGA、CPU0、CPU1，打开屏幕 USB 供电，再上传模型和分词器。加载到 RAM，断电会丢失。
- 板卡仍在运行时：运行 `启动本地LLM.cmd`，先受控关闭旧会话再重载。
- 已经加载完毕：运行 `打开本地LLM界面.cmd`，或启动 `python scripts/board_llm_web.py` 后访问 `http://127.0.0.1:8790`。
- 原有屏幕镜像仍在 `http://127.0.0.1:8787`；用 `打开电脑画面.cmd` 启动。模型结果也会更新板屏。
- 本地翻译现已通过百炼识别和播报接入语音；它不是完全离线语音模型。运行期配置入口为 `configure_cloud_runtime.py`，凭据不保存在模型和源码里。

上下文为 512 token，包含 system、ChatML、输入和预留生成。单次输入最多 2048 UTF8 字节，system 最多 512 字节，最多生成 256 token，二者仍须满足总上下文限制。板屏保留前 255 个 Unicode 字符，电脑可取回完整输出。小模型可能漏译、添译或答错，不能把成功运行等同于准确翻译或实时同传。

## 模型与运行库

上游为 [Qwen/Qwen2.5-0.5B-Instruct](https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct)，固定修订 `7ae557604adf67be50417f59c2c2f167def9a775`。原始文件的上游 LFS 校验、许可证和本地量化记录在 `models/`。全部大矩阵采用 group64 对称 Q6，激活 Q8，保留共享词嵌入；没有把整个词表展开成 FP32。

| 资产 | 大小 | SHA256 |
| --- | ---: | --- |
| Q6 权重 | 401,637,888 字节 | `e7ebb0a8435ac90173ef9cefa753adc7fe2f6a5fb8fa86757a98f1344dcf4863` |
| 分词器 | 5,277,796 字节 | `b6aa0fa09ec4913e8892cc1f25c0c76f24a72dbd3074131782bea58b6a944aea` |

24 层、896 隐藏维度、14 个 Q head / 2 个 KV head；包含 Qwen 的半分 RoPE、偏置、GQA、SiLU、RMSNorm 和完整 BPE/NFC 分词。SDK 2018.3 编译为 ARMv7 NEON softfp，启动显式开启 VFP/NEON。推理和分词在 CPU0；CPU1 保持 USB 触摸，PL 保持音频、视频和网口。

| DDR 区域 | 用途 |
| --- | --- |
| `0x04000000..0x1bf08200` | Q6 权重，末地址不包含 |
| `0x1c000000` 起 | 分词器 |
| `0x1c800000` 起 32 MiB | 推理 arena，512 上下文约 12.64 MiB，另有约 233 KiB 批量预填充工作区 |
| `0x1f000000` 起 8 MiB | 有界分词工作堆 |

模型上传使用 UDP 4178，分词器 4179，命令 4190。接收地址、大小和 SHA 固定在固件里；客户端不发送任意内存地址。上传带 CRC、顺序确认、续传和完整传输 SHA 校验；模型借用后锁定。协议用于可信直连网线，不提供网络身份认证。

## 重建与验证范围

在工程根目录用 SDK 2018.3 运行：

```text
python scripts/build_cloud_app.py --mode zhen --speech --arm-tls --arm-tls-packets --arm-tls-active-recv --low-latency-playback --touch --uart-baud 2000000 --remote-display --tinyqwen --output-dir output/voice_local/microphone
python scripts/run_z7_nano_usb_host.py build
python scripts/load_tinyqwen.py --app-dir output/voice_local/microphone --cold-start --bind 192.168.1.102 --local-voice --configure
```

硬件没有新增引脚或 PL 算法，沿用已验证的 2018.3 位流。部分 Windows 目录映射下，SDK Tcl 的 `file normalize` 会丢失 Documents 目录；新加载入口默认使用 `xsct_path_compat.bat`，仅当原绝对路径存在且规范化结果不存在时保留原路径。

实际构建、加载、触摸和推理证据以 `output/tinyqwen/microphone/` 为准。原生随机数值对照、真实权重对照及分词测试只证明各自范围，不能代替实板生成；现场推理结果与耗时保存在其 `board_validation/`。
