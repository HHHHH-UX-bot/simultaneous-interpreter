/* Host protocol/state-machine harness ONLY. Deliberately mocked model engine;
 * its output is test-supplied token IDs and is not an inference/quality test. */
#include "tiny_board_llm.h"
#include "tinyqwen.h"
#include <string.h>
#include <stdint.h>
const void *tbl_test_tokenizer_blob;
static float logits[151936];
static uint32_t generated[1024],generated_count,generated_at,pumps,forwards,prefills;
static uint32_t cancel_at;
static uint8_t cancel_packet[32];
static uint64_t cancel_owner;
static int locked;
void tbl_test_assets(const void *blob) {tbl_test_tokenizer_blob=blob;}
void tbl_test_generated(const uint32_t *ids,uint32_t n) {memcpy(generated,ids,n*4);generated_count=n;generated_at=0;pumps=forwards=prefills=0;cancel_at=0;}
void tbl_test_cancel(uint64_t owner,const uint8_t *packet,uint32_t after) {cancel_owner=owner;memcpy(cancel_packet,packet,32);cancel_at=after;}
uint32_t tbl_test_forwards(void) {return forwards;}
uint32_t tbl_test_prefills(void) {return prefills;}
int tbl_test_locked(void) {return locked;}
void remote_llm_lock(int value) {locked=value;}
void tbl_ui_update(const char *source,const char *output,const char *status) {(void)source;(void)output;(void)status;}
void tbl_pump(void) {
    uint8_t reply[1200];pumps++;
    /* Deliberately exercise nested tick protection during layer pumping. */
    tbl_tick();
    if(cancel_at&&pumps>=cancel_at) {cancel_at=0;(void)tbl_handle_packet(cancel_owner,cancel_packet,32,reply,sizeof(reply));}
}
uint32_t tmt_crc32(const void *data,size_t n) {
    const uint8_t *p=(const uint8_t *)data;uint32_t v=0xffffffffu;unsigned j;
    while(n--) {v^=*p++;for(j=0;j<8;j++)v=(v>>1)^((v&1)?0xedb88320u:0);}
    return ~v;
}
int tq_init(struct tq_model *m,const void *blob,size_t bytes) {
    (void)blob;(void)bytes;memset(m,0,sizeof(*m));m->vocab=151936;m->context=512;m->layers=2;m->dim=64;return 0;
}
size_t tq_state_required(const struct tq_model *m,uint32_t context) {(void)m;(void)context;return 64;}
int tq_state_init(const struct tq_model *m,struct tq_state *s,void *arena,size_t bytes,uint32_t context) {
    (void)m;(void)arena;(void)bytes;memset(s,0,sizeof(*s));s->context=context;s->logits=logits;return 0;
}
void tq_reset(struct tq_state *s) {s->next_position=0;s->error=0;generated_at=0;}
void tq_set_progress(struct tq_state *s,tq_progress_fn cb,void *p) {s->progress=cb;s->progress_opaque=p;}
static void layers(struct tq_state *s,uint32_t pos) {
    if(s->progress) {s->progress(s->progress_opaque,0,pos);s->progress(s->progress_opaque,1,pos);}
    s->next_position++;
}
int tq_prefill_step(const struct tq_model *m,struct tq_state *s,uint32_t token,uint32_t pos) {
    (void)m;(void)token;prefills++;layers(s,pos);return 0;
}
float *tq_forward(const struct tq_model *m,struct tq_state *s,uint32_t token,uint32_t pos) {
    uint32_t i,target;(void)m;(void)token;forwards++;layers(s,pos);
    for(i=0;i<151936;i++)logits[i]=-100.0f;
    target=generated_at<generated_count?generated[generated_at++]:151645u;
    logits[target]=10.0f;logits[151800]=1000.0f;return logits;
}

static int voice_busy;
int tbl_ui_busy(void){return voice_busy;}
void tbl_test_busy(int v){voice_busy=v;}
size_t tq_batch4_required(const struct tq_model *m){(void)m;return 64;}
int tq_prefill_batch4(const struct tq_model *m,struct tq_state *s,const uint32_t tokens[4],void *w,size_t bytes){(void)w;(void)bytes;for(unsigned i=0;i<4;i++)tq_prefill_step(m,s,tokens[i],s->next_position);return 0;}

int tbl_ui_error(void){return 0;}
size_t tbl_voice_info(uint8_t *p,size_t cap){if(cap<96)return 0;memset(p,0,96);p[0]=0x51;p[1]=0x4c;p[2]=0x56;p[3]=0x31;return 96;}
