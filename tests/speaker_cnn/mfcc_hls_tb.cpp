#include <stdint.h>
#include <stdio.h>
#include "mfcc_vectors.h"
extern "C" void speaker_mfcc_core(const int16_t[400],int16_t[16],uint32_t *);
int main() {
    for(unsigned n=0;n<7;++n) {
        int16_t out[16];uint32_t energy=0;
        speaker_mfcc_core(input_vectors[n],out,&energy);
        if(energy!=expected_energy[n]){printf("ENERGY_MISMATCH %u\n",n);return 1;}
        for(unsigned d=0;d<16;++d)if(out[d]!=expected_mfcc[n][d]) {
            printf("MFCC_MISMATCH %u %u %d %d\n",n,d,out[d],expected_mfcc[n][d]);return 2;
        }
    }
    puts("PASS_HLS_MFCC_SEVEN_INDEPENDENT_VECTORS");return 0;
}
