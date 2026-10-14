#define main legacy_suite
#include "network_runtime_test.c"
#undef main
static uint8_t sent[2048];
static uint32_t sent_bytes,remaining,serial_busy;
static char send_command[64];
static uint32_t send_command_used;
static uint32_t tx_read(uint32_t offset) {
    return offset==4?reg_read(offset)|(serial_busy?4u:0u):reg_read(offset);
}
static void tx_write(uint32_t offset,uint32_t value) {
    if(network_info.phase!=4) {reg_write(offset,value);return;}
    check(offset==16,"bulk task writes TX only");
    if(remaining) {
        sent[sent_bytes++]=(uint8_t)value;
        if(!--remaining) enqueue("\r\nSEND OK\r\n");
        return;
    }
    send_command[send_command_used++]=(char)value;
    if(value!='\n') return;
    send_command[send_command_used]=0;
    check(!strncmp(send_command,"AT+CIPSEND=0,",13),"single owned TCP client");
    remaining=(uint32_t)strtoul(send_command+13,0,10);
    check(remaining>0 && remaining<=sizeof(sent),"bounded bulk command length");
    send_command_used=sent_bytes=0;enqueue("\r\nOK\r\n> ");
}
static void connected(void) {
    reset(0);network_init(tx_read,tx_write);
    network_config.action=1;network_config.request_sequence=1;
    network_config.ssid_bytes=3;network_config.password_bytes=8;
    memcpy((void *)network_config.credentials,"LAN",3);
    memcpy((void *)(network_config.credentials+33),"testpass",8);
    serial_busy=remaining=send_command_used=sent_bytes=0;
    ready();enqueue("0,CONNECT\r\n");tick();check(network_info.phase==4,"bulk peer connected");
}
int main(void) {
    check(legacy_suite()==0,"legacy 32-byte ACK behavior");
    uint8_t input[2048],expected[2048];
    for(uint32_t i=0;i<sizeof(input);++i) input[i]=expected[i]=(uint8_t)(i*13u);
    connected();check(network_send(input,1536),"large block queued");
    memset(input,0,sizeof(input));check(!network_send(input,16),"busy transaction rejects second queue");
    for(uint32_t i=0;i<5;++i) tick();
    check(sent_bytes==1536 && !memcmp(sent,expected,1536),"queued copy remains exact after caller reuse");
    check(network_info.phase==4 && !network_info.error,"large send completes without changing connection");
    connected();serial_busy=1;check(network_send(expected,2048),"maximum block queued");tick();tick();
    check(!sent_bytes,"payload waits for command stop bit");serial_busy=0;
    for(uint32_t i=0;i<5;++i) tick();
    check(sent_bytes==2048 && !memcmp(sent,expected,2048),"maximum block sent exactly once");
    check(!network_send(expected,0) && !network_send(expected,2049),"size boundary");
    printf("PASS_NETWORK_TRANSMIT checks=%u\n",checks);return 0;
}
