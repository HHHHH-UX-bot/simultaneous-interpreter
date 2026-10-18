/* Bare-metal fixed-point build for the Z7-Nano Cortex-A9 (no float, no threads).
   Generated from libopus v1.4 configure defaults: --enable-fixed-point
   --disable-float-api --disable-asm --disable-rtcd. VAR_ARRAYS selects C99 VLAs
   for temporary allocation (no alloca), matching the freestanding C11 build. */
#ifndef OPUS_BARE_CONFIG_H
#define OPUS_BARE_CONFIG_H
#define OPUS_BUILD 1
#define FIXED_POINT 1
#define DISABLE_FLOAT_API 1
#define VAR_ARRAYS 1
#define PACKAGE_VERSION "1.4"
#endif