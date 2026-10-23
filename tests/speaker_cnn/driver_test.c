#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../software/network_ui/speaker_local.h"
#include "../../software/network_ui/speaker_weights.h"
#include "../../software/network_ui/network.h"
#include "../../software/network_ui/speech_playback.h"
volatile struct network_status network_info;
static uint32_t registers[16384],tls,busy,playing,voice_output,pauses,resumes,now,corrupt;
uint32_t speaker_test_read(uint32_t offset){assert(offset<65536u && !(offset&3u));return registers[offset/4u]^(corrupt && offset==0x8004u?1u:0u);}
void speaker_test_write(uint32_t offset,uint32_t value){
 assert(offset<65536u && !(offset&3u));
 if(offset>=0x8000u){
  assert(!(registers[2]&1u));uint32_t index=(offset-0x8000u)/4u;
  if(!index){registers[4]=0;registers[5]=2166136261u;}
  assert(index==registers[4]);++registers[4];registers[5]=(registers[5]^value)*16777619u;
  registers[offset/4u]=value;registers[6]=registers[0x800cu/4u];
 }else if(offset==0x1cu){assert(registers[4]==SPEAKER_MODEL_WORDS && value==registers[5]);registers[3]|=1u;}
 else if(offset==8u){registers[2]=value&3u;if(value&4u){registers[0x34u/4u]=0;registers[0x24u/4u]=registers[0x2cu/4u]=0;}}
 else if(offset==0x20u){assert(registers[0x24u/4u]<512u);++registers[0x24u/4u];}
 else registers[offset/4u]=value;
}
int arm_tls_active(void){return tls!=0;}
int network_stream_busy(void){return busy!=0;}
int local_voice_uses_output(void){return voice_output!=0;}
void local_voice_control(enum ui_command c){if(c==UI_PAUSE)++pauses;else if(c==UI_RESUME)++resumes;}
void cloud_live_control(enum ui_command c){(void)c;}
void speech_playback_stop(void){playing=0;}
void speech_playback_status(struct speech_status *s){memset(s,0,sizeof(*s));s->state=playing?3u:1u;}
static void tick(uint32_t ms){now+=ms;speaker_local_tick(now);}
static void embedding(uint32_t coordinate){
 assert(registers[2]&1u);for(uint32_t i=0;i<64u;++i)registers[0x100u/4u+i]=i==coordinate?1000u:0u;
 uint32_t seq=registers[0x34u/4u];registers[0x50u/4u]=seq?registers[0x50u/4u]+10240u:20720u;
 registers[0x54u/4u]=128u;registers[0x34u/4u]=seq+1u;tick(640u);
}
static void control(struct ui_state *s,enum ui_command c){assert(ui_control(s,c));speaker_local_control(c);}
static void enroll(struct ui_state *s,enum ui_command command,uint32_t coordinate){
 control(s,command);assert(speaker_local_enrolling());tick(1u);
 for(uint32_t i=0;i<6u;++i)embedding(coordinate);
 assert(!speaker_local_enrolling());
}
int main(void){
 struct ui_state s;memset(registers,0,sizeof(registers));registers[0]=0x314b5053u;registers[1]=1u;ui_init(&s);s.model_mode=1;
 speaker_local_init(&s);assert(speaker_diag[1]==1 && !speaker_diag[2]);assert(registers[4]==7076 && registers[5]==SPEAKER_MODEL_FNV);
 uint8_t pcm[1024]={0};assert(speaker_local_test_pcm(0,pcm,8)==-1);
 speaker_test_write(8,7);assert(speaker_local_test_pcm(0,pcm,1024)==1);assert(registers[0x24u/4u]==512u);
 assert(speaker_local_test_pcm(0,pcm,1024)==1 && registers[0x24u/4u]==512u);
 assert(speaker_local_test_pcm(512,pcm,2)==0);assert(speaker_local_test_pcm(900,pcm,8)==-2);
 speaker_test_write(8,5);
 control(&s,UI_START);network_info.phase=4;tls=busy=1;
 control(&s,UI_AI_ENROLL_1);assert(pauses && s.session==UI_PAUSED && s.speaker_status==7u && !(registers[2]&1u));
 tick(5000u);assert(s.speaker_progress==0 && s.speaker_mask==0 && resumes==0);
 network_info.phase=0;tls=0;tick(100u);assert(!(registers[2]&1u));busy=0;tick(1u);assert(registers[2]&1u);
 for(uint32_t i=0;i<6u;++i)embedding(0);
 assert(!speaker_local_enrolling() && s.speaker_mask==1 && s.speaker_id==1 && s.session==UI_RUNNING && resumes==1);
 for(uint32_t i=0;i<3u;++i)embedding(0);assert(s.speaker_id==1 && s.speaker_score==100);
 for(uint32_t i=0;i<3u;++i)embedding(5);assert(s.speaker_id==0);
 enroll(&s,UI_AI_ENROLL_2,1);assert(s.speaker_mask==3 && s.speaker_id==2);
 for(uint32_t i=0;i<3u;++i)embedding(0);assert(s.speaker_id==1);
 enroll(&s,UI_AI_ENROLL_3,0);assert(s.speaker_mask==3 && s.speaker_status==9);
 playing=1;tick(1u);assert(!(registers[2]&1u) && s.speaker_status==10u);playing=0;tick(1u);assert(registers[2]&1u);
 for(uint32_t i=0;i<3u;++i)embedding(1);assert(s.speaker_id==2);tick(2000u);assert(!s.speaker_id);
 control(&s,UI_AI_ENROLL_4);tick(1u);tick(30001u);assert(!speaker_local_enrolling() && s.speaker_status==6u && s.speaker_mask==3);
 control(&s,UI_AI_ENROLL_4);control(&s,UI_AI_CANCEL_ENROLL);assert(!speaker_local_enrolling() && s.session==UI_RUNNING);
 network_info.phase=4;tls=1;control(&s,UI_AI_ENROLL_4);tick(12001u);assert(!speaker_local_enrolling() && s.speaker_status==8u && !s.speaker_progress);network_info.phase=tls=0;
 control(&s,UI_AI_ERASE);assert(!s.speaker_mask && !s.speaker_id);
 control(&s,UI_PAUSE);enroll(&s,UI_AI_ENROLL_1,3);assert(s.speaker_mask==1 && s.session==UI_PAUSED);
 corrupt=1;speaker_local_init(&s);assert(speaker_diag[2]==2u && s.speaker_status==8u);
 puts("PASS_SPEAKER_DRIVER_MODEL_GUARD_PRIVACY_ENROLL_CANCEL_TIMEOUT_UNKNOWN_DAC");return 0;
}
