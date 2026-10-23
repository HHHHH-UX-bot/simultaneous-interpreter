# 语音控制与帧格式的主机测试

在工程根目录运行 `python tests/voice_llm/run_unit_tests.py --cc <本机原生 gcc>`。使用普通电脑 GCC，不是 ARM 交叉编译器；只需要 Python 标准库。

测试链接实际 `local_voice.c`，验证识别提交、TTS 预连接、发送顺序、播放排空、暂停和模式切换；云端、模型和 DAC 均为明确的测试替身。帧测试编译实际 `cloud_client.c` 的发送函数，解码其掩码帧并检查唯一事件编号、转义、长度边界与 commit/finish 顺序。

这些测试不访问硬件，也不证明模型效果、音频听感或云服务可用。实板证据见 [语音说明](../../docs/design/LOCAL_VOICE_LLM.md)。
