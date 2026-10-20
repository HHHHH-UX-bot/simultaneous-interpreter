# 单端 UART 波形采样准备

核验日期：2026-10-20。当前为准备完成、等待电脑识别 JTAG；未向开发板下载或发送测试数据，未取得受控 UART 波形。接续状态由 [ENGLISH](../../context/ENGLISH.md#tls-传输诊断进行中2026-10-20)维护。

## 文件与证据

- [准备清单](offline.json)：源码/ELF 指纹、测试结果、实际执行范围。
- [发送程序构建](build.json)：独立 OCM 程序，不编译网络、UI、USB Host 或凭据。
- [采集链路](capture.json)：直接 libusb、CH0/CH1、20 MS/s、400 万点，无受控发送源；仅采集完整性检查。
- [解码测试](decoder_tests.log)：5 项通过，包括 33041 字节与字节损坏/缺失/多余的拒绝。

## 接续入口

发送程序为 [main.c](../../../software/uart_wire_test/main.c)，构建入口 [build_uart_wire_test.py](../../../scripts/repair/build_uart_wire_test.py)。复用原 OCM 启动和链接文件，邮箱在 `0x1e000`，已知公开载荷由操作者在实际运行前上传到 DDR `0x01000000`。邮箱第 4/5/6 字分别是长度、UART 分频和节流模式，第 2 字写 1 开始；应只在核对 PL、停妥 CPU1/USB/VDMA、加载本专用程序后使用。程序限制等待时间，并报告写入字节数、FNV、硬件发送数及错误；不应把它覆盖到正在运行的产品进程。

采集入口为 [slogic_capture.py](../../../scripts/repair/slogic_capture.py)，需要用户便携包中的 `libusb-1.0.dll`，不安装依赖或分发该二进制。使用厂商的 `0xb1` 四字节启动命令及 `0x81` bulk 端点，双通道每字节打包 4 个采样；实现依据和适用型号写在源码中。原始数据只留在被忽略的 output/，不提交通信载荷。

[uart_wire_decode.py](../../../scripts/repair/uart_wire_decode.py)校验原始文件与完成清单的长度/SHA，再从 CH0 解码 1 Mbaud、8N1；只有完整字节数量及内容都相同、无帧错误才通过。离线测试为 [test_uart_wire_decode.py](../../../tests/test_uart_wire_decode.py)。生产速率/饱和速率样本可在板卡重新可见后分别验证，不预先写为通过。

单端接法将 ESP8266 完全移除，接线已获用户确认；仅测 FPGA 到现有线缆和分析仪输入的通路。即使后续通过，也不能排除原模块接入时的负载、电平或接收/转发问题。当前缺少 JTAG 枚举，不能证明 FPGA 已配置、程序在运行或屏幕已恢复。
