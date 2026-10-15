#ifndef NETWORK_UI_CLOUD_LIVE_H
#define NETWORK_UI_CLOUD_LIVE_H
#include "../subtitle_ui/ui_model.h"
struct cloud_status {
    uint32_t magic,sequence,request,ack,phase,error,messages,asr_final;
    uint32_t translations,committed,ignored,received,pings,closed,epoch,mask_used;
};
struct cloud_caption {uint32_t sequence,source[UI_MAX_TEXT],translated[UI_MAX_TEXT];};
extern volatile struct cloud_status cloud_report;
extern volatile struct cloud_caption cloud_caption;
void cloud_live_init(struct ui_state *model);
void cloud_live_tick(uint32_t now);
int cloud_live_owns_ui(void);
#endif
