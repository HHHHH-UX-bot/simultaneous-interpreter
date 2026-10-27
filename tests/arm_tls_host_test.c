/* Same TLS engine/config as ARM, with a fragmented nonblocking WinSock TCP BIO.
   No API key; verifies real service certificate and an unauthenticated reply. */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "network.h"
#include "arm_tls.h"
struct tls_seed {uint32_t magic,epoch;uint8_t bytes[64];};
extern volatile struct tls_seed arm_tls_seed;
extern volatile uint32_t arm_tls_diag[24];
extern volatile uint32_t arm_tls_fault[12];
static uint32_t wire_tx;
volatile struct network_status network_info;
static uint8_t tx[2048];static uint32_t txsize,txpos,rxbytes,failed,attached,corrupt,corrupted;
void cache_clean_range(const void *p,unsigned n) {(void)p;(void)n;}
void cloud_client_stop(void) {}
void network_stream_failed(void) {failed=1;network_info.phase=6;}
static void (*receiver)(uint8_t);
int network_stream_attach(void (*p)(uint8_t)) {receiver=p;network_info.phase=4;attached=1;return 1;}
int network_send(const uint8_t *p,uint32_t n) {if(txsize || n>2048) return 0;memcpy(tx,p,n);txsize=n;txpos=0;return 1;}
static void plain_received(uint8_t b) {(void)b;++rxbytes;}
static void plain_received_bytes(const uint8_t *p,uint32_t n) {(void)p;rxbytes+=n;}
int main(int argc,char **argv) {
    int negative=argc>1 && !strcmp(argv[1],"expired");
    int noseed=argc>1 && !strcmp(argv[1],"noseed");
    int full_record=argc>1 && !strcmp(argv[1],"full_record");
    int bulk=argc>1 && !strcmp(argv[1],"bulk");
    corrupt=argc>1 && !strcmp(argv[1],"corrupt");
    WSADATA w;WSAStartup(MAKEWORD(2,2),&w);
    struct addrinfo *ai=0;int rc=getaddrinfo("ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com","443",0,&ai);
    if(rc) return 2;
    SOCKET s=socket(ai->ai_family,SOCK_STREAM,IPPROTO_TCP);
    rc=connect(s,ai->ai_addr,(int)ai->ai_addrlen);freeaddrinfo(ai);if(rc) return 3;
    unsigned long nb=1;ioctlsocket(s,FIONBIO,&nb);
    arm_tls_init();
    if(!noseed) {
        uint8_t bytes[64];if(BCryptGenRandom(0,bytes,64,BCRYPT_USE_SYSTEM_PREFERRED_RNG)) return 4;
        for(int i=0;i<64;++i) arm_tls_seed.bytes[i]=bytes[i];
        SecureZeroMemory(bytes,64);arm_tls_seed.epoch=(uint32_t)time(0)+(negative?630720000u:0);arm_tls_seed.magic=0x31444553u;
    }
    uint32_t start=GetTickCount(),sent=0;
    network_info.phase=8;
    while(GetTickCount()-start<35000u && !failed) {
        if(txsize) {int n=send(s,(const char *)tx+txpos,(int)(txsize-txpos>97?97:txsize-txpos),0);if(n>0) {
            wire_tx+=(uint32_t)n;txpos+=(uint32_t)n;if(txpos==txsize) txpos=txsize=0;} else if(WSAGetLastError()!=WSAEWOULDBLOCK) {fprintf(stderr,"send_error=%d\n",WSAGetLastError());break;}}
        uint8_t buf[137];int n=recv(s,(char *)buf,sizeof(buf),0);
        if(n>0 && receiver) for(int i=0;i<n;++i) {if(corrupt && sent && !corrupted && i==n-1) {buf[i]^=1;corrupted=1;}receiver(buf[i]);}
        else if(n==0) break;
        arm_tls_tick(GetTickCount()-start);
        if(arm_tls_ready() && !sent) {
            if(bulk) arm_tls_attach_bytes(plain_received_bytes);else arm_tls_attach(plain_received);
            const char *q="GET / HTTP/1.1\r\nHost: ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com\r\nConnection: close\r\n\r\n";
            uint8_t padded[2048];uint32_t bytes=(uint32_t)strlen(q);
            if(full_record) {
                const char *prefix="GET / HTTP/1.1\r\nHost: ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com\r\nX-Pad: ";
                const char *suffix="\r\nConnection: close\r\n\r\n";
                memset(padded,'A',sizeof(padded));memcpy(padded,prefix,strlen(prefix));
                memcpy(padded+sizeof(padded)-strlen(suffix),suffix,strlen(suffix));
                q=(const char *)padded;bytes=sizeof(padded);
            }
            if(arm_tls_send((const uint8_t *)q,bytes)) sent=1;
        }
        if(rxbytes) break;
        Sleep(1);
    }
    closesocket(s);WSACleanup();
#ifdef TLS_TEST_WRONG_HOST
    negative=1;
#endif
    if(noseed) negative=1;
    if(corrupt) negative=1;
    int pass=negative?(failed && !rxbytes && !sent):(sent && rxbytes && arm_tls_diag[3]==0);
    if(corrupt) pass=failed && corrupted && !rxbytes;
    if(noseed) pass=pass && !attached;
    if(full_record) pass=pass && arm_tls_diag[12]==2048u && arm_tls_diag[19]>0;
    pass=pass && (negative?(arm_tls_fault[0]==0x31465441u && arm_tls_fault[2]==arm_tls_diag[2]):!arm_tls_fault[0]);
    printf("{\"passed\":%s,\"error\":%d,\"verify_flags\":%u,\"handshakes\":%u,\"handshake_ms\":%u,\"plaintext_rx\":%u,\"ciphertext_rx\":%u,\"ciphertext_tx\":%u,\"wire_tx\":%u,\"attached\":%u,\"corrupted\":%u}\n",pass?"true":"false",(int)arm_tls_diag[2],arm_tls_diag[3],arm_tls_diag[9],arm_tls_diag[11],rxbytes,arm_tls_diag[4],arm_tls_diag[5],wire_tx,attached,corrupted);
    return pass?0:1;
}
