#include "tiny_features.h"
#include "tiny_tables.h"
#include <limits.h>
static int32_t logq(uint64_t v) {
    if(!v) return 0;
    uint32_t n=0;uint64_t t=v;
    while(t>1u) {t>>=1;++n;}
    uint32_t fraction=n>=8u?(uint32_t)(v>>(n-8u)):(uint32_t)(v<<(8u-n));
    return (int32_t)(n*256u+fraction-256u);
}
uint32_t tiny_features(const int16_t *pcm,int16_t out[TINY_DIM]) {
    int32_t re[512]={0},im[512]={0};uint64_t energy=0,power[257];
    int32_t average=0;
    for(uint32_t i=0;i<TINY_FRAME;++i) average+=pcm[i];
    average/=(int32_t)TINY_FRAME;
    for(uint32_t i=0;i<TINY_FRAME;++i) {
        int32_t x=(int32_t)pcm[i]-average;
        energy+=(uint64_t)((int64_t)x*x);
        re[i]=(x*tiny_window[i])>>15;
    }
    for(uint32_t i=1,j=0;i<512u;++i) {
        uint32_t bit=256u;for(;j&bit;bit>>=1) j^=bit;j^=bit;
        if(i<j) {int32_t t=re[i];re[i]=re[j];re[j]=t;}
    }
    for(uint32_t len=2;len<=512u;len<<=1) for(uint32_t i=0;i<512u;i+=len) {
        for(uint32_t j=0;j<len/2u;++j) {
            uint32_t k=j*(512u/len),a=i+j,b=a+len/2u;
            int32_t tr=(int32_t)(((int64_t)tiny_cos[k]*re[b]+(int64_t)tiny_sin[k]*im[b])>>15);
            int32_t ti=(int32_t)(((int64_t)tiny_cos[k]*im[b]-(int64_t)tiny_sin[k]*re[b])>>15);
            int32_t ar=re[a],ai=im[a];
            re[a]=(ar+tr)/2;im[a]=(ai+ti)/2;re[b]=(ar-tr)/2;im[b]=(ai-ti)/2;
        }
    }
    for(uint32_t i=0;i<=256u;++i) power[i]=(uint64_t)((int64_t)re[i]*re[i]+(int64_t)im[i]*im[i]);
    int32_t bands[20];
    for(uint32_t b=0;b<20u;++b) {
        uint64_t sum=1;uint32_t l=tiny_mel[b],c=tiny_mel[b+1u],r=tiny_mel[b+2u];
        for(uint32_t k=l;k<c;++k) sum+=power[k]*(k-l)/(c-l);
        for(uint32_t k=c;k<r;++k) sum+=power[k]*(r-k)/(r-c);
        bands[b]=logq(sum);
    }
    for(uint32_t d=0;d<TINY_DIM;++d) {
        int64_t sum=0;for(uint32_t b=0;b<20u;++b) sum+=(int64_t)bands[b]*tiny_dct[d][b];
        int32_t value=(int32_t)(sum/(32768*4));if(value>32767)value=32767;if(value<-32767)value=-32767;
        out[d]=(int16_t)value;
    }
    return (uint32_t)(energy/TINY_FRAME);
}
uint32_t tiny_dtw(const int16_t *a,uint32_t n,const int16_t *b,uint32_t m) {
    if(!n || !m || n>300u || m>96u) return UINT_MAX;
    uint32_t prev[97],next[97];
    for(uint32_t j=0;j<=m;++j)prev[j]=0x3fffffffu;
    prev[0]=0;
    for(uint32_t i=0;i<n;++i) {
        next[0]=0x3fffffffu;
        for(uint32_t j=1;j<=m;++j) {
            uint32_t cost=0;for(uint32_t d=0;d<TINY_DIM;++d) {
                int32_t delta=(int32_t)a[i*TINY_DIM+d]-b[(j-1u)*TINY_DIM+d];
                cost+=(uint32_t)(delta<0?-delta:delta);
            }
            uint32_t best=prev[j-1u];if(prev[j]<best)best=prev[j];if(next[j-1u]<best)best=next[j-1u];
            next[j]=best+cost;
        }
        for(uint32_t j=0;j<=m;++j)prev[j]=next[j];
    }
    return prev[m]/(n+m)/TINY_DIM;
}
