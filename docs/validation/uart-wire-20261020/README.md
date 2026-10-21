# 单端 UART 实体采样

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

核验日期：2026-10-20。JTAG 重插后已恢复，以下三组真实采样通过。接续状态由 [ENGLISH](../../context/ENGLISH.md#tls-传输诊断进行中2026-10-20)维护；本页只记录 ESP 移除后的单端试验。

## 实测结果

| 样本 | 字节数 | 发送方式 | 完整比较与停止位 |
| --- | ---: | --- | --- |
| [短样本](short_board.json) | 8192 | 1 Mbaud 连续发送 | 零差异、零帧错误 |
| [满速](burst_board.json) | 262144 | 1 Mbaud 连续发送 | 零差异、零帧错误 |
| [节流](paced_board.json) | 65536 | 8 字节/ms | 零差异、零帧错误 |

总计 335872 字节；20 MS/s、CH0、8N1。采集长度/SHA、完整解码数量/字节、板端接受量/FNV/硬件发送数均分别核对。原始采样仅保存在被忽略的 output/。公开证据包括 [阶段清单](single_end_stage.json)、[下载绑定](single_end_load.json)、[配置前状态](hardware_before_load.json)和[波形图](waveform.png)。`hardware_after_rewire.json` 是用户后来断电改回 ESP 后的状态，不能当作试验失败或试验前配置证据。

执行 ELF SHA 为 `541693f1e23006519f6e94c533692dc88ba7925b162951b0759f3e9fe76f1f8d`；当时 main.c SHA 在阶段清单中，对应源码可由 Git `fe6701e` 追溯。随后源码增加 mode=2 AT 分块诊断，不能把后来的构建指纹套用到上述波形。实际采样编排见 [run_uart_wire_case.py](../../../scripts/repair/run_uart_wire_case.py)，板端控制见 [uart_wire_test.tcl](../../../scripts/repair/uart_wire_test.tcl)。

## 早期准备证据（保留原范围）

- [准备清单](offline.json)：源码/ELF 指纹、测试结果、实际执行范围。
- [发送程序构建](build.json)：独立 OCM 程序，不编译网络、UI、USB Host 或凭据。
- [采集链路](capture.json)：直接 libusb、CH0/CH1、20 MS/s、400 万点，无受控发送源；仅采集完整性检查。
- [解码测试](decoder_tests.log)：5 项通过，包括 33041 字节与字节损坏/缺失/多余的拒绝。

## 接续入口

发送程序为 [main.c](../../../software/uart_wire_test/main.c)，构建入口 [build_uart_wire_test.py](../../../scripts/repair/build_uart_wire_test.py)。复用原 OCM 启动和链接文件，邮箱在 `0x1e000`，已知公开载荷由操作者在实际运行前上传到 DDR `0x01000000`。邮箱第 4/5/6 字分别是长度、UART 分频和节流模式，第 2 字写 1 开始；应只在核对 PL、停妥 CPU1/USB/VDMA、加载本专用程序后使用。程序限制等待时间，并报告写入字节数、FNV、硬件发送数及错误；不应把它覆盖到正在运行的产品进程。

采集入口为 [slogic_capture.py](../../../scripts/repair/slogic_capture.py)，需要用户便携包中的 `libusb-1.0.dll`，不安装依赖或分发该二进制。使用厂商的 `0xb1` 四字节启动命令及 `0x81` bulk 端点，双通道每字节打包 4 个采样；实现依据和适用型号写在源码中。原始数据只留在被忽略的 output/，不提交通信载荷。

[uart_wire_decode.py](../../../scripts/repair/uart_wire_decode.py)校验原始文件与完成清单的长度/SHA，再从 CH0 解码 1 Mbaud、8N1；只有完整字节数量及内容都相同、无帧错误才通过。离线测试为 [test_uart_wire_decode.py](../../../tests/test_uart_wire_decode.py)。上面的三组实测独立于早期准备结果，不互相替代。

单端接法将 ESP8266 完全移除，接线已获用户确认；仅测 FPGA 到现有线缆和分析仪输入的通路。三组通过也不能排除原模块接入时的负载、电平或接收/转发问题；未测量模拟电压和边沿裕量。本次未启动产品 UI 或 USB Host。用户随后恢复 ESP 接线，接回模块后的 [TCP 对照](../esp-tcp-20261020/README.md)独立记录。
