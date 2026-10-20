"""Check the actual Zynq reset routine against a register-order model."""
import argparse,hashlib,json,subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--gcc',type=Path,required=True);a=p.parse_args()
    path=ROOT/'software/z7_nano_usb/usb_board.c';source=path.read_text()
    begin=source.index('void usb_port_reset(void) {');end=source.index('\nstatic uint8_t ulpi',begin)
    routine=source[begin:end]
    temp=ROOT/'tmp/usb_reset';temp.mkdir(exist_ok=True)
    out=ROOT/'output/z7_nano/usb/reset_validation.json'
    unit=r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#define USB_BASE 0xe0002000u
#define CHECK(c) do{if(!(c)){fprintf(stderr,"failed line %d\n",__LINE__);exit(1);}}while(0)
static uint32_t cmd,status,port,clock_ms,error,mode,pr_reads,rs_stops,pr_writes,rs_starts;
static jmp_buf failure;
volatile uint32_t usb_reset_trace[8];
uint32_t tusb_time_millis_api(void){return ++clock_ms;}
uint32_t usb_read(uintptr_t a){
    if(a==USB_BASE+0x140)return cmd;
    if(a==USB_BASE+0x144)return status;
    CHECK(a==USB_BASE+0x184);
    if((port&0x100) && mode!=2 && ++pr_reads==3)port&=~0x100u;
    return port;
}
void usb_write(uintptr_t a,uint32_t v){
    if(a==USB_BASE+0x140){
        if(!(v&1)){++rs_stops;if(mode!=1)status|=0x1000;}
        else {CHECK(rs_stops && pr_writes && !(port&0x100) && (status&0x1000));++rs_starts;if(mode!=3)status&=~0x1000u;}
        cmd=v;return;
    }
    CHECK(a==USB_BASE+0x184 && !(cmd&1) && (status&0x1000));
    CHECK((v&0x100) && !(v&0x2e));++pr_writes;port=v;
}
void usb_wait(uintptr_t a,uint32_t mask,uint32_t expected,uint32_t code){
    for(unsigned n=0;n<20;++n)if((usb_read(a)&mask)==expected)return;
    error=code;longjmp(failure,1);
}
ROUTINE
int main(void){
    for(mode=0;mode<4;++mode){
        cmd=0x88b3d;status=0;port=0x8000182f;clock_ms=error=pr_reads=rs_stops=pr_writes=rs_starts=0;
        for(unsigned n=0;n<8;++n)usb_reset_trace[n]=0;
        if(!setjmp(failure))usb_port_reset();
        CHECK(rs_stops==1);
        if(!mode){CHECK(!error && rs_starts==1 && usb_reset_trace[7]==1 && (usb_reset_trace[3]&0x1000) && !(usb_reset_trace[4]&0x100) && !(usb_reset_trace[5]&0x1000));}
        else {CHECK(error==0x105+mode && !usb_reset_trace[7]);if(mode<3)CHECK(!rs_starts);}
    }
    puts("PASS_USB_RESET_ORDER cases=4");return 0;
}
'''.replace('ROUTINE',routine)
    c=temp/'reset_test.c';c.write_text(unit);exe=temp/'reset_test.exe'
    built=subprocess.run([str(a.gcc),'-std=c11','-O2','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],capture_output=True)
    if built.returncode:raise RuntimeError(built.stderr.decode())
    result=subprocess.run([str(exe)],capture_output=True);result.check_returncode()
    record={'status':'passed','scope':'Actual board reset routine against mock registers, normal order and three hardware timeout boundaries; no USB electrical or board acceptance',
        'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'native_result':result.stdout.decode().strip()}
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(record,indent=2)+'\n');print(record['native_result'])
if __name__=='__main__':main()
