#ifndef TINYQWEN_H
#define TINYQWEN_H
#include <stddef.h>
#include <stdint.h>

#define TQ_MAGIC 0x31575154u
#define TQ_HEADER_BYTES 8192u
#define TQ_MAX_LAYERS 48u
#define TQ_GROUP 64u
enum {TQ_OK=0,TQ_ARGUMENT=-1,TQ_HEADER=-2,TQ_FORMAT=-3,TQ_BOUNDS=-4,
      TQ_DUPLICATE=-5,TQ_MEMORY=-6,TQ_INCOMPLETE=-7,TQ_POSITION=-8};
struct tq_matrix {const uint8_t *q;const float *scale;uint32_t rows,cols,bits,stride;};
struct tq_layer {
    const float *attn_norm,*mlp_norm,*q_bias,*k_bias,*v_bias;
    struct tq_matrix q,k,v,o,gate,up,down;
};
struct tq_model {
    uint32_t dim,ffn,layers,heads,kv_heads,vocab,context,head_size,kv_dim,bits;
    float rms_eps,rope_theta;
    struct tq_matrix embedding;
    const float *final_norm;
    struct tq_layer layer[TQ_MAX_LAYERS];
};
typedef void (*tq_progress_fn)(void *opaque,uint32_t layer,uint32_t position);
struct tq_state {
    float *x,*xb,*xb2,*hb,*hb2,*q,*att,*logits,*key_cache,*value_cache;
    float *aq_scale,*inv_freq,*rope_cos,*rope_sin;
    int8_t *aq;
    uint32_t context,next_position;
    int error;
    tq_progress_fn progress;
    void *progress_opaque;
};
/* Success is zero. Model byte storage is borrowed, read-only and 4-byte aligned.
   Weights stay Q4/Q6, including embeddings; only the requested embedding row is
   dequantized. No file system, allocation, OS or framework is required. */
int tq_init(struct tq_model *model,const void *blob,size_t bytes);
size_t tq_state_required(const struct tq_model *model,uint32_t context);
int tq_state_init(const struct tq_model *model,struct tq_state *state,
                  void *arena,size_t arena_bytes,uint32_t context);
void tq_reset(struct tq_state *state);
void tq_set_progress(struct tq_state *state,tq_progress_fn callback,void *opaque);
/* Position must be consecutive, beginning at zero after init/reset. The result
   contains vocab FP32 logits. NULL means state.error describes a failure. */
float *tq_forward(const struct tq_model *model,struct tq_state *state,
                  uint32_t token,uint32_t position);
/* Process a prompt token without the very large tied lm_head projection. */
int tq_prefill_step(const struct tq_model *model,struct tq_state *state,
                    uint32_t token,uint32_t position);
/* Four prompt positions evaluated layer by layer. Weights are unpacked once
   for four Q8 vectors. No logits are produced; KV remains equivalent to step. */
size_t tq_batch4_required(const struct tq_model *model);
int tq_prefill_batch4(const struct tq_model *model,struct tq_state *state,
                     const uint32_t tokens[4],void *workspace,size_t bytes);
#endif
