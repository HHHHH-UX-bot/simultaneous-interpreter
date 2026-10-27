#ifndef NETWORK_UI_GLOSSARY_H
#define NETWORK_UI_GLOSSARY_H
#include <stdint.h>
/* glossary.txt on the TF card: one "中文 = English" pair per line, '#' starts a
   comment, UTF-8 (a BOM is ignored). The live translation session sends the
   pairs as hot words, keyed by the language being spoken. Pairs are accepted
   in file order while their JSON fits GLOSSARY_JSON_BUDGET, which keeps the
   whole session.update inside one 2 KiB WebSocket frame. */
#define GLOSSARY_FILE_BYTES 4096u
#define GLOSSARY_JSON_BUDGET 1200u
#define GLOSSARY_PAIRS 128u
/* Returns the pairs accepted; lines that are malformed or do not fit are skipped. */
uint32_t glossary_load(const uint8_t *text,uint32_t bytes);
uint32_t glossary_terms(void);
uint32_t glossary_skipped(void);
/* Copies base (a session.update ending in "}}}}": skip options, translation,
   session, event) and inserts ,"corpus":{"phrases":{...}} into its translation
   object. Returns the length written, or 0 if base has another shape or the
   result does not fit capacity. With no pairs the copy is unchanged. */
uint32_t glossary_session(const char *base,uint32_t base_bytes,int english_source,char *out,uint32_t capacity);
#endif
