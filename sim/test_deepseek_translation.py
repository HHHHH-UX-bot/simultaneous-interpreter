"""Offline contract, bounded input, transport deadline and private-key checks."""
import hashlib
import json
import sys
import threading
import time
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"software/model_backend"))
import deepseek_translation as cloud


def reply(text="请确认每个人都能听清发言。", finish="stop"):
    return json.dumps({"model": "deepseek-flash", "choices": [
        {"finish_reason": finish, "message": {"role": "assistant", "content": text}}],
        "usage": {"prompt_tokens": 60, "completion_tokens": 15, "total_tokens": 75}},
        ensure_ascii=False).encode("utf-8")


class Transport:
    def __init__(self):
        self.interrupted = threading.Event()

    def settimeout(self, timeout):
        self.timeout = timeout

    def shutdown(self, mode):
        self.interrupted.set()


class Connection:
    def __init__(self, raw, status=200, stall=False):
        self.raw, self.status, self.stall = raw, status, stall
        self.sock = Transport()
        self.closed = False
        self.sent = []

    def connect(self):
        pass

    def request(self, method, path, body, headers):
        self.sent.append((method, path, body, headers))

    def getresponse(self):
        return self

    def read(self, limit):
        if self.stall:
            if not self.sock.interrupted.wait(1):
                raise AssertionError("Deadline did not interrupt stalled response")
            raise OSError("mock private transport detail")
        return self.raw[:limit]

    def close(self):
        self.closed = True


class Tests(unittest.TestCase):
    def run_reply(self, raw, reason=None, status=200):
        connection = Connection(raw, status)
        translator = cloud.DeepSeekTranslator("test-only-key")
        with patch.object(cloud.http.client, "HTTPSConnection", return_value=connection):
            if reason:
                with self.assertRaises(cloud.TranslationError) as raised:
                    translator.translate("Please confirm everyone can hear the speaker.")
                self.assertEqual(raised.exception.reason, reason)
                self.assertNotIn("test-only-key", str(raised.exception))
                self.assertIsNone(translator.last_result)
            else:
                text = translator.translate("Please confirm everyone can hear the speaker.")
                self.assertEqual(text, "请确认每个人都能听清发言。")
                self.assertEqual(translator.last_result["Thinking"], "disabled")
        self.assertTrue(connection.closed)
        return connection, translator

    def test_valid_official_contract(self):
        connection, translator = self.run_reply(reply())
        self.assertEqual(len(connection.sent), 1)
        method, path, body, headers = connection.sent[0]
        self.assertEqual((method, path), ("POST", "/chat/completions"))
        payload = json.loads(body)
        self.assertEqual(payload["model"], "deepseek-flash")
        self.assertEqual(payload["thinking"], {"type": "disabled"})
        self.assertFalse(payload["stream"])
        self.assertEqual(payload["max_tokens"], 384)
        self.assertEqual(headers["Authorization"], "Bearer test-only-key")
        self.assertNotIn("test-only-key", body.decode("utf-8"))
        self.assertNotIn("test-only-key", json.dumps(translator.last_result))

    def test_bad_input_has_no_request(self):
        for source in ("", "x"*256, "x\n", "\ud800", "\U0001f600"*193, None):
            with self.subTest(source_type=type(source).__name__):
                with patch.object(cloud.http.client, "HTTPSConnection") as factory:
                    with self.assertRaises(cloud.TranslationError):
                        cloud.DeepSeekTranslator("test-only-key").translate(source)
                    factory.assert_not_called()

    def test_key_header_injection_rejected(self):
        for key in ("", "key\r\nInjected: 1", "key with space", "密钥", "k"*4097):
            with self.assertRaises(cloud.TranslationError):
                cloud.DeepSeekTranslator(key)

    def test_http_error_does_not_read_or_retry(self):
        connection, _ = self.run_reply(b"test-only-key private body", "http_error", 402)
        self.assertEqual(len(connection.sent), 1)

    def test_key_echo_is_not_exported(self):
        self.run_reply(reply("test-only-key"), "response_contains_key")

    def test_escaped_key_echo_is_not_exported(self):
        self.run_reply(reply("test-only-key").replace(b"test-only-key", b"\\u0074est-only-key"),
                       "response_contains_key")

    def test_failure_clears_previous_result(self):
        _, translator = self.run_reply(reply())
        with self.assertRaises(cloud.TranslationError):
            translator.translate("")
        self.assertIsNone(translator.last_result)

    def test_response_size(self):
        self.run_reply(b" "*(cloud.MAX_RESPONSE_BYTES+1), "response_size")

    def test_invalid_json(self):
        self.run_reply(b"invalid provider body", "response_format")

    def test_missing_fields(self):
        self.run_reply(b"{}", "response_format")

    def test_multiple_choices(self):
        value = json.loads(reply())
        value["choices"] *= 2
        self.run_reply(json.dumps(value).encode(), "response_choices")

    def test_truncated_translation_is_not_accepted(self):
        self.run_reply(reply(finish="length"), "incomplete_translation")

    def test_empty_or_long_translation(self):
        for text in (" ", "文"*256):
            self.run_reply(reply(text), "text_length")

    def test_control_character_translation(self):
        self.run_reply(reply("中\x00文"), "text_characters")

    def test_null_translation(self):
        value = json.loads(reply())
        value["choices"][0]["message"]["content"] = None
        self.run_reply(json.dumps(value).encode(), "response_format")

    def test_invalid_usage(self):
        value = json.loads(reply())
        value["usage"]["total_tokens"] = True
        self.run_reply(json.dumps(value).encode(), "response_usage")

    def test_stalled_body_is_interrupted(self):
        connection = Connection(reply(), stall=True)
        started = time.monotonic()
        with patch.object(cloud.http.client, "HTTPSConnection", return_value=connection):
            with self.assertRaises(cloud.TranslationError) as raised:
                cloud.DeepSeekTranslator("test-only-key", timeout=0.05).translate("Hello.")
        self.assertEqual(raised.exception.reason, "timeout")
        self.assertLess(time.monotonic()-started, 0.5)
        self.assertTrue(connection.closed)


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Tests)
    result = unittest.TextTestRunner(verbosity=1).run(suite)
    record = {"Status": "passed" if result.wasSuccessful() else "failed", "Tests": result.testsRun,
              "Scope": "Offline mocks only; no credentials, network or API calls", "SourceHashes": {
        str(path.relative_to(ROOT)).replace("\\", "/"): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in (Path(__file__), ROOT/"software/model_backend/deepseek_translation.py",
                     ROOT/"scripts/run_deepseek_translation.py")}}
    target = ROOT/"output/model_backend/deepseek/offline_validation.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(record, indent=2)+"\n", encoding="utf-8")
    raise SystemExit(0 if result.wasSuccessful() else 1)
