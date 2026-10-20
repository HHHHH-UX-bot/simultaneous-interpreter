# ESP8266 透传与确认分块对照

核验日期：2026-10-20。专用 ARM 程序、UART 1 Mbaud、ESP8266、电脑 TCP 接收端；没有 TLS、云模型、UI、USB Host 或真实音频任务。产品当前状态与暂停入口见 [NOW](../../context/NOW.md)。本次没有写 Flash、SD 或 ESP 固件。

## 真实板上结果

| 路径 | 输入 | 结果 | 证据 |
| --- | ---: | --- | --- |
| 透传、连续发送 | 8 KiB | 收满但 40 字节不同 | [失败现场](transparent_short.json)、[替换分析](replacement_analysis.json) |
| 透传、8 字节/ms | 64 KiB | 全部一致 | [结果](transparent_paced_64k.json) |
| 透传、8 字节/ms | 1 MiB | 共同前缀首差异 141580，未通过 | [中间现场](transparent_paced_1m_intermediate.json)、[稍后发送完成](transparent_paced_1m_completed.json) |
| CIPSEND 1024、提示、SEND OK | 8 KiB | 全部一致，约 46 kB/s | [结果](packet_8k.json) |
| 同上 | 256 KiB | 全部一致，约 55 kB/s | [结果](packet_256k.json) |
| 同上 | 1 MiB | 全部一致，约 55 kB/s | [结果](packet_1m.json) |

三组分块试验的板端 FNV、载荷计数、UART 硬件发送数（含每块 17 字节命令）均核对；溢出和帧错误为 0。结果同时确认测试结束后普通 AT 模式、TCP 已关闭。1 MiB 文件沿用 `paced1m` 名称，但板端 `report[6]=2`，实际为逐块确认调度，不是 8 字节/ms；结果字段 `paced=1` 来自旧样本清单，不描述此轮调度。接收速度按实际收到数据的区间估算，不含首段和 JTAG 编排时间，不是产品吞吐承诺。

首次透传的 2880–2919 字节恰好等于输入 4340–4379，距离 1460 字节。这支持继续排查 ESP 接收/转发缓冲的假设，尚不能确认具体厂商缺陷，也不能单凭数字认定物理电气无关。单端分析仪负载下的波形通过见 [UART 实测](../uart-wire-20261020/README.md)。

## 失败与清理边界

1 MiB 透传接收超时处，板端仍在发送；当时清理脚本立即尝试 `prepare`，被状态 2 的所有权检查拒绝，所以该次退出没有成功。后续等待专用发送程序完成全部 1048576 字节，再由 ARM 连续发出 `+++` 并确认 AT、模式 0 和无活动 TCP，见 [恢复记录](recovery.json)。首个短测的 JTAG 逐字节退出也未确认，原始失败保留。没有把未收到的尾部或中间快照旧计数当作全部线损。

以下 `executed_*` 文本是**实际执行脚本的冻结证据**，不是建议重跑入口；历史驱动的上述清理缺陷未被隐去。若再做透传对照，应先修正驱动：在有界等待发送完成或明确停止后才退出，并分别保留传输与清理状态。不要在状态 2 时绕过板端保护。

## 源码与装载绑定

[stage.json](stage.json)绑定复制证据的 SHA。[packet_build.json](packet_build.json)绑定实际 mode=2 ELF 和源码；[packet_load.json](packet_load.json)、[加载返回](packet_load_console.txt)和[实际加载脚本](executed_packet_reload.tcl.txt)记录从专用诊断程序的完成态暖加载、CPU1 停止、PL 未更换。编排与私有 UART 桥的实际源文本分别为 [driver](executed_tcp_driver.py.txt)和 [RPC](executed_uart_rpc.tcl.txt)；只保留公开样本及元数据，未录制凭据。构建记录的 scope 沿用了“no network”旧描述，准确含义是没有网络栈/产品网络任务：mode=2 会向已经由主机建好的 TCP 连接发送 AT 分块命令。

## 产品候选：未上板

`build_cloud_app.py --arm-tls --arm-tls-packets` 是本轮新增的显式候选，复用现有 MUX1/被动接收/CIPSEND/SEND OK 状态机，TLS 认证仍在 ARM，BIO 每块最多 1024 字节。原透传构建方式保留。暂停会记住关闭请求、等当前 AT 事务结束后关闭；即使立即点继续也先关闭旧连接，再启动新会话。

离线证据为 [传输回归](product_transport.json)、[真实 TLS 引擎检查](product_tls_engine.json)、[ARM 候选构建](product_build.json)和[复核](review.json)。这些不等于候选真实 ESP/TLS、屏幕、触摸或麦克风验收。用户因即将断电要求收尾，未下载此候选；最后板上仅是已完成 TCP 并停在普通 AT 的专用诊断程序，不能按正常 UI 在运行接续。

后续按顺序验证：重新上电的板态与候选装载 → 无 Key TLS 握手/收发与关闭 → 短固定音频云译 → 真实麦克风、快速暂停/继续与触摸 → 持续运行。每一项取得对应证据再继续。本页列出步骤不构成新的执行授权。
