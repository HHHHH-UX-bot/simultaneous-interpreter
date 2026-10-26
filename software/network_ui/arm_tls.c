#include "arm_tls.h"
#include "network.h"
#include "cloud_client.h"
#include "../ps_bringup/cache.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"
#include "mbedtls/platform_util.h"
#include <string.h>
#include <time.h>
#include "tls_root.h"

#define HOST "ws-zckg39zhbiii3yrv.cn-beijing.maas.aliyuncs.com"
#define RX_BYTES 131072u
/* 私有 OCM 区仅接收种子，诊断工具不得读取。magic 最后写入。 */
struct tls_seed {uint32_t magic,epoch;uint8_t bytes[64];};
__attribute__((section(".arm_tls_seed"),used)) volatile struct tls_seed arm_tls_seed;
__attribute__((section(".tls_runtime"),used)) static uint8_t arena[524288],rx[RX_BYTES];
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t arm_tls_diag[24];
/* First failure metadata only; no seed, plaintext, ciphertext or TLS keys. */
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t arm_tls_fault[12];
#ifdef Z7_TINYQWEN
/* Accepted/completed masked application frames: lengths and fingerprints
   only. HTTP Authorization, payload text, PCM, seeds and keys are excluded. */
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t arm_tls_app_tx[2u+64u*6u];
#endif
#ifdef Z7_TLS_TRACE
#include "tls_tx_trace.h"
/* Diagnostic fingerprints of 256-byte ciphertext blocks, never payloads. */
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t arm_tls_tx_trace[260];
/* First complete block survives ring wrap and binds the relay connection. */
__attribute__((section(".cloud_runtime"),used)) volatile uint32_t arm_tls_tx_identity[2];
static uint32_t trace_session;
#endif
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config config;
static mbedtls_x509_crt ca;
static mbedtls_ctr_drbg_context rng;
static uint8_t seed[64],plain[2048],out[8192];
static uint32_t seed_used,head,tail,used,pending,phase,now_ms,started,epoch,seed_ms,initialized,diag_ms;
static void (*consumer)(uint8_t);
static void (*consumer_bytes)(const uint8_t *,uint32_t);
static void clean(void) {cache_clean_range((const void *)arm_tls_diag,sizeof(arm_tls_diag));}
static void app_trace(uint32_t kind,const uint8_t *p,uint32_t n,int rc) {
#ifdef Z7_TINYQWEN
    if(!n || p[0]!=0x81u)return;
    uint32_t hash=2166136261u;for(uint32_t i=0;i<n;++i)hash=(hash^p[i])*16777619u;
    uint32_t at=2u+(arm_tls_app_tx[1]%64u)*6u;
    arm_tls_app_tx[at]=kind;arm_tls_app_tx[at+1u]=now_ms;arm_tls_app_tx[at+2u]=n;
    arm_tls_app_tx[at+3u]=hash;arm_tls_app_tx[at+4u]=(uint32_t)rc;
    arm_tls_app_tx[at+5u]=(uint32_t)ssl.MBEDTLS_PRIVATE(out_left);++arm_tls_app_tx[1];
    cache_clean_range((const void *)arm_tls_app_tx,sizeof(arm_tls_app_tx));
#else
    (void)kind;(void)p;(void)n;(void)rc;
#endif
}
mbedtls_ms_time_t mbedtls_ms_time(void) {return (mbedtls_ms_time_t)now_ms;}
static time_t tls_time(time_t *p) {
    time_t t=(time_t)epoch+(uint32_t)(now_ms-seed_ms)/1000u;
    if(p) *p=t;
    return t;
}
/* newlib 无系统时钟；证书日期使用主机验证过的 UTC，加板端单调计时。 */
struct tm *mbedtls_platform_gmtime_r(const mbedtls_time_t *tt,struct tm *t) {
    int64_t seconds=(int64_t)*tt;
    if(seconds<0) return 0;
    memset(t,0,sizeof(*t));
    t->tm_sec=(int)(seconds%60);seconds/=60;t->tm_min=(int)(seconds%60);
    seconds/=60;t->tm_hour=(int)(seconds%24);int64_t days=seconds/24;
    t->tm_wday=(int)((days+4)%7);
    int y=1970;
    for(;;++y) {int n=365+(y%4==0 && (y%100!=0 || y%400==0));if(days<n) break;days-=n;}
    t->tm_year=y-1900;t->tm_yday=(int)days;
    static const int months[]={31,28,31,30,31,30,31,31,30,31,30,31};
    int m=0;
    for(;m<11;++m) {int n=months[m]+(m==1 && y%4==0 && (y%100!=0 || y%400==0));if(days<n) break;days-=n;}
    t->tm_mon=m;t->tm_mday=(int)days+1;return t;
}
static int entropy(void *ctx,unsigned char *p,size_t n) {
    (void)ctx;
    if(n>sizeof(seed)-seed_used) return MBEDTLS_ERR_CTR_DRBG_ENTROPY_SOURCE_FAILED;
    memcpy(p,seed+seed_used,n);mbedtls_platform_zeroize(seed+seed_used,n);seed_used+=(uint32_t)n;
    return 0;
}
static int bio_send(void *ctx,const unsigned char *p,size_t n) {
    (void)ctx;
#ifdef Z7_TLS_PASSTHROUGH
    if(n>2048) n=2048;
#else
    if(n>1024) n=1024; /* One acknowledged ESP block at a time. */
#endif
    if(!network_send(p,(uint32_t)n)) return MBEDTLS_ERR_SSL_WANT_WRITE;
#ifdef Z7_TLS_TRACE
    if(!arm_tls_fault[0]) {
        tls_tx_trace_add(arm_tls_tx_trace,p,n);
        if(!arm_tls_tx_identity[0] && arm_tls_tx_trace[2]>=256u) {
            arm_tls_tx_identity[0]=trace_session;arm_tls_tx_identity[1]=arm_tls_tx_trace[5];
        }
    }
#endif
    arm_tls_diag[5]+=(uint32_t)n;return (int)n;
}
static int bio_recv(void *ctx,unsigned char *p,size_t n) {
    (void)ctx;if(!used) return MBEDTLS_ERR_SSL_WANT_READ;
    if(n>used) n=used;
    for(size_t i=0;i<n;++i) {p[i]=rx[tail];tail=(tail+1u)%RX_BYTES;}
    used-=(uint32_t)n;return (int)n;
}
static void received(uint8_t b) {
    if(used==RX_BYTES) {++arm_tls_diag[7];return;}
    rx[head]=b;head=(head+1u)%RX_BYTES;++used;++arm_tls_diag[4];
    if(used>arm_tls_diag[8]) arm_tls_diag[8]=used;
}
static void failed(int code) {
    if(!arm_tls_fault[0]) {
        uint32_t snapshot[]={0x31465441u,now_ms,(uint32_t)code,pending,
            (uint32_t)ssl.MBEDTLS_PRIVATE(out_left),(uint32_t)ssl.MBEDTLS_PRIVATE(out_msglen),
            used,network_info.phase,network_info.last_code,network_info.received_bytes,
            arm_tls_diag[5],arm_tls_diag[12]};
        for(uint32_t i=0;i<12;++i) arm_tls_fault[i]=snapshot[i];
        cache_clean_range((const void *)arm_tls_fault,sizeof(arm_tls_fault));
#ifdef Z7_TLS_TRACE
        network_tls_trace_freeze();
        cache_clean_range((const void *)arm_tls_tx_trace,sizeof(arm_tls_tx_trace));
        cache_clean_range((const void *)arm_tls_tx_identity,sizeof(arm_tls_tx_identity));
#endif
    }
    arm_tls_diag[2]=(uint32_t)code;
    /* Pinned Mbed TLS 3.6.7 returns this code after validating a two-byte alert. */
    if(code==MBEDTLS_ERR_SSL_FATAL_ALERT_MESSAGE)
        arm_tls_diag[23]=ssl.MBEDTLS_PRIVATE(in_msg)[1];
    arm_tls_diag[3]=mbedtls_ssl_get_verify_result(&ssl);
    ++arm_tls_diag[10];phase=3;consumer=0;consumer_bytes=0;pending=0;cloud_client_stop();network_stream_failed();clean();
}
void arm_tls_init(void) {
    arm_tls_seed.magic=arm_tls_seed.epoch=0;
    for(uint32_t i=0;i<64;++i) arm_tls_seed.bytes[i]=0;
    memset((void *)arm_tls_diag,0,sizeof(arm_tls_diag));arm_tls_diag[0]=0x31534c54u;
    memset((void *)arm_tls_fault,0,sizeof(arm_tls_fault));
#ifdef Z7_TINYQWEN
    memset((void *)arm_tls_app_tx,0,sizeof(arm_tls_app_tx));arm_tls_app_tx[0]=0x31585041u;
    cache_clean_range((const void *)arm_tls_app_tx,sizeof(arm_tls_app_tx));
#endif
    cache_clean_range((const void *)arm_tls_fault,sizeof(arm_tls_fault));
    phase=initialized=used=head=tail=pending=0;consumer=0;consumer_bytes=0;clean();
#ifdef Z7_TLS_TRACE
    memset((void *)arm_tls_tx_trace,0,sizeof(arm_tls_tx_trace));trace_session=0;
    arm_tls_tx_identity[0]=arm_tls_tx_identity[1]=0;
#endif
}
int arm_tls_ready(void) {return phase==2 && !consumer && !consumer_bytes;}
int arm_tls_active(void) {return phase==1 || phase==2;}
int arm_tls_attach(void (*p)(uint8_t)) {if(phase!=2 || !p || consumer || consumer_bytes) return 0;consumer=p;return 1;}
int arm_tls_attach_bytes(void (*p)(const uint8_t *,uint32_t)) {if(phase!=2 || !p || consumer || consumer_bytes) return 0;consumer_bytes=p;return 1;}
int arm_tls_send(const uint8_t *p,uint32_t n) {
    if(phase!=2 || pending || !n || n>sizeof(plain)) return 0;
    /* A read may own pending TLS control output. Flush that operation before
       accepting an application write; ssl_write retries must own out_left. */
    if(ssl.MBEDTLS_PRIVATE(out_left))return 0;
    if(p[0]==0x89u) {++arm_tls_diag[16];arm_tls_diag[18]=n;arm_tls_diag[22]=n>1?p[1]:0;}
    memcpy(plain,p,n);pending=n;app_trace(1u,plain,n,0);return 1;
}
#ifdef Z7_SD_BOOT
#include "sd_boot_runtime.h"
/* An SD-booted board has no clock source: its time starts at the config
   file's creation, so a certificate issued after that looks "not yet valid".
   Ignore only that flag; chain, name and expiry checks stay in force. */
static int sd_boot_verify(void *context,mbedtls_x509_crt *crt,int depth,uint32_t *flags) {
    (void)context;(void)crt;(void)depth;*flags&=~(uint32_t)MBEDTLS_X509_BADCERT_FUTURE;return 0;
}
#endif
static int setup(void) {
    if(!initialized) {
        if(arm_tls_seed.magic!=0x31444553u || arm_tls_seed.epoch<1767225600u) return -1;
        epoch=arm_tls_seed.epoch;seed_ms=now_ms;seed_used=0;
        for(uint32_t i=0;i<64;++i) {seed[i]=arm_tls_seed.bytes[i];arm_tls_seed.bytes[i]=0;}
        arm_tls_seed.magic=arm_tls_seed.epoch=0;
        mbedtls_memory_buffer_alloc_init(arena,sizeof(arena));
        mbedtls_platform_set_time(tls_time);mbedtls_ctr_drbg_init(&rng);mbedtls_x509_crt_init(&ca);
        int rc=mbedtls_ctr_drbg_seed(&rng,entropy,0,(const unsigned char *)"z7-tls-v1",9);
        mbedtls_platform_zeroize(seed,sizeof(seed));if(rc) return rc;
        rc=mbedtls_x509_crt_parse(&ca,(const unsigned char *)tls_root,sizeof(tls_root));if(rc) return rc;
        initialized=1;
    }
    mbedtls_ssl_init(&ssl);mbedtls_ssl_config_init(&config);
    int rc=mbedtls_ssl_config_defaults(&config,MBEDTLS_SSL_IS_CLIENT,MBEDTLS_SSL_TRANSPORT_STREAM,MBEDTLS_SSL_PRESET_DEFAULT);
    if(rc) return rc;
    static const int suites[]={MBEDTLS_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,MBEDTLS_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,0};
    mbedtls_ssl_conf_ciphersuites(&config,suites);
    mbedtls_ssl_conf_authmode(&config,MBEDTLS_SSL_VERIFY_REQUIRED);
#ifdef Z7_SD_BOOT
    if(sd_boot_active()) mbedtls_ssl_conf_verify(&config,sd_boot_verify,0);
#endif
    mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&rng);mbedtls_ssl_conf_ca_chain(&config,&ca,0);
    rc=mbedtls_ssl_setup(&ssl,&config);if(rc) return rc;
    rc=mbedtls_ssl_set_hostname(&ssl,HOST);
    if(rc) return rc;
    mbedtls_ssl_set_bio(&ssl,0,bio_send,bio_recv,0);return 0;
}
void arm_tls_tick(uint32_t now) {
    now_ms=now;
    if(network_info.phase!=4 && network_info.phase!=8) {
        if(phase) {mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);phase=0;consumer=0;consumer_bytes=0;pending=used=head=tail=0;}
    } else if(!phase && network_info.phase==8) {
#ifdef Z7_TLS_TRACE
        if(!arm_tls_fault[0]) {
            tls_tx_trace_begin(arm_tls_tx_trace,++trace_session);
            arm_tls_tx_identity[0]=arm_tls_tx_identity[1]=0;
            network_tls_trace_begin(trace_session);
        }
#endif
        int rc=setup();
        if(rc) {failed(rc);return;}
        if(!network_stream_attach(received)) {failed(-2);return;}
        started=now;phase=1;++arm_tls_diag[9];
    }
    if(phase==1) {
#ifdef TLS_TEST_WRONG_HOST
        /* 测试仍发送正确 SNI，再单独验证错误域名必须拒绝证书。 */
        if(arm_tls_diag[5]) (void)mbedtls_ssl_set_hostname(&ssl,"wrong.invalid");
#endif
        int rc=mbedtls_ssl_handshake(&ssl);
        if(rc==0) {
            if(mbedtls_ssl_get_verify_result(&ssl)) {failed(-3);return;}
            phase=2;arm_tls_diag[11]=now-started;
        } else if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) {failed(rc);return;}
        else if(now-started>30000u) {failed(-4);return;}
    }
    if(phase==2) {
        if(pending) {
            int rc=mbedtls_ssl_write(&ssl,plain,pending);
            if(rc>0) {
                app_trace(2u,plain,pending,rc);
                if(plain[0]==0x89u) {++arm_tls_diag[17];arm_tls_diag[21]=now;}
                arm_tls_diag[12]+=(uint32_t)rc;pending-=(uint32_t)rc;
                if(pending) memmove(plain,plain+rc,pending);
            } else if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) {failed(rc);return;}
            else {++arm_tls_diag[19];arm_tls_diag[20]=now;}
        }
        /* Active UART receive can keep delivering while TLS plaintext is
           parsed byte-by-byte. Yield after 4 KiB so the independent UART and
           microphone FIFOs are serviced before a burst can fill them. */
#ifdef Z7_ARM_TCP_ACTIVE_RECV
        const uint32_t reads=2u,read_bytes=2048u;
#else
        const uint32_t reads=8u,read_bytes=sizeof(out);
#endif
        /* Preserve the nonblocking write's exact buffer and TLS output state
           until completion. ssl_read must not flush a write behind its retry. */
        for(uint32_t i=0;!pending && i<reads;++i) {
            int rc=mbedtls_ssl_read(&ssl,out,read_bytes);
            if(rc>0) {
                arm_tls_diag[13]+=(uint32_t)rc;
                if(consumer_bytes) consumer_bytes(out,(uint32_t)rc);
                else if(consumer) for(int j=0;j<rc;++j) consumer(out[j]);
            }
            else {if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) failed(rc?rc:-5);break;}
        }
    }
    arm_tls_diag[1]=phase;arm_tls_diag[6]=used;arm_tls_diag[14]=pending;arm_tls_diag[15]=now;
    if(now-diag_ms>=200) {diag_ms=now;clean();
#ifdef Z7_TLS_TRACE
        cache_clean_range((const void *)arm_tls_tx_trace,sizeof(arm_tls_tx_trace));
        cache_clean_range((const void *)arm_tls_tx_identity,sizeof(arm_tls_tx_identity));
        network_tls_trace_clean();
#endif
    }
}
