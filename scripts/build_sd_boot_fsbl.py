"""Generate the Zynq FSBL from the SD-boot PS platform (Vitis 2026.1 Python API).

Run with: vitis -s scripts/build_sd_boot_fsbl.py <sd_boot_out_dir>
XSCT is disabled in 2026.1; creating a standalone platform component from the
XSA builds the zynq_fsbl boot component. software/sd_boot/fsbl_hooks.c then
replaces the generated hooks, FSBL_DEBUG enables UART0 boot messages, and the
FSBL is rebuilt and copied to <out>/fsbl.elf.
On a Chinese-locale Windows the Vitis server prints a GBK line the API cannot
decode; run under "chcp 65001" (scripts/build_sd_boot.py does).
"""
import os
import re
import shutil
import subprocess
import sys

import vitis

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out = os.path.abspath(sys.argv[1])
workspace = os.path.join(out, "ws")
if os.path.isdir(workspace):
    # Generated device-tree headers are read-only.
    shutil.rmtree(workspace, onerror=lambda func, path, _: (os.chmod(path, 0o666), func(path)))
client = vitis.create_client()
client.set_workspace(workspace)
platform = client.create_platform_component(name="sdboot", hw_design=os.path.join(out, "sd_boot_ps.xsa"),
                                            os="standalone", cpu="ps7_cortexa9_0", domain_name="standalone_a9_0")
platform.build()
vitis.dispose()

fsbl = os.path.join(workspace, "sdboot", "zynq_fsbl")
for name in ("fsbl_hooks.c", "sd_boot.h"):
    shutil.copy2(os.path.join(root, "software", "sd_boot", name), os.path.join(fsbl, name))
config = os.path.join(fsbl, "UserConfig.cmake")
text = open(config, encoding="utf-8").read()
text, count = re.subn(r'set\(USER_COMPILE_DEFINITIONS\s*\n""', 'set(USER_COMPILE_DEFINITIONS\n"FSBL_DEBUG"', text)
if count != 1:
    raise SystemExit("UserConfig.cmake layout changed")
open(config, "w", encoding="utf-8").write(text)
build = os.path.join(fsbl, "build")
cache = open(os.path.join(build, "CMakeCache.txt"), encoding="utf-8").read()
cmake = re.search(r"^CMAKE_COMMAND:INTERNAL=(.+)$", cache, re.M).group(1).strip()
subprocess.run([cmake, build], check=True)
subprocess.run([cmake, "--build", build], check=True)
shutil.copy2(os.path.join(build, "fsbl.elf"), os.path.join(out, "fsbl.elf"))
print("PASS_SD_BOOT_FSBL")
