#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wifi_at_probe.h"
static unsigned char fifo[4096];
static char command[64];
static uint32_t head,tail,command_size,divider,module_divider,clock_ms,tx_until;
static uint32_t sent,received,framing,overflow,echo,scenario,checks,rate_sets;
static void check(int ok,const char *name) {++checks;if(!ok) {fprintf(stderr,"FAIL %s\n",name);exit(1);}}
static void enqueue(const char *text) {while(*text) {fifo[tail++%sizeof(fifo)]=(unsigned char)*text++;++received;}}
static uint32_t read_reg(uint32_t offset) {
    switch(offset) {
    case 0:return 0x57494631u;
    case 4:return (head!=tail?1u:0u)|2u|((int32_t)(clock_ms-tx_until)<0?4u:0u)|(scenario==5?0u:0x100u);
    case 8:return divider;
    case 20:return head!=tail?0x80000000u|fifo[head++%sizeof(fifo)]:0;
    case 32:return overflow;case 36:return framing;case 40:return sent;case 44:return received;
    default:fprintf(stderr,"Unexpected read %u\n",offset);exit(1);
    }
}
static void module_command(void) {
    if(echo) enqueue(command);
    if(!strcmp(command,"\r\n")) return;
    if(!strcmp(command,"AT\r\n")) {if(scenario!=4) enqueue("\r\nOK\r\n");}
    else if(!strcmp(command,"ATE0\r\n")) {echo=0;enqueue("\r\nOK\r\n");}
    else if(!strcmp(command,"AT+GMR\r\n")) enqueue("\r\nAT version:2.2.1\r\nSDK version:3.4\r\nOK\r\n");
    else if(!strcmp(command,"AT+UART_CUR=921600,8,1,0,0\r\n")) {
        ++rate_sets;
        if(scenario==2) enqueue("\r\nERROR\r\n");
        else {module_divider=54;enqueue("\r\nOK\r\n");if(scenario==1) framing=1;}
    } else if(!strcmp(command,"AT+UART_CUR?\r\n")) {
        if(scenario==3) enqueue("\r\n+UART_CUR:115200,8,1,0,0\r\nOK\r\n");
        else if(scenario==6) enqueue("\r\n+UART_CUR:921600,8,1,0,3\r\nOK\r\n");
        else enqueue("\r\n+UART_CUR:921600,8,1,0,0\r\nOK\r\n");
    } else {fprintf(stderr,"Unexpected module command\n");exit(1);}
}
static void write_reg(uint32_t offset,uint32_t value) {
    if(offset==12) {check(value==15,"atomic FIFO/stat reset");head=tail=sent=received=framing=overflow=0;return;}
    if(offset==8) {
        check((read_reg(4)&0x10cu)==0x100u,"divider changes only on idle high line");
        check(value==54,"only accepted fast divisor selected");divider=value;return;
    }
    check(offset==16,"writes only defined UART registers");
    ++sent;tx_until=clock_ms+10;
    if(divider!=module_divider) {framing=1;return;}
    check(command_size+1<sizeof(command),"module command bounded");
    command[command_size++]=(char)value;command[command_size]=0;
    if(value=='\n') {module_command();command_size=0;}
}
static void reset(uint32_t kind,uint32_t module_rate) {
    head=tail=command_size=clock_ms=tx_until=sent=received=framing=overflow=rate_sets=0;
    scenario=kind;divider=434;module_divider=module_rate;echo=1;
    wifi_probe_init(read_reg,write_reg,clock_ms);
}
static void run(void) {
    for(unsigned i=0;i<200 && wifi_info.phase!=2 && wifi_info.phase!=6;++i) {clock_ms+=50;wifi_probe_tick(clock_ms);}
}
static void success(const char *name) {
    check(wifi_info.phase==2 && !wifi_info.error,name);
    check(wifi_info.at_ok && wifi_info.echo_off && wifi_info.firmware_ok,"fresh high-rate AT/echo/firmware proof");
    check(wifi_info.reserved0==54 && wifi_info.reserved1==921600,"actual divisor and queried module rate");
    check(!framing && !overflow && sent==36,"fresh counters include all five acceptance commands");
}
int main(void) {
    reset(0,434);run();success("cold module switches to high rate");check(rate_sets==1,"one volatile rate command");
    reset(0,54);run();success("warm module survives JTAG reload");check(!rate_sets,"warm path does not rewrite module rate");
    reset(1,434);run();success("rate-change reply can arrive at the new rate");
    reset(2,434);run();check(wifi_info.phase==6 && !wifi_info.at_ok,"unsupported setting cannot certify high rate");
    reset(3,54);run();check(wifi_info.error==10,"wrong module query rate rejected");
    reset(6,54);run();check(wifi_info.error==10,"unwired flow control rejected");
    reset(4,434);run();check(wifi_info.error==8 && !wifi_info.at_ok,"missing module times out at two known rates");
    reset(5,434);run();check(wifi_info.error==9 && !wifi_info.at_ok,"low RX line prevents unsafe divider write");
    reset(0,434);clock_ms=0xfffffff0u;tx_until=clock_ms;wifi_probe_init(read_reg,write_reg,clock_ms);run();success("timer wrap during switching");
    framing=1;clock_ms+=50;wifi_probe_tick(clock_ms);check(wifi_info.error==3,"steady-state framing still fails");
    printf("PASS_Z7_UART_921600_HOST checks=%u\n",checks);return 0;
}
