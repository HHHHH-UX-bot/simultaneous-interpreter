# 分阶段执行与验收

当前授权范围见 [NOW](NOW.md)。以下是执行方法和放行条件，不是自动执行授权；未实现的硬件/模型命令不写成可运行入口。对应事实和资料见 [MAP](MAP.md)，风险见 [RISKS](RISKS.md)。

2026-10-13 当前硬件已更换为 Z7-Nano 7010，新板按 [BOARD_MIGRATION](../design/BOARD_MIGRATION.md) 的 M 阶段执行。下面原 P2b/W 阶段的板级命令针对 RevB，不绕过身份保护操作新板。电脑模型阶段保持其原范围。

## P0 方案与基础文档（本轮）

前置：项目根目录和规则已确认，优先使用有效现有信息。

操作：建立职责明确的入口与专题；核对与音频、触摸、DDR、HDMI 关联的资料；完成系统分工、接口草案与未知项。

放行：已确认决定与建议分开；事实有来源；按需读写规则完整；新建文件和直接链接检查通过。当前实证在 NOW，不因方案详尽而宣布业务完成。

## P1 离线交互原型（本轮）

前置：不需要音频模块或触摸数据线；浏览器可打开本地交付。

操作：打开 [原型](../../output/ui/index.html)，用鼠标/键盘检查开始/暂停、定稿记录、字号、原文、设置、清空确认、会话详情和模拟故障。验收按 [UI_SPEC](../design/UI_SPEC.md#验收检查)。

放行：目标逻辑分辨率下无控件重叠或正文丢失；长句可读；不会把草稿反复存入历史；清空/暂停后旧定时结果不回填；断线有明确恢复入口。自动检查与截图只证明浏览器原型，UI 审美与操作习惯仍需用户体验确认。

## P2a 纯 PL 静态 UI 演示

前置：沿用已验证的板卡、HDMI 约束与时序；无需先建立 Linux/DDR 软件。用户当前已授权生成、验证、构建与 JTAG 临时配置。静态版不响应触摸或按钮，也不运行模型。

入口：[run_static_ui.ps1](../../scripts/run_static_ui.ps1)，参数 `-Action assets / simulate / build / probe / program`。显式传入可用 Vivado 启动器；生成图片和模型向量时使用 machine 环境的 Python 启动器，参数名为 `-VivadoPath`、`-PythonPath`，不固化个人安装目录。

顺序：

1. `assets`：生成明确标注静态演示的原生分辨率截图、16 色调色板与 8×8 去重小块；整图还原必须覆盖全部像素，目视核对量化画面。
2. `simulate`：两个完整有效画面逐像素匹配独立图像参考；检查光栅推进、HS/VS/DE、消隐和复位，再检查独立 TMDS 编码模型及输出引脚的逐位串化符号。
3. `build`：必须有当前仿真哈希证据；实际 BRAM 用量、setup/hold、引脚、生成时钟及 DRC 通过才能生成可下载产物。
4. `probe`：当前构建及位流哈希一致，只允许一个 JTAG 目标和一个 xc7z010。
5. `program`：先使旧结果失效，再通过 JTAG 配置，读取 DONE/EOS/CRC，清除旧探针关联；记录当前位流哈希、目标和核验时间。失败记录不得保留 passed。由用户确认实屏和 [量化预览](../../output/static_ui/ui_quantized.png) 一致。

每阶段失败时停止其依赖阶段。资产、源码、约束或位流变化会使现有证据失效。通过编译或 DONE 状态不替代实屏确认。JTAG 临时配置断电失效，本入口不写 QSPI/SD。

当前已执行与未执行状态见 NOW；上述步骤本身不授权后续任务。实现结构见 [STATIC_DEMO](../design/STATIC_DEMO.md)。

## HDMI 屏幕启动与异常恢复

核验日期：2026-10-11；范围为当前 ZYNQ_MINI_REVB、KD070V02、Wi-Fi 集成位流，开发板/ESP 持续供电时的三次用户实屏观察。结果见 [NOW](NOW.md) 和 [显示记录](../../output/wifi/diagnostics/display_acceptance.json)，具体根因尚未确认。

1. 保持开发板和 ESP8266 供电及接线不动。
2. 将屏幕供电线与 HDMI 都拔下，等待约 10 秒。
3. 先接回 HDMI，再接屏幕供电。
4. 观察文字、布局和画面是否清楚稳定；同样步骤仍异常时保存异常画面及当时条件，不用单次插拔结果认定某根串口线有故障。

只拔屏幕供电线曾出现随机抖动；先供电后 HDMI 在用户当前实测中没有画面。本流程不重新配置 FPGA，也不把临时 JTAG 位流写入启动介质。若开发板本身断电，应重新确认实际运行的位流。

## W ESP8266 串口与联网

前置：按 MAP 核实模块、接线、Bank 电压、供电与共地；静态 HDMI 基线可恢复。使用 machine Python、已安装 Vivado 和现有 hw_server。凭据运行时输入，禁止写入日志或位流。

1. W0：run_wifi.py simulate，验证 UART、FIFO、AXI 背压、复位/预取边界和波特率切换；执行 sim/test_wifi_host.py 验证分片、二进制/中文、转义、发送提示符和临时服务清理。
2. 集成：run_wifi.py build，资源、时序、DRC、时钟和引脚通过后，依次 probe、program。所有动作显式传入 --vivado 启动器；输入或位流变化使依赖证据失效。只作 JTAG 临时配置。
3. W1：wifi_link.py probe，确认寄存器身份、真实 AT、回显关闭、波特率和固件版本；无响应时先定位电气或固件原因。
4. W2/W3：wifi_link.py connect，另传 --subnet 当前局域网 CIDR，运行时输入 SSID/密码；使用易失参数，核对关联与地址，验证随机标记、全部字节值和中文 UTF-8 的双向 TCP 数据。
5. 下载状态、模块响应、网络关联与实屏分别记录。计数器非零、数据不一致或任一步骤失败时，不宣布联网验收通过。

显示异常时，先停止串口事务，记录实际位流、接线和用户重启的是屏幕还是板卡，再进行单变量对照；屏幕重启不能替代集成版验收。若位流身份或供电/复位状态无法确认，暂停依赖它们的网络操作。若当前位流身份及真实 UART 应答已确认，则网络可以独立取证，显示稳定性仍单独保持未通过。需要改线或操作模块复位时，先核实具体模块引脚与启动条件，避免同时改变多个变量。间歇性现象不根据单次接线前后对照定因。

当前结构由电脑/JTAG 操作串口，尚不等于 PS 自主联网；HDMI 是静态画面，测试文案不表示已上屏或真实翻译。技术合同与凭据处理见 [WIFI_LINK](../design/WIFI_LINK.md)，实际阶段结果见 NOW。

## P2b PS 实时中文与模拟 UI（后续）

状态：P2b-0/P2b-1 与 P2b-2 均已通过；P2b-3 中文实现、电脑/ARM 验证、板上启动、整帧比对、控制/旧会话隔离、实际 1842 秒持续更新与用户实屏验收均通过，证据以 NOW 为准。操作入口分别见 [PS_BRINGUP](../design/PS_BRINGUP.md)、[FRAMEBUFFER](../design/FRAMEBUFFER.md)、[DYNAMIC_UI](../design/DYNAMIC_UI.md)。本阶段不依赖真实音频模块或触摸线；保留静态和图形基线，沿用已有 HDMI 时序与引脚，按以下依赖顺序推进。

| 子阶段 | 输入与操作范围 | 放行证据 |
|---|---|---|
| P2b-0 前置核查 | 从 MAP 路由至已有 RevB 资料，定位 PS/DDR 工程或配置、硬件导出与对应软件版本；核对现有工具、启动方式和日志/调试通道。Linux/BSP、SD 与串口条件未知时先标待核实，不混用旧 HDF 与新 XSA | 一份有来源的配置/版本对应记录，明确可复用项和必要缺口；没有可依据的 DDR 初始化配置时停在这里，不猜参数 |
| P2b-1 最小 PS/DDR | 使用已核实的初始化基线验证 PS 程序运行、DDR 读写和可重复启动；可以先用最小裸机工程取证，正式 Linux/FreeRTOS 选择另行记录 | 程序运行与内存检查结果、实际测试范围、初始化/工具版本及恢复方法。测试区须与代码、数据和其他缓冲隔离，不猜固定地址，不改写 QSPI/SD |
| P2b-2 可更新帧缓冲 | 按实际可支持的格式，验证 CPU 写入 DDR、PL 连续读帧、纯色/棋盘格、RGB 顺序、stride、Cache 同步与安全翻页；显示驱动尚不存在时明确需要实现，不把 VDMA 当成现成 fbdev/DRM | CPU 更新像素能在实屏生效；无未解释的欠载、越界或撕裂，记录资源/时序与用户实屏结果 |
| P2b-3 中文与模拟字幕 | 先固定中文和字体，再实现换行、长句、定稿历史、暂停/清空和状态变化；通过定时或测试消息驱动，始终标模拟。LVGL 仅在选型和平台条件确认后接入 | 中文实际可读，长句/旧会话/修订行为符合 INTERFACES；更新性能与内存计数可观察；没有触摸配件时不声明实触通过 |

放行：用户确认实际 HDMI 屏中的中文、字号、历史和刷新；至少 30 分钟模拟更新无撕裂/欠载、无异常内存增长。构建成功、配置成功与画面验收分别保留。未取得证据前不进入依赖此输出的整机联调。

P2b-3 板端测试由专用 UI 入口选择 ELF，再由共享显示驱动配置/启动；观察依赖已运行的本机硬件调试服务。字体、状态、抓屏、命令与长期检查的方法统一由 DYNAMIC_UI 维护，不复制到本节。30 分钟检查必须使用真实经过时间、当前运行快照、连续定稿进度及错误/保护计数；主机一小时模拟时间测试不能替代。

## W4 板端自主网络与音频传输带宽（后续）

状态：W4-0 PS 自主 AT 及实屏已通过，平台方法见 [PS_WIFI](../design/PS_WIFI.md)；W4-1 自主网络文本及用户实屏已通过，证据与方法见 [NETWORK_UI](../design/NETWORK_UI.md)。前置 P2b-1 的 PS/DDR、W1/W2/W3 及 P2b-2/P2b-3 均已通过；持续音频吞吐另行取得证据，完整 W4 尚未放行。JTAG 只作为诊断/配置通道，不当作运行态的数据中转。

1. 由 PS 接管 UART 寄存器和 AT 状态机；定义唯一事务所有者，避免 JTAG 与 PS 同时消费接收队列或发送 AT。验证上电、关联、断线和重连，不在异常时无限排队。
2. 先用明确标注的模拟字幕消息验证电脑到板端的数据合同，再更新实际中文画面；核对 UTF-8、长度上限、session/segment/revision 和旧消息丢弃。W3 的 TCP 回环不是 WebSocket、翻译服务或动态字幕实现。
3. 按 SYSTEM_PLAN 的音频负载预算实测持续吞吐、误码、溢出、缓冲峰值和恢复。先查实际固件对易失波特率设置及回退的支持，再逐档验证；较高波特率或压缩方案均是候选，未经实测不锁定。不能因 115200 下短包通过而接入连续 PCM。

放行：板端应用无须电脑经 JTAG 代管 AT；结果消息能驱动实屏变化；至少 30 分钟传输试验覆盖拟议音频数据率及协议开销，未解释的缺口/误码/溢出为零，断线恢复与有界缓冲得到证据。网络吞吐通过不替代真实采样、实际模型或长期整机验收。

## C 云端语音接入

方案与阶段顺序由 [CLOUD_SPEECH](../design/CLOUD_SPEECH.md#分阶段放行)维护。C0/C1-0 已在各自范围验收；C1-1 尚未放行；当前可信连接进展由 [C1-2](../design/CLOUD_SPEECH.md#c1-2-cyea-与板端-tls-预检)维护。

2026-10-15 已按用户继续指令先做可信连接预检；总体按 [复盘后的验证顺序](../design/CLOUD_SPEECH.md#2026-10-15-复盘后的验证顺序建议)推进。原 C0–C5 编号与历史证据保留，不能把旧板命令、PCM 预算或本机通过套用为新候选的放行。

C1-2 工具要求本机硬件服务 `127.0.0.1:3121` 可用、相同 Z7N3/ARM 程序、PS 网络尚未配置（request/ack 都为零）。[清单工具](../../scripts/probe_z7_nano_tls_inventory.py)查询公开配置；`--transport-check` 是不带 Key 的 auth=0 传输诊断，必须关闭已建立连接并恢复/读回 auth=2，不代表安全连接成功。[预检工具](../../scripts/probe_z7_nano_tls.py)使用 `--credentials-stdin` 的隐藏输入提交 Wi-Fi，先确认回显关闭和 SYSSTORE0，检查时间，再执行 TLS 正例/错身份拒绝/正例；不能把密码写入命令参数或日志。

开发板重启后，先用 JTAG probe 核对实际 DONE/USR_ACCESS。未配置 PL 时不读其 MMIO，也不依据旧 Host 记录执行 stop；按当前板卡身份重新 program → run → capture，再恢复 USB probe/Host。CPU 调试目标不可见时停止访问，记录实际故障并完整断电恢复；本轮恢复证据见 [记录](../../output/z7_nano/stream/restore_after_restart.json)。

[CA 工具](../../scripts/provision_z7_nano_ca.py)的 `backup` 只读取并保存完整 8192 字节的公开客户端 CA；`install` 先重新核对 live CA 等于已验证同板备份，才擦除/写入固定 `client_ca`，候选限定为已核对的 GlobalSign R3。`restore` 显式恢复该同板备份，会覆盖当前 CA。该工具会写 CA 数据，不能用文档命令代替当次实施授权。二进制写入中断时模块可能仍在等待数据；先检查阶段，不盲发 AT 或反复重试。工具完成和回读通过后，另行执行无 Key 安全预检；未经安全连接放行不发送云 Key。

C1-3 [WSS 会话工具](../../scripts/probe_z7_nano_wss.py)使用相同的 fresh 应用和本次安全 TLS 记录，模块须已加入当前 Wi-Fi。运行 `--credentials-stdin`，经终端隐藏输入提供只含 `key` 的运行 JSON；工具在当前 auth2/SNI/CN 与 SSL 建连成功后才发送鉴权。预期 HTTP 101、session.created、session.updated、零 UART 错误、TCP 关闭与 MUX 恢复；仍无音频。CIPSEND 未确认完成时不补发 AT，状态不明先定位。电脑仅驱动串口，此操作不证明 ARM 自主云连接。

C1-4 [文件短测工具](../../scripts/pilot_z7_nano_cloud_file.py)运行 `--audio output/model_backend/fixtures/english_reference.wav --credentials-stdin`，隐藏输入只含 key 的 JSON。前置为当前匹配的 stream ARM 程序、已关联 Wi-Fi、同模块可信 TLS 参考、正确 CA 和常驻触摸；每次重新查询 auth2、设置 SNI/CN 并实际建立可信 TLS，不复用旧连接冒充本次成功。工具不安装依赖，音频由已有 PyAV/libopus 编码。

默认四个 Ogg 页一次 append；添加 `--pages-per-append 8` 为同样本的八页对照，两档已分别通过短测。该参数只改变预装帧，无需重新编译/下载 ARM；不得据短测直接选定持续音频参数，结果由 [分组对照](../../output/z7_nano/stream/packetization_comparison.json)维护。

音频掩码帧一次装入固定 DDR 并完整回读，发送和接收由 PS 完成，电脑只批量取回事件；不通过逐字节 JTAG 发送音频。完成必须同时满足 PS 全帧 SEND OK、字节计账、非空原文/译文和 session.finished，随后确认 PS 释放、关闭 TCP、恢复 MUX0。下一次短测自动从已释放态 reset3，不需重烧。当前文件发送区和接收留存区各 512 KiB，实际可用时长取决于封装；CLI 的 60 秒上限不代表长期持续测试已通过；当前只有一次延后观测的一分钟完整结果。一分钟溢出对照使用 `--defer-observation` 可将 JTAG 轮询延后到音频结束 5 秒之后；此模式的电脑取回时间不代表云端时延，结果仍须按相同完成/收尾条件判定。

若出现 `output/z7_nano/tls/uart_uncertain.json`，代表 CIPSEND 或 PS 所有权未确认。先按当前尝试和公开状态定位，不能直接删标记、盲发 AT 或用 CPU 重载代替 ESP 恢复；原始凭据/串口数据不落盘。公开 GMR、当前所有权及清理结果构成恢复证据，文档步骤不授权绕过门禁。短测方法和分配地址由 [C1-4](../design/CLOUD_SPEECH.md#c1-4-ps-调度的加密音频短测)维护；它不是长时容量、真实采音或独立产品验收。

C1-5 的独立 C 解码验证使用 `python -B scripts/validate_cloud_events.py`，需要已配置的本机 GCC 与 ARM 工具链；本轮已执行通过，范围见 [C1-5](../design/CLOUD_SPEECH.md#c1-5-c-端云事件解码)。它编译本机测试和 ARM 对象，不连接板卡或云端；仓库固定样例始终执行，本机完整公开会话样例存在时额外对照。

C1-6 于 2026-10-15 完成实现与离线验证，当时按用户要求暂缓板测；该历史限制已由后续接线恢复及继续指令解除。不得根据旧 program/Host 记录推断设备仍在线。离线阶段在现有 machine 环境执行：

```powershell
python -B scripts/validate_cloud_live.py
python -B scripts/run_z7_nano_network.py build-app
```

前者编译实际 C 帧/事件/UI 链路，回放公开消息，执行私有 Tcl RPC 的模拟目标检查和现有 Python 传输检查，再用实际渲染器生成离线预览；不使用 JTAG、网络或 Key。后者仅编译 ARM 应用，复用既有 PL；字体和相关 UI/网络检查必须与当前输入一致，不能手工改哈希绕过。阶段证据与边界由 [C1-6](../design/CLOUD_SPEECH.md#c1-6-arm-云事件到字幕)维护。

2026-10-16 已执行板测：先以 DONE/USR_ACCESS 核对状态，停 USB DMA 后按既有流程配置/运行匹配程序，恢复显示及常驻触摸并取得用户验收。ESP 缺线造成的首次 AT 超时，在用户接好后通过同版重启恢复；再完成无 Key TLS 身份对照。实际执行 `python -B scripts/pilot_z7_nano_cloud_file.py --audio output/model_backend/fixtures/english_reference.wav --onboard-ui --credentials-stdin`，凭据只经隐藏输入提供。`--onboard-ui` 自动延后 JTAG 观测，由 ARM 消费云事件；电脑仍负责 TLS/WSS 初始化、预编码文件和 32 枚随机控制掩码。每次任务先停旧 consumer，成功后保留最终字幕，释放 PS、关闭 TCP 并恢复 MUX0。本次完成非空原译、定稿/字节核账、实际像素及用户实屏验收，并观测一次栈写水位；数值、固定证据与未覆盖范围由 C1-6 专题维护。继续时重新确认实际目标和运行状态，不直接进入长测或把该命令当作独立启动流程。

C1-7a 已执行 `python -B scripts/validate_z7_nano_tls_boot.py`、相关网络回归及 ARM 构建。首次板测在 JOIN 通知处停止，本机复现修正后，重新配置/运行、AT 验证、整帧比对与常驻触摸均通过；用户随后要求暂停。接续入口 `python -B scripts/probe_z7_nano_tls_boot.py --credentials-stdin` **修正版尚未重跑**：匹配程序运行，网络/音频/云 consumer 空闲，显示/触摸验收后，隐藏输入运行期 Wi-Fi 配置。工具仅写邮箱，由 ARM 执行正确/错误/正确身份与关闭对照，不输入 API Key、不操作证书或固件。遇不确定标记先核对失败尝试及实际所有权，取得恢复证据再归档，不能直接删除重试。地址、恢复范围与下一步见 [C1-7a](../design/CLOUD_SPEECH.md#c1-7a-arm-自主可信-tls)。

C1-0 入口为 [固定查询工具](../../scripts/probe_z7_nano_capabilities.py)。在项目根目录选择 machine 环境；前置是匹配的 Z7N3 平台、当前源码验证和 ARM 构建。重新下载依次 stop Host → program → run；之后执行下列已实测命令：

```powershell
python -B scripts/probe_z7_nano_capabilities.py
python -B scripts/run_z7_nano_network.py capture
python -B scripts/run_z7_nano_usb.py run-probe --platform network
python -B scripts/run_z7_nano_usb_live.py start --platform network
```

查询只从本次程序的空闲状态触发一次；已有活动关联时不复用该入口。完成后核对当前程序的画面与触摸；复测使用新的匹配启动记录，不能把旧产物作为本次运行。C1-0 查询没有配置无线、证书、Flash 或云端 Key；后续连接操作另按其前置条件实施。

C1-1 的逐包入口是 [measure_z7_nano_upload](../../scripts/measure_z7_nano_upload.py)，已执行 `--seconds 60 --local-ip <当前 WLAN IPv4>`；前置为匹配程序、活动 Host、关联记录和空闲 LAN 服务。透传入口是 [measure_z7_nano_passthrough](../../scripts/measure_z7_nano_passthrough.py)，使用同样时长与地址参数，加 `--credentials-stdin` 从隐藏输入取得运行凭据；前置为新启动的空闲程序。工具先绑定测试监听，再经 RAM 请求板卡连接，退出前确认普通 AT 模式及 TCP 关闭。接收端核对全部块的长度、序号、内容和完整发送时长，保留失败指标；短试验不足时不进入长试验。两档透传实机均失败，当前代码仅保留为诊断入口，容量边界见 [C1-1](../design/CLOUD_SPEECH.md#c1-1-连续上传与串口调度)。

C2-PC 已用 [公开合成英语文件](../../output/model_backend/fixtures/english_reference.wav)执行一次真实云端对照。使用 machine 环境，从项目根目录运行 [run_bailian_file](../../scripts/run_bailian_file.py)，以 `--audio` 指定该 WAV、`--bind-ip` 指定当前实际 WLAN IPv4。输入须为 16 kHz、单声道、16 位 PCM；业务空间和 Key 由终端隐藏输入提供，不能放入参数、文件或原始日志。客户端验证 TLS、禁用代理并等待服务端完成，结果写入独立尝试目录；方法、结果与验收边界由 [C2-PC](../design/CLOUD_SPEECH.md#c2-pc-受控音频云端对照)维护。新网络或凭据不能沿用历史成功结论。

同条件模型/编码对照可添加 `--model qwen3.5-livetranslate-flash-realtime --format pcm` 或 `--format opus`；默认仍为 Qwen3.8 PCM。Opus 使用已有模型隔离环境的 PyAV/libopus，不安装新依赖；工具直接从同一个 WAV 生成 Ogg 页并按采样时间发送。`response.text.done` 是 3.5 的权威定稿，不能把 stash 临时文本写入最终结果；`session.finished` 才结束会话。电脑对照与板上真实连接的证据分别记录。

## P3 USB 触摸

前置：核对屏幕 TOUCH 与开发板 USB 数据口、Host/适配线和 VBUS 电气路径。HDMI 只负责视频；JTAG 口和 UART 口不是可替代的触摸 Host。

操作：真实枚举设备；记录设备标识和 HID/input 支持；用输入事件工具核对原始坐标；完成显示坐标映射。再验证四角、中心、按钮、按下/松开、拖动记录和拔插恢复。

放行：无轴反向、偏移、鬼点、松开丢失与重复按钮触发；电气条件确认后才进行热插拔测试；用户实触验收。未完成时只保持鼠标/模拟输入，不把它记为触摸通过。

当前 Z7-Nano 裸机实现及各阶段证据由 [USB_TOUCH](../design/USB_TOUCH.md)维护。在项目根目录、machine Python 环境中使用以下已执行过的常驻入口；start 后退出电脑观察不会停止 CPU1，sample 不停核，stop 确认 Host/DMA 停止。

上述入口默认选择 M3 `display`；M5-0 自主 AT 使用 `--platform wifi`，M5 网络应用使用 `--platform network`。按 [NOW](NOW.md) 的当前 RAM 应用选择，不把最新编译文件当作已下载程序；停机时仍使用当前已管理实例的平台。各实例绑定实际配置、程序和 generation。

```powershell
python -B scripts/run_z7_nano_usb_live.py start
python -B scripts/run_z7_nano_usb_live.py sample
python -B scripts/run_z7_nano_usb_live.py stop
```

前置是当前显示配置/程序、完整帧比对与 PHY 探测均通过。开发板断电后，先确认当前 JTAG/PL 状态、退役上次易失会话记录，再依次恢复显示 program → run → capture、USB run-probe → start → sample，并重新取得实屏验收。保留历史结果，不能把旧 running 文件当作当前证据；当前没有独立上电启动能力。

## A 音频实时回环与时延显示（后续）

状态（2026-10-17）：PL 实现、仿真、Vivado 2018.3 构建、下载、寄存器读取与**板级回环**均已执行。根因是 5 V 供电余量：开发板 USB 5 V 被屏幕拖到 4.3 V，模块板上 LT1117-3.3 掉出稳压区，codec 应答 I2C 但晶振不启动。用户改用充电宝主供电 + 排针 5V 直供模块后：`live=1`、BCLK 3.072 MHz、`L1=192 BCLK=62 µs`、`over/under=0`、显示 `format_errors=0`。仍待耳机听感、实屏读数确认和 ≥30 min 持续运行。**屏幕供电**：HDMI 屏由开发板 PS USB0 主机端口的 VBUS 供电（TPS2051B 开关，由 PORTSC1.PP 使能）；`ps7_init` 只配置 PHY/MIO 不开端口电源，因此仅烧 PL+启动字幕应用时屏幕会是黑的。`scripts/z7_nano_audio.tcl run` 现在会在 PS 初始化后复刻 `usb_board_init()` 的寄存器序列自动打开端口电源（打印 `USB0_PORT_POWER ... PP=1`），无需再单独跑 CPU1 触摸主程序；需要触摸时仍需按 USB 专题加载 CPU1 host 固件。接口推断、时延分层定义与门禁见 [AUDIO_LOOPBACK](../design/AUDIO_LOOPBACK.md)。

前置：A0 已完成（型号 WM8731、板卡与模块逐针映射、引脚占用、模块自带 12.288 MHz 晶振，结论见 [audio_module.json](../../hardware/z7_nano/audio_module.json)）。**2026-10-17 更正电源/地针**：模块 J3-1=GND、J3-2=VCC5V、J3-37/38=GND、J3-39/40=D3V3；开发板 JP2-11=VCC_5V、JP2-29=VCC_3V3、JP2-12/30=GND。正确接线：J3-2→JP2-11、J3-39/40→JP2-29、J3-1/37/38→JP2-12 或 30；**GND 必须共地**。I2C 地址实测为 0x1A（CSB=0）。**注意**：模块的 BCLK/LRCLK 要先用 I2C 配置 R7 主模式与 R9 使能后才输出；但“I2C 应答正常、四路引脚零跳变”说明模块没有时钟，优先查供电/共地，不要改软件。

操作（每步取得证据后再进入下一步；本轮均未执行）：

1. A1 约束核对：BCLK/ADCLRC/DACLRC 作输入时钟与数据约束（`create_clock`、`set_input_delay`）、MRCC 与 IOSTANDARD；不修改现有 hdmi.xdc。
2. A2 I2C 初始化：实现 I2C 主机（PL 软 I2C 或 PS 侧），写 R7（主模式/I2S/位宽）、R8（采样率）、R6、R4/R5、R2/R3、R9；先只做寄存器读写与回读，再用 ILA/示波器确认 BCLK/LRCLK 出现。地址先试 0x1A，失败再试 0x1B。
3. A3 PL I2S 从机采集：仿真逐位对照 → ILA 逻辑观察 → 已知输入实采与溢出统计。
4. A4 PL 内 ADCDAT→DACDAT 直通回环（耳机可听），先取最小延迟基线。
5. A5 L1 时延计数与寄存器暴露；需要软件处理时加 L2 打点。
6. A6 复用现有渲染器新增只读时延读数，不改变既有布局、按钮和交互。
7. A7 持续运行、耳机听感与 L3 声学测量。

构建与运行入口在 A1–A3 建立后回填本节；本轮不虚构脚本名或命令。

构建与运行入口（2026-10-17 起可用）：

- `python -B scripts/run_audio_loopback.py simulate --xsim-bin <Vivado>/bin` — 已执行（2026-10-17，本机 xsim：五个自检 testbench 全通过，位精确回环延迟 3 帧 = 192 BCLK）。证据：`output/z7_nano/audio_loopback/simulation.json`。
- `vivado -mode batch -source scripts/audio_synth_ooc.tcl` — 已执行（逐模块 OOC 综合检查，不需要 BD）。证据：`output/z7_nano/audio_ooc/`。
- `python -B scripts/run_audio_loopback.py build --vivado C:/Xilinx/Vivado/2018.3/bin/vivado.bat` — **已执行**（2026-10-17，Vivado 2018.3 完整 BD + 综合/实现/位流通过，setup/hold = 7.289/0.044 ns）。产物 `output/z7_nano/audio/z7_nano_audio_top.bit` 与 `implementation.json`；`build.json` 需在输入文件不再变动后重跑一次生成（首次运行期间改了文档 JSON，哈希校验中止）。
- 位流下载、PS7 初始化、字幕应用启动与音频寄存器读取：`xsdb scripts/z7_nano_audio.tcl run 210241277120 <bit> <record.json> <ps7_init.tcl> output/display/subtitle_ui.elf` — **已执行**（2026-10-17）。读出的 `AUDIO_*` 行给出 I2C/时钟/引脚计数/L1：`AUDIO_PINS` 四路计数应随时间增大，`bclk_alive=1`，`live=1`，`L1 us≈62`。**当前结果**：`codec_found=1`、`regs_written=11`，但 `AUDIO_PINS` 全 0、`live=0` → 模块无时钟，先修供电/共地。

放行：耳机可听到实时回环且无爆音/断音；L1 数值可重复并与采样率一致；屏幕读数标明层级与测量点，未测得时显示“未测量”；至少 30 分钟无未解释溢出；用户实屏确认既有画面未变并确认回环可用。仿真、约束/构建、下载、听感与延迟数值分别记录，不能互相替代。

## P4 音频采样（模块到货后）

前置：取得型号/资料、接口与电平；完成 INTERFACES 的接线表，核对针序。不同协议需要对应接收逻辑与约束，不复制猜测引脚。

依次执行：时钟/位序仿真 → 数字接口 ILA/逻辑分析 → DMA 样本 → 可回放音频 → 必要 DSP。每步单独记录证据。BASIC 下沿用已验证的手工实例化 ILA 路线，不依赖不支持的网表插入流程。

放行：采样格式、通道、采样率和幅度符合合同；已知输入可回放且无掉样/持续削波；至少 30 分钟持续采样，无未解释 FIFO/DMA 溢出。DSP 旁路对照未降低语音可懂度；采音通过后才归因 ASR/MT 质量。

## P5 电脑端识别与翻译（后续）

当前子阶段：P5a 已完成受控英语文件的真实 CPU ASR/MT，P5b 已实现有界 HTTP 文件接口；最新执行/评审状态由 [MODEL_BACKEND](../design/MODEL_BACKEND.md) 维护。真实麦克风、板端上传、模型译文上屏和完整质量/时延验收尚未通过。

前置：源语言已确认、电脑资源与模型适配已核对；运行环境/模型下载在对应实施授权内。先使用受控测试音频，不读取个人数据。

已实现操作入口：`scripts/run_model_file.py`、`scripts/validate_model_backend.py`、`scripts/run_translation_service.py` 和 `scripts/validate_translation_service.py`。须从项目目录用 machine Python 执行；依赖和模型置于本项目，服务的地址、私有运行时配置、限制及停止方法由 MODEL_BACKEND 维护。文件执行通过不自动放行板卡联网或连续语音。

后续操作：冻结一组代表性短句/长句/静音/噪声/术语样本；分别检查 ASR 与中文 MT；测完整冷启动、RTF、内存、P50/P95 时延与乱序/重复处理。已定位的合成噪声幻觉保留失败与单变量 VAD 对照；模型不能把安静背景“识别”为虚构字幕。

放行：约定质量标准和性能预算达成或形成明确调整记录；真实推理结果与测试原文/参考译文可对照。没有实测时不发布“实时”“支持全部语言”等能力声明。

## P6 端到端与可靠性（后续）

前置：P2b、W4、P3、P4、P5 分别通过；协议版本、格式与各产物哈希可追踪。P2a 静态画面不能替代 P2b，W3 短包回环不能替代 W4。

操作：实音频 → 电脑 → 中文屏显，触摸控制同步；验证暂停、重开、清空、长句、断网、模型退出、重连、缺失/重复/旧结果与过载。先短时正确性，再至少 2 小时稳定性，记录 P95 延迟、吞吐、样本缺口和 UI 响应。

放行：字幕不重复、不串会话，故障可恢复，无无限缓冲和内存增长，用户确认实际声音到中文显示符合需求。可靠性通过仍不等于获得量产、公网部署或启动介质改写授权。

## 当前可用操作

浏览器原型入口仍是 `output/ui/index.html`。静态板卡入口为 `scripts/run_static_ui.ps1`，只负责 P2a；Wi-Fi 阶段入口与已验证运行方法见 W 和 WIFI_LINK。最小 PS/DDR 与可更新显示分别由 PS_BRINGUP、FRAMEBUFFER 路由；各命令仅负责对应阶段，尚无统一产品启动命令。实际授权以当前用户指令为准，阶段结果以 NOW 和对应证据为准。

## Git 维护

仓库、分支及忽略边界见 [MAP](MAP.md#git-与-github)，用户上传授权见 D12。提交前核对待提交文件与本阶段验证；只暂存相关源码和文档，保留已有用户修改。源码字节规则不得被行尾转换覆盖；验证产物仍按其原范围留在本地。

按 `status → diff/暂存范围 → commit → push → 远程提交核对` 维护，不强推或覆盖未知远程历史。当前没有自动部署、写启动介质或改变仓库公开性。克隆后的运行产物按对应阶段入口生成，旧本机证据不能直接作为新机器/新板的验收。

2026-10-16 用户要求将本次改动经 fork 以 PR 提交。执行前置：确认 fork 远端与目标分支；确认工作树确实带 Git 元数据（本轮核对时交付目录是解压副本、不含 `.git`，不能直接提交）；只暂存本次文档改动。PR 的基线仓库、分支与是否合并由用户确认；不顺带改动业务代码、构建产物或仓库可见性。
