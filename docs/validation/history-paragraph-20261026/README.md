# 历史页自动成段验证（2026-10-26）

对象：Z7-Nano 7010，SD 卡独立启动；Vivado/Vitis 2026.1、ARM GCC 13.4。规则见 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

用户反馈原历史页“一个方框一小段话”效果差，要求自动整合成大段。

## 改动

- `ui_render.c`：连续定稿按电脑导出完整文本的同一规则合并（会话或方向变化、4 句、约 360 个字符、第 3 句起的转折词另起一段），英文间补空格，中文直接相连。两列整段换行，段高取两边较大值，段间只留空白，去掉逐句底色和分隔线；段首保留首条记录的序号。
- 每条记录的 `pixel_height` 改存所在段高度按字数分摊的份额，原有“删除最旧记录时修正滚动位置”的逻辑不变。记录结构、64 条容量和模型/快照 ABI 不变。
- 段落拼接与换行暂存共 12,348 B，放在 `.ui_runtime.history`（快照之后、同一 512 KiB 预留区）：CLOUD_DDR 内 `.bss` 后面已放不下。快照、`board`、`cloud_health`、`sd_boot_diag` 等 JTAG 工具使用的地址未变。
- `tests/history_ui_test.c`：模型大小断言从 135804 改为 135940（PR #7 加 Wi-Fi 名称后已过期）。

## 结果

| 项目 | 结果 |
| --- | --- |
| 主机历史页测试 | `PASS_HISTORY_UI checks=377 capacity=64 model_bytes=135940`；预览图见本机 `output/history_ui_20261021/` |
| 主机字幕/设置回归 | `PASS_SUBTITLE_HOST_RENDER`、`PASS_SETTINGS_UI checks=39` 及其 TTS/迟到预览项 |
| ARM 构建（`--sd-boot`） | 通过；SNAPSHOT_DDR 284,444 B / 512 KiB，CLOUD_DDR 262,272 B；ELF SHA256 `0614143715f59fa58c403d241aa138386f704818bc49fe95aed3aedd57b9063d` |
| BOOT.BIN | SHA256 `a6c8374415eeba7a2d3c035461cc6f8c31db0eff11603a89a44be999f73197ed`，411,859,940 字节；FSBL、位流 `97ddda29…`、Qwen 资产不变。写卡后回读哈希一致，`z7cfg.bin` 未动 |
| 板上 | 用户换卡上电：启动正常，说几句后历史页已合并成大段，显示正常 |

替换前卡内是 SD 启动第 3 次上电验收的 `BOOT.BIN`（SHA256 `4c21987f…`）。

## 未验证

- 没有在板上专门测转折词分段、4 句/长度分段、切换方向分段和满 64 条后的滚动修正；这些只由主机测试覆盖。
- 没有经 JTAG 加载这次的 ELF。
