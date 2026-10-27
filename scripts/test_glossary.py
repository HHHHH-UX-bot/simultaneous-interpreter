"""Build and run tests/glossary_test.c; every session message it prints must be valid JSON."""
import argparse, json, os, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--gcc", default=os.environ.get("CC", "gcc"))
    a = p.parse_args()
    work = ROOT / "tmp/glossary"
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "glossary_test.exe"
    subprocess.run([a.gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(ROOT / "tests/glossary_test.c"), str(ROOT / "software/network_ui/glossary.c"), "-o", str(exe)],
                   check=True, cwd=ROOT)
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True)
    lines = result.stdout.decode("utf-8").splitlines()
    if result.returncode or not lines or not lines[-1].startswith("PASS_GLOSSARY"):
        raise SystemExit(result.stdout.decode("utf-8", "replace") + result.stderr.decode("utf-8", "replace"))
    sessions = [json.loads(line) for line in lines[:-1]]
    zh, en, capped = (s["session"]["translation"]["corpus"]["phrases"] for s in sessions)
    assert zh == {"会议": "meeting", "引号": 'say "hi" \\ bye', "FPGA": "FPGA"}, zh
    assert en == {"meeting": "会议", 'say "hi" \\ bye': "引号", "FPGA": "FPGA"}, en
    assert len(capped) > 20
    assert all(s["session"]["translation"]["same_language_skip_options"] == {"skip_text": False, "skip_audio": False} for s in sessions)
    print(lines[-1])


if __name__ == "__main__":
    main()
