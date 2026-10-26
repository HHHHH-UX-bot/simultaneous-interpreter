#ifndef TINY_FEATURES_H
#define TINY_FEATURES_H
#include <stdint.h>
#define TINY_DIM 12
#define TINY_FRAME 400
/* 16 kHz mono, 25 ms window, 20 log-mel bands, 12 cepstral coefficients.
   Integer only: identical frontend for host training and bare-metal inference. */
uint32_t tiny_features(const int16_t *pcm,int16_t out[TINY_DIM]);
uint32_t tiny_dtw(const int16_t *a,uint32_t n,const int16_t *b,uint32_t m);
#endif
