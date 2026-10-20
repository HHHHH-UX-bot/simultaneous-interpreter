/* Dedicated, credential-free UART wire test. No UI/network/USB tasks. */
#include <stdint.h>

#define UART ((volatile uint32_t *)0x43c30000u)
#define TICKS_MS 333333u
__attribute__((section(".mailbox"),used)) volatile uint32_t report[32];
static uint32_t ticks(void) { return *(volatile uint32_t *)0xf8f00200u; }
static void barrier(void) { __asm__ volatile("dsb sy" ::: "memory"); }

static int put_byte(uint8_t byte) {
    uint32_t start=ticks();
    while(!(UART[1]&2u)) {
        if(ticks()-start>TICKS_MS*100u) {report[8]=5;return 0;}
    }
    UART[4]=byte;return 1;
}

/* The controlled TCP peer sends no application data. Consume AT replies only. */
static int wait_reply(int prompt) {
    uint32_t start=ticks(),tail=0;
    while(ticks()-start<TICKS_MS*3000u) {
        if(!(UART[1]&1u)) continue;
        uint32_t word=UART[5];
        if(!(word&0x80000000u)) continue;
        uint8_t byte=(uint8_t)word;tail=(tail<<8)|byte;
        if((prompt && byte=='>') || (!prompt && tail==0x44204f4bu)) return 1;
        if(tail==0x52524f52u || tail==0x4641494cu) return 0;
    }
    return 0;
}

int main(void) {
    for(uint32_t i=0;i<32u;++i) report[i]=0;
    report[0]=0x31545755u;
    if(UART[0]!=0x57494631u) {report[8]=1;report[1]=4;for(;;) {}}
    *(volatile uint32_t *)0xf8f00208u=1u;
    report[1]=1;
    for(;;) {
        if(report[2]!=1u) continue;
        uint32_t count=report[4],divider=report[5],paced=report[6];
        report[2]=0;report[7]=report[8]=report[15]=0;report[1]=2;barrier();
        if(!count || count>1048576u || divider<16u || divider>65535u || paced>2u || (paced==2u && count%1024u)) {
            report[8]=2;report[1]=4;continue;
        }
        UART[3]=15u;barrier();
        uint32_t start=ticks();
        while((UART[1]&4u) || !(UART[1]&256u)) {
            if(ticks()-start>TICKS_MS*100u) {report[8]=3;break;}
        }
        if(report[8]) {report[1]=4;continue;}
        UART[2]=divider;barrier();
        if(UART[2]!=divider) {report[8]=4;report[1]=4;continue;}
        const volatile uint8_t *payload=(const volatile uint8_t *)0x01000000u;
        uint32_t hash=2166136261u,slot=ticks(),budget=0;
        start=slot;report[9]=start;
        for(uint32_t n=0;n<count;++n) {
            if(paced==1u && budget==8u) {
                while(ticks()-slot<TICKS_MS) {}
                slot=ticks();budget=0;
            }
            if(paced==2u && !(n%1024u)) {
                static const char command[]="AT+CIPSEND=1024\r\n";
                for(uint32_t i=0;i<sizeof(command)-1u;++i) if(!put_byte((uint8_t)command[i])) break;
                if(report[8]) break;
                if(!wait_reply(1)) {report[8]=7;break;}
            }
            uint8_t byte=payload[n];if(!put_byte(byte)) break;
            hash=(hash^byte)*16777619u;++budget;report[7]=n+1u;
            if(paced==2u && n%1024u==1023u) {
                if(!wait_reply(0)) {report[8]=8;break;}
                ++report[15];
            }
        }
        uint32_t waiting=ticks();
        while(UART[1]&4u) {
            if(ticks()-waiting>TICKS_MS*1000u) {report[8]=6;break;}
        }
        report[10]=ticks();report[11]=UART[10];report[12]=UART[7];
        report[13]=UART[1];report[14]=hash;barrier();
        report[1]=report[8]?4u:3u;
    }
}
