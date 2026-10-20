# 中译中实时字幕与共享 TLS（2026-10-20）

## 使用范围

本模式将中文麦克风音频编码为 Opus/Ogg，调用独立 `qwen3-asr-flash-realtime`，将 ASR 中文原文显示到屏幕，保留 400 ms 静音断句、128 帧队列和最多 20 帧/页的积压追赶。它不请求英文译文或译音。串口和 TLS 修复沿用 [ZHEN](ZHEN.md) 的架构，ESP8266 只转发 TCP，Zynq 负责经过证书链、域名和日期验证的 TLS 1.2。

当前候选使用显式参数 `--mode zhzh --arm-tls --uart-baud 1000000`。原有命令默认值保留，不因切换文档入口而改变旧模式行为。共用的网络能力通过 `Z7_STREAM_TRANSPORT` 启用，与译音开关解耦；中译英仍启用同一组能力。固定 DDR 内存、随机种子、认证和心跳实现共用，不维护第二份 TLS 库。

用户授权保存桌面工程并创建 PR；本次提交没有重新操作板卡、没有合并 PR 或固化启动介质。当前板上保持此前按用户要求加载的中译中麦克风副本。

## 实现入口

- [network.h](../../software/network_ui/network.h)、[network.c](../../software/network_ui/network.c)：共享传输开关、AT/TCP 调度、部分通知/发送回复边界、故障恢复和实际分频校验。
- [main.c](../../software/network_ui/main.c)、[wifi_recover.c](../../software/network_ui/wifi_recover.c)：冷/暖 UART 速率探测和有界恢复；恢复无需开启英文译音。
- [build_cloud_app.py](../../scripts/build_cloud_app.py)：允许中文 ASR 使用 ARM TLS；独立输出目录防止两种模式混用固件和符号。
- [cloud_client.c](../../software/network_ui/cloud_client.c)、[cloud_live.c](../../software/network_ui/cloud_live.c)：ASR 会话、原文字幕和 TLS 数据/Ping/Pong，复用已合并的共享实现。
- [validate_zhzh_transport.py](../../scripts/validate_zhzh_transport.py)、[check_zhzh_microphone_ready.py](../../scripts/repair/check_zhzh_microphone_ready.py)：原生传输回归和已知麦克风固件的公共计数观察。

## 验证与来源

证据统一见 [docs/validation/zhzh-20261020](../validation/zhzh-20261020/)。必须区分两种对象：

1. **实际加载副本**：用户桌面中译中工程经修复，麦克风 ELF SHA256 为 `236f49eab6934c414ce81a091e3df1484808fb45b2693052f93bc7ea0b1dd128`。实际 JTAG 加载、显示报告、音频时钟和 USB0 PP=1 通过，配置返回云连接就绪。随后 4 次公共计数观察覆盖约 35 秒，上传页数 435→654，单个 TLS 会话，网络/云端/TLS/UART 错误、丢帧和采集溢出为零。观察期间没有识别定稿；这是麦克风采集/上传就绪检查，不是识别质量或长测验收。
2. **本 PR 检出**：在已合并的中译英共享代码上启用 ASR 传输能力，保留两种模式的现有功能。它不是将整个中文旧副本覆盖到主分支；文件布局、额外诊断和产物哈希会不同。这里的原生回归及 SDK 2018.3 构建另行记录，未再次加载，不能用旧副本的板测哈希冒充本 PR ELF 已板测。

PR 检出的验证结果由上述证据目录中的 `checkout_*` 记录维护；本地完整保存版另含已加载源码、匹配固件/符号、必要生成输入和原中译中位流/PS 初始化。Git 不保存二进制、字库产物、API 密钥或热点密码。

仍待核实：长时间连续实际说话、真人准确率、完整声学端到端延迟、独立上电启动及触摸。T1 仍为服务端 VAD 触发到首字；不能将其当作完整端到端延迟。中译英的 92 次合成输入板测不替代中译中验收。

## 构建与使用

按需读取 [RUNBOOK 的中译中入口](RUNBOOK.md#中译中共享-tls2026-10-20)。新检出须准备平台头、中文字库；自测还需要已有固定 PCM。ESP8266 使用 2.4 GHz 网络；主机仅通过进程环境在运行时传入凭据、一次性随机种子和 UTC。后续再次加载仍依当次授权，不因文档列出命令而自动执行。
