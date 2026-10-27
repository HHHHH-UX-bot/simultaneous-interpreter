# 功能整理验证：调试开关、面对面对话、术语表与删除旧功能（2026-10-27）

对象：Z7-Nano 7010 SD 启动；Vivado/Vitis 2026.1、ARM GCC 13.4。决定见 [D27](../../context/DECISIONS.md#d27-功能整理调试开关面对面对话术语表删除旧功能2026-10-27)，界面见 [UI_SPEC](../../design/UI_SPEC.md#调试信息开关与面对面对话2026-10-27)，术语表见 [SD_BOOT](../../design/SD_BOOT.md#术语表2026-10-27)。

## 主机与构建

| 项目 | 结果 |
| --- | --- |
| 全量 PC 回归 `scripts/run_host_tests.py` | 27/27 通过 |
| 术语表 `scripts/test_glossary.py` | `PASS_GLOSSARY accepted_from_4k=43 session_bytes=1343`：BOM、注释、CRLF、坏行跳过、引号与反斜杠转义、两个方向、容量上限；会话消息逐条按 JSON 解析；示例文件 5 条全部生效 |
| 卡上读取 `scripts/test_sd_model.py` | 加入 `GLOSSARY.TXT` 目录项后仍通过，读到 2 条、跳过 1 条坏行 |
| 界面 `scripts/test_speaker_ui.py` | 调试开关只在设置页生效；语言菜单第三行进入对话；上下两组说话按钮各自切方向，已在该方向时按暂停/空闲给出继续/开始；两个退出按钮都能退出；对话画面无缺字、无裁切 |
| 删除传输分支前后对照 | 用相同参数各构建一次产品 ELF：`.text` SHA256 `d69d6145…`、`.data` SHA256 `62530d3f…` 两次相同，各段大小相同，只有调试信息不同 |
| ARM 构建（`--sd-boot --tinyqwen`） | ELF SHA256 `b5a198f148a19cb0ca970f8fc1bd8ed52dbc5584ecd6294162721d9e80742b82`；APP_DDR 569,256 B，SNAPSHOT_DDR 284,476 B，CLOUD_DDR 262,272 B，REMOTE_DDR 8,739,360 B。面对面对话首版用 2.4 MB 旋转缓冲，链接时 APP_DDR 溢出，已改为在帧缓冲内旋转 |
| BOOT.BIN | 4,990,208 字节，SHA256 `34c3eb9bca502aed326b338f2e7b743d6d53566708bb5c20f3b8f47702c745c6` |

## 板上

写卡：`write_sd_card.py <卷路径> --boot-only --glossary assets/glossary.example.txt`，`BOOT.BIN` 与 `glossary.txt`（364 字节，SHA256 `c989759d…`）回读一致；Qwen 与 `z7cfg.bin` 未动。之后均为 JTAG 只读（不停核、不写内存）。

| 项目 | 结果 |
| --- | --- |
| 第 1 次上电 | 画面正常，触摸无反应。CPU1 已枚举 `1a86:e5e3`（1 个 HID 接口），心跳在走，但原始 HID 报告为 0，CPU0 一次点击也没收到。CPU0 侧模型就绪、术语表 5 条。与 [R3](../../context/RISKS.md) 记录的间歇零报告一致 |
| 第 2 次（完整断电后）上电 | 触摸正常：观察期内 1,306 条 HID 报告、CPU0 收到 59 次点击 |
| 模型与术语表 | 应用 5.47 秒开始读卡，69.4 秒两份摘要一致并就绪；`glossary_terms`=5 |
| 用户确认 | 调试信息开关、面对面对话、术语表都试过，都正常；识别正常 |

## 未验证

- 只做了一次短测：对话模式下的长时间轮流使用、两个方向切换时的连接耗时，没有单独测量。
- 术语表只用了 5 条示例；接近 1200 字节上限的会话消息对连接与延迟的影响没在板上测。
- 触摸零报告的根因仍未找到，这次是完整断电重启恢复的。
