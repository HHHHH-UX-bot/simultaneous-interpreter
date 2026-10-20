#include "ui_render.h"
#include "ui_assets.h"

#define PAPER 0xffffffu
#define CANVAS 0xedf3f5u
#define INK 0x172a38u
#define MUTED 0x526575u
#define ACTION 0x006c68u
#define LINE 0xd7e1e5u
#define TINT 0xe0f0ecu

struct paint {uint32_t *pixels;struct ui_render_stats *stats;};
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
    const struct ui_glyph *g=glyph(f,cp);return g?g->advance:f->size;
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
static void put_glyph(struct paint *p,const struct ui_font *f,uint32_t cp,int x,int y,
                      uint32_t color,struct clip c) {
    const struct ui_glyph *g=glyph(f,cp);
    /* A missing codepoint falls back to the box glyph instead of failing the
       screen; only a font without the box itself is counted as an asset error. */
    if(!g) {g=glyph(f,0x25a1u);if(!g) {++p->stats->missing_glyphs;return;}}
    if(!g) return;
    for(uint32_t gy=0;gy<g->height;++gy) for(uint32_t gx=0;gx<g->width;++gx) {
        uint32_t index=gy*g->width+gx;
        uint32_t packed=f->pixels[g->offset+(index>>1)];
        uint32_t coverage=(index&1u)?(packed&15u):(packed>>4);
        if(!coverage) continue;
        int px=x+g->x_offset+(int)gx,py=y+g->y_offset+(int)gy;
        if(px<c.x || py<c.y || px>=c.x+c.w || py>=c.y+c.h ||
           px<0 || py<0 || px>=(int)UI_WIDTH || py>=(int)UI_HEIGHT) {
            ++p->stats->clipped_glyph_pixels;continue;
        }
        uint32_t *pixel=&p->pixels[py*UI_WIDTH+px];
        *pixel=coverage==15u?color:blend(*pixel,color,coverage);
        ++p->stats->pixels_written;
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
#ifdef Z7_TRANSLATE_EN
    if(s->session==UI_PAUSED) return "Paused";
    if(s->session==UI_IDLE) return "Ready";
    switch(s->stage) {case UI_RECOGNIZING:return "Recognizing";case UI_TRANSLATING:return "Translating";
        case UI_FINAL:return "Final";default:return "Listening";}
#else
    if(s->session==UI_PAUSED) return "paused";
    if(s->session==UI_IDLE) return "ready";
    switch(s->stage) {case UI_RECOGNIZING:return "recognizing";case UI_TRANSLATING:return "translating";
        case UI_FINAL:return "final";default:return "listening";}
#endif
}
#ifdef Z7_TRANSLATE_EN
/* Built-in English status text uses the existing printable ASCII glyphs. */
static void english_text(uint32_t out[64],const char *text) {
    uint32_t n=0;while(text[n] && n<63u) {out[n]=(uint8_t)text[n];++n;}out[n]=0;
}
#endif
static void button(struct paint *p,int x,int w,const uint32_t *text,int selected) {
    round_rect(p,x,530,w,56,8,selected?ACTION:LINE);
    if(!selected) round_rect(p,x+1,531,w-2,54,7,PAPER);
    int tx=x+(w-(int)text_width(font(18),text))/2;
    one_line(p,text,18,tx,546,w-12,selected?PAPER:INK);
}
void ui_render(uint32_t *pixels,const struct ui_state *s,uint32_t elapsed_ms,
               uint32_t requested_page,struct ui_render_stats *stats) {
    uint32_t *words=(uint32_t *)stats;for(uint32_t i=0;i<sizeof(*stats)/4u;++i) words[i]=0;
    struct paint p={pixels,stats};
    rect(&p,0,0,UI_WIDTH,UI_HEIGHT,CANVAS);rect(&p,0,0,UI_WIDTH,64,PAPER);rect(&p,0,63,UI_WIDTH,1,LINE);
    round_rect(&p,24,13,36,36,8,ACTION);
    rect(&p,32,22,20,3,PAPER);rect(&p,32,29,16,3,PAPER);rect(&p,32,36,10,3,PAPER);
    one_line(&p,label("brand"),24,72,17,64,INK);
#ifdef Z7_TRANSLATE_EN
    static const uint32_t direction[]={0x4e2d,0x6587,' ',0x2192,' ','E','n','g','l','i','s','h',0};
    static const uint32_t mode_badge[]={'Z','H',' ','-','>',' ','E','N',0};
    static const uint32_t english_caption[]={'E','n','g','l','i','s','h',0};
    static const uint32_t bilingual_hint[]={'C','h','i','n','e','s','e',' ','/',' ','E','n','g','l','i','s','h',' ','s','u','b','t','i','t','l','e','s',0};
    one_line(&p,direction,18,156,21,320,MUTED);
#else
    one_line(&p,label("language"),18,156,21,320,MUTED);
#endif
    one_line(&p,label("touch"),14,720,24,116,MUTED);
    round_rect(&p,850,17,150,30,15,TINT);
#ifdef Z7_TRANSLATE_EN
    one_line(&p,mode_badge,14,894,22,96,ACTION);
#else
    one_line(&p,label("demo"),14,894,22,96,ACTION);
#endif
    round_rect(&p,24,84,696,404,12,PAPER);round_rect(&p,740,84,260,404,12,0xe5eef0u);
#ifdef Z7_TRANSLATE_EN
    one_line(&p,english_caption,18,44,100,220,INK);
#else
    one_line(&p,label("caption"),18,44,100,220,INK);
#endif
    round_rect(&p,566,97,134,28,14,TINT);
#ifdef Z7_TRANSLATE_EN
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
#ifdef Z7_TRANSLATE_EN
    const uint32_t *caption=s->translated;
#else
    const uint32_t *caption=s->translated[0]?s->translated:label(s->session==UI_IDLE?"ready":"retained");
#endif
    stats->caption_lines=paragraph(&p,caption,size,(struct clip){44,148,656,198},INK,requested_page,
                                  &stats->caption_pages,&stats->caption_page);
#ifdef Z7_TRANSLATE_EN
    english_text(note_text,!s->translated[0]?"":(s->session==UI_PAUSED?"Paused":(s->stage==UI_FINAL?"Final":"Translating")));
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
    for(uint32_t i=0;i<8u;++i) {
        int h=s->session==UI_RUNNING?8+(int)((elapsed_ms/170u+i*7u)%24u):4;
        round_rect(&p,914+(int)i*8,246-h,4,h,2,ACTION);
    }
#ifdef Z7_TRANSLATE_EN
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
    button(&p,212,144,label("clear"),0);button(&p,368,120,label("original"),0);
    if(s->show_source) rect(&p,388,575,80,3,ACTION);
    one_line(&p,label("font"),14,510,551,40,MUTED);
    static const uint32_t minus[]={'A','-',0},plus[]={'A','+',0};
    button(&p,558,56,minus,0);button(&p,690,56,plus,0);
    one_line(&p,label(s->font_index==0?"small":(s->font_index==2?"large":"normal")),18,625,547,56,INK);
    button(&p,844,156,label("settings"),0);
}
