# ARM 分块 TLS 产品实机验证

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

核验日期：2026-10-21；Z7-Nano 7010、ESP8266、1 Mbaud、CYEA。本轮先验证昨日 `--arm-tls-packets` 候选，随后因长测停更加入可选诊断；未安装依赖或写 Flash/SD/ESP 固件。当前运行与下一步由 [NOW](../../context/NOW.md)维护；昨日单端与纯 TCP 对照见 [原始诊断](../esp-tcp-20261020/README.md)。

## 已完成范围

| 对象 | 本轮结果 | 证据 |
| --- | --- | --- |
| 断电后的板态与装载 | DONE/EOS=0；核对来源后重新加载 PL/PS/ELF | [配置前](hardware_before.json)、[冷加载](cold_load.json)、[实际返回](cold_result.json) |
| 画面与触摸 | 614400 像素零差异；用户确认主按钮/A+、画面正常 | [像素](initial_frame.json)、[用户验收](display_touch_acceptance.json) |
| 不带有效 Key 的 ARM TLS | verify flags=0、握手约 450 ms；随后 network idle/error0、UART 错误0 | [握手及关闭](key_free_tls.json) |
| 固定英文云译 | 300 秒观察、相邻首末快照间隔 298.7 秒；新增 28 条定稿，TLS/UART/上传丢页均0 | [观察序列](fixed_observation.json)、[汇总](fixed_result.json) |
| 快速暂停/继续 | 等暂停命令确认后立即继续，新握手 1→2，会话恢复；未直接测量当时是否处于发送块中段 | [结果](quick_resume.json) |
| 暂停与真实字幕像素 | network/client idle，错误0；保留字幕的整帧仍零差异 | [暂停现场](fixed_paused.json)、[帧](fixed_frame.json) |
| 真实麦克风与字号 | 用户说话后新原文/中文正常，A+ 和画面正常；本次公开计数零 TLS/UART 错误 | [计数](microphone_short.json)、[用户验收](user_acceptance.json) |

固定版 ELF 为 `3f4a2bdc2bfef524ebcb145116f9ee3668c8360e53e5b9788ef7e2f6b8275050`，麦克风版为 `fbd06dcb8cae9f353df8cce1ad72de25f451dee1f9cba31f7da603fd3d5dbd25`。来源见 [固定版构建](fixed_build.json)、[麦克风版构建](microphone_build.json)、[短测加载](fixed_load.json)和[麦克风短测加载](microphone_short_load.json)。编译器路径/项目根仅作公开路径替换，保留原记录 SHA；运行配置只在私有进程和 RAM，未进入文件。

首个 TLS 探测脚本曾在 UI 非 idle 时重复发“开始”而被拒绝；网络配置尚未写入。核对 UI 状态规则后只修正测试起始控制，不修改产品代码。正式无有效 Key 探测使用公开无效令牌，观察证书握手后立即暂停、正常关闭；没有进入付费音频会话。它不是错误域名/过期证书的本板负例，后者仍只沿用昨日原生检查的范围。

## 30 分钟观察（中止，未通过）

同一固定样本版的 43 个观察快照跨 544.1 秒，定稿后段停在 41，上传继续、TLS/UART 未报错；后来无重连自行恢复到 84 条，再受控暂停。[本轮加载](endurance_load.json)、[触摸启动](endurance_touch_start.json)、[观察汇总](endurance_interrupted.json)、[停顿现场](stall_live.json)和[恢复后暂停](stall_paused.json)绑定此次运行。原观察器只检查连接错误，未检查有效输出活性；本次明确不判为长测通过。`cloud_health[3]=13` 是最近事件类型 AUDIO_DELTA，不是错误计数。

## 单次接收诊断（已完成取证）

可选 `--arm-tls-recv-probe` 在 60 秒无真实 TCP 字节且 AT 完全空闲时，沿现有调度器查询一次 CIPRECVLEN，公开前态、返回长度和期间新通知数。不绕过发送事务，不做周期轮询。非零只证明查询时有缓冲数据；若期间出现新通知，不能由此断言这些字节已滞留 60 秒。

本次 41 个快照观察至 606.2 秒，定稿停在 45，末段超过 150 秒无新定稿，上传继续、TLS/UART/丢页仍为零。查询于应用时间 686004 ms 触发，上次真实 TCP 字节为 626004 ms；686008 ms 应答成功，**返回 0 字节、期间新通知 0**。后续又收到少量密文字节，但观察结束时仍无新定稿。受控暂停后的定稿仍为 45。结果不支持这次现场是“ESP 有缓冲但 ARM 未取”，不能据此直接归因服务端，也未宣称修复。

证据：[观察序列](receive_probe_observation.json)、[诊断后暂停](receive_probe_paused.json)、[实际加载](receive_probe_load.json)、[构建](receive_probe_build.json)。[17 项原生传输回归](transport_validation.json)包含单次触发、精确二进制排空、诊断失败后跨会话误归属保护，以及查询期间新通知；代码复核未留 P1/P2。复核者未操作硬件，本板结果由实际运行记录给出。

下一阶段先对照固定音频上传连续性与云回传时序，控制观察频率，并区分文本/译音回传的影响；按证据再修改。重跑长测须检查有效字幕活性，不能只以零 TLS 错误判通过。

## 当前恢复状态

已关闭诊断、重建默认麦克风版并暖加载，ELF 为 `f85d0df10714eb4d6bab4fba7e09193bfe554ee2c06e3b905c36cb6ca0637f97`。与此前已实屏验收的 `fbd06d...` 对比，10 个 PT_LOAD 段的地址、大小、权限与内容完全相同，仅 ELF 调试信息不同，见[加载映像对比](default_image_comparison.json)。恢复后整帧 614400 像素零差异，常驻 CPU1 已启动并取样，CYEA/云端会话就绪，未再播放固定样本。此次恢复后的物理屏幕和说话效果没有新增肉眼验收，先前用户短测验收仍只属于原范围。

[恢复构建](restored_build.json)、[恢复加载](restored_load.json)、[恢复后帧](restored_frame.json)、[配置前空闲状态](restored_paused.json)、[当前就绪计数](restored_ready.json)、[常驻触摸](restored_touch.json)分别绑定本次恢复。

## 边界和接续

CYEA 的本轮结果不表示 OnePlus12 已通过；无 TLS 告警也不是对 ESP 固件确切根因的证明。固定样本、真实麦克风短测、长期运行、听感/声学延迟和独立启动分别记录。未导出私有种子、密钥、密文/音频载荷或真实麦克风录音。

[stage.json](stage.json)保存证据指纹；`historical_source_sha256` 属于加入诊断前的短测，不能要求这些历史哈希匹配修改后的工作树。[normal_reload.tcl.txt](normal_reload.tcl.txt)、[key_free_probe.py.txt](key_free_probe.py.txt)、[quick_resume.tcl.txt](quick_resume.tcl.txt)与[observe_probe.py.txt](observe_probe.py.txt)是本次实际脚本的冻结文本；运行前仍须核对当前所有权和匹配 ELF，不是无条件执行入口。暖加载先停止对应 CPU1/USB，确认普通 AT 模式及无 TCP，再使用 UART_CUR 回到 115200、确认应答后暖加载；没有向未知透传连接发送 AT。
