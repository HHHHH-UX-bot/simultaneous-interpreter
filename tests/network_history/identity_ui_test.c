#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../software/network_ui/wifi_identity.h"
#include "../../software/subtitle_ui/ui_model.h"
#include "../../software/subtitle_ui/ui_render.h"
static uint32_t pixels[1024u*600u+64u];
static void draw(struct ui_state *s){
 struct ui_render_stats stats;
 for(unsigned i=0;i<32;++i)pixels[i]=pixels[1024*600+32+i]=0x1234abcd;
 ui_render(pixels+32,s,1000,0,&stats);assert(!stats.missing_glyphs && !stats.clipped_glyph_pixels);
 for(unsigned i=0;i<32;++i)assert(pixels[i]==0x1234abcd && pixels[1024*600+32+i]==0x1234abcd);
}
int main(void){
 struct wifi_identity wifi;
 assert(wifi_identity_parse(&wifi,"\r\n+CWJAP:\"CYEA\",\"00:11:22:33:44:55\",6,-40\r\nOK\r\n"));assert(wifi.connected && !strcmp((char *)wifi.name,"CYEA"));
 assert(wifi_identity_parse(&wifi,"+CWJAP:\"A\\\"B\\\\C\\,D\",\"mac\",1,-50"));assert(!strcmp((char *)wifi.name,"A\"B\\C,D"));
 assert(wifi_identity_parse(&wifi,"+CWJAP:\"12345678901234567890123456789012\",\"m\""));
 const char *bad[]={"ERROR", "+CWJAP:\"\",\"m\"", "+CWJAP:\"123456789012345678901234567890123\",\"m\"", "+CWJAP:\"bad\\q\",\"m\"", "+CWJAP:\"unfinished"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){assert(!wifi_identity_parse(&wifi,bad[i]));assert(!wifi.connected && !wifi.name[0]);}
 struct ui_state s;ui_init(&s);draw(&s);
 uint8_t name[33]="CYEA";ui_wifi_identity(&s,1,name);assert(s.wifi_connected && s.wifi_name[3]=='A');
 uint32_t seq=s.change_sequence;ui_wifi_identity(&s,1,name);assert(seq==s.change_sequence);
 draw(&s);s.settings_page=1;draw(&s);s.settings_page=0;s.history_page=1;draw(&s);s.history_page=0;s.ai_page=1;draw(&s);
 assert(ui_control(&s,UI_CLEAR));assert(s.wifi_connected && s.wifi_name[0]=='C');
 memcpy(name,"12345678901234567890123456789012",33);ui_wifi_identity(&s,1,name);draw(&s);
 const uint8_t unicode[33]={0xe4,0xb8,0xad,0xe6,0x96,0x87,0xf0,0x9f,0x98,0x80,0};
 ui_wifi_identity(&s,1,unicode);assert(s.wifi_name[0]==0x4e2d && s.wifi_name[2]==0x1f600);draw(&s);
 ui_wifi_identity(&s,0,name);assert(!s.wifi_connected && !s.wifi_name[0]);draw(&s);
 puts("PASS_ACTUAL_WIFI_PARSER_AND_FOUR_UI_PAGES_BOUNDED_RENDER");return 0;
}
