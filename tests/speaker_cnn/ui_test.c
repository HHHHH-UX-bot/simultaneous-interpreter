#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../software/subtitle_ui/ui_model.h"
#include "../../software/subtitle_ui/ui_render.h"
#include "../../software/subtitle_ui/ui_touch.h"
static uint32_t memory[1024u*600u+64u];
static void draw(struct ui_state *s,uint32_t n){
 struct ui_render_stats stats;for(uint32_t i=0;i<32u;++i)memory[i]=memory[1024u*600u+32u+i]=0xa5c35a3cu;
 ui_render(memory+32,s,1000,0,&stats);assert(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
 for(uint32_t i=0;i<32u;++i)assert(memory[i]==0xa5c35a3cu && memory[1024u*600u+32u+i]==0xa5c35a3cu);
 const char *folder=getenv("SPEAKER_UI_PREVIEW");if(folder && (n==0 || n==2 || n==6 || n==32 || n==34 || n==40 || n==41 || n>=50)){
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
 /* Board model states on the model page (30..37, 38: verified, engine starting),
    then the ready notice over the main page (40) and the model page (41). */
 s.ai_page=1;
 for(uint32_t load=0;load<8u;++load){s.model_load=load;s.model_percent=load==UI_MODEL_LOADING?45u:0u;draw(&s,30u+load);}
 s.model_load=UI_MODEL_LOADING;s.model_percent=100u;draw(&s,38u);
 s.ai_page=0;s.model_load=UI_MODEL_READY;s.model_notice=1;draw(&s,40u);
 s.ai_page=1;draw(&s,41u);
 /* 调试信息 toggle on the settings page (50: shown), then the face-to-face
    view reached from the third picker row; both halves' buttons by touch. */
 s.ai_page=0;s.model_notice=0;assert(ui_control(&s,UI_OPEN_SETTINGS));
 assert(ui_touch_remote(&s,420,428,420,428)==UI_TOGGLE_DEBUG+1u && s.show_debug);
 assert(ui_control(&s,UI_CLOSE_SETTINGS));draw(&s,50u);
 assert(ui_touch_remote(&s,420,428,420,428)==0u);   /* main page: no toggle there */
 assert(ui_control(&s,UI_OPEN_SETTINGS) && ui_touch_remote(&s,420,428,420,428)==UI_TOGGLE_DEBUG+1u && !s.show_debug);
 assert(ui_control(&s,UI_CLOSE_SETTINGS));
 assert(ui_touch_remote(&s,300,30,300,30)==UI_LANGUAGE_MENU+1u && s.language_menu);draw(&s,51u);
 assert(ui_touch_remote(&s,300,198,300,198)==UI_OPEN_CONVERSATION+1u && s.conversation && !s.language_menu);
 static const uint32_t chinese[]={0x4eca,0x5929,0x7684,0x4f1a,0x8bae,0x73b0,0x5728,0x5f00,0x59cb,0x3002,0};
 static const uint32_t english[]={'T','o','d','a','y','\'','s',' ','m','e','e','t','i','n','g',' ','s','t','a','r','t','s',' ','n','o','w','.',0};
 assert(s.language==UI_ZH_EN);s.session=UI_RUNNING;
 memcpy(s.source,chinese,sizeof(chinese));memcpy(s.translated,english,sizeof(english));draw(&s,52u);
 assert(ui_touch_remote(&s,100,40,100,40)==UI_SELECT_EN_ZH+1u && s.language==UI_EN_ZH && s.conversation);
 memcpy(s.source,english,sizeof(english));memcpy(s.translated,chinese,sizeof(chinese));draw(&s,53u);
 assert(ui_touch_remote(&s,100,40,100,40)==0u);   /* already speaking and running */
 assert(ui_touch_remote(&s,850,555,850,555)==UI_SELECT_ZH_EN+1u && s.language==UI_ZH_EN);
 s.session=UI_PAUSED;assert(ui_touch_remote(&s,850,555,850,555)==UI_RESUME+1u && s.session==UI_RUNNING);
 assert(ui_touch_remote(&s,900,40,900,40)==UI_CLOSE_CONVERSATION+1u && !s.conversation);
 assert(ui_control(&s,UI_OPEN_CONVERSATION) && ui_touch_remote(&s,100,555,100,555)==UI_CLOSE_CONVERSATION+1u);
 puts("PASS_SPEAKER_UI_11_STATES_4_ENROLL_TOUCH_MODE_SWITCH_8_MODEL_STATES_NOTICE_DEBUG_CONVERSATION_BOUNDED_RENDER");return 0;
}
