#ifndef TINY_BOARD_LLM_H
#define TINY_BOARD_LLM_H
#include <stddef.h>
#include <stdint.h>

#define TBL_PORT 4190u
#define TBL_MAGIC 0x314c4254u /* TBL1 */
#define TBL_HEADER_BYTES 32u
#define TBL_STATUS_BYTES 64u
#define TBL_CHUNK_BYTES 1024u
#define TBL_INPUT_BYTES 2048u
#define TBL_SYSTEM_BYTES 512u
#define TBL_CONTEXT 512u
#define TBL_MAX_GENERATION 256u
#define TBL_OUTPUT_BYTES 65536u

enum {TBL_STATUS=1,TBL_BEGIN=2,TBL_DATA=3,TBL_RUN=4,TBL_GET=5,
      TBL_CANCEL=6,TBL_RELEASE=7,TBL_VOICE=8};
enum {TBL_WAIT_ASSETS=0,TBL_READY=1,TBL_RECEIVING=2,TBL_QUEUED=3,
      TBL_PREFILL=4,TBL_GENERATING=5,TBL_DONE=6,TBL_FAILED=7,
      TBL_CANCELLED=8};
enum {TBL_OK=0,TBL_BAD_FORMAT=1,TBL_BAD_CRC=2,TBL_ASSETS_NOT_READY=3,
      TBL_BUSY=4,TBL_WRONG_JOB=5,TBL_WRONG_OWNER=6,TBL_BAD_OFFSET=7,
      TBL_BAD_STATE=8,TBL_INCOMPLETE=9,TBL_BAD_UTF8=10,
      TBL_CONTEXT_FULL=11,TBL_INIT_ERROR=12,TBL_ENGINE_ERROR=13,
      TBL_TOKENIZER_ERROR=14,TBL_OUTPUT_FULL=15,TBL_NONFINITE=16};
enum {TBL_FINISH_NONE=0,TBL_FINISH_EOS=1,TBL_FINISH_LIMIT=2,
      TBL_FINISH_CONTEXT=3,TBL_FINISH_CANCEL=4,TBL_FINISH_ERROR=5};

/* Startup once. It does not dereference fixed asset addresses until both
 * validated upload profiles are marked ready. No automatic inference prompt. */
void tbl_init(void);
void tbl_set_assets_ready(int model_ready,int tokenizer_ready);
/* 0 waiting for assets, 1 initialized and usable, 2 initialization failed. */
int tbl_engine_state(void);
/* TOP-LEVEL MAIN LOOP ONLY: synchronous inference; never invoke from a network,
 * render-service, interrupt, or pump callback. Reentry is ignored defensively. */
void tbl_tick(void);
/* Speech controller queues work here; inference remains in the top-level loop. */
int tbl_submit_local(const char *text,const char *system,uint32_t max_tokens);
void tbl_cancel_local(void);
/* Network callback: bounded copies/state changes only, never runs inference.
 * owner = stable source IPv4 + UDP port tag. Reply buffer should be >=1120B.
 * Returns 0 for unrecognized/too-short datagrams or insufficient reply space. */
size_t tbl_handle_packet(uint64_t owner,const uint8_t *packet,size_t bytes,
                         uint8_t *reply,size_t capacity);

/* Firmware integration callbacks. pump runs after each transformer layer and
 * must keep Ethernet, audio and UI controls responsive, without calling tick.
 * UI text is NUL-terminated UTF8; source remains the submitted input. */
void tbl_pump(void);
void tbl_ui_update(const char *source,const char *output,const char *status);
int tbl_ui_busy(void); /* TTS still borrows the current generated text. */
int tbl_ui_error(void);
size_t tbl_voice_info(uint8_t *output,size_t capacity);
/* Supplied by remote_display.c: lock/unlock BOTH validated asset transports. */
void remote_llm_lock(int locked);
#endif
