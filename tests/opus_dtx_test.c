#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "opus.h"
#include "opus_codec.h"
int main(void) {
 if(!opus_codec_init()) return 1;
 int error=0;OpusDecoder *d=opus_decoder_create(16000,1,&error);if(!d || error) return 2;
 int16_t pcm[320],decoded[320];uint8_t packet[128];unsigned short_silent=0,silence_bytes=0,voiced_bytes=0;
 for(unsigned frame=0;frame<500;++frame) {
  int voice=frame<50 || frame>=350;
  for(unsigned i=0;i<320;++i) {unsigned at=frame*320+i;pcm[i]=voice?(int16_t)(((at%80)*300)-12000):0;}
  int n=opus_codec_encode(pcm,packet,sizeof(packet));if(n<=0 || n>60) return 3;
  if(opus_decode(d,packet,n,decoded,320,0)!=320) return 4;
  if(frame>=100 && frame<300) {silence_bytes+=(unsigned)n;if(n<5) ++short_silent;}
  if(frame>=400) voiced_bytes+=(unsigned)n;
 }
 opus_decoder_destroy(d);
 if(short_silent<150 || silence_bytes>=2000 || voiced_bytes<5000) return 5;
 printf("PASS_OPUS_DTX decoded=500x320 voice_bytes_per_frame=%u silent_bytes=%u/200_frames small_silent_frames=%u\n",voiced_bytes/100,silence_bytes,short_silent);return 0;
}
