#include "../network_ui/language_profile.h"
#include "ui_layout.h"
#include "ui_render.h"
#include "ui_assets.h"

#define PAPER 0xffffffu
#define CANVAS 0xedf3f5u
#define INK 0x172a38u
#define MUTED 0x526575u
#define ACTION 0x006c68u
#define LINE 0xd7e1e5u
#define TINT 0xe0f0ecu

struct paint {uint32_t *pixels;struct ui_render_stats *stats;uint32_t viewport_clip;};
struct text_lines {uint16_t begin[UI_MAX_TEXT],end[UI_MAX_TEXT];uint32_t count;};
struct clip {int x,y,w,h;};
static void (*render_service)(void);
void ui_render_set_service(void (*service)(void)) {render_service=service;}
static int key_equal(const char *a,const char *b) {
    while (*a && *a==*b) {++a;++b;}
    return *a==*b;
}
static const uint32_t *label(const char *key) {
    static const uint32_t empty[]={0};
    for(uint32_t i=0;i<ui_label_count;++i) if(key_equal(key,ui_labels[i].key)) return ui_labels[i].text;
    return empty;
}
static const struct ui_font *font(uint32_t size) {
    for(uint32_t i=0;i<ui_font_count;++i) if(ui_fonts[i].size==size) return &ui_fonts[i];
    return &ui_fonts[0];
}
static const struct ui_glyph *glyph(const struct ui_font *f,uint32_t cp) {
    uint32_t lo=0,hi=f->count;
    while(lo<hi) {uint32_t mid=(lo+hi)/2u;
        if(f->glyphs[mid].codepoint<cp) lo=mid+1u;else hi=mid;}
    return lo<f->count && f->glyphs[lo].codepoint==cp ? &f->glyphs[lo] : 0;
}
static uint32_t advance(const struct ui_font *f,uint32_t cp) {
    const struct ui_glyph *g=glyph(f,cp);
    if(g) return g->advance;
    if(f->size!=36u && f->size!=44u) return f->size;
    /* The live GB2312 subset is stored once at 28 px. */
    const struct ui_font *live=font(28);g=glyph(live,cp);
    return g?(g->advance*f->size+14u)/28u:f->size;
}
static uint32_t text_width(const struct ui_font *f,const uint32_t *text) {
    uint32_t width=0;
    for(uint32_t i=0;i<UI_MAX_TEXT && text[i];++i) width+=advance(f,text[i]);
    return width;
}
static void rect(struct paint *p,int x,int y,int w,int h,uint32_t color) {
    int x0=x<0?0:x,y0=y<0?0:y,x1=x+w,y1=y+h;
    if(x1>(int)UI_WIDTH) x1=UI_WIDTH;
    if(y1>(int)UI_HEIGHT) y1=UI_HEIGHT;
    for(int row=y0;row<y1;++row) {
        for(int col=x0;col<x1;++col) p->pixels[row*UI_WIDTH+col]=color;
        if(render_service && (row&3)==0) render_service();
    }
    if(x1>x0 && y1>y0) p->stats->pixels_written+=(uint32_t)((x1-x0)*(y1-y0));
}
static void round_rect(struct paint *p,int x,int y,int w,int h,int radius,uint32_t color) {
    rect(p,x+radius,y,w-radius*2,h,color);
    rect(p,x,y+radius,radius,h-radius*2,color);
    rect(p,x+w-radius,y+radius,radius,h-radius*2,color);
    for(int row=0;row<radius;++row) {
        int dy=radius-1-row,span=0;
        while(span<radius && span*span+dy*dy<radius*radius) ++span;
        rect(p,x+radius-span,y+row,span,1,color);
        rect(p,x+w-radius,y+row,span,1,color);
        rect(p,x+radius-span,y+h-1-row,span,1,color);
        rect(p,x+w-radius,y+h-1-row,span,1,color);
    }
}
static uint32_t blend(uint32_t back,uint32_t fore,uint32_t a) {
    uint32_t b=15u-a;
    uint32_t r=(((back>>16)&255u)*b+((fore>>16)&255u)*a+7u)/15u;
    uint32_t g=(((back>>8)&255u)*b+((fore>>8)&255u)*a+7u)/15u;
    uint32_t blue=((back&255u)*b+(fore&255u)*a+7u)/15u;
    return (r<<16)|(g<<8)|blue;
}
static uint32_t coverage_at(const struct ui_font *f,const struct ui_glyph *g,int x,int y) {
    if(x<0 || y<0 || x>=g->width || y>=g->height) return 0;
    uint32_t index=(uint32_t)y*g->width+(uint32_t)x;
    uint32_t packed=f->pixels[g->offset+(index>>1)];
    return (index&1u)?(packed&15u):(packed>>4);
}
static void ink(struct paint *p,int x,int y,uint32_t color,uint32_t coverage,struct clip c) {
    if(!coverage) return;
    if(x<c.x || y<c.y || x>=c.x+c.w || y>=c.y+c.h ||
       x<0 || y<0 || x>=(int)UI_WIDTH || y>=(int)UI_HEIGHT) {
        if(!p->viewport_clip) ++p->stats->clipped_glyph_pixels;
        return;
    }
    uint32_t *pixel=&p->pixels[y*UI_WIDTH+x];
    *pixel=coverage==15u?color:blend(*pixel,color,coverage);
    ++p->stats->pixels_written;
}
static void put_glyph(struct paint *p,const struct ui_font *f,uint32_t cp,int x,int y,
                      uint32_t color,struct clip c) {
    const struct ui_glyph *g=glyph(f,cp);
    if(!g && (f->size==36u || f->size==44u)) {
        const struct ui_font *live=font(28);g=glyph(live,cp);
        if(g) {
            /* Bilinear coverage keeps the existing anti-aliased style without
               duplicating the common Han subset in the 2 MiB font reserve. */
            int left=g->x_offset*(int)f->size/28;
            int top=g->y_offset*(int)f->size/28;
            uint32_t w=(g->width*f->size+27u)/28u,h=(g->height*f->size+27u)/28u;
            for(uint32_t gy=0;gy<h;++gy) for(uint32_t gx=0;gx<w;++gx) {
                uint32_t sx=gx*28u*256u/f->size,sy=gy*28u*256u/f->size;
                int ix=(int)(sx>>8),iy=(int)(sy>>8);
                uint32_t fx=sx&255u,fy=sy&255u;
                uint32_t a=coverage_at(live,g,ix,iy)*(256u-fx)+coverage_at(live,g,ix+1,iy)*fx;
                uint32_t b=coverage_at(live,g,ix,iy+1)*(256u-fx)+coverage_at(live,g,ix+1,iy+1)*fx;
                uint32_t coverage=(a*(256u-fy)+b*fy+32768u)>>16;
                ink(p,x+left+(int)gx,y+top+(int)gy,color,coverage,c);
            }
            return;
        }
    }
    /* A missing codepoint falls back to the box glyph instead of failing the
       screen; only a font without the box itself is counted as an asset error. */
    if(!g) {g=glyph(f,0x25a1u);if(!g) {++p->stats->missing_glyphs;return;}}
    for(uint32_t gy=0;gy<g->height;++gy) for(uint32_t gx=0;gx<g->width;++gx) {
        ink(p,x+g->x_offset+(int)gx,y+g->y_offset+(int)gy,color,coverage_at(f,g,(int)gx,(int)gy),c);
    }
}
static void draw_line(struct paint *p,const struct ui_font *f,const uint32_t *text,
                      uint32_t begin,uint32_t end,int x,int y,uint32_t color,struct clip c) {
    for(uint32_t i=begin;i<end && text[i];++i) {
        put_glyph(p,f,text[i],x,y,color,c);x+=(int)advance(f,text[i]);
        if(render_service) render_service();
    }
}
static void one_line(struct paint *p,const uint32_t *text,uint32_t size,int x,int y,int width,uint32_t color) {
    const struct ui_font *f=font(size);uint32_t n=0;
    while(n<UI_MAX_TEXT && text[n]) ++n;
    draw_line(p,f,text,0,n,x,y,color,(struct clip){x-3,y-2,width+6,(int)size+12});
}
static int closing(uint32_t cp) {
    return cp==0xff0cu || cp==0x3002u || cp==0xff01u || cp==0xff1fu ||
           cp==0xff1bu || cp==0xff1au || cp==0x3001u || cp==0xff09u || cp==0x300bu;
}
static int opening(uint32_t cp) {return cp==0xff08u || cp==0x300au || cp==0x201cu;}
static void wrap(const uint32_t *text,const struct ui_font *f,uint32_t width,struct text_lines *out) {
    uint32_t pos=0;out->count=0;
    while(pos<UI_MAX_TEXT && text[pos] && out->count<UI_MAX_TEXT) {
        while(pos<UI_MAX_TEXT && text[pos]==' ') ++pos;
        if(pos>=UI_MAX_TEXT || !text[pos]) break;
        uint32_t start=pos,end=pos,used=0,word_break=start;
        while(end<UI_MAX_TEXT && text[end] && text[end]!='\n') {
            uint32_t step=advance(f,text[end]);
            if(used+step>width && end>start) break;
            used+=step;if(text[end]==' ') word_break=end;
            ++end;
        }
        uint32_t next=end;
        if(end<UI_MAX_TEXT && text[end] && text[end]!='\n') {
            if(text[end]<128u && word_break>start) {end=word_break;next=end+1u;}
            else if(end-start>1u && (closing(text[end]) || opening(text[end-1u]))) {--end;next=end;}
        } else if(end<UI_MAX_TEXT && text[end]=='\n') {next=end+1u;}
        if(end==start) {end=start+1u;next=end;}
        while(end>start && text[end-1u]==' ') --end;
        out->begin[out->count]=(uint16_t)start;out->end[out->count]=(uint16_t)end;++out->count;
        pos=next;
    }
}
static uint32_t paragraph(struct paint *p,const uint32_t *text,uint32_t size,struct clip c,
                          uint32_t color,uint32_t page,uint32_t *pages,uint32_t *actual_page) {
    struct text_lines lines;const struct ui_font *f=font(size);
    int left=0,right=0;
    for(uint32_t i=0;i<f->count;++i) {
        const struct ui_glyph *g=&f->glyphs[i];
        if(-g->x_offset>left) left=-g->x_offset;
        int overhang=g->x_offset+(int)g->width-(int)g->advance;
        if(overhang>right) right=overhang;
    }
    uint32_t height=(size*145u+99u)/100u;
    uint32_t rows=(uint32_t)c.h/height;if(!rows) rows=1;
    wrap(text,f,(uint32_t)(c.w-left-right),&lines);
    uint32_t total=(lines.count+rows-1u)/rows;if(!total) total=1;
    uint32_t selected=page%total;
    for(uint32_t row=0;row<rows && selected*rows+row<lines.count;++row) {
        uint32_t index=selected*rows+row;
        draw_line(p,f,text,lines.begin[index],lines.end[index],c.x+left,c.y+(int)(row*height),color,c);
    }
    if(pages) *pages=total;
    if(actual_page) *actual_page=selected;
    return lines.count;
}
static void decimal(uint32_t value,uint32_t *text) {
    uint32_t reverse[12],n=0;
    do {reverse[n++]='0'+value%10u;value/=10u;} while(value && n<12u);
    for(uint32_t i=0;i<n;++i) text[i]=reverse[n-1u-i];
    text[n]=0;
}
void ui_format_elapsed(uint32_t ms,uint32_t text[16]) {
    uint32_t seconds=ms/1000u,minutes=(seconds/60u)%60u,hours=seconds/3600u;
    uint32_t n=0;
    if(hours) {
        if(hours<10u) text[n++]='0';
        decimal(hours,text+n);while(text[n]) ++n;text[n++]=':';
    }
    text[n++]='0'+minutes/10u;text[n++]='0'+minutes%10u;text[n++]=':';
    text[n++]='0'+(seconds%60u)/10u;text[n++]='0'+seconds%10u;text[n]=0;
}
static const char *stage_key(const struct ui_state *s) {
#ifdef Z7_UI_ENGLISH
    if(s->stage==UI_CONNECTING) return "Connecting";
    if(s->stage==UI_OFFLINE) return "Offline";
    if(s->session==UI_PAUSED) return "Paused";
    if(s->session==UI_IDLE) return "Stopped";
    switch(s->stage) {case UI_RECOGNIZING:return "Recognizing";case UI_TRANSLATING:return "Translating";
        case UI_FINAL:return "Final";
#ifdef Z7_ARM_TLS
        default:return "Ready";}
#else
        default:return "Listening";}
#endif
#else
    if(s->stage==UI_CONNECTING) return "connecting";
    if(s->stage==UI_OFFLINE) return "offline";
    if(s->session==UI_PAUSED) return "paused";
    if(s->session==UI_IDLE) return "standby";
    switch(s->stage) {case UI_RECOGNIZING:return "recognizing";case UI_TRANSLATING:return "translating";
        case UI_FINAL:return "final";
#ifdef Z7_ARM_TLS
        default:return "service_ready";}
#else
        default:return "listening";}
#endif
#endif
}
#ifdef Z7_UI_ENGLISH
/* Built-in English status text uses the existing printable ASCII glyphs. */
static void english_text(uint32_t out[64],const char *text) {
    uint32_t n=0;while(text[n] && n<63u) {out[n]=(uint8_t)text[n];++n;}out[n]=0;
}
#endif
static void button_at(struct paint *p,int x,int y,int w,const uint32_t *text,int selected) {
    round_rect(p,x,y,w,56,8,selected?ACTION:LINE);
    if(!selected) round_rect(p,x+1,y+1,w-2,54,7,PAPER);
    int tx=x+(w-(int)text_width(font(18),text))/2;
    one_line(p,text,18,tx,y+16,w-12,selected?PAPER:INK);
}
static void button(struct paint *p,int x,int w,const uint32_t *text,int selected) {
    button_at(p,x,530,w,text,selected);
}
#ifdef Z7_BILINGUAL
static const uint32_t language_directions[2][19]={
    {'E','n','g','l','i','s','h',' ','-','>',' ','C','h','i','n','e','s','e',0},
    {'C','h','i','n','e','s','e',' ','-','>',' ','E','n','g','l','i','s','h',0}};
#endif
#ifdef Z7_LANGUAGE_SWITCH
static void language_menu(struct paint *p,const struct ui_state *s) {
    if(!s->language_menu) return;
    int x=UI_LANGUAGE_X,y=UI_LANGUAGE_MENU_Y,w=UI_LANGUAGE_WIDTH,h=2*UI_LANGUAGE_ROW_HEIGHT;
    round_rect(p,x+3,y,w,h+7,10,LINE);
    round_rect(p,x,y-4,w,h+8,10,PAPER);
    for(uint32_t row=0;row<2u;++row) {
        int selected=s->language==row,at=y+(int)row*UI_LANGUAGE_ROW_HEIGHT;
        if(selected) round_rect(p,x+6,at+3,w-12,UI_LANGUAGE_ROW_HEIGHT-6,7,TINT);
        one_line(p,language_directions[row],18,x+16,at+15,w-56,selected?ACTION:INK);
        if(selected) round_rect(p,x+w-30,at+21,10,10,5,ACTION);
    }
}
#endif
uint32_t ui_latency_pack(uint32_t loop_us,int loop_valid,uint32_t text_ms,int text_valid) {
    uint32_t l=loop_us>9999u?0x4000u|9999u:loop_us;
    uint32_t t=text_ms>9999u?0x4000u|9999u:text_ms;
    return (loop_valid?(0x8000u|l):0u) | ((text_valid?(0x8000u|t):0u)<<16);
}
void ui_render_latency(uint32_t *pixels,uint32_t readings,struct ui_render_stats *stats) {
    struct paint p={pixels,stats,0};
    static const uint32_t labels[2][8]={{'L','1',' ','L','O','O','P',0},{'T','1',' ','T','E','X','T',0}};
    static const uint32_t units[2][3]={{'u','s',0},{'m','s',0}};
    for(uint32_t i=0;i<2u;++i) {
        int x=492+(int)i*112;uint32_t value=(readings>>(i*16u))&0xffffu;
        round_rect(&p,x,8,104,48,8,LINE);round_rect(&p,x+1,9,102,46,7,0xf1f6f6u);
        one_line(&p,labels[i],14,x+10,11,84,MUTED);
        uint32_t number[16]={'-','-',0};
        if(value&0x8000u) {
            decimal(value&0x3fffu,number);
            if(value&0x4000u) {uint32_t n=0;while(number[n])++n;number[n++]='+';number[n]=0;}
        }
        one_line(&p,number,18,x+10,30,62,(value&0x8000u)?ACTION:MUTED);
        one_line(&p,units[i],14,x+78,33,20,MUTED);
    }
}
#ifdef Z7_HISTORY_UI
static void settings_view(struct paint *p,const struct ui_state *s) {
    one_line(p,label("settings"),28,24,14,300,INK);
    round_rect(p,24,84,476,404,12,PAPER);round_rect(p,520,84,480,404,12,PAPER);
    one_line(p,label("settings_sound"),28,44,100,400,INK);
    one_line(p,label("settings_tts"),18,44,158,260,INK);
    button_at(p,328,142,148,label(s->tts_enabled?"on":"off"),s->tts_enabled);
    one_line(p,label("settings_volume"),18,44,230,120,INK);
    static const uint32_t minus[]={'-',0},plus[]={'+',0};
    uint32_t number[24];decimal(s->output_volume,number);uint32_t n=0;while(number[n]) ++n;number[n++]='%';number[n]=0;
    button_at(p,168,212,64,minus,0);one_line(p,number,24,252,226,72,INK);button_at(p,328,212,64,plus,0);
    one_line(p,label("settings_rate"),18,44,306,120,INK);
    static const uint32_t rates[3][5]={{'0','.','8','x',0},{'1','.','0','x',0},{'1','.','2','x',0}};
    button_at(p,168,288,64,minus,0);one_line(p,rates[s->rate_index<3?s->rate_index:1],24,252,302,72,INK);button_at(p,328,288,64,plus,0);
    one_line(p,label("settings_dac_only"),14,44,366,412,MUTED);
    button_at(p,44,400,180,label(s->output_muted?"settings_unmute":"settings_mute"),s->output_muted);
    one_line(p,label("settings_display"),28,540,100,400,INK);
    one_line(p,label("font"),18,540,158,100,INK);
    button_at(p,660,142,64,minus,0);button_at(p,912,142,64,plus,0);
    one_line(p,label(s->font_index==0?"small":(s->font_index==2?"large":"normal")),18,772,158,112,INK);
    one_line(p,label("settings_source"),18,540,230,250,INK);
    button_at(p,824,212,152,label(s->show_source?"on":"off"),s->show_source);
    one_line(p,label("settings_language"),18,540,306,400,INK);
    button_at(p,540,360,204,language_directions[0],s->language==UI_EN_ZH);
    button_at(p,764,360,212,language_directions[1],s->language==UI_ZH_EN);
    one_line(p,label("settings_reconnect"),14,540,444,420,MUTED);
    rect(p,0,508,UI_WIDTH,92,PAPER);rect(p,0,508,UI_WIDTH,1,LINE);
    button(p,24,176,label("history_back"),1);
    one_line(p,label("settings_session_only"),14,232,550,752,MUTED);
}
static const uint32_t *history_text(const struct ui_history_entry *entry,int english) {
    int source_is_english=entry->language==UI_EN_ZH;
    return english==source_is_english?entry->source:entry->translated;
}
static uint32_t history_height(const struct ui_history_entry *entry) {
    struct text_lines lines;uint32_t count=0;
    for(int english=0;english<2;++english) {
        wrap(history_text(entry,english),font(28),UI_HISTORY_TEXT_WIDTH,&lines);
        if(lines.count>count) count=lines.count;
    }
    uint32_t height=count*41u+40u;return height<92u?92u:height;
}
void ui_history_prepare(struct ui_state *state) {
    uint32_t total=0;
    for(uint32_t i=0;i<state->history_count;++i) {
        struct ui_history_entry *entry=state->history+i;
        if(!entry->pixel_height) entry->pixel_height=history_height(entry);
        total+=entry->pixel_height;
    }
    state->history_limit=total>UI_HISTORY_BODY_HEIGHT?total-UI_HISTORY_BODY_HEIGHT:0;
    if(state->history_follow || state->history_scroll>state->history_limit)
        state->history_scroll=state->history_limit;
}
static void history_paragraph(struct paint *p,const uint32_t *text,int x,int y) {
    struct text_lines lines;wrap(text,font(28),UI_HISTORY_TEXT_WIDTH,&lines);
    struct clip c={x-4,UI_HISTORY_BODY_Y,UI_HISTORY_TEXT_WIDTH+12,UI_HISTORY_BODY_HEIGHT};
    for(uint32_t row=0;row<lines.count;++row) {
        int at=y+(int)row*41;
        if(at+44<UI_HISTORY_BODY_Y || at>=UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT) continue;
        draw_line(p,font(28),text,lines.begin[row],lines.end[row],x,at,INK,c);
    }
    p->stats->history_lines+=lines.count;
}
static void history_view(struct paint *p,const struct ui_state *s) {
    round_rect(p,24,13,36,36,8,ACTION);
    rect(p,32,22,20,3,PAPER);rect(p,32,29,16,3,PAPER);rect(p,32,36,10,3,PAPER);
    one_line(p,label("brand"),24,72,17,64,INK);
    one_line(p,label("history_title"),18,166,21,290,INK);
    one_line(p,label("touch"),14,720,24,116,MUTED);
    uint32_t number[24];decimal(s->history_count,number);
    uint32_t n=0;while(number[n])++n;number[n++]=' ';number[n++]='/';number[n++]=' ';
    decimal(UI_HISTORY_MAX,number+n);
    round_rect(p,850,17,150,30,15,TINT);one_line(p,number,18,889,20,100,ACTION);
    round_rect(p,24,84,976,404,12,PAPER);
    one_line(p,label("history_english"),18,76,99,380,INK);
    one_line(p,label("history_chinese"),18,548,99,380,INK);
    rect(p,40,130,932,1,LINE);
    if(!s->history_count) {
        round_rect(p,478,212,68,48,8,LINE);round_rect(p,480,214,64,44,6,PAPER);
        rect(p,492,225,40,2,MUTED);rect(p,492,235,32,2,MUTED);rect(p,492,245,22,2,MUTED);
        const uint32_t *text=label("history_empty");
        one_line(p,text,28,(1024-(int)text_width(font(28),text))/2,284,380,INK);
        text=label("history_hint");one_line(p,text,18,(1024-(int)text_width(font(18),text))/2,334,420,MUTED);
    } else {
        int y=UI_HISTORY_BODY_Y-(int)s->history_scroll;p->viewport_clip=1;
        for(uint32_t i=0;i<s->history_count;++i) {
            const struct ui_history_entry *entry=s->history+i;
            int h=(int)(entry->pixel_height?entry->pixel_height:history_height(entry));
            if(y+h>UI_HISTORY_BODY_Y && y<UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT) {
                int top=y<UI_HISTORY_BODY_Y?UI_HISTORY_BODY_Y:y;
                int bottom=y+h>UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT?UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT:y+h;
                if(i&1u) rect(p,40,top,932,bottom-top,0xf6f9fau);
                decimal(s->final_count-s->history_count+i+1u,number);uint32_t len=0;while(number[len])++len;
                draw_line(p,font(14),number,0,len,44,y+16,MUTED,
                          (struct clip){40,UI_HISTORY_BODY_Y,32,UI_HISTORY_BODY_HEIGHT});
                history_paragraph(p,history_text(entry,1),UI_HISTORY_LEFT_X,y+18);
                history_paragraph(p,history_text(entry,0),UI_HISTORY_RIGHT_X,y+18);
                if(y+h-1>=UI_HISTORY_BODY_Y && y+h-1<UI_HISTORY_BODY_Y+UI_HISTORY_BODY_HEIGHT)
                    rect(p,40,y+h-1,932,1,LINE);
            }
            y+=h;
        }
        p->viewport_clip=0;rect(p,511,140,1,348,LINE);
        int track=UI_HISTORY_BODY_HEIGHT,thumb=(int)((uint32_t)track*track/(s->history_limit+track));
        if(thumb<28) thumb=28;
        int at=UI_HISTORY_BODY_Y+(s->history_limit?(int)(s->history_scroll*(uint32_t)(track-thumb)/s->history_limit):0);
        round_rect(p,982,UI_HISTORY_BODY_Y,5,track,2,LINE);round_rect(p,982,at,5,thumb,2,ACTION);
    }
    rect(p,0,508,UI_WIDTH,92,PAPER);rect(p,0,508,UI_WIDTH,1,LINE);
    button(p,UI_HISTORY_BACK_X,UI_HISTORY_BACK_WIDTH,label("history_back"),1);
    one_line(p,label("history_limit"),14,196,550,332,MUTED);
    button(p,UI_HISTORY_OLDER_X,UI_HISTORY_PAGE_WIDTH,label("history_older"),0);
    button(p,UI_HISTORY_NEWER_X,UI_HISTORY_PAGE_WIDTH,label("history_newer"),0);
    button(p,UI_HISTORY_CLEAR_X,UI_HISTORY_CLEAR_WIDTH,label("history_clear"),0);
    if(s->history_confirm) {
        /* Opaque, quiet confirmation; all background hit regions are blocked. */
        round_rect(p,272,192,480,222,14,LINE);round_rect(p,274,194,476,218,12,PAPER);
        one_line(p,label("history_confirm"),28,332,225,360,INK);
        one_line(p,label("history_keep"),18,332,279,380,MUTED);
        button_at(p,UI_HISTORY_CANCEL_X,UI_HISTORY_CONFIRM_Y,UI_HISTORY_CONFIRM_WIDTH,label("history_cancel"),0);
        button_at(p,UI_HISTORY_CONFIRM_X,UI_HISTORY_CONFIRM_Y,UI_HISTORY_CONFIRM_WIDTH,label("history_delete"),1);
    }
}
#endif

void ui_render(uint32_t *pixels,const struct ui_state *s,uint32_t elapsed_ms,
               uint32_t requested_page,struct ui_render_stats *stats) {
    uint32_t *words=(uint32_t *)stats;for(uint32_t i=0;i<sizeof(*stats)/4u;++i) words[i]=0;
    struct paint p={pixels,stats,0};
    rect(&p,0,0,UI_WIDTH,UI_HEIGHT,CANVAS);rect(&p,0,0,UI_WIDTH,64,PAPER);rect(&p,0,63,UI_WIDTH,1,LINE);
#ifdef Z7_HISTORY_UI
    if(s->settings_page) {settings_view(&p,s);return;}
    if(s->history_page) {history_view(&p,s);return;}
#endif
    round_rect(&p,24,13,36,36,8,ACTION);
    rect(&p,32,22,20,3,PAPER);rect(&p,32,29,16,3,PAPER);rect(&p,32,36,10,3,PAPER);
    one_line(&p,label("brand"),24,72,17,64,INK);
#ifdef Z7_UI_ENGLISH
#ifdef Z7_BILINGUAL
    static const uint32_t badges[2][9]={{'E','N',' ','-','>',' ','Z','H',0},{'Z','H',' ','-','>',' ','E','N',0}};
    static const uint32_t captions[2][8]={{'C','h','i','n','e','s','e',0},{'E','n','g','l','i','s','h',0}};
    uint32_t selected=s->language==UI_ZH_EN;
    const uint32_t *direction=language_directions[selected],*mode_badge=badges[selected],*english_caption=captions[selected];
#ifdef Z7_LANGUAGE_SWITCH
    round_rect(&p,UI_LANGUAGE_X,UI_LANGUAGE_Y,UI_LANGUAGE_WIDTH,UI_LANGUAGE_HEIGHT,8,s->language_menu?TINT:0xf1f6f6u);
    for(int i=0;i<3;++i) rect(&p,UI_LANGUAGE_X+UI_LANGUAGE_WIDTH-28+i*3,30+i*2,3,3,MUTED);
    for(int i=0;i<3;++i) rect(&p,UI_LANGUAGE_X+UI_LANGUAGE_WIDTH-19+i*3,34-i*2,3,3,MUTED);
    one_line(&p,direction,18,UI_LANGUAGE_X+12,21,UI_LANGUAGE_WIDTH-52,INK);
#else
    one_line(&p,direction,18,156,21,320,MUTED);
#endif
#else
#if defined(Z7_RECOGNIZE_EN)
    static const uint32_t direction[]={'E','n','g','l','i','s','h',' ','-','>',' ','E','n','g','l','i','s','h',0};
    static const uint32_t mode_badge[]={'E','N',' ','-','>',' ','E','N',0};
    static const uint32_t english_caption[]={'E','n','g','l','i','s','h',0};
#else
    static const uint32_t direction[]={0x4e2d,0x6587,' ',0x2192,' ','E','n','g','l','i','s','h',0};
    static const uint32_t mode_badge[]={'Z','H',' ','-','>',' ','E','N',0};
    static const uint32_t english_caption[]={'E','n','g','l','i','s','h',0};
#endif
    one_line(&p,direction,18,156,21,320,MUTED);
#endif
#else
    one_line(&p,label("language"),18,156,21,320,MUTED);
#endif
#ifdef Z7_HISTORY_UI
    round_rect(&p,716,8,104,48,8,LINE);round_rect(&p,717,9,102,46,7,0xf1f6f6u);
    static const uint32_t voice_label[]={'T','2',' ','V','O','I','C','E',0},ms[]={'m','s',0};
    uint32_t voice_number[16]={'-','-',0};
    if(s->voice_latency_valid) decimal(s->voice_latency_ms>99999u?99999u:s->voice_latency_ms,voice_number);
    one_line(&p,voice_label,14,726,11,84,MUTED);one_line(&p,voice_number,18,726,28,62,INK);one_line(&p,ms,14,792,32,22,MUTED);
#else
    one_line(&p,label("touch"),14,720,24,116,MUTED);
#endif
    round_rect(&p,850,17,150,30,15,TINT);
#ifdef Z7_UI_ENGLISH
    one_line(&p,mode_badge,14,894,22,96,ACTION);
#else
    one_line(&p,label("demo"),14,894,22,96,ACTION);
#endif
    round_rect(&p,24,84,696,404,12,PAPER);round_rect(&p,740,84,260,404,12,0xe5eef0u);
#ifdef Z7_UI_ENGLISH
    one_line(&p,english_caption,18,44,100,220,INK);
#else
    one_line(&p,label("caption"),18,44,100,220,INK);
#endif
    round_rect(&p,566,97,134,28,14,TINT);
#ifdef Z7_UI_ENGLISH
    uint32_t status_text[64],note_text[64];
    english_text(status_text,stage_key(s));
    const uint32_t *stage=status_text;uint32_t sw=text_width(font(14),stage);
    if(sw>118u) english_text(status_text,"Listening");
#else
    const uint32_t *stage=label(stage_key(s));uint32_t sw=text_width(font(14),stage);
    if(sw>118u) stage=label("listening");
#endif
    one_line(&p,stage,14,580,101,118,ACTION);
    static const uint32_t sizes[]={28,36,44};uint32_t size=sizes[s->font_index<3u?s->font_index:1u];
#ifdef Z7_UI_ENGLISH
    const uint32_t *caption=s->translated;
#else
    const uint32_t *caption=s->translated[0]?s->translated:label(s->session==UI_IDLE?"ready":"retained");
#endif
    stats->caption_lines=paragraph(&p,caption,size,(struct clip){44,148,656,198},INK,requested_page,
                                  &stats->caption_pages,&stats->caption_page);
#ifdef Z7_UI_ENGLISH
    english_text(note_text,!s->translated[0]?"":(s->session==UI_PAUSED?"Paused":
        (s->stage==UI_FINAL?"Final":(s->stage==UI_TRANSLATING?"Translating":"Previous subtitle retained"))));
    one_line(&p,note_text,14,44,360,440,MUTED);
#else
    one_line(&p,label(s->session==UI_PAUSED?"paused":(s->stage==UI_FINAL?"final":"retained")),14,44,360,440,MUTED);
#endif
    uint32_t number[20];
    if(stats->caption_pages>1u) {
        decimal(stats->caption_page+1u,number);uint32_t n=0;while(number[n]) ++n;
        number[n++]='/';uint32_t tail[12];decimal(stats->caption_pages,tail);
        uint32_t i=0;while(tail[i]) number[n++]=tail[i++];number[n]=0;
        one_line(&p,label("more"),14,512,360,132,ACTION);one_line(&p,number,14,665,360,32,ACTION);
    }
    rect(&p,44,390,656,1,LINE);one_line(&p,label("source"),14,44,399,120,MUTED);
    if(s->show_source) stats->source_lines=paragraph(&p,s->source,28,(struct clip){44,421,656,60},MUTED,
                                                  requested_page,0,0);
    one_line(&p,label("session"),18,760,100,220,INK);
    one_line(&p,label("time"),14,760,143,100,MUTED);ui_format_elapsed(elapsed_ms,number);
    one_line(&p,number,elapsed_ms>=3600000u?20:24,760,166,120,INK);
    one_line(&p,label("count"),14,898,143,70,MUTED);decimal(s->final_count,number);
    one_line(&p,number,24,898,166,80,INK);
    rect(&p,760,207,220,1,LINE);one_line(&p,label("input"),14,760,218,146,MUTED);
#ifdef Z7_HISTORY_UI
    const char *voice_key=!s->tts_enabled?"tts_off":(s->output_muted || !s->output_volume?"tts_muted":
        (s->voice_status==3u?"tts_playing":(s->voice_status==2u?"tts_buffering":(s->voice_status==4u?"tts_unavailable":"tts_ready"))));
    one_line(&p,label(voice_key),18,760,242,220,ACTION);
#else
    for(uint32_t i=0;i<8u;++i) {
        int h=s->session==UI_RUNNING && s->stage!=UI_CONNECTING && s->stage!=UI_OFFLINE?
            8+(int)((elapsed_ms/170u+i*7u)%24u):4;
        round_rect(&p,914+(int)i*8,246-h,4,h,2,ACTION);
    }
#endif
#ifdef Z7_ARM_TLS
    const char *connection=s->stage==UI_CONNECTING?"connection_wait":
        (s->stage==UI_OFFLINE?"connection_offline":(s->session==UI_IDLE?"connection_start":
        (s->session==UI_PAUSED?"connection_paused":"connection_ready")));
    paragraph(&p,label(connection),14,(struct clip){760,274,220,25},MUTED,0,0,0);
#elif defined(Z7_UI_ENGLISH)
    static const uint32_t bilingual_hint[]={'L','i','v','e',' ','s','u','b','t','i','t','l','e','s',0};
    paragraph(&p,bilingual_hint,14,(struct clip){760,254,220,45},MUTED,0,0,0);
#else
    paragraph(&p,label("pending"),14,(struct clip){760,254,220,45},MUTED,0,0,0);
#endif
    one_line(&p,label("history"),14,760,303,160,MUTED);
    if(!s->history_count) paragraph(&p,label("empty"),28,(struct clip){760,340,220,96},MUTED,0,0,0);
    uint32_t shown=s->history_count>2u?2u:s->history_count;
    for(uint32_t i=0;i<shown;++i) {
        uint32_t index=s->history_count-shown+i;int y=322+(int)i*82;
        round_rect(&p,760,y,220,78,8,PAPER);decimal(s->final_count-shown+i+1u,number);
        one_line(&p,number,14,772,y+3,190,MUTED);
        /* The 28 px font is the one carrying the GB2312 level-1 extension; use
           it here too so recent Chinese records are readable instead of boxes. */
        uint32_t pages=0;stats->history_lines+=paragraph(&p,s->history[index].translated,28,
            (struct clip){772,y+20,196,56},INK,0,&pages,0);
        if(pages>1u) {rect(&p,963,y+71,2,2,MUTED);rect(&p,969,y+71,2,2,MUTED);rect(&p,975,y+71,2,2,MUTED);}
    }
    rect(&p,0,508,UI_WIDTH,92,PAPER);rect(&p,0,508,UI_WIDTH,1,LINE);
    button(&p,24,176,label(s->session==UI_RUNNING?"pause":(s->session==UI_PAUSED?"resume":"start")),1);
#ifdef Z7_HISTORY_UI
    button(&p,212,144,label("history_open"),0);
#else
    button(&p,212,144,label("clear"),0);
#endif
    button(&p,368,120,label("original"),0);
    if(s->show_source) rect(&p,388,575,80,3,ACTION);
    one_line(&p,label("font"),14,510,551,40,MUTED);
    static const uint32_t minus[]={'A','-',0},plus[]={'A','+',0};
    button(&p,558,56,minus,0);button(&p,690,56,plus,0);
    one_line(&p,label(s->font_index==0?"small":(s->font_index==2?"large":"normal")),18,625,547,56,INK);
    button(&p,844,156,label("settings"),0);
#ifdef Z7_LANGUAGE_SWITCH
    language_menu(&p,s); /* Last layer: menu rows cover the subtitle card. */
#endif
}
