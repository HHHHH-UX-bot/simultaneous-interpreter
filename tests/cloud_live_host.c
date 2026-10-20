/* Native boundary for the actual wire/decoder/UI adapter; no AT or hardware. */
#include "../software/network_ui/cloud_live.h"
#include "../software/network_ui/cloud_wire.h"
#include "../software/network_ui/network.h"
#include "../software/network_ui/stream_pilot.h"
#include <string.h>
volatile struct network_status network_info;
volatile struct stream_pilot_status stream_pilot;
static struct ui_state model;
static void (*receive_byte)(uint8_t);
static uint32_t now,reply_size;
static uint8_t reply[2048];
void stream_pilot_set_receiver(void (*receive)(uint8_t)) {receive_byte=receive;}
int network_send(const uint8_t *data,uint32_t bytes) {memcpy(reply,data,bytes);reply_size=bytes;return 1;}
void network_stream_failed(void) {}
void cloud_host_start(void) {
    memset((void *)&network_info,0,sizeof(network_info));memset((void *)&stream_pilot,0,sizeof(stream_pilot));
    ui_init(&model);now=0;reply_size=0;cloud_live_init(&model);
    for(uint32_t i=0;i<128;++i) cloud_masks[i]=(uint8_t)i;
    cloud_report.request=1;cloud_live_tick(now);network_info.phase=4;
}
void cloud_host_feed(const uint8_t *data,uint32_t bytes) {
    for(uint32_t i=0;i<bytes;++i) receive_byte(data[i]);
    cloud_live_tick(++now);
}
void cloud_host_sample(uint32_t *out) {
    now+=200;cloud_live_tick(now);
    const volatile uint32_t *in=(const volatile uint32_t *)&cloud_report;
    for(uint32_t i=0;i<16;++i) out[i]=in[i];
}
void cloud_host_control(uint32_t command) {(void)ui_control(&model,(enum ui_command)command);}
void cloud_host_request(uint32_t request) {network_info.phase=0;cloud_report.request=request;cloud_live_tick(++now);network_info.phase=4;}
const struct ui_state *cloud_host_model(void) {return &model;}
const uint32_t *cloud_host_source(void) {return model.source;}
const uint32_t *cloud_host_translation(void) {return model.translated;}
uint32_t cloud_host_final_count(void) {return model.final_count;}
uint32_t cloud_host_history_count(void) {return model.history_count;}
const uint8_t *cloud_host_reply(uint32_t *bytes) {*bytes=reply_size;return reply;}
