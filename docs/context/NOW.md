# 当前状态

## 本机：PR #7 合并后的 2026.1 重建与烧录（2026-10-26）

队友 [PR #7](https://github.com/HHHHH-UX-bot/simultaneous-interpreter/pull/7) 已按用户指示合并为 `e38fbe6` 并推送，内容与 PR head `791da60` 相同；其中 15 个文件的 LF→CRLF 行尾改写原样保留，没有规范化。本机只装了 Vivado/Vitis 2026.1，按用户“如果冲突就用2026的”重建，范围见 [D24](DECISIONS.md#d24-合并-pr-7本机以-vivadovitis-20261-重建2026-10-26)。

- 当前板载（JTAG 易失）：PL 为 `output/z7_nano/remote_display/`，Vivado 2026.1，USR_ACCESS `0x5a374e35`，位流 SHA256 `97ddda29282ba4189b3a1cfaa04aff3ba850e1a8f01899d27b52a6312a31fd80`，setup/hold 0.040/0.017 ns；应用为 `output/network_history/microphone/cloud_app.elf`，ARM GCC 13.4.0，SHA256 `78b7c93eb9863c7cab927bf8796aa5ba16601facdfb64be5a90f6050b6136cc9`，Selftest=false；CPU1 触摸主机在运行。下方 10-23 各节的哈希是队友机器上的 2018.3 构建，不是当前板态。
- 已验证：完整加载与显示报告，屏幕 USB 供电且检测到触摸设备连接，声纹加速器模型校验和 MFCC/CNN 周期，Qwen 资产的板上 SHA 复核与引擎初始化，主界面 614,400 像素与本机渲染零差异；五项主机回归通过。
- 未验证：板上 Qwen 推理、云端 ASR/TTS 与译音、实际 Wi-Fi 名称、触摸操作、声纹登记、电脑画面与历史导出。原因：没有输入运行期凭据，电脑有线网卡没有到 `192.168.1.50` 的链路，现场无人。
- 这次 Qwen 资产经 JTAG 写入 DDR 再热重启 CPU0（约 23 分钟）；整板断电后需重新加载。网口链路恢复后应走 UDP 上传。
- 结果与边界见[本轮验证](../validation/merge-pr7-2026-20261026/README.md)。

## 前轮（队友机器，SDK 2018.3）：网络名称与完整历史文本（2026-10-23）

已完成 SDK 2018.3 编译、JTAG 暖加载及实板检查。当前真实麦克风应用为 `output/network_history/microphone/`，Selftest=false，ELF SHA256 `5b89f9e5c962dec83158857ddf0263637063cbc3bd1313b6e5416c42bbfa0a3a`；保留声纹 PL `0x5a374e35` 与原板上 Qwen，模型和分词器均已校验。根目录三个本地 LLM/声纹启动入口已指向此应用，整板断电后仍需重新加载和输入运行期配置。

板屏主界面、设置、声纹及历史页，以及电脑 8787/8790 两个网页均显示当前 Wi-Fi 名称。名称取自 ESP 实际 `AT+CWJAP?` 关联响应，断开时清除；不读取密码用于显示。实际 CYEA、未连接状态、四页零缺字/零裁剪、两个 CPU 和 USB 屏幕供电均已检查。

CSV/TXT/JSON 保留逐条定稿，并在末尾追加完整原文与完整译文；按会话、翻译方向、转折词及段落长度分段，不调用模型改写原句。原生渲染/协议、三格式与另存为测试通过；另用两句受控文字完成真实板上翻译及导出逐字核对。最多合并当前 RAM 保留的 64 条，旧记录存储范围不变。最终证据见 `output/network_history/native_validation.json`、`network_display_validation.json`、`history_export_validation.json`；实现与复测入口由 [REMOTE_DISPLAY](../design/REMOTE_DISPLAY.md) 维护。

## 前轮：FPGA MFCC + CNN 本地声纹（2026-10-23）

已完成并下载运行。声纹 MFCC 与 CNN 均在 PL，四位 RAM 模板及匹配在 ARM；板屏与电脑镜像共用登记和识别状态。Vivado 2018.3 实现/时序、最终训练权重、全部 2,048 MFCC 和 64 维嵌入的实板逐值检查、纯公开语音的登记/同声/异声/清除、活跃云连接下登记关闭上传均通过。登记时真实 TCP/TLS 关闭，八秒观察中上传页与 TLS 发送均未增加，取消后恢复连接。早期布局映射失败和测试夹具先混入实麦前缀的记录已保留，不作为最终受控证据。

该轮验收的真实麦克风固件为 `output/speaker_cnn/microphone_udp_test/`，Selftest=false；ELF SHA256 `50417d2902655a42cd874aa482ae985754b7e8b33fcb96987be197560b431123`，PL SHA256 `910a6eff383b17d413f6a5ae85fcfdaf7ba80ef22177fd2ac4d572607f0021f3`、USR_ACCESS `0x5a374e35`。Qwen 全权重与分词器已重新校验、CPU1 触摸运行。新固件受控文字“请把窗户关上。”得到板上译文“Close the windows.”，23.81 秒模型完成、27.13 秒全部播完，23,040 PCM 收放一致、零欠载。最终就绪状态由 `output/speaker_cnn/final_ready.json` 维护；用户真人声纹准确率、噪声及主观听感仍需实际使用检验。入口、模型测试限制和全部阶段证据由 [LOCAL_SPEAKER_CNN](../design/LOCAL_SPEAKER_CNN.md) 维护。

## 前轮：STT、板上 Qwen 与 TTS（2026-10-23）

已恢复正式实麦固件 output/voice_local/microphone/cloud_app.elf，Selftest=false；当前 build/load/touch_live 身份匹配，USB 屏幕供电、CPU1 心跳和电脑云端/本地点击 ACK 已复核。链路为百炼 ASR → 板上 Qwen Q6 翻译 → 百炼 TTS → WM8731；电脑不执行模型。

现行固件连续 12 / 12 轮文字 → 板上模型 → TTS → DAC 播完，覆盖换句与长句，逐轮 PCM 收放一致、零欠载。双语受控音频也完成识别、板上翻译与播放。TTS 等待 response.done 后再发送 finish，并恢复 1.28 秒起播缓冲；旧版无首包及 320 毫秒缓冲欠载记录保留，不作通过依据。

中文识别定稿到起播，首轮 43.48 秒、重复同句 15.83 秒。新文字提交到模型完成 22.56 秒、全部播完 27.48 秒。已做精确前缀 KV 缓存、四 token 批量预填充、短提示和 TTS 准备重叠。仍不是实时同传，译文可能不自然，语音依赖云端。完整证据与限制由 [LOCAL_VOICE_LLM](../design/LOCAL_VOICE_LLM.md) 维护。

## 前轮：板上 Qwen 文本基线（历史）

用户明确要求停止以固定短句代替本地模型，先部署能处理新文本的真实 LLM。本轮采用 Qwen2.5-0.5B-Instruct 全矩阵 Q6，在 Cortex-A9 裸机运行，电脑只负责输入/显示。SDK 2018.3 构建、FPGA/CPU0 加载及 CPU1 触摸已通过；401,637,888 字节权重及分词器已传入 DDR 并校验。三次新文本实板推理和一次网页服务提交均完成，约 65–77 秒/条；英译中测试存在位置关系误译，且与原始 BF16 参考一致。当前已实现真实文本 LLM 部署，尚未接入离线 STT/TTS，也不具备实时同传速度。现场结果见 `output/tinyqwen/microphone/board_validation/deployment_summary.json`。新入口、限制和证据路径见 [BOARD_LLM](../design/BOARD_LLM.md)。

## 前轮：声纹与板上离线原型（历史，2026-10-22）

用户授权在桌面新建工程，适配 Vivado 2018.3，加入说话人声纹、板端与电脑共同的云端/本地切换，并自动编译和 JTAG 下载。本轮副本 `simultaneous-interpreter-local-speaker-20261022` 从上游 `51c0349` 克隆，未覆盖前轮桌面工程。以下前轮章节仅保留历史范围，不代表当前板载身份。

- 新入口为 `scripts/build_local_ai.ps1`、`scripts/load_local_ai.py`；正式应用、匹配 build/load/touch_live 和现场检查在 `output/local_ai/microphone/`。PL 平台为 `output/z7_nano/remote_display/`，仍为 2018.3、xc7z010clg400-1、USR_ACCESS=0x5a374e34。
- **设置 → 模型与声纹** 可切云端自由对话或本地 12 短句；实体触摸与电脑鼠标共用 C 状态。四个登记位、3 秒有效语音进度、超时/过短/未知状态已实现。声纹只在内存，与已登记声音作实验性相似度比较，不宣称真人身份准确率或认证能力。
- 板上本地采用整数 MFCC 与 DTW 声学模板、固定英中对应及 24 kHz 短句音库，模型为 38,916 个 int16 模板值。它不识别任意句子，也不执行开放式机器翻译或通用 TTS。切本地后停止云上传；模式、方向与会话变化隔离未完成音频。
- 音频仿真、2018.3 实现/时序、ARM 与 CPU1 构建、本机实际 C 推理与 UI 回归已通过；72 个模板样本、48 个未入模板的语速样本识别正确，8 个库外合成句拒绝。声纹的同声/异声/重复登记歧义、取消、超时和云重连后登记回归通过。这些不是真人准确率数据。
- 实际 JTAG 下载、本地 ARM 推理、DAC 短句播放计数、两页各 614400 像素零差异及电脑鼠标切换均有本轮证据。最终恢复真实麦克风/云端。最新身份和分项结果以本轮 `output/local_ai/board_validation/summary.json`、正式应用 `build.json`/`load.json` 与最后就绪记录为准；公开摘要见[本轮验证](../validation/local-ai-20261022/README.md)。
- SpeechBrain 1.1.2 / ECAPA 已在项目隔离的电脑 CPU 环境运行，固定权重修订和哈希保存在 `output/local_ai/speechbrain/model/manifest.json`；同一/不同合成声音得分约 0.979/0.235。它是独立的电脑文件对照，不承担板端实时声纹推理。
- 程序、配置、声纹和历史仍为 JTAG/RAM 易失状态，未制作独立启动镜像。真人登记、房间噪声、自由说话时的离线短句可用率、长时间云端并发和听感仍待用户实际试用；不借合成样本或像素比对宣布这些通过。

完整实现与使用方法由 [LOCAL_AI](../design/LOCAL_AI.md) 维护。

## 前轮：桌面译音、设置与连续长句优化（历史）

用户本轮要求桌面新建工程，适配Vivado 2018.3，完成设置、中英双向译文TTS、保持UI风格并显示延迟，自动编译和JTAG烧录。新目录为桌面 `simultaneous-interpreter-tts-20261021`，从上游 `940e2a6` 开始，旧桌面工程未覆盖。本轮授权替代原D19仅交接/本地回环范围，见[D20](DECISIONS.md#d20-桌面译音与设置工程2026-10-21)。


后续用户反馈长句后半段译音延迟约4–5秒，本轮优化下行接收、明文解析和起播缓冲，保留默认语速和音质。范围见[D21](DECISIONS.md#d21-译音延迟优化2026-10-21)，实测与保留限制见[延迟验证](../validation/tts-latency-20261021/README.md)。

连续较长多句的字幕/译音卡顿已按用户新指令继续优化并自动测试，范围见[D22](DECISIONS.md#d22-连续六句的字幕与译音优化2026-10-22)。

## 前轮板载版本（已由本轮替换）

2026-10-22合并前复核：远程点击/快照上下文及小幅滚轮的三处P2已修正，独立复核和原生/ARM/音频仿真通过，见[PR #6复核](../validation/pr6-review-20261022/README.md)。本次未重新烧录；下列板载指纹和实屏结果仍是队友修正前的原阶段记录，不能当作新源码已在板验收。下一次加载须重新构建并按实际身份恢复，不改旧产物哈希求通过。

2026-10-22新增PL网口电脑实际画面、鼠标控制和历史CSV/TXT/JSON另存为，用户范围见[D23](DECISIONS.md#d23-pl网口电脑画面与历史保存2026-10-22)，构建/网络/保存方式见[REMOTE_DISPLAY](../design/REMOTE_DISPLAY.md)，证据见[本轮验证](../validation/remote-display-20261022/README.md)。此前译音/六句版本仍保留独立产物，不与本次加载身份混用。

- Z7-Nano / xc7z010clg400-1，JTAG线缆210241277120；KD070V02 1024×600，CPU0负责UI/采音/ARM TLS/云连接，CPU1常驻USB触摸。
- 正式应用 `output/remote_display/microphone/cloud_app.elf`，SHA256 `dc0f82a78a88a01f0a10c7e8d30335be278522b8c55fdaea2e111d63f85b61d0`。RemoteDisplay=true、Selftest=false、LatencyProbe=false、EventIDTrace=false；真实麦克风，默认中文译英文，可切英译中。ARM TLS确认上传、主动IPD、2Mbaud、Opus O2及1.28/2秒自适应缓冲，默认语速/音质保持。
- CPU1每毫秒服务USB/触摸。PL为Vivado2018.3新增100Mbps Etherlite/RGMII位流，USR_ACCESS=0x5a374e34、SHA256 `855b4fb689a80629f79eb73ed38004d2face18739c2803ab5249f9afa563d623`，时序0.212/0.011ns。音频逻辑和原UI风格保持，电脑传实际RGB像素，并将点击/拖动交给共用UI处理。
- 同一40.129秒六句样本，两次最终产品配置均13条识别/译文/译音、原文匹配1.0、全部PCM播完、单次TLS、零网络故障/恢复、丢页、欠载、串口或解析错误。最后译文尾部6.89–8.47秒，全部译音尾部11.27–13.08秒；1M选定完整epoch对照10.789/14.061秒。保持1.0x仍会因译音长度超过输入而排队。
- 新网口平台同时显示时，40.129秒六句测试13条ASR/译文及13段译音全部完成，1,056,000PCM样本全部播放，原文匹配1.0，单次TLS、零丢页/欠载/串口及网络故障；最后译文6.604秒、全部DAC尾部12.562秒。网口PNG与DDR614400像素零差异，导出13条记录与独立板上快照逐字一致。
- 最终真实麦克风就绪至 `2026-10-22T08:24:16.187978+00:00`：network4/error0、client4、TLS2/error0、USB PP1，上传314→540，CPU1心跳96121→132407。无故障/恢复且仅1次TLS握手；真实麦克风长句听觉反馈仍独立验收。
- 当前证据为新正式build/load/touch_live/final_ready和[网口验证](../validation/remote-display-20261022/README.md)，此前[长句验证](../validation/long-speech-20261022/README.md)单独保留。失败复测、调试读取干扰、云会话波动和计时边界完整保留；不能将合成测试当作真人声学P50/P95。

## 已实现与已验证

- 独立设置页：播报开关、0–100%数字音量、静音、0.8/1.0/1.2x速度、字号、显示原文、翻译方向。开关页不重连、不清历史；保留原浅色、圆角、青绿色UI。偏好只存RAM。
- 百炼同一密钥已实测支持标准TTS，无需新账号。板端实际用LiveTranslate的文本+目标语音输出，DAC不再本地环回；中英两方向固定样本云端/板端译音均有独立证据。
- 主界面显示L1 LOOP、T1 TEXT、T2 VOICE及播放状态；T2是首段PCM收到到DAC起播，不能当成完整声学延迟。
- 八项音频仿真、Vivado2018.3实现与时序、SDK2018.3 ARM/CPU1构建通过；设置39检查、事件顺序3152检查、96条正常定稿与空结果检查通过；20组播放/传输回归、500条英译中与历史377检查通过。
- 设置页/主界面各614400像素与原生渲染零差异，缺字/裁剪0；真实控制路径检查音量/静音/速度且epoch保持。用户明确确认“已听到译音，音量正常”。手指逐项设置与每档音量/速度听感没有逐项验收。

完整实现由[译音与设置](../design/TTS_SETTINGS.md)维护；分项证据、旧故障与修正范围见[本轮验证](../validation/tts-settings-20261021/README.md)。

## 故障修正与保留边界

本轮真实输入再次复现R28。局部修正迟到预览覆盖final/重建旧槽位、空ASR final被解析器拒绝、遗漏response.done最终译文的路径；空结果明确计数，不补造原文或历史。有效云错误码不再因说明文字为中文而被忽略。起播改为先填实际DAC FIFO，避免只有21ms硬件缓存的欠载窗口。诊断版真实输入接受1个空ASR、14条正常历史且队列排空、616320PCM样本全部播放、无故障；之后恢复上面的正式无事件诊断版。

这些是本次复现的局部修复，不宣称R28/R29全部成因、双向长期可靠性、代表性准确率或完整声学P50/P95已经放行。已有云端UNEXPECTED_ASR_ERROR等风险仍见[RISKS](RISKS.md)，历史失败证据保留原结论。

## 使用入口

- 根目录 `启动FPGA声纹.cmd`：重新 JTAG 加载当前声纹/网络名称固件并输入运行期 Wi-Fi 和百炼配置；整板断电后用 `断电后启动本地LLM.cmd`。当前屏幕 USB 供电已开。这些入口默认调用 SDK 2018.3 的 xsct/hw_server；只有 2026.1 的机器需改用 `scripts/load_tts.py --xsct <Vitis 2026.1 xsdb>`，再用 `scripts/configure_cloud_runtime.py --xsdb <同一 xsdb>` 输入凭据，并先启动 2026.1 的 hw_server。
- 根目录 `打开电脑画面.cmd`：启动本机服务并打开 `http://127.0.0.1:8787`，点击/拖动操作同步到板端；下方导出按钮可选CSV/TXT/JSON和保存位置。导出当前RAM保留最多64条定稿，文件在电脑独立保存。
- `scripts/build_tts.ps1` 重建软件与CPU1；`-RebuildFpga` 先做音频仿真再运行Vivado实现。加载用 `scripts/load_tts.py --configure`。
- 设置、历史、凭据与JTAG程序断电丢失；未写Flash/SD/ESP固件。源码、日志、工程备份不含密钥/无线密码或真人原始音频。
- 新旧构建与符号不能混用；调试/换固件先查实际板态和加载身份。此前译音/长句源码已提交d91171d；用户进一步要求本次网口、电脑操作及历史导出源码一并提交，沿用[PR #6](https://github.com/HHHHH-UX-bot/simultaneous-interpreter/pull/6)；PR #6 已于 2026-10-22 合并为 `51c0349`，PR #7 已于 2026-10-26 合并为 `e38fbe6`。

入口规则见[AGENTS](../../AGENTS.md)，工具/目录见[MAP](MAP.md)、[RUNBOOK](RUNBOOK.md)，代码路由见[PROJECT_INDEX](../../PROJECT_INDEX.md)。
