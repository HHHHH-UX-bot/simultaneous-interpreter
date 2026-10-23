// Actual int8 CNN inference. Model storage is loaded locally through PL BRAM.
#include <stdint.h>
#ifndef SPEAKER_HLS
#ifdef _WIN32
#define SP_EXPORT __declspec(dllexport)
#else
#define SP_EXPORT
#endif
#else
#define SP_EXPORT
#endif
static int32_t byte_weight(const uint32_t model[8192],unsigned base,unsigned at) {
#pragma HLS INLINE
    return (int8_t)(model[base+(at>>2)]>>((at&3u)*8u));
}
template<unsigned IC,unsigned IH,unsigned IW,unsigned OC,unsigned WB,unsigned BB>
static void conv(const int8_t *input,int8_t *output,const uint32_t model[8192],unsigned shift) {
#pragma HLS INLINE
    const unsigned OH=IH/2,OW=IW/2;
    for(unsigned oc=0;oc<OC;++oc)for(unsigned y=0;y<OH;++y)for(unsigned x=0;x<OW;++x) {
        int32_t acc=(int32_t)model[BB+oc];
        for(unsigned ic=0;ic<IC;++ic)for(unsigned ky=0;ky<3;++ky)for(unsigned kx=0;kx<3;++kx) {
            int sy=(int)(y*2+ky)-1,sx=(int)(x*2+kx)-1;
            unsigned index=((oc*IC+ic)*3+ky)*3+kx;
            if(sy>=0 && sy<(int)IH && sx>=0 && sx<(int)IW)
                acc+=(int32_t)input[(ic*IH+(unsigned)sy)*IW+(unsigned)sx]*byte_weight(model,WB,index);
        }
        int32_t value=acc>0?acc>>shift:0;
        output[(oc*OH+y)*OW+x]=(int8_t)(value>127?127:value);
    }
}
extern "C" SP_EXPORT void speaker_cnn_core(const int16_t features[2048],const uint32_t model[8192],int16_t embedding[64],uint32_t *status) {
#pragma HLS INTERFACE ap_memory port=features
#pragma HLS INTERFACE ap_memory port=model
#pragma HLS INTERFACE ap_memory port=embedding
#pragma HLS INTERFACE ap_vld port=status
#pragma HLS ALLOCATION instances=mul limit=1 operation
    unsigned shifts=model[4];
    if(model[0]!=0x314b5053u || model[1]!=1 || model[2]!=7076u ||
       (shifts&255u)>15u || ((shifts>>8)&255u)>15u || ((shifts>>16)&255u)>15u) {
        for(unsigned d=0;d<64;++d)embedding[d]=0;
        *status=1;return;
    }
    int8_t input[2048],layer1[8192],layer2[4096],layer3[2048],pool[64];
#pragma HLS RESOURCE variable=input core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=layer1 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=layer2 core=RAM_1P_BRAM
#pragma HLS RESOURCE variable=layer3 core=RAM_1P_BRAM
    for(unsigned d=0;d<16;++d) {
        int32_t sum=0;
        for(unsigned t=0;t<128;++t)sum+=features[t*16+d];
        int32_t mean=sum>>7,gain=(int32_t)model[16+d];
        for(unsigned t=0;t<128;++t) {
            int64_t value=((int64_t)((int32_t)features[t*16+d]-mean)*gain)>>16;
            if(value>127)value=127;
            if(value<-127)value=-127;
            input[t*16+d]=(int8_t)value;
        }
    }
    conv<1,128,16,16,256,32>(input,layer1,model,shifts&255u);
    conv<16,64,8,32,292,48>(layer1,layer2,model,(shifts>>8)&255u);
    conv<32,32,4,64,1444,80>(layer2,layer3,model,(shifts>>16)&255u);
    for(unsigned c=0;c<64;++c) {
        int32_t sum=0;
        for(unsigned at=0;at<32;++at)sum+=layer3[c*32+at];
        pool[c]=(int8_t)(sum>>5);
    }
    for(unsigned d=0;d<64;++d) {
        int32_t sum=(int32_t)model[144+d];
        for(unsigned c=0;c<64;++c)sum+=(int32_t)pool[c]*byte_weight(model,6052,d*64+c);
        if(sum>32767)sum=32767;
        if(sum<-32768)sum=-32768;
        embedding[d]=(int16_t)sum;
    }
    *status=0;
}
