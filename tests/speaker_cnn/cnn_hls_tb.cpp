#include <stdint.h>
#include <stdio.h>
#include "cnn_vectors.h"
extern "C" void speaker_cnn_core(const int16_t[2048],const uint32_t[8192],int16_t[64],uint32_t *);
int main() {
    int16_t out[64];uint32_t status=99;
    speaker_cnn_core(test_cnn_input,test_model,out,&status);
    if(status)return 1;
    for(unsigned i=0;i<64;++i)if(out[i]!=test_cnn_embedding[i]) {
        printf("CNN_MISMATCH %u %d %d\n",i,out[i],test_cnn_embedding[i]);return 2;
    }
    puts("PASS_HLS_TRAINED_CNN_EXACT_INTEGER_EMBEDDING");return 0;
}
