#define Z7_TINYQWEN
#define Z7_ARM_TLS
#define Z7_OPUS
void cache_clean_range(const void *,unsigned);
#include "cloud_client.c"
#include <stdio.h>
#include <assert.h>
int local_voice_role(void){return 2;}
enum ui_language cloud_live_language(void){return UI_ZH_EN;}
void cache_clean_range(const void *p,unsigned n){(void)p;(void)n;}
void cloud_wire_bytes(const uint8_t *p,uint32_t n){(void)p;(void)n;}
void cloud_wire_byte(uint8_t p){(void)p;}
void speech_playback_utterance(void){}
void speech_playback_disconnect(void){}
int opus_codec_init(void){return 1;}
void cloud_live_begin(uint32_t n){(void)n;}
int arm_tls_attach_bytes(void (*p)(const uint8_t *,uint32_t)){(void)p;return 1;}
const char *local_voice_path(void){return "unused";}
int arm_tls_send(const uint8_t *p,uint32_t n){
 assert(p[0]==0x81 && (p[1]&128));uint32_t length=p[1]&127,at=2;
 if(length==126){length=(uint32_t)p[2]*256u+p[3];at=4;}
 assert(length+at+4==n);const uint8_t *mask=p+at;at+=4;
 for(uint32_t i=0;i<length;++i)putchar(p[at+i]^mask[i%4]);putchar('\n');return 1;
}
int main(void){
 client.phase=CLOUD_STREAMING;assert(send_session_config());
 const char text[]="Open \"window\".\n\xe4\xbd\xa0\xe5\xa5\xbd";
 assert(cloud_client_tts_text(text,sizeof(text)-1));assert(cloud_client_tts_commit());assert(cloud_client_tts_finish());
 assert(!cloud_client_tts_text(text,241));return 0;
}
