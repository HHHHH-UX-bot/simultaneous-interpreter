# PS 自主网络模拟字幕

适用范围：本文维护旧 RevB W4-1 LAN 模拟字幕协议与当时工具，不能按它的“电脑连接 ESP/无真实采音”推断当前产品。现行 Z7-Nano 麦克风→ARM TLS→百炼与 CPU1 触摸看 [NOW](../context/NOW.md)，旧协议及证据保留其对象范围。

核验对象：2026-10-11 的 W4-1 文本试验。它把局域网电脑提供的已知模拟结果送入现有中文模型和 HDMI 双缓冲；没有采样声音或执行识别/翻译模型。平台、引脚和 UART 寄存器分别沿用 [PS_WIFI](PS_WIFI.md) 和 [WIFI_LINK](WIFI_LINK.md)，显示/模型权威来源为 [DYNAMIC_UI](DYNAMIC_UI.md)。当前阶段进度以 [NOW](../context/NOW.md) 为准。

## 软件与所有权

应用入口 [main.c](../../software/network_ui/main.c) 保留已验收显示程序，加入独立 AT 探测及 [network.c](../../software/network_ui/network.c) 协作任务。关联和服务器准备好后，由网络结果接管字幕，停止预设演示计时器。CPU 是 UART 的唯一事务所有者；JTAG 仅启动应用、读取公共状态和提交一次私有配置。

新应用及证据在 `output/network_ui/`；不覆盖 `output/ps_wifi/` 的自主 AT 历史或原动态中文证据。`output/ps_runtime/active_app.json` 表示当前物理 PS 软件所有者；实际操作还需核对本应用的运行/配置尝试、快照、ELF、位流和真实 FPGA 状态，不能只信该文件。所有硬件工具共用 `tmp/ps/operation.lock` 和已有跨输出故障保护。

## 配置及公共状态

公共 `NET1` 状态位于 OCM `0x1e140`，16 个 u32：magic、奇偶序列、phase、error、config_ack、STA IPv4、client、accepted、rejected、last_request、last_code、received_bytes、overflow、framing、echo_verified、epoch。phase 0 是等待配置，1 是设置中，3 是服务器就绪，4 是已有客户端，6 是故障。客户端为空时为 `0xffffffff`。IPv4 字段按网络八位组顺序组成 u32。观察器要求公共序列实际前进并与 UI epoch 对齐。

私有 128 字节 `NCF1` 配置位于 `0x1e180`：[network.h](../../software/network_ui/network.h) 定义固定布局。凭据仅经标准输入和私有调试进程管道送到运行时 RAM；不作为命令参数，不写配置文件，不读回或记录私有区。提交先写内容、最后更新请求序列；CPU 消费后立即清空凭据区与长度。当前工具只接受新启动、phase 0 的应用。

无线设置依次执行 ATE0、重新 GMR 验证、关闭旧服务器、SYSSTORE=0、CWMODE=1、CWJAP、CIFSR、CIPMUX、CIPDINFO、CIPRECVMODE 和 TCP 服务器。发送凭据前要求新的 GMR 同时具有 AT/SDK 版本且没有命令回显，并要求 SYSSTORE=0 明确成功；只看到 ATE0 的 OK 不足以放行。关联采用易失参数。AT 回复没有密码学事务标识；该方法的证据范围为串行、正常响应的 AT 模块，不能认证任意注入的旧完整版本响应。

当前试验在用户已确认的 `192.168.31.0/24` 局域网，端口 8266。没有登录路由器、调整防火墙、修改 ESP 固件或写入启动介质。

电脑必须具有到模块的实际局域网路径。2026-10-11 一次复验时，电脑 WLAN 已在另一网段，连接模块的路由选中了 xray_tun 默认路由，TCP 超时且板端没有新连接/请求；这不是模块关联失败。用户把电脑接回 CYEA 后，实际路由恢复为 WLAN 的 `192.168.31.0/24`。见 [路由对照与恢复](../../output/network_ui/diagnostics/lan_route_recovery.json)。诊断没有修改系统代理、防火墙或全局路由；普通 ping 应答不足以证明 TCP 字幕通道可达。

## 文本试验协议

正式产品的 WebSocket/音频方案仍是 [INTERFACES](INTERFACES.md) 草案。本阶段使用独立版本 1 TCP 二进制协议，[protocol.h/c](../../software/network_ui/protocol.h) 是实际实现；不能把它称为已实现 WebSocket 或音频传输。

请求头为 36 字节、9 个小端 u32：`SUB1=0x31425553`、version=1、request、type、epoch、segment、revision、source_revision、payload_bytes；随后仅跟该长度的 UTF-8 文本。

| type | 实际含义 |
|---|---|
| 0 | HELLO，无正文；回复当前 epoch 和历史计数 |
| 1 | UI 测试控制，segment 承载 ui_command 枚举，其余正文/版本字段为 0 |
| 2 / 3 | ASR 模拟草稿 / 定稿，revision 标识原文版本 |
| 4 / 5 | 模拟翻译处理中 / 中文定稿，source_revision 必须匹配已定稿原文 |

当前正文最多 768 字节、255 个 Unicode 标量，只接受现有七种字号都覆盖的演示字库字符。非法 UTF-8、控制字符、空文本、超限、未知字形、旧 epoch、重复请求或违反模型顺序的结果分别拒绝；不能把演示字库当作完整中文字体。

确认包为 32 字节、8 个小端 u32：`ACK1=0x314b4341`、version、request、type、code、current_epoch、history_count、final_count。code 0 成功，1 头部、2 长度、3 UTF-8、4 字库、5 epoch、6 模型、7 请求序列、8 忙。电脑等待完整且匹配的 ACK 后才发下一个请求。当前连接请求序列单调递增；新连接从 1 开始，epoch 从 HELLO/控制 ACK 获取。

## 边界与恢复

`+IPD,id,length:` 正文按声明长度接收，不把正文内的 OK、ERROR、FAIL、`>` 当 AT 应答。CIPSEND 必须取得实际发送提示，完整发送 32 字节 ACK 后等待 SEND OK。一个正在确认的包之外只允许一个有界后继包；它不是无限队列，也没有音频吞吐证据。

正常 TCP 断线释放客户端并暂停字幕，保留中文定稿与历史；重新连接后使用显式恢复控制，取得新的 epoch，再提交新结果。Wi-Fi 丢失、模块重启、过期事务、溢出或帧错误停下网络步骤。未读完 IPD/帧的故障保持接收边界，不允许配置重试清空它；应按已授权的 `program` → `run` 流程重新启动应用，不盲目解释残留字节。

网络服务为开发局域网文本试验，没有产品级客户端认证、加密、自动无线重连或可靠持久会话。115200 的音频带宽缺口见 [SYSTEM_PLAN](SYSTEM_PLAN.md#8-资源带宽与延迟预算)；当前文本结果不能替代 W4 音频验收。

## 已定位的 ARM 配置异常

2026-10-11 的关联配置提交后，没有取得消费确认。确认停止 CPU 后读取的公开诊断为：PC `0x10078`（异常循环）、CPSR 模式 `0x17`（Abort）、LR `0x11834`、DFSR `0x801`（写入对齐异常）、DFAR `0x18975`（奇数地址），SCTLR 的 MMU/cache 为关闭。对应旧 ELF 的 `0x1182c` 是将指令结尾 CR/LF 写入可变字节位置的 `STRH`。原始公开证据见 [故障寄存器](../../output/network_ui/diagnostics/alignment_fault.log)；不包含凭据内容或私有区读回。

修复在 [ARM 构建入口](../../scripts/run_network_ui.py) 加入 `-mno-unaligned-access`，与现有 MMU 关闭的裸机环境匹配；[GCC 官方说明](https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html)定义该选项禁止生成未对齐的半字/字访问。[Cortex-A9 官方手册](https://documentation-service.arm.com/static/5e8e2743fd977155116a5ea9)说明 DFSR 对齐故障编码。该修复保持 C 逻辑、PL 位流和显示时序。单独修正编译设置的真实运行已取得 ConfigAck=1 与持续进度，随后模块在 CWMODE_CUR 阶段明确拒绝指令；两类故障分别定位，不混作一个问题。

当前固件实际报告为 AT 2.2.2.0-dev / Bin 2.2.1。后续改用原 [Wi-Fi 主机工具](../../scripts/wifi_link.py) 已有的现代 AT 分支：必须先确认 SYSSTORE=0，再设置 CWMODE=1 并发送 CWJAP。[ESP-AT 2.2 官方说明](https://docs.espressif.com/projects/esp-at/en/release-v2.2.0.0_esp8266/AT_Command_Set/Basic_AT_Commands.html)定义易失参数模式。离线模块替身现在只接受明确的支持指令，未知与旧 _CUR 指令返回 ERROR，并验证易失设置失败时没有私有 join；不再让任意未知指令得到模拟 OK。新的板上关联仍单独记录。

调试恢复曾另有目标属性缺失问题；现在只在完整电缆序号和唯一核心身份核实后选择目标，缺失属性有界等待，错目标拒绝。该修复已有独立模拟检查及真实停机证据；不能将其当成此次 ARM 对齐故障的原因。私有配置失败只记录允许的阶段、CPU 状态和原因枚举，工具原始输出与错误文本丢弃。

## 分阶段操作与证据

以下方法已实现；方法本身不构成执行授权。

1. `validate_network_protocol.py`、`validate_network_runtime.py --gcc-directory <现有 native GCC 目录>`、`validate_network_client.py`：协议、异常串口、私有输入与电脑分帧离线检查。
2. `run_network_ui.py build --gcc <现有 ARM GCC>`：核对前置证据与独立平台，构建并检查 OCM/DDR/公共与私有邮箱布局。
3. `run_network_ui.py program` → `run`：沿用已验证平台，实际停止旧 CPU/DMA、配置、初始化并启动。每次消费一次配置令牌；失败后按保护门闩恢复。
4. `network_ui_debug.py sample`：独立 AT 和网络 phase 0 必须与当前执行、真实平台及持续显示一并核对。
5. `network_connect.py --credentials-stdin`：运行时输入一次，取得公共消费确认，再验证 phase 3、echo_verified、STA 地址和错误计数。该输入不会落盘。
6. `network_demo_client.py --gcc-directory <native GCC 目录>`：电脑发送模拟草稿/定稿与随机原文，核对完整 ACK、旧数据拒绝、暂停/恢复和 TCP 断线重连；抓取不可变板端模型并逐像素重建，实屏仍由用户验收。

当前离线证据：[协议检查](../../output/network_ui/protocol_tests.json)、[运行时检查](../../output/network_ui/runtime_tests.json)、[电脑端检查](../../output/network_ui/client_tests.json)、[ARM 构建](../../output/network_ui/build-app.json)、[当前对齐/现代 AT 评审](../../output/network_ui/review/alignment_review.json)、[当前工具评审](../../output/network_ui/review/frontend_review.json)。[初版运行时评审](../../output/network_ui/review/review.json)保留原源码范围。板上结果及用户验收另行记录，不能从离线文件推定通过。

2026-10-11 新应用的自主关联与 TCP 服务器通过：[关联结果](../../output/network_ui/connection.json)、[配置](../../output/network_ui/program.json)、[运行](../../output/network_ui/run.json)。公共状态 phase=3、config_ack=1、echo_verified=1，溢出与帧错误均为零；动态租约地址以关联结果为准。网络模拟字幕和新实屏验收仍独立验证，关联成功本身不是结果上屏证据。

W4-1 完整试验已通过：[23 个请求及模型/像素证据](../../output/network_ui/pilot.json)、[用户实际屏幕验收](../../output/network_ui/user_acceptance.json)。18 个接受、5 个预期拒绝，3 条定稿；4 份实际帧缓冲共 2457600 像素与同状态重建完全一致。暂停/恢复、字号、错误输入保护、断线及重连后的完整历史通过；最终 TCP 客户端已关闭，服务器保持就绪，界面保留最后一条译文。用户确认画面正确、清楚稳定。当前请求内容为明确标注的模拟数据，尚无音频采样/真实翻译、连续吞吐或 USB 触摸证据。
