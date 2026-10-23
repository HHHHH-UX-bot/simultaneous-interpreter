#ifndef TINY_MODEL_H
#define TINY_MODEL_H
#include <stdint.h>
struct tiny_template {uint32_t language,phrase,frames;const int16_t *data;};
struct tiny_voice {uint32_t samples;const int8_t *data;};
extern const struct tiny_template tiny_templates[];
extern const uint32_t tiny_template_count;
extern const uint32_t tiny_text[2][12][64];
extern const struct tiny_voice tiny_voices[2][12];
#endif
