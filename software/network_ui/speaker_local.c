#include "speaker_local.h"
#include "speaker_weights.h"
#include "local_voice.h"
#include "cloud_live.h"
#include "cloud_client.h"
#include "network.h"
#include "arm_tls.h"
#include "speech_playback.h"
#include <math.h>
#include <string.h>
#ifdef Z7_MMU_CACHE
extern void cache_clean_range(const void *,unsigned);
#endif
#ifndef SPEAKER_MMIO_READ
#define SPEAKER_MMIO_READ(offset) (*(volatile uint32_t *)(0x43c40000u+(offset)))
#define SPEAKER_MMIO_WRITE(offset,value) (*(volatile uint32_t *)(0x43c40000u+(offset))=(value))
#endif
static struct ui_state *view;
static float profiles[4][64],enroll_sum[64],recent[3][64];
static uint32_t ready,slot,waiting,resume_after,last_sequence,last_sample,enroll_samples,enroll_windows;
static uint32_t clock_ms,deadline,last_publish,last_embedding_ms,gate,recent_count,recent_index;
volatile uint32_t speaker_diag[32] __attribute__((aligned(32)));
static uint32_t rd(uint32_t a){return SPEAKER_MMIO_READ(a);}
static void wr(uint32_t a,uint32_t v){SPEAKER_MMIO_WRITE(a,v);}
static void barrier(void){
#if defined(__arm__)
 __asm__ volatile("dsb sy" ::: "memory");
#endif
}
static void changed(void){++view->change_sequence;}
static float dot(const float *a,const float *b){float r=0;for(uint32_t i=0;i<64u;++i)r+=a[i]*b[i];return r;}
static int normalize(float *v){float n=sqrtf(dot(v,v));if(n<1e-8f)return 0;for(uint32_t i=0;i<64u;++i)v[i]/=n;return 1;}
static void reset_window(void){
 uint32_t mode=rd(8)&3u;wr(8,mode|4u);barrier();last_sequence=last_sample=recent_count=recent_index=0;
 memset(recent,0,sizeof(recent));
}
static void pause_translation(void){
 if(view->session==UI_RUNNING)(void)ui_control(view,UI_PAUSE);
 local_voice_control(UI_PAUSE);cloud_live_control(UI_PAUSE);speech_playback_stop();
}
static void resume_translation(void){
 if(!resume_after)return;
 resume_after=0;
 if(view->session==UI_PAUSED && ui_control(view,UI_RESUME)){
  local_voice_control(UI_RESUME);if(!view->model_mode)cloud_live_control(UI_RESUME);
 }
}
static void finish(uint32_t status,int resume){
 slot=waiting=0;view->speaker_enroll=0;view->speaker_status=status;
 if(status!=2u){view->speaker_id=view->speaker_score=0;view->speaker_progress=0;}
 memset(enroll_sum,0,sizeof(enroll_sum));reset_window();changed();
 if(resume)resume_translation();else resume_after=0;
}
int speaker_local_enrolling(void){return slot!=0;}
int speaker_local_test_pcm(uint32_t offset,const uint8_t *pcm,uint32_t bytes){
 if(!ready || rd(8)!=3u || !pcm || !bytes || (bytes&1u) || bytes>1024u)return -1;
 uint32_t samples=bytes/2u,before=0,after=0,queued=0,stable=0;
 for(uint32_t i=0;i<3u;++i){before=rd(0x2c);queued=rd(0x24);after=rd(0x2c);if(before==after){stable=1;break;}}
 if(!stable)return 0;
 uint32_t total=after+queued;
 if(offset<=0xffffffffu-samples && offset<total && offset+samples==total)return 1;
 if(offset!=total)return -2;
 if(queued+samples>512u)return 0;
 for(uint32_t i=0;i<bytes;i+=2u)wr(0x20,(uint32_t)pcm[i]|((uint32_t)pcm[i+1u]<<8));
 return 1;
}
void speaker_local_init(struct ui_state *state){
 view=state;ready=slot=waiting=resume_after=clock_ms=last_publish=gate=0;
 last_sequence=last_sample=recent_count=recent_index=last_embedding_ms=0;
 memset(profiles,0,sizeof(profiles));memset((void *)speaker_diag,0,sizeof(speaker_diag));
 speaker_diag[0]=0x314c5053u;view->speaker_mask=view->speaker_id=view->speaker_score=0;view->speaker_status=8u;
 /* The loader pins the speaker bitstream SHA and programs it before CPU0. */
 if(rd(0)!=0x314b5053u || rd(4)!=1u){speaker_diag[2]=1;return;}
 wr(8,0);barrier();
 for(uint32_t i=0;i<SPEAKER_MODEL_WORDS;++i)wr(0x8000u+i*4u,speaker_model_words[i]);
 barrier();
 for(uint32_t i=0;i<SPEAKER_MODEL_WORDS;++i)if(rd(0x8000u+i*4u)!=speaker_model_words[i]){speaker_diag[2]=2;return;}
 if(rd(0x10)!=SPEAKER_MODEL_WORDS || rd(0x14)!=SPEAKER_MODEL_FNV || rd(0x18)!=SPEAKER_MODEL_ID){speaker_diag[2]=3;return;}
 wr(0x1c,SPEAKER_MODEL_FNV);barrier();
 if(!(rd(0x0c)&1u)){speaker_diag[2]=4;return;}
 ready=1;speaker_diag[1]=1;speaker_diag[3]=SPEAKER_MODEL_ID;speaker_diag[4]=SPEAKER_MODEL_FNV;
 wr(8,5u);view->speaker_status=3u;changed();
}
int speaker_local_control(enum ui_command command){
 if(command>=UI_AI_ENROLL_1 && command<=UI_AI_ENROLL_4){
  if(!ready){view->speaker_enroll=0;view->speaker_status=8;changed();return 1;}
  if(!slot)resume_after=view->session==UI_RUNNING;
  slot=(uint32_t)command-UI_AI_ENROLL_1+1u;waiting=1;deadline=clock_ms+12000u;
  enroll_samples=enroll_windows=0;memset(enroll_sum,0,sizeof(enroll_sum));
  view->speaker_enroll=slot;view->speaker_id=view->speaker_score=view->speaker_progress=0;view->speaker_status=7u;
  pause_translation();wr(8,0);gate=1;reset_window();changed();return 1;
 }
 if(command==UI_AI_CANCEL_ENROLL){if(slot)finish(3u,1);return 1;}
 if(command==UI_AI_ERASE){
  memset(profiles,0,sizeof(profiles));view->speaker_mask=0;
  if(slot)finish(3u,1);else{view->speaker_id=view->speaker_score=0;view->speaker_status=3;changed();}
  return 1;
 }
 if(slot && (command==UI_START || command==UI_RESUME || command==UI_PAUSE || command==UI_CLEAR || command==UI_AI_LOCAL || command==UI_AI_CLOUD || command==UI_SELECT_EN_ZH || command==UI_SELECT_ZH_EN))finish(3u,0);
 return 0;
}
static void identify(const float *v){
 float mean[64]={0};memcpy(recent[recent_index],v,64u*sizeof(float));recent_index=(recent_index+1u)%3u;if(recent_count<3u)++recent_count;
 for(uint32_t j=0;j<recent_count;++j)for(uint32_t i=0;i<64u;++i)mean[i]+=recent[j][i];
 if(!normalize(mean))return;
 float best=-1,second=-1;uint32_t id=0;
 for(uint32_t s=0;s<4u;++s)if(view->speaker_mask&(1u<<s)){
  float value=dot(mean,profiles[s]);if(value>best){second=best;best=value;id=s+1u;}else if(value>second)second=value;
 }
 uint32_t accepted=recent_count>=3u && best>=.44f && best-second>=.06f?id:0u;
 uint32_t score=best>0.f?(uint32_t)(fminf(best,1.f)*100.f+.5f):0u;
 uint32_t status=accepted?2u:3u;
 if(view->speaker_id!=accepted || view->speaker_score!=score || view->speaker_status!=status){view->speaker_id=accepted;view->speaker_score=score;view->speaker_status=status;changed();}
}
void speaker_local_tick(uint32_t now){
 clock_ms=now;if(!view || !ready)return;
 if(slot && waiting){
  if(network_info.phase==0u && !arm_tls_active() && !network_stream_busy()){
   waiting=0;deadline=now+30000u;wr(8,5u);gate=0;reset_window();view->speaker_status=1;changed();++speaker_diag[8];
  }else if((int32_t)(now-deadline)>=0){++speaker_diag[9];finish(8u,1);}
 }
 if(slot && !waiting && (int32_t)(now-deadline)>=0){++speaker_diag[10];finish(enroll_samples?5u:6u,1);}
 struct speech_status playback;speech_playback_status(&playback);
 uint32_t blocked=waiting || (!slot && (playback.state==2u || playback.state==3u || local_voice_uses_output()));
 if(blocked!=gate){
  gate=blocked;wr(8,blocked?0u:1u);reset_window();view->speaker_id=view->speaker_score=0;
  if(!slot){if(blocked)view->speaker_status=10u;else if(view->speaker_status==10u)view->speaker_status=3u;changed();}
 }
 if(!gate){
  uint32_t sequence=rd(0x34);
  if(sequence && sequence!=last_sequence){
   float embedding[64];for(uint32_t i=0;i<64u;++i)embedding[i]=(float)(int16_t)rd(0x100u+i*4u);
   uint32_t sample=rd(0x50),voiced=rd(0x54);barrier();
   if(rd(0x34)==sequence && !rd(0x60) && normalize(embedding)){
    uint32_t delta=last_sample?sample-last_sample:20720u;last_sample=sample;last_sequence=sequence;last_embedding_ms=now;++speaker_diag[5];
    if(slot && !waiting){
     if(delta>20720u)delta=10240u;
     enroll_samples+=delta*voiced/128u;for(uint32_t i=0;i<64u;++i)enroll_sum[i]+=embedding[i];++enroll_windows;
     view->speaker_progress=enroll_samples>=64000u?100u:enroll_samples*100u/64000u;changed();
     if(enroll_samples>=64000u && enroll_windows>=5u){
      if(normalize(enroll_sum)){
       uint32_t duplicate=0;for(uint32_t s=0;s<4u;++s)if(s!=slot-1u && (view->speaker_mask&(1u<<s)) && dot(enroll_sum,profiles[s])>=.90f)duplicate=1;
       if(duplicate){++speaker_diag[11];finish(9u,1);}
       else{memcpy(profiles[slot-1u],enroll_sum,sizeof(enroll_sum));view->speaker_mask|=1u<<(slot-1u);view->speaker_id=slot;view->speaker_score=100;view->speaker_progress=100;++speaker_diag[6];finish(2u,1);}
      }else finish(5u,1);
     }
    }else identify(embedding);
   }
  }
  if(!slot && now-last_embedding_ms>1800u && (view->speaker_id || view->speaker_score)){view->speaker_id=view->speaker_score=0;view->speaker_status=3;recent_count=recent_index=0;changed();}
 }
 if(now-last_publish>=200u){
  last_publish=now;speaker_diag[7]=now;speaker_diag[12]=slot;speaker_diag[13]=waiting;speaker_diag[14]=enroll_samples;speaker_diag[15]=enroll_windows;
  speaker_diag[16]=view->speaker_mask;speaker_diag[17]=view->speaker_id;speaker_diag[18]=view->speaker_score;speaker_diag[19]=view->speaker_status;
  for(uint32_t i=0;i<8u;++i)speaker_diag[20u+i]=rd(0x2cu+i*4u);
  speaker_diag[28]=rd(0x58);speaker_diag[29]=rd(0x5c);speaker_diag[30]=rd(0x60);speaker_diag[31]=gate;
#ifdef Z7_MMU_CACHE
  cache_clean_range((const void *)speaker_diag,sizeof(speaker_diag));
#endif
 }
}
