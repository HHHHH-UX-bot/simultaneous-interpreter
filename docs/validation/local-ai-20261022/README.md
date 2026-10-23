# 声纹与离线原型验证（2026-10-22）

对象为本轮从 `51c0349` 新建的桌面副本；运行身份由 [NOW](../../context/NOW.md) 与当前应用自己的 build/load/touch_live 记录维护。使用与算法边界见 [LOCAL_AI](../../design/LOCAL_AI.md)。

## 已执行

- 现有八项音频仿真，Vivado 2018.3 完整网口平台综合/实现/位流通过；setup/hold slack 为 0.212/0.011 ns，引脚、时钟和 DRC 检查通过。ARM/CPU1 使用 SDK 2018.3 构建。
- 实际 C 前端、DTW、UI/触摸和登记逻辑在本机运行：72/72 模板样本、48/48 未入模板的语速样本正确；8/8 库外合成句拒绝；数字静音、低/较高幅噪声不生成字幕。同声匹配、异合成声音拒绝、重复声纹歧义拒绝、超时/取消/清除、会话变化和重连后新登记均有检查。
- 旧设置 39 检查、迟到预览/定稿 3152 检查、译音音量/静音/速度、网口 626103 检查、共用鼠标路径 28 检查与历史 CSV/TXT/JSON 保存回归通过。
- 独立自测 ELF 在真实 xc7z010/Cortex-A9 上运行合成 PCM。中文短句实际形成定稿与英文短句音库播放计数，另一语言的库外候选拒绝；本地时 TLS/云会话启动计数均为 0。DTW 单次匹配约 18 ms、MFCC 特征计算峰值约 4 ms，最终数字保存在本次板测 summary。
- 实板本地主界面与模型/声纹页各读取 614400 像素，与本机同一 C 渲染器逐像素比较，均零差异。电脑显示真实板卡画面，并成功通过鼠标点云端再点本地，两次均获得板端 ACK。
- 自测之后重新加载正式真实麦克风应用、启动 CPU1、恢复 USB 供电及运行期 Wi-Fi/API 配置，实际收到 CLOUD_READY。公开计数的四次就绪检查中上传持续增长，采样溢出、串口错误、TLS 错误和播放欠载均为零。最后加载身份以当前产物和最后检查为准。
- SpeechBrain 1.1.2 的 PC CPU ECAPA 真正推理通过，同一/不同合成声音余弦分数为 0.978969/0.234663，权重固定修订为 `0f99f2d0ebe89ac095bcc5903c4dd8f72b367286`；未宣称裸机运行该模型。

## 本机证据入口

- `output/z7_nano/audio_loopback/simulation.json`，`output/z7_nano/remote_display/build.json`。
- `output/local_ai/native_validation.json`、`output/tts/native/validation.json`、`output/remote_display/native/validation.json`。
- `output/local_ai/board_selftest/build.json`/`load.json`、`output/local_ai/board_validation/summary.json`、各页 `comparison.json` 与 `screen.png`、`remote_ai.png`。
- `output/local_ai/microphone/build.json`/`load.json`/`touch_live.json`，最后真实麦克风就绪记录。
- `output/local_ai/speechbrain/validation.json`、`model/manifest.json`、`speechbrain_runtime/wheel_manifest.json` 与 `installed.lock`。

首轮板测因已有电脑查看器占有 UDP 会话而未能建立第二客户端；本地推理和像素检查仍通过，失败记录保存在 `first_attempt_viewer_lease.json`。后续直接复用已有 loopback HTTP 查看器，真实电脑操作通过，没有放宽板端会话隔离或改写旧失败结果。

## 未验证

没有用户真人登记样本、真人声纹 FAR/FRR、真人离线短句准确率、复杂噪声/回声测试、长时间带声纹并发云同传或新的听觉验收；未制作掉电启动镜像。本地是固定短句声学/字典/音库原型，不能称为任意语音的离线同声传译。
