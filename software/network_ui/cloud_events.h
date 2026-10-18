#ifndef NETWORK_UI_CLOUD_EVENTS_H
#define NETWORK_UI_CLOUD_EVENTS_H
#include "../subtitle_ui/ui_model.h"
#define CLOUD_JSON_BYTES 4096u
#define CLOUD_ID_BYTES 80u
enum cloud_kind {CLOUD_IGNORE,CLOUD_ASR_FINAL,CLOUD_TRANSLATION,CLOUD_RESPONSE_DONE,CLOUD_FINISHED,CLOUD_ERROR,CLOUD_ITEM_LINK,
                 CLOUD_SESSION_CREATED,CLOUD_SESSION_UPDATED,CLOUD_TRANSLATION_DELTA,CLOUD_SPEECH_STARTED};
enum cloud_parse_code {CLOUD_OK,CLOUD_JSON,CLOUD_SCHEMA,CLOUD_LIMIT};
struct cloud_event {
    enum cloud_kind kind;
    uint32_t completed;
    uint32_t audio_start_ms;   /* speech_started: onset position in the uploaded audio stream */
    char item_id[CLOUD_ID_BYTES],response_id[CLOUD_ID_BYTES],error_code[CLOUD_ID_BYTES];
    char error_message[CLOUD_ID_BYTES];
    char previous_id[CLOUD_ID_BYTES];
    uint32_t text[UI_MAX_TEXT];
};
/* One complete JSON text message after WebSocket reassembly. Output is valid
   only on CLOUD_OK. Unknown events are ignored; text whitespace becomes spaces.
   No UI mutation, heap allocation, socket, or session/segment mapping here. */
enum cloud_parse_code cloud_event_parse(const char *json,uint32_t bytes,struct cloud_event *event);
#endif
