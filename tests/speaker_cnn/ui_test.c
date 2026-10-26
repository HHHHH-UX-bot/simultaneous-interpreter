#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../software/subtitle_ui/ui_model.h"
#include "../../software/subtitle_ui/ui_render.h"
#include "../../software/subtitle_ui/ui_touch.h"
static uint32_t memory[1024u*600u+64u];
static void draw(struct ui_state *s,uint32_t n){
 struct ui_render_stats stats;for(uint32_t i=0;i<32u;++i)memory[i]=memory[1024u*600u+32u+i]=0xa5c35a3cu;
 ui_render(memory+32,s,1000,0,&stats);assert(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
 for(uint32_t i=0;i<32u;++i)assert(memory[i]==0xa5c35a3cu && memory[1024u*600u+32u+i]==0xa5c35a3cu);
 const char *folder=getenv("SPEAKER_UI_PREVIEW");if(folder && (n==0 || n==2 || n==6)){
  char name[1024];snprintf(name,sizeof(name),"%s/ui_%u.rgbx",folder,n);FILE *f=fopen(name,"wb");assert(f);assert(fwrite(memory+32,4,1024u*600u,f)==1024u*600u);fclose(f);
 }
}
int main(void){
 struct ui_state s;ui_init(&s);assert(ui_control(&s,UI_OPEN_AI));
 for(uint32_t state=0;state<11u;++state){s.speaker_status=state;s.speaker_enroll=state==1u||state==7u?1u:0u;s.speaker_progress=state==1u?67u:0u;s.speaker_id=state==2u?3u:0u;s.speaker_score=state==2u?81u:0u;draw(&s,state);}
 s.speaker_enroll=0;
 for(uint32_t i=0;i<4u;++i){uint32_t x=100u+i*240u;assert(ui_touch_remote(&s,x,375,x,375)==(uint32_t)UI_AI_ENROLL_1+i+1u);assert(s.speaker_enroll==i+1u);}
 assert(ui_touch_remote(&s,100,450,100,450)==UI_AI_CANCEL_ENROLL+1u);assert(!s.speaker_enroll);
 s.speaker_mask=15u;assert(ui_touch_remote(&s,360,450,360,450)==UI_AI_ERASE+1u);
 assert(ui_touch_remote(&s,400,168,400,168)==UI_AI_LOCAL+1u);assert(s.model_mode==1u);
 assert(ui_touch_remote(&s,120,168,120,168)==UI_AI_CLOUD+1u);assert(s.model_mode==0u);
 assert(ui_touch_remote(&s,112,554,112,554)==UI_CLOSE_AI+1u);assert(s.settings_page && !s.ai_page);
 s.settings_page=0;s.speaker_id=2u;s.speaker_score=78u;draw(&s,20u);
 puts("PASS_SPEAKER_UI_11_STATES_4_ENROLL_TOUCH_MODE_SWITCH_BOUNDED_RENDER");return 0;
}
