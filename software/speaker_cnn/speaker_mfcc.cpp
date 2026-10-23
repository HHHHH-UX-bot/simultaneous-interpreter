// Integer feature definition shared by offline training, HLS C simulation and RTL.
#include <stdint.h>
#include "speaker_tables.h"
#ifdef SPEAKER_HLS
#include <ap_int.h>
typedef ap_int<18> fft_t;
typedef ap_int<36> product_t;
typedef ap_uint<40> power_t;
#else
typedef int32_t fft_t;
typedef int64_t product_t;
typedef uint64_t power_t;
#endif
static int32_t logq(uint64_t v) {
    if(!v)return 0;
    uint32_t n=0;uint64_t t=v;
    if(t>>32){t>>=32;n+=32;}if(t>>16){t>>=16;n+=16;}
    if(t>>8){t>>=8;n+=8;}if(t>>4){t>>=4;n+=4;}
    if(t>>2){t>>=2;n+=2;}if(t>>1)++n;
    uint32_t mantissa=n>=8?(uint32_t)(v>>(n-8)):(uint32_t)(v<<(8-n));
    return (int32_t)(n*256+mantissa-256);
}
extern "C" void speaker_mfcc_core(const int16_t pcm[400],int16_t mfcc[16],uint32_t *energy) {
#pragma HLS INTERFACE ap_memory port=pcm
#pragma HLS INTERFACE ap_memory port=mfcc
#pragma HLS INTERFACE ap_vld port=energy
#pragma HLS ALLOCATION instances=mul limit=4 operation
    fft_t re[512],im[512];power_t power[257];int32_t bands[32];
#pragma HLS RESOURCE variable=re core=RAM_2P_BRAM
#pragma HLS RESOURCE variable=im core=RAM_2P_BRAM
#pragma HLS RESOURCE variable=power core=RAM_1P_BRAM
    int32_t average=0;uint64_t square=0;
    for(unsigned i=0;i<400;++i)average+=pcm[i];
    average/=400;
    for(unsigned i=0;i<512;++i) {
        int32_t x=i<400?(int32_t)pcm[i]-average:0;
        if(i<400)square+=(int64_t)x*x;
        // Store directly in bit-reversed order; zero padding is part of the ABI.
        unsigned j=0;
        for(unsigned k=0;k<9;++k)j=(j<<1)|((i>>k)&1u);
        re[j]=i<400?(fft_t)(((int64_t)x*sp_window[i])>>15):(fft_t)0;im[j]=0;
    }
    *energy=(uint32_t)(square/400);
    for(unsigned stage=1;stage<=9;++stage) {
        unsigned len=1u<<stage,half=len>>1;
        for(unsigned i=0;i<512;i+=len)for(unsigned j=0;j<half;++j) {
            unsigned k=j*(512/len),a=i+j,b=a+half;
            product_t tr=((product_t)sp_cos[k]*re[b]+(product_t)sp_sin[k]*im[b])>>15;
            product_t ti=((product_t)sp_cos[k]*im[b]-(product_t)sp_sin[k]*re[b])>>15;
            fft_t ar=re[a],ai=im[a];
            re[a]=(ar+tr)>>1;im[a]=(ai+ti)>>1;re[b]=(ar-tr)>>1;im[b]=(ai-ti)>>1;
        }
    }
    for(unsigned i=0;i<257;++i)
        power[i]=(power_t)((product_t)re[i]*re[i]+(product_t)im[i]*im[i]);
    for(unsigned b=0;b<32;++b) {
        uint64_t sum=1;
        for(unsigned k=sp_mel_offsets[b];k<sp_mel_offsets[b+1];++k)
            sum+=((uint64_t)power[sp_mel_bins[k]]*(uint32_t)sp_mel_weights[k])>>15;
        bands[b]=logq(sum);
    }
    for(unsigned d=0;d<16;++d) {
        int64_t sum=0;
        for(unsigned b=0;b<32;++b)sum+=(int64_t)bands[b]*sp_dct[d*32+b];
        int64_t x=sum/(32768*4);
        if(x>32767)x=32767;
        if(x<-32768)x=-32768;
        mfcc[d]=(int16_t)x;
    }
}
#ifndef SPEAKER_HLS
#ifdef _WIN32
#define SP_EXPORT __declspec(dllexport)
#else
#define SP_EXPORT
#endif
extern "C" SP_EXPORT uint32_t speaker_mfcc_frames(const int16_t *pcm,uint32_t n,int16_t *out,uint32_t *energy) {
    uint32_t count=n<400?0:1+(n-400)/160;
    for(uint32_t i=0;i<count;++i)speaker_mfcc_core(pcm+i*160,out+i*16,energy+i);
    return count;
}
#endif
