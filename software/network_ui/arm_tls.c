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
static mbedtls_ssl_context ssl;
static mbedtls_ssl_config config;
static mbedtls_x509_crt ca;
static mbedtls_ctr_drbg_context rng;
static uint8_t seed[64],plain[2048],out[8192];
static uint32_t seed_used,head,tail,used,pending,phase,now_ms,started,epoch,seed_ms,initialized,diag_ms;
static void (*consumer)(uint8_t);
static void clean(void) {cache_clean_range((const void *)arm_tls_diag,sizeof(arm_tls_diag));}
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
    (void)ctx;if(n>2048) n=2048;
    if(!network_send(p,(uint32_t)n)) return MBEDTLS_ERR_SSL_WANT_WRITE;
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
    arm_tls_diag[2]=(uint32_t)code;
    arm_tls_diag[3]=mbedtls_ssl_get_verify_result(&ssl);
    ++arm_tls_diag[10];phase=3;consumer=0;pending=0;cloud_client_stop();network_stream_failed();clean();
}
void arm_tls_init(void) {
    arm_tls_seed.magic=arm_tls_seed.epoch=0;
    for(uint32_t i=0;i<64;++i) arm_tls_seed.bytes[i]=0;
    memset((void *)arm_tls_diag,0,sizeof(arm_tls_diag));arm_tls_diag[0]=0x31534c54u;
    phase=initialized=used=head=tail=pending=0;consumer=0;clean();
}
int arm_tls_ready(void) {return phase==2 && !consumer;}
int arm_tls_active(void) {return phase==1 || phase==2;}
int arm_tls_attach(void (*p)(uint8_t)) {if(phase!=2 || !p || consumer) return 0;consumer=p;return 1;}
int arm_tls_send(const uint8_t *p,uint32_t n) {
    if(phase!=2 || pending || !n || n>sizeof(plain)) return 0;
    if(p[0]==0x89u) {++arm_tls_diag[16];arm_tls_diag[18]=n;arm_tls_diag[22]=n>1?p[1]:0;}
    memcpy(plain,p,n);pending=n;return 1;
}
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
    mbedtls_ssl_conf_rng(&config,mbedtls_ctr_drbg_random,&rng);mbedtls_ssl_conf_ca_chain(&config,&ca,0);
    rc=mbedtls_ssl_setup(&ssl,&config);if(rc) return rc;
    rc=mbedtls_ssl_set_hostname(&ssl,HOST);
    if(rc) return rc;
    mbedtls_ssl_set_bio(&ssl,0,bio_send,bio_recv,0);return 0;
}
void arm_tls_tick(uint32_t now) {
    now_ms=now;
    if(network_info.phase!=4 && network_info.phase!=8) {
        if(phase) {mbedtls_ssl_free(&ssl);mbedtls_ssl_config_free(&config);phase=0;consumer=0;pending=used=head=tail=0;}
    } else if(!phase && network_info.phase==8) {
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
                if(plain[0]==0x89u) {++arm_tls_diag[17];arm_tls_diag[21]=now;}
                arm_tls_diag[12]+=(uint32_t)rc;pending-=(uint32_t)rc;
                if(pending) memmove(plain,plain+rc,pending);
            } else if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) {failed(rc);return;}
            else {++arm_tls_diag[19];arm_tls_diag[20]=now;}
        }
        for(uint32_t i=0;i<8;++i) {
            int rc=mbedtls_ssl_read(&ssl,out,sizeof(out));
            if(rc>0) {arm_tls_diag[13]+=(uint32_t)rc;if(consumer) for(int j=0;j<rc;++j) consumer(out[j]);}
            else {if(rc!=MBEDTLS_ERR_SSL_WANT_READ && rc!=MBEDTLS_ERR_SSL_WANT_WRITE) failed(rc?rc:-5);break;}
        }
    }
    arm_tls_diag[1]=phase;arm_tls_diag[6]=used;arm_tls_diag[14]=pending;arm_tls_diag[15]=now;
    if(now-diag_ms>=200) {diag_ms=now;clean();}
}
