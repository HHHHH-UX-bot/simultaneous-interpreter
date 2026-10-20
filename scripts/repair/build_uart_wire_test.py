"""Build the OCM-only, credential-free UART wire test; does not load hardware."""
import argparse
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_GCC = Path('D:/AMDDesignTools/2026.1/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--gcc',type=Path,default=DEFAULT_GCC)
    p.add_argument('--output',type=Path,default=ROOT/'output/uart_wire_test')
    args=p.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    inputs=['software/ps_bringup/start.S','software/ps_bringup/link.ld',
            'software/uart_wire_test/main.c','scripts/repair/build_uart_wire_test.py']
    elf=out/'uart_wire_test.elf'
    cmd=[str(args.gcc),'-mcpu=cortex-a9','-marm','-mfloat-abi=soft','-O2','-g',
         '-ffreestanding','-fno-builtin','-fno-stack-protector','-Wall','-Wextra','-Werror',
         '-std=c11','-nostdlib','-Wl,--build-id=none','-T',str(ROOT/inputs[1]),
         '-Wl,-Map='+str(out/'uart_wire_test.map'),str(ROOT/inputs[0]),str(ROOT/inputs[2]),'-o',str(elf)]
    result=subprocess.run(cmd,check=True,capture_output=True,text=True)
    (out/'build.log').write_text(result.stdout+result.stderr,encoding='utf-8')
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    record={'status':'built','elf_sha256':sha(elf),
            'input_sha256':{n:sha(ROOT/n) for n in inputs},
            'compiler_sha256':sha(args.gcc),'mailbox':0x1e000,'payload':0x01000000,
            'scope':'OCM UART wire stimulus; no network, USB host, UI, credentials or persistent writes'}
    (out/'build.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('PASS_UART_WIRE_BUILD',record['elf_sha256'])

if __name__=='__main__':main()
