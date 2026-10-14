# 动态中文字幕演示

## 实现范围

核验日期：2026-10-11；本专题负责 P2b-3 的实现、地址扩展和操作方法。实际板上结果与用户验收以 [NOW](../context/NOW.md) 及对应执行记录为准。

当前采用最小裸机 C 程序绘制 DDR 双缓冲，沿用 [FRAMEBUFFER](FRAMEBUFFER.md) 的 HDMI 时序、像素格式、VDMA 和安全翻页。它是可验证的原型阶段，没有据此决定最终产品采用裸机、FreeRTOS 或 Linux/LVGL。

画面保持 [UI_SPEC](UI_SPEC.md) 的“译声”布局：中文主卡、英语原文、处理阶段、右侧最近两条定稿和固定底栏。始终显示“模拟演示”和“触摸待接入”；音频与后端状态也明确标为模拟。程序循环六对预设文本，不读取音频、不联网推理、不处理 USB 触摸。设置按钮当前仅是视觉占位，不能声明实际设置功能。

## 源码职责

| 位置 | 职责 |
|---|---|
| [ui_model](../../software/subtitle_ui/ui_model.h) | 有界会话、定稿历史、版本与旧会话隔离 |
| [ui_render](../../software/subtitle_ui/ui_render.c) | 中文、英语、分页、三档字号及固定布局；ARM 与电脑使用同一实现 |
| [ui_demo](../../software/subtitle_ui/ui_demo.c) | 预设识别草稿、识别定稿、翻译中和中文定稿的计时驱动 |
| [main](../../software/subtitle_ui/main.c)、[link](../../software/subtitle_ui/link.ld) | 板端定时器、双缓冲、保护区、调试邮箱和捕获快照 |
| [字幕及标签输入](../../assets/subtitle_demo.json)、[字体生成器](../../scripts/generate_subtitle_assets.py) | 生成 UTF-32 文本和 4 位覆盖率字形；来源及字符集由生成清单维护 |
| [构建与启动](../../scripts/run_subtitle_ui.py) | 绑定当前字体、模型测试、电脑渲染、ARM ELF 与已验证 PL 构建 |
| [板端观察及测试控制](../../scripts/ui_debug.py)、[抓屏](../../scripts/ps_subtitle_ui.tcl) | 读取计数、JTAG 语义命令、稳定活动缓冲捕获；不代替真实触摸 |
| [像素比较](../../scripts/compare_subtitle_capture.py) | 将板上不可变状态用电脑端同一绘制函数重建，比较整帧像素 |

## 字符、状态与内存边界

模型文本采用 UTF-32：每段最多 255 个 Unicode 标量加结束符；超限或无效标量原子拒绝。内部同时只处理一个片段；原文版本与译文版本分别检查。中文定稿须先收到匹配的原文定稿及 `UI_TRANSLATING_EVENT`，按当前 epoch/segment 去重；已定稿片段在本 epoch 不重新打开。当前中文 partial 关闭。后续网络入口需明确把外部 session 映射到内部 epoch，而不能忽略 [INTERFACES](INTERFACES.md) 的会话归属。

历史固定保存四条，屏上显示最近两条；草稿不进入历史。暂停取消未定稿并保留最后定稿；恢复换 epoch 并保留历史；清空换 epoch、停止到就绪并清除记录。字号和原文开关保持。全部模型和绘制状态使用固定内存，没有堆分配或无限追加日志。

主字幕字号为 28/36/44；其他标签和原文使用相同资产内的小字号。长句按实际字形宽度换行、约四秒翻页；最长演示句保留 28 秒，使各页可见。字形左右 bearing 在文本区域内预留，禁止把合法字形裁切当成布局成功。

字库目前是演示用子集，**不是完整中文字库**。字符集、字体来源与资产大小的唯一清单是 [assets_manifest](../../output/subtitle_ui/assets_manifest.json)。未知字形使当前板端演示失败并报告错误，不能直接接收任意真实译文。正式产品需要补足字符覆盖和字体再分发许可；当前系统字体生成结果不等于产品许可已核清。

显示缓冲、VDMA 和视频寄存器的权威布局仍由 FRAMEBUFFER 维护。新增区域由 link.ld 约束：

| 区域 | 地址与范围 | 用途 |
|---|---|---|
| UI 状态报告 | `0x0001E080`，64 字节 | seqlock、快照布局、绘制耗时、epoch/历史/定稿计数、命令确认 |
| 测试命令邮箱 | `0x0001E0C0`，32 字节 | 请求序号、预期 epoch、语义命令与接受/拒绝结果 |
| 字体与文本资产 | `0x01800000`，预留 2 MiB | ARM ELF 的只读 DDR 载荷 |
| 两份不可变渲染快照 | `0x01B00000`，预留 64 KiB | 每个显示缓冲对应一份状态、时间、页号及绘制统计 |

ARM 编译显式使用 32 位枚举；静态断言和链接区大小检查保证电脑/ARM 模型均为 11,384 字节、每份快照为 11,448 字节。每幅显示缓冲前后各有 256 字节保护区，每次绘制后检查。CPU 缓存/MMU 仍沿用最小已验证路径关闭；后续性能优化必须加入正确的 Cache 同步。

`last_render_ms/max_render_ms` 使用硬件经过时间；JTAG 在绘制中暂停 CPU 会计入该次耗时。因此全局最大值可能包含调试暂停，不能直接当作稳态绘制性能。持续检查中每次采样的最近绘制耗时另行分析，采样值也不能冒充全部帧的性能分布。

## 构建、启动和观察

使用已有 machine Python、ARM GCC、Vivado 和 XSDB，无新增依赖。以下为方法模板，执行权限来自当前用户指令；模板本身不授权烧录：

```powershell
python scripts/validate_subtitle_ui.py --gcc '<电脑端 GCC exe>'
python scripts/validate_subtitle_demo.py --gcc '<电脑端 GCC exe>'
python scripts/run_subtitle_ui.py build --gcc '<ARM GCC exe>'
python scripts/run_subtitle_ui.py program --vivado '<vivado.bat>' --xsdb '<xsdb.bat>'
python scripts/run_subtitle_ui.py run --vivado '<vivado.bat>' --xsdb '<xsdb.bat>'
python scripts/ui_debug.py sample --vivado '<vivado.bat>' --xsdb '<xsdb.bat>'
python scripts/ui_debug.py capture --vivado '<vivado.bat>' --xsdb '<xsdb.bat>'
python scripts/compare_subtitle_capture.py '<本次硬件捕获目录>' --gcc-directory '<电脑端 GCC bin>'
```

UI 构建会选择新的 ELF 给共享显示驱动；通用 `run_framebuffer.py build-app` 则重新选择原图形测试 ELF。两种应用源码和独立执行快照均保留；不能混用 UI 构建记录和图形测试应用。断电后必须再次配置与初始化；一次配置仅允许一次 PS 初始化，规则见 FRAMEBUFFER。

测试命令为 `start/pause/resume/clear/font-minus/font-plus/source-toggle`。`ui_debug.py command --command ...` 通过 JTAG 写入邮箱；可附 `--expected-epoch` 验证旧会话命令拒绝。命令确认要求板上接受/拒绝结果及新的显示代次，不能只以写寄存器成功判定控件生效。真实触摸尚未接通。

所有观察/控制先验证实时 FPGA 配置，再检查当前位流、ELF、独立运行快照及配置令牌；只读调试进程若未退出，同样关闭共享硬件操作门闩。抓屏暂停 CPU 生产者但保持 VDMA 读取，等待实际活动缓冲稳定，读取对应快照和整帧后恢复 CPU。停核可能中断 seqlock 写入，故捕获使用停核前已核对的不可变布局及实际 VDMA 缓冲，不在停核后等待写者完成。

## 验证证据与范围

- [字体检查](../../output/subtitle_ui/assets_validation.json)：字体元数据、打包位图及文本字符覆盖。
- [模型回归](../../output/subtitle_ui/model_tests.json)：旧 epoch/版本、暂停/恢复、清空、Unicode 和有界历史。
- [模拟计时回归](../../output/subtitle_ui/demo_tests.json)：真实演示驱动的阶段顺序、长句停留、恢复/清空、计时回绕；模拟一小时不等于真实板上长期运行。
- [电脑渲染检查](../../output/subtitle_ui/host_validation.json)：同一 C 绘制代码的状态、三档字号、所有长句页、字形边缘、缓冲保护及计时边界。
- [ARM 构建](../../output/subtitle_ui/build.json)：ELF 哈希、加载区域、模型/快照布局及源码绑定。
- [实际捕获](../../output/subtitle_ui/latest_capture.json)、[逐像素比较](../../output/subtitle_ui/pixel_comparison.json)、[用户实屏确认](../../output/subtitle_ui/user_acceptance.json)：分别核对板上字形/布局与实际 HDMI 清晰度，不将任一结果扩大为真实触摸或翻译能力。

- [控件对照](../../output/subtitle_ui/controls.json)：字号、原文显隐、清空/启动及暂停历史保护；原 START 判据修正与独立复核保留。
- [决定性旧会话对照](../../output/subtitle_ui/epoch_verification.json)：同一合法字号命令，旧 epoch 拒绝且完整模型字节不变，当前 epoch 接受；三份实际帧均逐像素一致，最后恢复标准字号与运行态。
- [暂停/恢复](../../output/subtitle_ui/resume_verification.json)：接受的暂停/恢复转换、换 epoch 与定稿历史保留。
- [真实长期运行](../../output/subtitle_ui/soak.json)：1842.266 秒、22 次实际采样，新增 110577 显示帧、2549 次换缓冲和 113 条模拟定稿，欠载/格式/DMA 错误为 0，缓冲保护通过。固定分配不随历史增长；非全 DDR 资格测试。
- [计时范围](../../output/subtitle_ui/render_timing_observation.json)：采样绘制耗时与调试停核计入最大值的解释。
- [独立评审](../../output/subtitle_ui/review.json)：实现和安全门闩的重要问题已修正；评审不替代硬件证据。

板上启动、实际像素、JTAG 命令、至少 30 分钟运行和用户肉眼验收分别取证。电脑预览或像素比对不能证明 HDMI 链路清晰稳定；模拟演示也不等于实际识别和翻译。
