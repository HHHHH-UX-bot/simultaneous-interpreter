# 英中双列历史记录页

2026-10-21，按用户要求先完成[文档整理](../docs-refresh-20261021/README.md)并提交 `f848a50`，再实现历史页。定稿队列/ASR 故障明确暂缓，未修改 cloud_live.c/cloud_client.c/network.c 等云事件、采音或重试策略。交互规范由 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)维护。

## 已实现

- 当前 ARM TLS 双语构建的主界面“清空记录”改为“历史记录”，进入独立全屏；返回不改变采音/连接/epoch。
- English 固定左列、中文固定右列，每条保存自己的方向；中译英与英译中混合仍放正确列，两列按较长文本共同确定行高并同步滚动。
- RAM 保留最近64条实际定稿，超过容量移除最旧；语言切换保留历史。只存合法定稿，不从预览补造记录，不承诺断电保存。
- 单指连续拖动、完整快速滑动、上一页/下一页、滚动条、空态、清空确认/取消。清空仅清历史，保留当前字幕/会话/偏好；旧 UI_CLEAR 的全会话重置合同保留为开发命令。
- 当前模型/快照135768/135832字节，两份快照271664字节，ARM TLS的独立DDR预留扩为512KiB；旧非ARM/独立ASR仍为11392/11456，CPU1链接256字节和反馈64字节不变。无新PL资源。

## 离线验证和复核

[native_history](native_history.json)为实际C模型/渲染/触摸，377检查（启用截图输出），覆盖64条淘汰、混合方向、各种滚动偏移、快速/连续手势、松手后立即返回、清空/取消与会话保留、守卫区和英中列映射。输入为公开示例，不是真人识别质量验证。

默认译音/仅文本两套各14678检查、原有500条定稿及4次切换；英译英11610，中译英251，中译中1640，原显示调试6项/网络客户端12项回归通过。[文本模式](native_text.json)、[译音模式](native_audio.json)、[英译英](native_enen.json)和[渲染](host_render.json)保留各自源码指纹。构建/验证方法为 `validate_history_ui.py`、原模式验证脚本和显式产品构建参数。

[独立复核](review.json)发现并修正了选错linker预留、旧松手/新按下混用，以及未采到按下的快速滑动遗漏。最终复核未发现其他实质问题；不据此代替ARM或实屏结果。

## 板级验证与用户验收

[产品ARM构建](product_build.json)通过；APP_DDR约410476字节/2MiB，字库2040772字节/2MiB、快照271664字节/512KiB。没有为UI更改重配PL。先确认旧会话关闭、普通AT/TCP关闭与CPU1/USB Halt，受控暖加载[离线UI夹具](fixture_build.json)，[加载](fixture_load.json)绑定其实际ELF。夹具只有20条公开示例，不联网/不采音。

- [示例历史实际帧](frame_history_latest.json)614400像素零差异；[用户确认](physical_fixture_acceptance.json)双列对齐、USB上下滑动、上一页/下一页、返回再进入及画面正常。不是仅从触摸计数推断。
- [板端清空检查](board_clear.json)：显示确认、取消保留20条、再次确认清为0条；当前字幕、会话、epoch、偏好与累计final_count=20保持不变。方法为已接受的UI命令邮箱；原生触摸命中测试另见native_history，不声称用户已手指验证确认/取消。
- [历史空态](frame_history_empty.json)和[正式版主界面](frame_product_main.json)分别614400像素零差异，截图见[空态](history-empty.png)、[正式主界面](product-main.png)。正式主界面截图在运行期配置前取得，Connecting不是连云失败。

## 当前正式运行与边界

夹具退出后按其已知空闲115200状态，CPU1/USB安全停止、普通AT与无TCP再次确认，然后暖加载真实麦克风ELF；[正式加载](product_load.json)及[实际结果](product_load_result.json)绑定ELF/同PL/父加载SHA，所用[交接脚本](restore_product.tcl.txt)没有把夹具误当1 Mbaud拥有者。正式版本Selftest=false、LocalUIFixture=false；20条示例不带入。

[CPU1触摸采样](product_touch.json)通过。运行期私有输入恢复既有Wi-Fi及百炼配置后，[公开计数](product_public_state.json)显示network phase/error=4/0、client phase=4（CLOUD_STREAMING），本次读数UART溢出/帧错为0。[用户确认](physical_product_acceptance.json)正式版本的历史入口、返回、A+状态及画面均正常。本次没有要求新的口述，不将连云/入口验收替代新真人识别、历史定稿或长期可靠性验证。当前所有权由 [NOW](../../context/NOW.md)维护。

已知定稿缺源和UNEXPECTED_ASR_ERROR仍未解决。只有成功进入UI历史的定稿才会出现；真实链路未提交时空态是如实显示，不能用示例宣布数据链路已正常。本阶段不修改云端配对、采音或重试策略，不写持久介质。
