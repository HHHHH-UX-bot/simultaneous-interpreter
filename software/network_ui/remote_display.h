#ifndef Z7_REMOTE_DISPLAY_H
#define Z7_REMOTE_DISPLAY_H
#include <stdint.h>
struct remote_pointer {uint32_t sequence,epoch;uint16_t press_x,press_y,x,y;};
void remote_display_init(uint32_t now);
void remote_display_tick(uint32_t now);
void remote_display_present(const uint32_t *pixels,uint32_t generation,uint32_t epoch);
int remote_display_input(struct remote_pointer *event);
void remote_display_ack(uint32_t command,uint32_t epoch);
int remote_display_history_requested(void);
void remote_display_history_publish(const void *entries,uint32_t count,uint32_t stride,uint32_t discarded,uint32_t epoch,uint32_t final_count);
#ifdef Z7_TINYQWEN
int remote_llm_assets_ready(void);
uint32_t remote_llm_asset_flags(void); /* bit 0: model, bit 1: tokenizer */
void remote_llm_lock(int locked);
void remote_llm_restore_tick(void);
#endif
#endif
