"""Translate the public reference once; never write the API key to files or argv."""
import argparse
import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"software/model_backend"))
from deepseek_translation import DeepSeekTranslator, TranslationError


def dialog_key(source):
    import tkinter as tk
    from tkinter import simpledialog
    window = tk.Tk()
    window.withdraw()
    window.attributes("-topmost", True)
    try:
        return simpledialog.askstring("译声 · DeepSeek API",
            "请输入官方 API key（输入隐藏，只保存在本次进程内存）。\n"
            "确定后仅发送 1 次英文测试，最多生成 384 tokens。\n\n" + source,
            show="*", parent=window)
    finally:
        window.destroy()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--key-dialog", action="store_true", required=True)
    args = parser.parse_args()
    folder = ROOT/"output/model_backend/deepseek/attempts"/datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    record = {"Status": "pending", "Attempt": folder.relative_to(ROOT).as_posix(),
              "Scope": "One public English reference text / cloud MT only; no ASR, audio, board or latency distribution"}
    key = translator = None
    try:
        previous = json.loads((ROOT/"output/model_backend/file_baseline.json").read_text(encoding="utf-8"))
        source = previous["ReferenceEnglish"]
        record.update(English=source, LocalReference={
            "Chinese": previous["Result"]["Chinese"], "MTSeconds": previous["Result"]["MTSeconds"],
            "VerifiedAtUTC": previous["VerifiedAtUTC"],
            "Scope": "Historical local MT of the same text; not a same-time performance comparison"},
            SourceHashes={str(path.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in (Path(__file__), ROOT/"software/model_backend/deepseek_translation.py")})
        key = dialog_key(source)
        if not key:
            record["Status"] = "cancelled"
        else:
            translator = DeepSeekTranslator(key.strip())
            translated = translator.translate(source)
            record.update(Status="executed", Result=translator.last_result,
                          HumanTranslationAcceptance="pending", BoardDisplay="not_attempted")
            print("DEEPSEEK_TRANSLATION " + translated, flush=True)
    except TranslationError as error:
        record.update(Status="failed", Error=error.reason, HTTPStatus=error.status)
    except (Exception, KeyboardInterrupt) as error:
        record.update(Status="failed", ErrorType=type(error).__name__)
    finally:
        key = translator = None
        text = json.dumps(record, ensure_ascii=False, indent=2) + "\n"
        (folder/"result.json").write_text(text, encoding="utf-8")
        (ROOT/"output/model_backend/deepseek/latest_text.json").write_text(text, encoding="utf-8")
    print("DEEPSEEK_TEXT_" + record["Status"].upper(), flush=True)
    return 0 if record["Status"] == "executed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
