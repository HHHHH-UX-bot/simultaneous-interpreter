"""Build and run tests/sd_model_test.c: the TF card model loader on host FAT32 images."""
import argparse, os, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--gcc", default=os.environ.get("CC", "gcc"))
    a = p.parse_args()
    work = ROOT / "tmp/sd_model"
    work.mkdir(parents=True, exist_ok=True)
    exe = work / "sd_model_test.exe"
    sources = ["tests/sd_model_test.c", "software/network_ui/sd_model.c",
               "third_party/mbedtls/library/sha256.c", "third_party/mbedtls/library/platform_util.c"]
    subprocess.run([a.gcc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-DSD_MODEL_HOST", "-DZ7_TINYQWEN",
                    "-I", str(ROOT / "software/network_ui"), "-I", str(ROOT / "third_party/mbedtls/include"),
                    *[str(ROOT / s) for s in sources], "-o", str(exe)], check=True, cwd=ROOT)
    result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
    print(result.stdout + result.stderr, end="")
    if result.returncode or "PASS_SD_MODEL" not in result.stdout:
        raise SystemExit("sd_model_test failed")


if __name__ == "__main__":
    main()
