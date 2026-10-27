#include "tiny_board_llm.h"
#include "tiny_tokenizer.h"
#include "tinyqwen.h"
#include "tiny_model_transport.h"
#include <math.h>
#include <string.h>

#ifndef TBL_MODEL_ADDRESS
#define TBL_MODEL_ADDRESS ((const void *)(uintptr_t)0x04000000u)
#endif
#ifndef TBL_MODEL_CAPACITY
#define TBL_MODEL_CAPACITY (384u*1024u*1024u)
#endif
#ifndef TBL_TOKENIZER_ADDRESS
#define TBL_TOKENIZER_ADDRESS ((const void *)(uintptr_t)0x1c000000u)
#endif
#ifndef TBL_TOKENIZER_SIZE
#define TBL_TOKENIZER_SIZE 5277796u
#endif
#ifndef TBL_ARENA_ADDRESS
#define TBL_ARENA_ADDRESS ((void *)(uintptr_t)0x1c800000u)
#endif
#ifndef TBL_ARENA_SIZE
#define TBL_ARENA_SIZE (32u*1024u*1024u)
#endif

static const char default_system[]="Translate accurately between Chinese and English as requested. Output only translation.";
__attribute__((weak)) size_t tbl_network_info(uint8_t *out,size_t cap){(void)out;(void)cap;return 0;}
__attribute__((weak)) int tbl_test_pcm(uint32_t offset,const uint8_t *pcm,uint32_t bytes){(void)offset;(void)pcm;(void)bytes;return -1;}
static struct {
    struct tq_model model;
    struct tq_state inference;
    TinyTokenizer tokenizer;
    TinyUtf8Stream utf8;
    uint64_t owner;
    uint32_t phase,job,input_bytes,system_bytes,received,max_tokens,prompt_tokens;
    uint32_t generated,output_bytes,position,layer,error,reason;
    int detail,model_ready,tokenizer_ready,initialized,attempted,locked,running,cancel;
    uint8_t request[TBL_SYSTEM_BYTES+TBL_INPUT_BYTES];
    char source[TBL_INPUT_BYTES+1],output[TBL_OUTPUT_BYTES+1];
    char prompt[TBL_SYSTEM_BYTES+TBL_INPUT_BYTES+160];
    int32_t ids[TBL_CONTEXT],cached_ids[TBL_CONTEXT];
    uint32_t cached_prompt_tokens;
    void *batch_workspace;
    size_t batch_bytes;
} board;

static uint32_t rd32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr32(uint8_t *p,uint32_t v) {unsigned i;for(i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t crc_packet(const uint8_t *p,size_t n) {
    /* Same IEEE CRC as model transport, over header[0:28] + payload. A bounded
       stack copy avoids exposing a streaming CRC API or heap in packet paths. */
    uint8_t joined[28u+TBL_STATUS_BYTES+TBL_CHUNK_BYTES];
    size_t data=n-TBL_HEADER_BYTES;
    memcpy(joined,p,28);if(data)memcpy(joined+28,p+32,data);
    return tmt_crc32(joined,28+data);
}
static const char *error_text(uint32_t code) {
    switch(code) {
    case TBL_OK:return "ok";case TBL_BAD_FORMAT:return "invalid operation, length, reserved field or parameter";
    case TBL_BAD_CRC:return "packet CRC32 mismatch";case TBL_ASSETS_NOT_READY:return "validated model and tokenizer uploads are required";
    case TBL_BUSY:return "job busy; cancel and release the current job first";case TBL_WRONG_JOB:return "unknown job id";
    case TBL_WRONG_OWNER:return "job belongs to another source endpoint";case TBL_BAD_OFFSET:return "offset mismatch or conflicting retransmission";
    case TBL_BAD_STATE:return "operation is invalid in the current job state";case TBL_INCOMPLETE:return "not all system and input bytes have arrived";
    case TBL_BAD_UTF8:return "text must be valid UTF8 without NUL bytes";case TBL_CONTEXT_FULL:return "prompt tokens plus requested generation exceed context 512";
    case TBL_INIT_ERROR:return "model, tokenizer or inference arena initialization failed";
    case TBL_ENGINE_ERROR:return "model forward failed";case TBL_TOKENIZER_ERROR:return "token encode or UTF8 decode failed";
    case TBL_OUTPUT_FULL:return "generated text exceeded output capacity";case TBL_NONFINITE:return "no finite token logits";
    default:return "unknown error";
    }
}
static const char *phase_text(void) {
    switch(board.phase) {
    case TBL_WAIT_ASSETS:return "llm_wait_assets";case TBL_READY:return "llm_ready";
    case TBL_RECEIVING:return "llm_receiving";case TBL_QUEUED:return "llm_queued";
    case TBL_PREFILL:return "llm_prefill";case TBL_GENERATING:return "llm_generating";
    case TBL_DONE:return board.reason==TBL_FINISH_EOS?"llm_done_eos":"llm_done_limit";
    case TBL_CANCELLED:return "llm_cancelled";case TBL_FAILED:return error_text(board.error);
    default:return "llm_unknown";
    }
}
static void ui(void) {tbl_ui_update(board.source,board.output,phase_text());}
static uint32_t flags(void) {return (board.model_ready?1u:0u)|(board.tokenizer_ready?2u:0u)|(board.initialized?4u:0u)|(board.running?8u:0u)|(board.cancel?16u:0u)|(board.locked?32u:0u)|(board.owner==UINT64_MAX?64u:0u)|(tbl_ui_busy()?128u:0u)|(tbl_ui_error()?256u:0u);}
static size_t response(const uint8_t *p,uint32_t code,const void *data,size_t n,uint8_t *out,size_t cap) {
    uint32_t words[16];size_t i,total;
    if(n>TBL_CHUNK_BYTES || cap<TBL_HEADER_BYTES+TBL_STATUS_BYTES+n)return 0;
    words[0]=1;words[1]=board.phase;words[2]=flags();words[3]=board.job;
    words[4]=board.input_bytes;words[5]=board.system_bytes;words[6]=board.received;
    words[7]=board.prompt_tokens;words[8]=board.generated;words[9]=board.max_tokens;
    words[10]=board.output_bytes;words[11]=board.position;words[12]=board.layer;
    words[13]=board.error;words[14]=board.reason;words[15]=(uint32_t)board.detail;
    wr32(out,TBL_MAGIC);wr32(out+4,rd32(p+4)|0x80u);wr32(out+8,rd32(p+8));
    wr32(out+12,board.job);wr32(out+16,board.phase);wr32(out+20,code);wr32(out+24,TBL_STATUS_BYTES+(uint32_t)n);
    for(i=0;i<16;i++)wr32(out+32+4*i,words[i]);
    if(n)memcpy(out+32+TBL_STATUS_BYTES,data,n);
    total=32+TBL_STATUS_BYTES+n;wr32(out+28,crc_packet(out,total));return total;
}
static size_t reply(const uint8_t *p,uint32_t code,uint8_t *out,size_t cap) {
    const char *text=code?error_text(code):NULL;
    return response(p,code,text,text?strlen(text):0,out,cap);
}
static int active(void) {return board.phase>=TBL_RECEIVING&&board.phase<=TBL_GENERATING;}
static void clear_job(void) {
    board.owner=0;board.job=0;board.input_bytes=board.system_bytes=board.received=0;
    board.max_tokens=board.prompt_tokens=board.generated=board.output_bytes=0;
    board.position=board.layer=board.error=board.reason=0;board.detail=board.cancel=0;
    board.source[0]=board.output[0]=0;memset(&board.utf8,0,sizeof(board.utf8));
    board.phase=board.initialized?TBL_READY:TBL_WAIT_ASSETS;
}
void tbl_init(void) {
    if(board.running)return;
    if(board.tokenizer.blob)tiny_tokenizer_destroy(&board.tokenizer);
    if(board.locked)remote_llm_lock(0);
    memset(&board,0,sizeof(board));board.phase=TBL_WAIT_ASSETS;
}
void tbl_set_assets_ready(int model_ready,int tokenizer_ready) {
    board.model_ready=!!model_ready;board.tokenizer_ready=!!tokenizer_ready;
    /* Initialized assets are locked and cannot change under inference. */
}
int tbl_engine_state(void) {return board.initialized?1:(board.attempted?2:0);}
static void failed(uint32_t code,int detail) {
    board.phase=TBL_FAILED;board.error=code;board.detail=detail;
    board.reason=TBL_FINISH_ERROR;ui();
}
static int initialize(void) {
    int status,special;const unsigned char *b;size_t n,required,model_bytes;
    board.attempted=1;remote_llm_lock(1);board.locked=1;
#ifdef TBL_MODEL_SIZE
    model_bytes=TBL_MODEL_SIZE;
#else
    /* The upload receiver already pinned the complete file size and digest.
       Read only its fixed-address header, with an independent capacity bound. */
    model_bytes=rd32((const uint8_t *)TBL_MODEL_ADDRESS+12);
#endif
    if(model_bytes<TQ_HEADER_BYTES||model_bytes>TBL_MODEL_CAPACITY) {failed(TBL_INIT_ERROR,TQ_BOUNDS);return 0;}
    status=tq_init(&board.model,TBL_MODEL_ADDRESS,model_bytes);
    if(status) {failed(TBL_INIT_ERROR,status);return 0;}
    required=tq_state_required(&board.model,TBL_CONTEXT);
    if(!required||required>TBL_ARENA_SIZE) {failed(TBL_INIT_ERROR,TQ_MEMORY);return 0;}
    status=tq_state_init(&board.model,&board.inference,TBL_ARENA_ADDRESS,TBL_ARENA_SIZE,TBL_CONTEXT);
    if(status) {failed(TBL_INIT_ERROR,status);return 0;}
    required=(required+31u)&~(size_t)31u;
    if(required>TBL_ARENA_SIZE || tq_batch4_required(&board.model)>TBL_ARENA_SIZE-required) {failed(TBL_INIT_ERROR,TQ_MEMORY);return 0;}
    board.batch_workspace=(uint8_t *)TBL_ARENA_ADDRESS+required;
    board.batch_bytes=TBL_ARENA_SIZE-required;
    status=tiny_tokenizer_init(&board.tokenizer,TBL_TOKENIZER_ADDRESS,TBL_TOKENIZER_SIZE);
    if(status) {failed(TBL_INIT_ERROR,status);return 0;}
    /* Structural self-test only. Never generate a canned sentence on startup. */
    if(board.tokenizer.vocab_size!=151665u||board.model.vocab<board.tokenizer.vocab_size||
       tiny_tokenizer_token_bytes(&board.tokenizer,151644,&b,&n,&special)||n!=12u||!special||memcmp(b,"<|im_start|>",12)||
       tiny_tokenizer_token_bytes(&board.tokenizer,151645,&b,&n,&special)||n!=10u||!special||memcmp(b,"<|im_end|>",10)||
       tiny_tokenizer_encode(&board.tokenizer,"",0,NULL,0,&n)||n) {
        failed(TBL_INIT_ERROR,TINY_TOKENIZER_INVALID);return 0;
    }
    board.initialized=1;board.phase=TBL_READY;ui();return 1;
}
static int valid_utf8(const uint8_t *s,size_t n) {
    size_t i=0;
    while(i<n) {
        uint32_t cp;unsigned k,j,c=s[i];
        if(!c)return 0;
        if(c<128) {i++;continue;}
        if(c>=0xc2&&c<=0xdf) {k=2;cp=c&31;}
        else if(c>=0xe0&&c<=0xef) {k=3;cp=c&15;}
        else if(c>=0xf0&&c<=0xf4) {k=4;cp=c&7;}
        else return 0;
        if(k>n-i)return 0;
        for(j=1;j<k;j++) {if((s[i+j]&0xc0)!=0x80)return 0;cp=(cp<<6)|(s[i+j]&63);}
        if((k==2&&cp<0x80)||(k==3&&cp<0x800)||(k==4&&cp<0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return 0;
        i+=k;
    }
    return 1;
}
static void progress(void *opaque,uint32_t layer,uint32_t position) {
    (void)opaque;board.layer=layer+1;board.position=position;tbl_pump();
}
static int append_output(const unsigned char *data,size_t bytes,int finish) {
    char out[4096];size_t n;
    if(bytes>(sizeof(out)-12u)/3u) {failed(TBL_TOKENIZER_ERROR,-1);return 0;}
    n=tiny_utf8_stream_push(&board.utf8,data,bytes,finish,out,sizeof(out));
    if(n==SIZE_MAX) {failed(TBL_TOKENIZER_ERROR,-1);return 0;}
    if(n>TBL_OUTPUT_BYTES-board.output_bytes) {failed(TBL_OUTPUT_FULL,0);return 0;}
    if(n)memcpy(board.output+board.output_bytes,out,n);
    board.output_bytes+=(uint32_t)n;board.output[board.output_bytes]=0;return 1;
}
static void complete(uint32_t reason) {
    if(!append_output(NULL,0,1))return;
    board.phase=reason==TBL_FINISH_CANCEL?TBL_CANCELLED:TBL_DONE;
    board.reason=reason;ui();
}
static size_t append_prompt(size_t at,const char *s,size_t n) {
    memcpy(board.prompt+at,s,n);return at+n;
}
static void run(void) {
    static const char prefix[]="<|im_start|>system\n";
    static const char middle[]="<|im_end|>\n<|im_start|>user\n";
    static const char suffix[]="<|im_end|>\n<|im_start|>assistant\n";
    size_t n=0,count=0;uint32_t pos,last,i;int status;
    n=append_prompt(n,prefix,sizeof(prefix)-1);
    n=append_prompt(n,board.system_bytes?(const char *)board.request:default_system,
                    board.system_bytes?board.system_bytes:sizeof(default_system)-1);
    n=append_prompt(n,middle,sizeof(middle)-1);
    n=append_prompt(n,(const char *)board.request+board.system_bytes,board.input_bytes);
    n=append_prompt(n,suffix,sizeof(suffix)-1);board.prompt[n]=0;
    memcpy(board.source,board.request+board.system_bytes,board.input_bytes);board.source[board.input_bytes]=0;
    status=tiny_tokenizer_encode(&board.tokenizer,board.prompt,n,board.ids,TBL_CONTEXT,&count);
    board.prompt_tokens=(uint32_t)count;
    if(status==TINY_TOKENIZER_CAPACITY||count+board.max_tokens>TBL_CONTEXT||!count) {failed(TBL_CONTEXT_FULL,status);return;}
    if(status) {failed(TBL_TOKENIZER_ERROR,status);return;}
    /* Exact-token prefix reuse: cached K/V entries before next_position are
       complete, including after cancellation. Nothing beyond the matching
       prefix participates in causal attention for the new prompt. Recompute
       the final prompt token so its logits are always for this request. */
    uint32_t reuse=0;
    while(reuse+1u<(uint32_t)count && reuse<board.cached_prompt_tokens &&
          reuse<board.inference.next_position && board.ids[reuse]==board.cached_ids[reuse])++reuse;
    if(!reuse)tq_reset(&board.inference);
    else {board.inference.next_position=reuse;board.inference.error=0;}
    memcpy(board.cached_ids,board.ids,count*sizeof(board.ids[0]));
    board.cached_prompt_tokens=(uint32_t)count;
    board.detail=(int)reuse;
    tq_set_progress(&board.inference,progress,NULL);
    board.phase=TBL_PREFILL;ui();
    for(i=reuse;i+1<(uint32_t)count;) {
        if(board.cancel) {complete(TBL_FINISH_CANCEL);return;}
        if(i+4u<(uint32_t)count) {
            uint32_t tokens[4];for(uint32_t j=0;j<4u;++j)tokens[j]=(uint32_t)board.ids[i+j];
            status=tq_prefill_batch4(&board.model,&board.inference,tokens,board.batch_workspace,board.batch_bytes);i+=4u;
        } else {status=tq_prefill_step(&board.model,&board.inference,(uint32_t)board.ids[i],i);++i;}
        if(status) {failed(TBL_ENGINE_ERROR,status);return;}
    }
    pos=(uint32_t)count-1;last=(uint32_t)board.ids[count-1];
    board.phase=TBL_GENERATING;ui();
    while(board.generated<board.max_tokens) {
        float *logits,best=-INFINITY;uint32_t token=UINT32_MAX;
        const unsigned char *bytes;size_t length;int special;
        if(board.cancel) {complete(TBL_FINISH_CANCEL);return;}
        if(pos>=TBL_CONTEXT) {complete(TBL_FINISH_CONTEXT);return;}
        logits=tq_forward(&board.model,&board.inference,last,pos);
        if(!logits) {failed(TBL_ENGINE_ERROR,board.inference.error);return;}
        if(board.cancel) {complete(TBL_FINISH_CANCEL);return;}
        /* Only real tokenizer IDs participate: padded model rows are masked. */
        for(i=0;i<board.tokenizer.vocab_size;i++)if(isfinite(logits[i])&&logits[i]>best) {best=logits[i];token=i;}
        if(token==UINT32_MAX) {failed(TBL_NONFINITE,0);return;}
        board.generated++;board.position=pos;
        if(token==151645u||token==151643u) {complete(TBL_FINISH_EOS);return;}
        if(tiny_tokenizer_token_bytes(&board.tokenizer,(int32_t)token,&bytes,&length,&special)) {failed(TBL_TOKENIZER_ERROR,-1);return;}
        if(!special&&!append_output(bytes,length,0))return;
        ui();tbl_pump();last=token;pos++;
    }
    complete(TBL_FINISH_LIMIT);
}
void tbl_tick(void) {
    if(board.running)return;
    if(!board.initialized) {
        if(board.model_ready&&board.tokenizer_ready&&!board.attempted) {
            board.running=1;(void)initialize();board.running=0;
        }
        return;
    }
    if(board.phase!=TBL_QUEUED)return;
    board.running=1;run();board.running=0;
}
int tbl_submit_local(const char *text,const char *system,uint32_t max_tokens) {
    size_t n=text?strlen(text):0,s=system?strlen(system):0;
    if(!board.initialized || active() || board.running || !n || n>TBL_INPUT_BYTES || s>TBL_SYSTEM_BYTES ||
       !max_tokens || max_tokens>TBL_MAX_GENERATION || !valid_utf8((const uint8_t *)text,n) ||
       !valid_utf8((const uint8_t *)system,s))return 0;
    uint32_t job=board.job+1u;if(!job)job=1;
    clear_job();board.owner=UINT64_MAX;board.job=job;board.input_bytes=(uint32_t)n;board.system_bytes=(uint32_t)s;
    memcpy(board.request,system,s);memcpy(board.request+s,text,n);board.received=(uint32_t)(n+s);
    board.max_tokens=max_tokens;board.phase=TBL_QUEUED;return 1;
}
void tbl_cancel_local(void) {if(active())board.cancel=1;}
size_t tbl_handle_packet(uint64_t owner,const uint8_t *p,size_t n,uint8_t *out,size_t cap) {
    uint32_t op,job,offset,value,count,total;
    if(!p||!out||n<32||rd32(p)!=TBL_MAGIC||cap<96)return 0;
    if(n>32+TBL_CHUNK_BYTES)return reply(p,TBL_BAD_FORMAT,out,cap);
    if(crc_packet(p,n)!=rd32(p+28))return reply(p,TBL_BAD_CRC,out,cap);
    op=rd32(p+4);job=rd32(p+12);offset=rd32(p+16);value=rd32(p+20);count=rd32(p+24);
    if(count!=n-32)return reply(p,TBL_BAD_FORMAT,out,cap);
    if(op==TBL_STATUS) {
        if(count||job||offset||value)return reply(p,TBL_BAD_FORMAT,out,cap);
        return reply(p,TBL_OK,out,cap);
    }
    if(op==10u) {
        uint8_t info[40];
        if(count||job||offset||value)return reply(p,TBL_BAD_FORMAT,out,cap);
        size_t bytes=tbl_network_info(info,sizeof(info));
        return response(p,TBL_OK,info,bytes,out,cap);
    }
    if(op==TBL_VOICE) {
        uint8_t info[96];
        if(count||job||offset||value)return reply(p,TBL_BAD_FORMAT,out,cap);
        size_t bytes=tbl_voice_info(info,sizeof(info));
        return response(p,TBL_OK,info,bytes,out,cap);
    }
    /* Local engineering transport. Production microphone mode rejects it;
       it cannot arm test mode or read/export voice profiles. */
    if(op==9u){
        if(job || value || !count || (count&1u))return reply(p,TBL_BAD_FORMAT,out,cap);
        int accepted=tbl_test_pcm(offset,p+32,count);
        return reply(p,accepted>0?TBL_OK:(accepted==0?TBL_BUSY:(accepted==-2?TBL_BAD_OFFSET:TBL_BAD_STATE)),out,cap);
    }
    if(op==TBL_BEGIN) {
        uint32_t input,system;
        if(count!=8||!job||offset||value<1||value>TBL_MAX_GENERATION)return reply(p,TBL_BAD_FORMAT,out,cap);
        input=rd32(p+32);system=rd32(p+36);
        if(!input||input>TBL_INPUT_BYTES||system>TBL_SYSTEM_BYTES)return reply(p,TBL_BAD_FORMAT,out,cap);
        if(!board.initialized)return reply(p,board.attempted?TBL_INIT_ERROR:TBL_ASSETS_NOT_READY,out,cap);
        if(board.job) {
            if(board.owner!=owner && !(board.owner==UINT64_MAX && !active() && !board.running && !tbl_ui_busy()))return reply(p,TBL_BUSY,out,cap);
            if(board.owner==owner && board.job==job) {
                if(board.input_bytes!=input||board.system_bytes!=system||board.max_tokens!=value)return reply(p,TBL_BAD_FORMAT,out,cap);
                return reply(p,TBL_OK,out,cap);
            }
            if(active()||board.running||tbl_ui_busy())return reply(p,TBL_BUSY,out,cap);
        }
        if(tbl_ui_busy())return reply(p,TBL_BUSY,out,cap);
        clear_job();board.owner=owner;board.job=job;board.input_bytes=input;board.system_bytes=system;
        board.max_tokens=value;board.phase=TBL_RECEIVING;return reply(p,TBL_OK,out,cap);
    }
    if(!board.job||job!=board.job)return reply(p,TBL_WRONG_JOB,out,cap);
    /* Board microphone jobs are visible on the same trusted direct cable as
       the framebuffer; only their GET is public. Mutating operations retain
       the original endpoint ownership checks. */
    if(owner!=board.owner && !(op==TBL_GET && board.owner==UINT64_MAX))return reply(p,TBL_WRONG_OWNER,out,cap);
    total=board.input_bytes+board.system_bytes;
    if(op==TBL_DATA) {
        if(!count||value||offset>total||count>total-offset)return reply(p,TBL_BAD_FORMAT,out,cap);
        if(offset<board.received) {
            if(count>board.received-offset||memcmp(board.request+offset,p+32,count))return reply(p,TBL_BAD_OFFSET,out,cap);
            return reply(p,TBL_OK,out,cap);
        }
        if(board.phase!=TBL_RECEIVING)return reply(p,TBL_BAD_STATE,out,cap);
        if(offset!=board.received)return reply(p,TBL_BAD_OFFSET,out,cap);
        memcpy(board.request+offset,p+32,count);board.received+=count;return reply(p,TBL_OK,out,cap);
    }
    if(count)return reply(p,TBL_BAD_FORMAT,out,cap);
    if(op==TBL_GET) {
        size_t available,wanted;
        if(offset>board.output_bytes||value>TBL_CHUNK_BYTES)return reply(p,TBL_BAD_OFFSET,out,cap);
        available=board.output_bytes-offset;wanted=value?value:TBL_CHUNK_BYTES;
        if(wanted>available)wanted=available;
        if(wanted>cap-96)wanted=cap-96;
        return response(p,TBL_OK,board.output+offset,wanted,out,cap);
    }
    if(offset||value)return reply(p,TBL_BAD_FORMAT,out,cap);
    if(op==TBL_RUN) {
        if(board.phase!=TBL_RECEIVING)return reply(p,TBL_OK,out,cap); /* idempotent RUN */
        if(board.received!=total)return reply(p,TBL_INCOMPLETE,out,cap);
        if(!valid_utf8(board.request,board.system_bytes)||!valid_utf8(board.request+board.system_bytes,board.input_bytes))return reply(p,TBL_BAD_UTF8,out,cap);
        board.phase=TBL_QUEUED;return reply(p,TBL_OK,out,cap);
    }
    if(op==TBL_CANCEL) {
        if(active()) {board.cancel=1;if(!board.running)complete(TBL_FINISH_CANCEL);}
        return reply(p,TBL_OK,out,cap);
    }
    if(op==TBL_RELEASE) {
        if(board.running||active())return reply(p,TBL_BUSY,out,cap);
        clear_job();return reply(p,TBL_OK,out,cap);
    }
    return reply(p,TBL_BAD_FORMAT,out,cap);
}
