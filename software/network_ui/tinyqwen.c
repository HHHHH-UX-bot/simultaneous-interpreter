/* Bare-metal Qwen2/Qwen2.5 decoder, group64 symmetric Q4/Q6 weights + Q8 vectors.
   The model computation follows the public Qwen2 architecture. No third-party
   source is embedded here. Scalar reference and ARMv7 NEON paths use the same
   layout; neither path expands the complete weight/embedding matrices. */
#include "tinyqwen.h"
#include <math.h>
#include <string.h>
#if defined(__ARM_NEON) && !defined(TQ_NO_NEON)
#include <arm_neon.h>
#define TQ_NEON 1
#endif

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static float rdf32(const uint8_t *p) {
    uint32_t bits=rd32(p);float value;memcpy(&value,&bits,4);return value;
}
static int set_matrix(struct tq_matrix *matrix,const uint8_t *base,size_t bytes,
                      uint32_t offset,uint32_t rows,uint32_t cols,
                      uint32_t expected_rows,uint32_t expected_cols,uint32_t bits) {
    uint64_t n=(uint64_t)rows*cols;
    if(rows!=expected_rows || cols!=expected_cols || !cols || cols%TQ_GROUP)return TQ_FORMAT;
    uint64_t packed=n*bits/8u,length=packed+n/TQ_GROUP*4u;
    if(offset<TQ_HEADER_BYTES || (offset&3u) || (uint64_t)offset+length>bytes)return TQ_BOUNDS;
    matrix->q=base+offset;matrix->scale=(const float *)(base+offset+(size_t)packed);
    matrix->rows=rows;matrix->cols=cols;matrix->bits=bits;matrix->stride=TQ_GROUP*bits/8u;return TQ_OK;
}
static int set_vector(const float **out,const uint8_t *base,size_t bytes,
                      uint32_t offset,uint32_t rows,uint32_t cols,uint32_t count) {
    if(cols || rows!=count)return TQ_FORMAT;
    if(offset<TQ_HEADER_BYTES || (offset&3u) || (uint64_t)offset+(uint64_t)rows*4u>bytes)return TQ_BOUNDS;
    *out=(const float *)(base+offset);return TQ_OK;
}
int tq_init(struct tq_model *m,const void *blob,size_t bytes) {
    if(!m || !blob || ((uintptr_t)blob&3u))return TQ_ARGUMENT;
    memset(m,0,sizeof(*m));
    if(bytes<TQ_HEADER_BYTES)return TQ_HEADER;
    const uint8_t *b=(const uint8_t *)blob;
    if(rd32(b)!=TQ_MAGIC || rd32(b+4)!=1u || rd32(b+8)!=TQ_HEADER_BYTES || rd32(b+12)!=bytes)return TQ_HEADER;
    m->dim=rd32(b+16);m->ffn=rd32(b+20);m->layers=rd32(b+24);
    m->heads=rd32(b+28);m->kv_heads=rd32(b+32);m->vocab=rd32(b+36);m->context=rd32(b+40);
    m->rms_eps=rdf32(b+48);m->rope_theta=rdf32(b+52);
    m->bits=rd32(b+60);if(!m->bits)m->bits=4u;
    uint32_t count=rd32(b+56);
    if((m->bits!=4u && m->bits!=6u) || rd32(b+44)!=TQ_GROUP || !m->dim || m->dim>8192u || m->dim%TQ_GROUP ||
       !m->ffn || m->ffn>32768u || m->ffn%TQ_GROUP || !m->layers || m->layers>TQ_MAX_LAYERS ||
       !m->heads || !m->kv_heads || m->dim%m->heads || m->heads%m->kv_heads ||
       !m->vocab || m->vocab>262144u || !m->context || m->context>32768u ||
       !(m->rms_eps>0.0f && m->rms_eps<1.0f) || !(m->rope_theta>1.0f && m->rope_theta<1.0e12f))return TQ_FORMAT;
    m->head_size=m->dim/m->heads;m->kv_dim=m->head_size*m->kv_heads;
    if(m->head_size&1u)return TQ_FORMAT;
    if(count!=m->layers*12u+2u || 256u+(uint64_t)count*20u>TQ_HEADER_BYTES)return TQ_FORMAT;
    uint32_t seen[TQ_MAX_LAYERS]={0},root_seen=0;
    for(uint32_t i=0;i<count;++i) {
        const uint8_t *d=b+256u+i*20u;
        uint32_t kind=rd32(d),layer=rd32(d+4),offset=rd32(d+8),rows=rd32(d+12),cols=rd32(d+16);
        int result=TQ_FORMAT;
        if(kind==0u || kind==13u) {
            uint32_t flag=kind==0u?1u:2u;
            if(layer!=0xffffffffu)return TQ_FORMAT;
            if(root_seen&flag)return TQ_DUPLICATE;
            root_seen|=flag;
            if(kind==0u)result=set_matrix(&m->embedding,b,bytes,offset,rows,cols,m->vocab,m->dim,m->bits);
            else result=set_vector(&m->final_norm,b,bytes,offset,rows,cols,m->dim);
        } else {
            if(layer>=m->layers || kind>12u)return TQ_FORMAT;
            uint32_t flag=1u<<kind;
            if(seen[layer]&flag)return TQ_DUPLICATE;
            seen[layer]|=flag;
            struct tq_layer *l=&m->layer[layer];
            switch(kind) {
            case 1:result=set_vector(&l->attn_norm,b,bytes,offset,rows,cols,m->dim);break;
            case 2:result=set_matrix(&l->q,b,bytes,offset,rows,cols,m->dim,m->dim,m->bits);break;
            case 3:result=set_matrix(&l->k,b,bytes,offset,rows,cols,m->kv_dim,m->dim,m->bits);break;
            case 4:result=set_matrix(&l->v,b,bytes,offset,rows,cols,m->kv_dim,m->dim,m->bits);break;
            case 5:result=set_matrix(&l->o,b,bytes,offset,rows,cols,m->dim,m->dim,m->bits);break;
            case 6:result=set_vector(&l->mlp_norm,b,bytes,offset,rows,cols,m->dim);break;
            case 7:result=set_matrix(&l->gate,b,bytes,offset,rows,cols,m->ffn,m->dim,m->bits);break;
            case 8:result=set_matrix(&l->up,b,bytes,offset,rows,cols,m->ffn,m->dim,m->bits);break;
            case 9:result=set_matrix(&l->down,b,bytes,offset,rows,cols,m->dim,m->ffn,m->bits);break;
            case 10:result=set_vector(&l->q_bias,b,bytes,offset,rows,cols,m->dim);break;
            case 11:result=set_vector(&l->k_bias,b,bytes,offset,rows,cols,m->kv_dim);break;
            case 12:result=set_vector(&l->v_bias,b,bytes,offset,rows,cols,m->kv_dim);break;
            default:break;
            }
        }
        if(result)return result;
    }
    if(root_seen!=3u)return TQ_INCOMPLETE;
    for(uint32_t i=0;i<m->layers;++i)if(seen[i]!=0x1ffeu)return TQ_INCOMPLETE;
    return TQ_OK;
}

static size_t aligned32(size_t n) {return (n+31u)&~(size_t)31u;}
size_t tq_state_required(const struct tq_model *m,uint32_t ctx) {
    if(!m || !m->dim || !ctx || ctx>m->context)return 0;
    size_t total=31u;
    /* x, xb, xb2, q; two MLP vectors; attention; logits; K/V; vector Q8
       scales and quantized values; RoPE inverse frequencies and sin/cos. */
    size_t count[]={m->dim,m->dim,m->dim,m->dim,m->ffn,m->ffn,ctx,m->vocab,
        (size_t)m->layers*ctx*m->kv_dim,(size_t)m->layers*ctx*m->kv_dim,
        (m->ffn>m->dim?m->ffn:m->dim)/TQ_GROUP,m->head_size/2u,m->head_size/2u,m->head_size/2u};
    for(size_t i=0;i<sizeof(count)/sizeof(count[0]);++i)total+=aligned32(count[i]*sizeof(float));
    total+=aligned32(m->ffn>m->dim?m->ffn:m->dim);
    return total;
}
struct tq_arena {uint8_t *p;size_t left;};
static void *take(struct tq_arena *a,size_t bytes) {
    size_t padding=(size_t)(-(uintptr_t)a->p)&31u;
    if(padding>a->left || bytes>a->left-padding)return 0;
    a->p+=padding;a->left-=padding;
    void *result=a->p;a->p+=bytes;a->left-=bytes;return result;
}
int tq_state_init(const struct tq_model *m,struct tq_state *s,void *memory,size_t bytes,uint32_t ctx) {
    if(!m || !s || !memory || !ctx || ctx>m->context)return TQ_ARGUMENT;
    memset(s,0,sizeof(*s));
    if(bytes<tq_state_required(m,ctx))return TQ_MEMORY;
    struct tq_arena a={(uint8_t *)memory,bytes};
#define FLOATS(name,count) do {s->name=(float *)take(&a,(size_t)(count)*sizeof(float));if(!s->name)return TQ_MEMORY;} while(0)
    FLOATS(x,m->dim);FLOATS(xb,m->dim);FLOATS(xb2,m->dim);FLOATS(q,m->dim);
    FLOATS(hb,m->ffn);FLOATS(hb2,m->ffn);FLOATS(att,ctx);FLOATS(logits,m->vocab);
    FLOATS(key_cache,(size_t)m->layers*ctx*m->kv_dim);
    FLOATS(value_cache,(size_t)m->layers*ctx*m->kv_dim);
    uint32_t max_vector=m->ffn>m->dim?m->ffn:m->dim;
    FLOATS(aq_scale,max_vector/TQ_GROUP);
    FLOATS(inv_freq,m->head_size/2u);FLOATS(rope_cos,m->head_size/2u);FLOATS(rope_sin,m->head_size/2u);
#undef FLOATS
    s->aq=(int8_t *)take(&a,max_vector);if(!s->aq)return TQ_MEMORY;
    s->context=ctx;
    for(uint32_t i=0;i<m->head_size/2u;++i)s->inv_freq[i]=powf(m->rope_theta,-2.0f*(float)i/(float)m->head_size);
    return TQ_OK;
}
void tq_reset(struct tq_state *s) {if(s){s->next_position=0;s->error=0;}}
void tq_set_progress(struct tq_state *s,tq_progress_fn callback,void *opaque) {
    if(s){s->progress=callback;s->progress_opaque=opaque;}
}
static void quantize(struct tq_state *s,const float *x,uint32_t n) {
    for(uint32_t group=0;group<n/TQ_GROUP;++group) {
        float maximum=0.0f;
        for(uint32_t j=0;j<TQ_GROUP;++j) {float v=fabsf(x[group*TQ_GROUP+j]);if(v>maximum)maximum=v;}
        float scale=maximum/127.0f,inv=maximum>0.0f?127.0f/maximum:0.0f;
        s->aq_scale[group]=scale;
        for(uint32_t j=0;j<TQ_GROUP;++j) {
            float value=x[group*TQ_GROUP+j]*inv;
            int q=(int)(value+(value>=0.0f?0.5f:-0.5f));
            if(q>127)q=127;
            if(q<-127)q=-127;
            s->aq[group*TQ_GROUP+j]=(int8_t)q;
        }
    }
}
static int32_t dot64(const uint8_t *q,const int8_t *x,uint32_t bits) {
#ifdef TQ_NEON
    int32x4_t acc=vdupq_n_s32(0);uint8x16_t mask=vdupq_n_u8(15u);int8x16_t eight=vdupq_n_s8(8);
    if(bits==6u) {
        uint8x16_t high=vld1q_u8(q+32u),two=vdupq_n_u8(3u);
        uint8x16_t a=vld1q_u8(q),b=vld1q_u8(q+16u);
        uint8x16_t value[4]={
            vorrq_u8(vandq_u8(a,mask),vshlq_n_u8(vandq_u8(high,two),4)),
            vorrq_u8(vandq_u8(b,mask),vshlq_n_u8(vandq_u8(vshrq_n_u8(high,2),two),4)),
            vorrq_u8(vshrq_n_u8(a,4),vshlq_n_u8(vandq_u8(vshrq_n_u8(high,4),two),4)),
            vorrq_u8(vshrq_n_u8(b,4),vshlq_n_u8(vshrq_n_u8(high,6),4))};
        for(uint32_t j=0;j<4u;++j) {
            int8x16_t weight=vsubq_s8(vreinterpretq_s8_u8(value[j]),vdupq_n_s8(32));
            int8x16_t input=vld1q_s8(x+j*16u);
            acc=vpadalq_s16(acc,vmull_s8(vget_low_s8(weight),vget_low_s8(input)));
            acc=vpadalq_s16(acc,vmull_s8(vget_high_s8(weight),vget_high_s8(input)));
        }
    } else {
    for(uint32_t j=0;j<32u;j+=16u) {
        uint8x16_t packed=vld1q_u8(q+j);
        int8x16_t low=vsubq_s8(vreinterpretq_s8_u8(vandq_u8(packed,mask)),eight);
        int8x16_t high=vsubq_s8(vreinterpretq_s8_u8(vshrq_n_u8(packed,4)),eight);
        int8x16_t a=vld1q_s8(x+j),b=vld1q_s8(x+32u+j);
        acc=vpadalq_s16(acc,vmull_s8(vget_low_s8(low),vget_low_s8(a)));
        acc=vpadalq_s16(acc,vmull_s8(vget_high_s8(low),vget_high_s8(a)));
        acc=vpadalq_s16(acc,vmull_s8(vget_low_s8(high),vget_low_s8(b)));
        acc=vpadalq_s16(acc,vmull_s8(vget_high_s8(high),vget_high_s8(b)));
    }
    }
    int32x2_t pair=vadd_s32(vget_low_s32(acc),vget_high_s32(acc));pair=vpadd_s32(pair,pair);
    return vget_lane_s32(pair,0);
#else
    int32_t sum=0;
    if(bits==6u) {
        for(uint32_t j=0;j<16u;++j) {
            uint32_t a=q[j],b=q[j+16u],high=q[j+32u];
            sum+=((int32_t)((a&15u)|((high&3u)<<4))-32)*(int32_t)x[j];
            sum+=((int32_t)((b&15u)|(((high>>2)&3u)<<4))-32)*(int32_t)x[j+16u];
            sum+=((int32_t)((a>>4)|(((high>>4)&3u)<<4))-32)*(int32_t)x[j+32u];
            sum+=((int32_t)((b>>4)|((high>>6)<<4))-32)*(int32_t)x[j+48u];
        }
    } else for(uint32_t j=0;j<32u;++j)sum+=((int32_t)(q[j]&15u)-8)*(int32_t)x[j]+((int32_t)(q[j]>>4)-8)*(int32_t)x[j+32u];
    return sum;
#endif
}
static void matmul(float *out,const struct tq_matrix *w,const struct tq_state *s) {
    uint32_t groups=w->cols/TQ_GROUP;
    for(uint32_t row=0;row<w->rows;++row) {
        float sum=0.0f;size_t start=(size_t)row*groups;
        for(uint32_t g=0;g<groups;++g) {
            int32_t dot=dot64(w->q+(start+g)*w->stride,s->aq+g*TQ_GROUP,w->bits);
            sum+=(float)dot*w->scale[start+g]*s->aq_scale[g];
        }
        out[row]=sum;
    }
}
static void embedding(float *x,const struct tq_matrix *w,uint32_t token) {
    uint32_t groups=w->cols/TQ_GROUP;
    for(uint32_t g=0;g<groups;++g) {
        const uint8_t *q=w->q+((size_t)token*groups+g)*w->stride;
        float scale=w->scale[(size_t)token*groups+g];
        if(w->bits==6u)for(uint32_t j=0;j<64u;++j) {
            uint32_t low=(q[j%32u]>>((j/32u)*4u))&15u;
            uint32_t high=(q[32u+j%16u]>>((j/16u)*2u))&3u;
            x[g*TQ_GROUP+j]=((int32_t)(low|(high<<4))-32)*scale;
        } else for(uint32_t j=0;j<32u;++j) {
            x[g*TQ_GROUP+j]=((int32_t)(q[j]&15u)-8)*scale;
            x[g*TQ_GROUP+j+32u]=((int32_t)(q[j]>>4)-8)*scale;
        }
    }
}
static void rmsnorm(float *out,const float *x,const float *weight,uint32_t n,float eps) {
    float sum=0.0f;for(uint32_t i=0;i<n;++i)sum+=x[i]*x[i];
    float inverse=1.0f/sqrtf(sum/(float)n+eps);
    for(uint32_t i=0;i<n;++i)out[i]=x[i]*inverse*weight[i];
}
static void rotate(float *x,uint32_t heads,uint32_t width,const struct tq_state *s) {
    uint32_t half=width/2u;
    for(uint32_t h=0;h<heads;++h)for(uint32_t j=0;j<half;++j) {
        float first=x[h*width+j],second=x[h*width+j+half];
        x[h*width+j]=first*s->rope_cos[j]-second*s->rope_sin[j];
        x[h*width+j+half]=second*s->rope_cos[j]+first*s->rope_sin[j];
    }
}
static int forward(const struct tq_model *m,struct tq_state *s,uint32_t token,uint32_t pos,int need_logits) {
    if(!m || !s)return TQ_ARGUMENT;
    if(token>=m->vocab || pos>=s->context || pos!=s->next_position)return s->error=TQ_POSITION;
    embedding(s->x,&m->embedding,token);
    for(uint32_t i=0;i<m->head_size/2u;++i) {
        float angle=(float)pos*s->inv_freq[i];s->rope_cos[i]=cosf(angle);s->rope_sin[i]=sinf(angle);
    }
    float attn_scale=1.0f/sqrtf((float)m->head_size);
    for(uint32_t index=0;index<m->layers;++index) {
        const struct tq_layer *l=&m->layer[index];
        size_t cache_start=((size_t)index*s->context+pos)*m->kv_dim;
        float *k=s->key_cache+cache_start,*v=s->value_cache+cache_start;
        rmsnorm(s->xb,s->x,l->attn_norm,m->dim,m->rms_eps);
        quantize(s,s->xb,m->dim);matmul(s->q,&l->q,s);matmul(k,&l->k,s);matmul(v,&l->v,s);
        for(uint32_t j=0;j<m->dim;++j)s->q[j]+=l->q_bias[j];
        for(uint32_t j=0;j<m->kv_dim;++j){k[j]+=l->k_bias[j];v[j]+=l->v_bias[j];}
        rotate(s->q,m->heads,m->head_size,s);rotate(k,m->kv_heads,m->head_size,s);
        for(uint32_t h=0;h<m->heads;++h) {
            uint32_t kv_head=h/(m->heads/m->kv_heads);
            const float *query=s->q+h*m->head_size;
            float maximum=-INFINITY;
            for(uint32_t t=0;t<=pos;++t) {
                const float *key=s->key_cache+((size_t)index*s->context+t)*m->kv_dim+kv_head*m->head_size;
                float sum=0.0f;for(uint32_t j=0;j<m->head_size;++j)sum+=query[j]*key[j];
                float score=sum*attn_scale;s->att[t]=score;if(score>maximum)maximum=score;
            }
            float sum=0.0f;
            for(uint32_t t=0;t<=pos;++t){s->att[t]=expf(s->att[t]-maximum);sum+=s->att[t];}
            for(uint32_t j=0;j<m->head_size;++j)s->xb[h*m->head_size+j]=0.0f;
            for(uint32_t t=0;t<=pos;++t) {
                float probability=s->att[t]/sum;
                const float *value=s->value_cache+((size_t)index*s->context+t)*m->kv_dim+kv_head*m->head_size;
                for(uint32_t j=0;j<m->head_size;++j)s->xb[h*m->head_size+j]+=probability*value[j];
            }
        }
        quantize(s,s->xb,m->dim);matmul(s->xb2,&l->o,s);
        for(uint32_t j=0;j<m->dim;++j)s->x[j]+=s->xb2[j];
        rmsnorm(s->xb,s->x,l->mlp_norm,m->dim,m->rms_eps);
        quantize(s,s->xb,m->dim);matmul(s->hb,&l->gate,s);matmul(s->hb2,&l->up,s);
        for(uint32_t j=0;j<m->ffn;++j)s->hb[j]=(s->hb[j]/(1.0f+expf(-s->hb[j])))*s->hb2[j];
        quantize(s,s->hb,m->ffn);matmul(s->xb,&l->down,s);
        for(uint32_t j=0;j<m->dim;++j)s->x[j]+=s->xb[j];
        if(s->progress)s->progress(s->progress_opaque,index,pos);
    }
    if(need_logits) {
        rmsnorm(s->xb,s->x,m->final_norm,m->dim,m->rms_eps);
        quantize(s,s->xb,m->dim);matmul(s->logits,&m->embedding,s);
    }
    ++s->next_position;s->error=TQ_OK;return TQ_OK;
}
float *tq_forward(const struct tq_model *m,struct tq_state *s,uint32_t token,uint32_t pos) {
    return forward(m,s,token,pos,1)==TQ_OK?s->logits:0;
}
int tq_prefill_step(const struct tq_model *m,struct tq_state *s,uint32_t token,uint32_t pos) {
    return forward(m,s,token,pos,0);
}

/* Batch-4 prefill keeps exact causal attention but reuses unpacked weight
   vectors across four positions. The workspace is supplied by the caller. */
struct tq_batch4 {float *x,*xb,*xb2,*q,*k,*v,*hb,*hb2,*scale,*cos,*sin;int8_t *aq;uint32_t stride;};
size_t tq_batch4_required(const struct tq_model *m) {
    if(!m || !m->dim || !m->ffn)return 0;
    uint32_t maximum=m->ffn>m->dim?m->ffn:m->dim;
    size_t counts[]={4u*m->dim,4u*m->dim,4u*m->dim,4u*m->dim,4u*m->kv_dim,4u*m->kv_dim,
        4u*m->ffn,4u*m->ffn,4u*(maximum/TQ_GROUP),4u*(m->head_size/2u),4u*(m->head_size/2u)};
    size_t total=31u;
    for(size_t i=0;i<sizeof(counts)/sizeof(counts[0]);++i)total+=aligned32(counts[i]*sizeof(float));
    return total+aligned32(4u*maximum);
}
static void dots4(int32_t out[4],const uint8_t *q,const int8_t *x,uint32_t stride,uint32_t bits) {
#ifdef TQ_NEON
    int32x4_t a0=vdupq_n_s32(0),a1=a0,a2=a0,a3=a0;
    uint8x16_t mask=vdupq_n_u8(15),high=bits==6u?vld1q_u8(q+32):vdupq_n_u8(0);
    for(uint32_t block=0;block<4u;++block) {
        uint8x16_t packed=vld1q_u8(q+(block&1u)*16u);
        uint8x16_t value=block<2u?vandq_u8(packed,mask):vshrq_n_u8(packed,4);
        if(bits==6u) {
            uint8x16_t shift=vdupq_n_u8((uint8_t)(block*2u));
            uint8x16_t upper=vshlq_u8(high,vnegq_s8(vreinterpretq_s8_u8(shift)));
            value=vorrq_u8(value,vshlq_n_u8(vandq_u8(upper,vdupq_n_u8(3)),4));
        }
        int8x16_t weight=vsubq_s8(vreinterpretq_s8_u8(value),vdupq_n_s8(bits==6u?32:8));
#define TQ_ACCUMULATE4(acc,t) do {int8x16_t input=vld1q_s8(x+(t)*stride+block*16u); \
    (acc)=vpadalq_s16((acc),vmull_s8(vget_low_s8(weight),vget_low_s8(input))); \
    (acc)=vpadalq_s16((acc),vmull_s8(vget_high_s8(weight),vget_high_s8(input)));} while(0)
        TQ_ACCUMULATE4(a0,0u);TQ_ACCUMULATE4(a1,1u);TQ_ACCUMULATE4(a2,2u);TQ_ACCUMULATE4(a3,3u);
#undef TQ_ACCUMULATE4
    }
#define TQ_REDUCE4(acc,t) do {int32x2_t pair=vadd_s32(vget_low_s32(acc),vget_high_s32(acc));pair=vpadd_s32(pair,pair);out[t]=vget_lane_s32(pair,0);} while(0)
    TQ_REDUCE4(a0,0);TQ_REDUCE4(a1,1);TQ_REDUCE4(a2,2);TQ_REDUCE4(a3,3);
#undef TQ_REDUCE4
#else
    for(uint32_t t=0;t<4u;++t)out[t]=dot64(q,x+t*stride,bits);
#endif
}
static void quantize4(const struct tq_model *m,struct tq_state *s,struct tq_batch4 *b,const float *x,uint32_t n) {
    int8_t *original_aq=s->aq;float *original_scale=s->aq_scale;
    (void)m;
    for(uint32_t t=0;t<4u;++t) {
        s->aq=b->aq+t*b->stride;s->aq_scale=b->scale+t*(b->stride/TQ_GROUP);quantize(s,x+t*n,n);
    }
    s->aq=original_aq;s->aq_scale=original_scale;
}
static void matmul4(float *out,const struct tq_matrix *w,const struct tq_batch4 *b) {
    uint32_t groups=w->cols/TQ_GROUP,scale_stride=b->stride/TQ_GROUP;
    for(uint32_t row=0;row<w->rows;++row) {
        float sum[4]={0,0,0,0};size_t start=(size_t)row*groups;
        for(uint32_t g=0;g<groups;++g) {
            int32_t dot[4];dots4(dot,w->q+(start+g)*w->stride,b->aq+g*TQ_GROUP,b->stride,w->bits);
            for(uint32_t t=0;t<4u;++t)sum[t]+=(float)dot[t]*w->scale[start+g]*b->scale[t*scale_stride+g];
        }
        for(uint32_t t=0;t<4u;++t)out[t*w->rows+row]=sum[t];
    }
}
int tq_prefill_batch4(const struct tq_model *m,struct tq_state *s,const uint32_t tokens[4],void *workspace,size_t bytes) {
    if(!m || !s || !tokens || !workspace)return TQ_ARGUMENT;
    if(s->next_position>s->context || s->context-s->next_position<4u)return s->error=TQ_POSITION;
    for(uint32_t t=0;t<4u;++t)if(tokens[t]>=m->vocab)return s->error=TQ_POSITION;
    size_t required=tq_batch4_required(m);if(!required || bytes<required)return s->error=TQ_MEMORY;
    struct tq_arena arena={(uint8_t *)workspace,bytes};struct tq_batch4 b;
    b.stride=m->ffn>m->dim?m->ffn:m->dim;
#define TQ_BATCH_FLOATS(name,n) do {b.name=(float *)take(&arena,(n)*sizeof(float));if(!b.name)return s->error=TQ_MEMORY;} while(0)
    TQ_BATCH_FLOATS(x,4u*m->dim);TQ_BATCH_FLOATS(xb,4u*m->dim);TQ_BATCH_FLOATS(xb2,4u*m->dim);
    TQ_BATCH_FLOATS(q,4u*m->dim);TQ_BATCH_FLOATS(k,4u*m->kv_dim);TQ_BATCH_FLOATS(v,4u*m->kv_dim);
    TQ_BATCH_FLOATS(hb,4u*m->ffn);TQ_BATCH_FLOATS(hb2,4u*m->ffn);TQ_BATCH_FLOATS(scale,4u*(b.stride/TQ_GROUP));
    TQ_BATCH_FLOATS(cos,4u*(m->head_size/2u));TQ_BATCH_FLOATS(sin,4u*(m->head_size/2u));
#undef TQ_BATCH_FLOATS
    b.aq=take(&arena,4u*b.stride);if(!b.aq)return s->error=TQ_MEMORY;
    uint32_t base=s->next_position,half=m->head_size/2u;
    for(uint32_t t=0;t<4u;++t) {
        embedding(b.x+t*m->dim,&m->embedding,tokens[t]);
        for(uint32_t j=0;j<half;++j) {float angle=(float)(base+t)*s->inv_freq[j];b.cos[t*half+j]=cosf(angle);b.sin[t*half+j]=sinf(angle);}
    }
    float attn_scale=1.0f/sqrtf((float)m->head_size);
    for(uint32_t layer=0;layer<m->layers;++layer) {
        const struct tq_layer *l=&m->layer[layer];
        for(uint32_t t=0;t<4u;++t)rmsnorm(b.xb+t*m->dim,b.x+t*m->dim,l->attn_norm,m->dim,m->rms_eps);
        quantize4(m,s,&b,b.xb,m->dim);matmul4(b.q,&l->q,&b);matmul4(b.k,&l->k,&b);matmul4(b.v,&l->v,&b);
        for(uint32_t t=0;t<4u;++t) {
            struct tq_state rope=*s;rope.rope_cos=b.cos+t*half;rope.rope_sin=b.sin+t*half;
            for(uint32_t j=0;j<m->dim;++j)b.q[t*m->dim+j]+=l->q_bias[j];
            for(uint32_t j=0;j<m->kv_dim;++j){b.k[t*m->kv_dim+j]+=l->k_bias[j];b.v[t*m->kv_dim+j]+=l->v_bias[j];}
            rotate(b.q+t*m->dim,m->heads,m->head_size,&rope);rotate(b.k+t*m->kv_dim,m->kv_heads,m->head_size,&rope);
            size_t at=((size_t)layer*s->context+base+t)*m->kv_dim;
            memcpy(s->key_cache+at,b.k+t*m->kv_dim,m->kv_dim*sizeof(float));
            memcpy(s->value_cache+at,b.v+t*m->kv_dim,m->kv_dim*sizeof(float));
        }
        for(uint32_t t=0;t<4u;++t)for(uint32_t h=0;h<m->heads;++h) {
            uint32_t kv_head=h/(m->heads/m->kv_heads),pos=base+t;
            const float *query=b.q+t*m->dim+h*m->head_size;float maximum=-INFINITY;
            for(uint32_t k=0;k<=pos;++k) {
                const float *key=s->key_cache+((size_t)layer*s->context+k)*m->kv_dim+kv_head*m->head_size;
                float sum=0;for(uint32_t j=0;j<m->head_size;++j)sum+=query[j]*key[j];
                float score=sum*attn_scale;s->att[k]=score;if(score>maximum)maximum=score;
            }
            float sum=0;for(uint32_t k=0;k<=pos;++k){s->att[k]=expf(s->att[k]-maximum);sum+=s->att[k];}
            float *output=b.xb+t*m->dim+h*m->head_size;
            for(uint32_t j=0;j<m->head_size;++j)output[j]=0;
            for(uint32_t k=0;k<=pos;++k) {
                float probability=s->att[k]/sum;
                const float *value=s->value_cache+((size_t)layer*s->context+k)*m->kv_dim+kv_head*m->head_size;
                for(uint32_t j=0;j<m->head_size;++j)output[j]+=probability*value[j];
            }
        }
        quantize4(m,s,&b,b.xb,m->dim);matmul4(b.xb2,&l->o,&b);
        for(uint32_t j=0;j<4u*m->dim;++j)b.x[j]+=b.xb2[j];
        for(uint32_t t=0;t<4u;++t)rmsnorm(b.xb+t*m->dim,b.x+t*m->dim,l->mlp_norm,m->dim,m->rms_eps);
        quantize4(m,s,&b,b.xb,m->dim);matmul4(b.hb,&l->gate,&b);matmul4(b.hb2,&l->up,&b);
        for(uint32_t j=0;j<4u*m->ffn;++j)b.hb[j]=(b.hb[j]/(1.0f+expf(-b.hb[j])))*b.hb2[j];
        quantize4(m,s,&b,b.hb,m->ffn);matmul4(b.xb,&l->down,&b);
        for(uint32_t j=0;j<4u*m->dim;++j)b.x[j]+=b.xb[j];
        if(s->progress)s->progress(s->progress_opaque,layer,base+3u);
    }
    memcpy(s->x,b.x+3u*m->dim,m->dim*sizeof(float));s->next_position+=4u;s->error=TQ_OK;return TQ_OK;
}
