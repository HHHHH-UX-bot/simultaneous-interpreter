# 完全本地的 MFCC + CNN 声纹

本轮在独立工程完成开发和烧录，原语音工程保留作为回退。当前运行真实麦克风声纹版；下列离线测试、实板检查和真人使用边界分别记录。

## 目标与数据边界

CNN 判断“谁在说话”，不承担文字识别。声纹支路直接从 FPGA 的麦克风采样分叉；MFCC、CNN 和登记模板匹配均在板上。普通翻译保留原来的云端 STT/TTS；声纹登记期间先暂停翻译并关闭云上传，再开始采集登记样本。特征、嵌入和登记模板不接入云 API。

## 已实现的结构

| 阶段 | 实现 |
| --- | --- |
| 音频 | 48 kHz ADC 独立分叉，63 tap 抗混叠 FIR 后 16 kHz，400 点窗，160 点步长 |
| MFCC | 去直流、Hamming、512 点定点 FFT、32 Mel 滤波、对数、16 维 DCT |
| CNN | 128×16 输入，三层 3×3 步幅 2 卷积，通道 16/32/64；全局均值，64 维嵌入 |
| 运算 | PL 串行复用 MAC，int8 权重和激活，定点累加；不占用翻译 CPU 做 CNN |
| 说话人 | 本地四个登记位、相似度与未知拒绝；原型模板保存在 RAM |
| 显示 | 板屏和电脑板屏镜像共用登记、识别结果与状态 |

训练在电脑离线进行，部署使用实际训练权重，不能用随机卷积或固定短句模板替代。先用 OpenSLR Mini LibriSpeech 检查训练流程，再使用 LibriSpeech train-clean-100；dev-clean 的未见说话人分别用于校准和最终评估，均为 CC BY 4.0。须按说话人隔离训练和评估，保留未见说话人的登记/查询及未知拒绝结果；这些数据不代表用户房间的真人准确率。

## 模型与离线证据

使用 251 名训练说话人的 train-clean-100，dev-clean 另外 40 人按身份分成 20 人校准及 20 人最终测试。三层卷积训练时使用 BatchNorm，部署时将其融合到权重与偏置，无额外硬件算子。验证集选择量化检查点，不以最后一次训练结果替代最佳验证模型。

- 模型二进制 SHA256：`1e52c250894dbe6297e5879d12013df66c95b38cb5337f26e2d0a556bf536f6b`。32 KiB 地址空间，实际装载 7,076 个 32 位字；ID `0xa3a5ae67`、装载 FNV32 `0xaab7fc2f`。
- 校准阈值为余弦相似度 0.44、第一与第二名差值 0.06。固定阈值后的独立测试：20 人身份分类 79%，四登记位已知查询拒绝 25%，未知查询误接纳 5.25%，已知查询错认并接纳 1%。详见 `output/speaker_cnn/holdout.json`。这是公开英文干净语料的单个 1.28 秒查询结果，不代表中文、房间噪声、用户麦克风或身份认证准确率；运行时还要求连续三个窗口平滑。
- MFCC 参考与独立实现整数完全一致。HLS C/RTL 联合仿真：MFCC 18,486 周期、CNN 3,662,554 周期，在 50 MHz 分别约 0.37 ms/帧及 73.25 ms/窗口。权重来自实际训练。证据见 `output/speaker_cnn/mfcc_numeric.json`、`hls_validation.json`。
- 完整 AXI 装载、7,076 字读回与校验解锁、20,720 PCM、128 帧和 64 维结果逐项一致，最终量化权重也已通过；见 `integration_sim.json`。48→16 kHz 支路独立测试通过，见 `capture_sim.json`。
- 登记驱动先暂停翻译，并等待网络、TLS 和发送队列均空闲后才启用登记采集；超时、取消、重复、未知和 DAC 播放隔离的实际 C 状态机测试通过，见 `driver_test.json`。四位登记及显示的 C 渲染/点击测试见 `output/speaker_cnn/ui/validation.json`。这些测试使用硬件/传输桩，不代表实板云上传已验证关闭。

登记约需四秒有效语音，按有效窗口累计，不按单纯等待时间推进。模板仅保存在 RAM；断电清空。正常翻译期间的云 STT/TTS 与完全本地的声纹支路是不同数据用途，登记时暂停翻译。

## 平台与实板验收

首次 Vivado 全平台布局失败，权重和特征缓存的多个逻辑读地址导致 LUTRAM 映射：25,934 LUT，超过 17,600。已改成明确的读地址复用和物理双口 BRAM，最终权重的完整仿真重新通过。失败日志及资源报告保留为 `output/speaker_cnn/first_platform_resources.rpt`，不能当作成功平台依据。新的构建仅在实现、时序和哈希校验均通过后发布 bit 文件。

最终平台已通过 Vivado 2018.3 综合、布局布线、DRC 和时序，8,637 / 17,600 LUT、38 / 60 BRAM、30 / 80 DSP，最差 setup/hold 余量 0.212 / 0.033 ns。构建与位流在 `output/z7_nano/speaker_cnn/`，供现有加载器使用的同哈希副本在 `output/z7_nano/remote_display/`，USR_ACCESS 为 `0x5a374e35`。

实板以公开 PCM 验证全部 2,048 个 MFCC、64 维 CNN 输出逐项一致，MFCC/CNN 周期与 HLS 一致，零丢帧/碰撞。先完成逐字 JTAG 校验，再以显式测试模式下的本地 UDP PCM 接口完成新固件重复校验；生产麦克风模式拒绝该接口，它不能开启测试模式或导出模板。见 `output/speaker_cnn/board_numeric.json` 与 `board_functional.json`。纯公开输入累计 71,248 个有效样本、7 个窗口完成登记，同声识别为说话人 1（相似度 99），另一个公开说话人拒为未知，随后清除全部模板并恢复实麦。初次夹具在启动测试模式前包含实麦前缀，已标记 `controlled_fixture_valid=false`，不作为纯公开受控依据。

活跃 ASR 连接进入登记后，确认网络 phase=0、TLS phase=0；八秒中 TLS 发送和 ASR 上传页增量均为零，暂停注入时登记有效语音进度不增长。取消后连接恢复，生产模式拒绝测试 PCM；见 `board_privacy.json`。实际板屏、电脑点击确认和模式切换在 `board_ui.json`、`final_ready.json` 及 `board_*mode.png`。

原链路回归为受控电脑文字 → 实际板上 Qwen → 百炼 TTS → DAC：输入“请把窗户关上。”，输出“Close the windows.”，模型完成 23.81 秒、整段播完 27.13 秒，23,040 PCM 全部播放、零欠载；见 `translation_regression.json`。这不是新的实麦 ASR 准确率或主观听感验收，现有本地翻译仍不是实时同传。

## 使用与重建

板屏或电脑镜像打开 **设置 → 模型与本地声纹**，登记说话人 1–4，连续说话约 4–6 秒至进度完成；可取消或清除。编号只表示本次 RAM 登记位，相似度不是身份概率。播报期间暂停声纹识别，避免识别扬声器；真人、中文及噪声环境效果仍需用户试用，不能用于身份认证。

最新应用目录由 [NOW](../context/NOW.md) 维护；本轮声纹验收应用目录为 `output/speaker_cnn/microphone_udp_test/`，它是 Selftest=false 的真实麦克风固件，工程测试接口默认关闭。正常重新加载运行根目录 `启动FPGA声纹.cmd`；整板断电后用 `断电后启动本地LLM.cmd`。这会重新装载易失的 FPGA、应用、CPU1 和 Qwen 权重；凭据仅输入到运行期。板屏 USB0 供电由加载器开启。登记和云配置断电丢失，未写 Flash/SD。

仓库已携带 `assets/speaker_cnn/model.bin` 固定权重和来源/校验清单，不包含用户登记模板。重建先运行 `scripts/prepare_speaker_firmware.py`，从该已提交模型生成固件 C/header，再以 `scripts/build_speaker_platform.py` 生成平台；音频组件仿真记录应由本克隆的 `scripts/run_audio_loopback.py simulate` 产生，路径为 `output/z7_nano/audio_loopback/simulation.json`。其前置组件仿真及 HLS 证据须与源码哈希匹配。应用使用 `scripts/build_cloud_app.py --mode zhen --speech --arm-tls --arm-tls-packets --arm-tls-active-recv --low-latency-playback --touch --remote-display --tinyqwen --speaker-cnn --output-dir output/speaker_cnn/microphone_udp_test`。模型训练/导出不属于每次烧录步骤，权重已经保存。

语料来源：[Mini LibriSpeech](https://www.openslr.org/31/)、[LibriSpeech](https://www.openslr.org/12/)。工具依据：[Vivado HLS 2018.3 UG902](https://docs.amd.com/v/u/2018.3-English/ug902-vivado-high-level-synthesis)。
