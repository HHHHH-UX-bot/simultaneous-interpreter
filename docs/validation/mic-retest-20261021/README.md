# 真实麦克风复测与定稿阻塞

记录范围说明（2026-10-21 文档核对）：本文及 JSON/哈希只证明以下原日期、对象、源码和测试范围，不是此刻板态，也不因新功能而重算旧结果。最新任务与已知故障看 [NOW](../../context/NOW.md)，历史页需求看 [UI_SPEC](../../design/UI_SPEC.md#板端历史记录页2026-10-21)。

2026-10-21，Z7-Nano 7010、AN831、ESP8266 1 Mbaud、CYEA。用户要求复测真实麦克风，以区分说话时机与产品故障。沿用已加载的语言菜单固件，无重新下载、复位或业务代码修改；不是固定音频自测。板卡身份和 ELF 绑定见 [session](session.json)。

## 两句实际短测

用户在会话就绪后、不操作按钮的条件下说 “Good morning.” 和 “I would like a cup of coffee.”，确认“两句都有新的英文和中文”。9 个公开状态采样相隔 63.9 秒：ASR 1→3、翻译 3→5、上传页 9734→10133、音频时间轴增加 63940 ms；epoch=20 未变化，network=4/error0、client=4、cloud=1/error0。UART 帧错误/溢出、采样溢出及上传丢页均为零。云失败和 TLS 失败锁存各为 1，短测期间均未增加；不能把旧码清零后声称本次固件从未出错。完整计数见 [observation](observation.json)。

这验证了当前两句的采音、上传、识别、翻译和实际文字预览，未验证麦克风声学质量、听感或长期稳定性。本轮未导出原始录音、真实语音文本、密钥或真实 utterance ID。

## 实际故障一：有预览，但定稿队列堵塞

实际快照中 committed 维持 0。按生成顺序解析已有公共诊断字段：

| 顺序 | 原文 ready / 有文本 | 译文 ready / 有文本 | response 完成 | 结果 |
| --- | --- | --- | --- | --- |
| 1、2 | 否 / 否 | 是 / 是 | 是 | 队头等原文 |
| 3、4、5 | 是 / 是 | 是 / 是 | 是 | 被旧队头阻塞 |

现场见 [queue](queue.json)。`cloud_commit()` 只提交最早配对，缺原文即返回；`cloud_preview()` 仍可显示最新译文。因此“看到新的两种文字”不能代替定稿/历史正确。已经确认阻塞条件，尚未记录两条真实缺失原文的完整事件顺序，不能断言是空 ASR、ID 关联错误、服务端缺事件或语言菜单本身造成。

对实际 C 代码执行独立的合成事件诊断：正常配对提交 1 条；前置一个无原文的完成译文、先空 ASR 后译文、先译文后空 ASR三种顺序均使后续完整配对无法提交，但预览和 error0 继续存在。补回匹配的非空 ASR 后两条同时提交。见 [运行结果](queue_repro.log.txt)和[诊断源码](queue_repro.c.txt)；复制源码到 `tmp/mic_retest_20261021/queue_repro.c` 可复用相对 include，使用 `validate_english_profile.py` 相同源码/编译选项及 `Z7_TEXT_ONLY_RESPONSE` 编译。该合成输入证明等待/清理逻辑能形成阻塞，不冒充实际服务端事件回放，也不是修复通过。

## 实际故障二：后续又收到云端 ASR 错误

短测结束后的只读诊断期间，实际再次进入 cloud phase6/error3，公共码 `UNEXPECTED_ASR_ERROR`，云失败计数 1→2；TLS 失败仍为 1，UART、采样溢出和上传丢页仍为零。见 [后续故障](later_fault.json)。这次失败不在上述 63.9 秒短测窗口内；用户在后续期间是否说话没有单独确认，不能写成受控长静默实验。该错误和队列阻塞是否同源尚未确定。

保存现场后，通过已有 UI_RESUME 执行一次受控恢复，没有新增自动重试、跳过队头、填造原文或超时丢句策略。命令被接受，见 [resume](resume_command.log.txt)；后续实际读取 network4/error0、cloud1/error0、client4、UI_RUNNING/LISTENING、language0、epoch22，见 [recovery](recovery.json)。新会话配对计数已重建，历史错误锁存保留；未要求用户再次说话，不把就绪状态写成第二次真人语音验收。CPU1 触摸生命周期未更动。

## 下一阶段

原排障计划保留：需区分“空 ASR 已结束”与“非空 ASR 尚未到达”，追踪真实缺源事件再定点修正。用户随后要求本轮暂缓该问题、先做历史页；故障仍未修复，不能把优先级调整解释为已通过。独立保存 UNEXPECTED_ASR_ERROR，后续恢复排障时再补多句/切换/静默对照。

来源指纹、范围与汇总由 [stage](stage.json)维护。当前板态由 [NOW](../../context/NOW.md)维护，方法仍遵循 [RUNBOOK](../../context/RUNBOOK.md#c-云端语音接入)。未写 Flash/SD 或 ESP 固件。
