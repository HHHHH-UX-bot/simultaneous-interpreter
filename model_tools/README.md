# 可选模型重建工具

已有桌面工程日常启动直接使用 models/ 中的已验证产物。Git 仓库不携带约 400 MB Q6、约 5 MB 二进制分词器或约 988 MB BF16；新克隆需按下列步骤准备。源修订与 SHA 在已提交的 models/source_manifest.json，Qwen 的 Apache 2.0 许可在 models/QWEN_LICENSE.txt。

`fetch_qwen.py` 按 models/source_manifest.json 固定修订重新获取原始文件并核对每个 SHA256。原始 BF16 权重约 988 MB，没有重复放入本地交付包。

`export_q4.py --bits 6` 需要 NumPy，从 models/model.safetensors 输出 Q6 文件到 output/。`export_tokenizer.py` 需要官方 tokenizers 0.23.3，完整提取 BPE、分类和规范化表；用 `--help` 查看源和输出参数。重建文件须与 models/ 中固定 SHA 一致，不能跳过固件上传检查。

真正的板上运行代码在 software/network_ui/tinyqwen.*、tiny_tokenizer.* 和 tiny_board_llm.*，电脑网页不导入这些模型重建工具。

新克隆的模型重建（需要网络、NumPy 及上文指定的 tokenizers）：

```text
python model_tools/fetch_qwen.py
python model_tools/export_q4.py --bits 6
python model_tools/export_tokenizer.py models/tokenizer.json output/qwen2_0.5b.tokenizer.bin
```

将 output/ 下生成的 qwen2.5-0.5b-instruct-g64-q6.tqw 和 qwen2_0.5b.tokenizer.bin 复制到 models/。加载器要求 SHA256 分别为 e7ebb0a8435ac90173ef9cefa753adc7fe2f6a5fb8fa86757a98f1344dcf4863、b6aa0fa09ec4913e8892cc1f25c0c76f24a72dbd3074131782bea58b6a944aea，不能修改哈希以绕过验证。

FPGA 声纹的固定 32 KiB 模型另在 assets/speaker_cnn/；运行 python scripts/prepare_speaker_firmware.py 即可生成 output/speaker_cnn/model_weights.c，不需重新训练。用户登记的声纹仅存在运行期 RAM。
