#ifndef NETWORK_UI_SD_MODEL_H
#define NETWORK_UI_SD_MODEL_H
#include <stdint.h>
/* SD boot only: load qwen.tok and qwen.tqw from the TF card once startup has
   settled. busy shortens the hashing slice while a session is running. */
void sd_model_tick(uint32_t now_ms,int busy);
/* enum ui_model_load value; percent of verified bytes while loading. */
uint32_t sd_model_status(uint32_t *percent);
#endif
