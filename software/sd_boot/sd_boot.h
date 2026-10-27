#ifndef Z7_SD_BOOT_H
#define Z7_SD_BOOT_H
#include <stdint.h>

/* SD-card boot handshake shared by the FSBL hook, the relocation stub, the
   interpreter firmware and scripts/build_sd_boot.py. OCM 0x2F000..0x2FFFF is
   used by neither the CPU0 nor the CPU1 image. */
#define SD_BOOT_INFO_ADDRESS    0x0002F000u
#define SD_BOOT_INFO_MAGIC      0x4253375Au   /* "Z7SB" */
#define SD_BOOT_CONFIG_ADDRESS  0x0002F100u
#define SD_BOOT_CONFIG_BYTES    0x00000E00u
#define SD_BOOT_CONFIG_MAGIC    0x4643375Au   /* "Z7CF" */
#define SD_BOOT_STUB_ADDRESS    0x1F800000u
#define SD_BOOT_PAYLOAD_ADDRESS 0x1F900000u
#define SD_BOOT_PAYLOAD_LIMIT   0x00600000u
#define SD_BOOT_PAYLOAD_MAGIC   0x4C50375Au   /* "Z7PL" */

/* Written by the stub after placing both images; payload_crc holds 0xbad0000N
   instead when the payload was rejected (the stub then stops). stub_start and
   stub_end are the global timer's low word, which the FSBL runtime started at
   zero: start is the FSBL's own time, end-start the stub's (333333 per ms). */
struct sd_boot_info {uint32_t magic,version,cpu1_entry,payload_crc,stub_start,stub_end;};

/* z7cfg.bin, copied verbatim by the FSBL hook. crc32 covers net..seed. */
struct sd_boot_config {
    uint32_t magic,version,bytes,crc32;
    uint32_t net[32];       /* struct network_configuration, request word unused */
    uint32_t cloud[85];     /* struct cloud_client_config */
    uint32_t epoch;         /* Unix time when the file was made */
    uint8_t seed[64];       /* mixed with per-boot microphone noise */
};

/* payload.bin: header, then count records, then segment bytes. crc32 covers
   everything after the header (data_bytes). Offsets are from the header. */
struct sd_boot_payload_header {uint32_t magic,version,count,cpu0_entry,cpu1_entry,data_bytes,crc32,reserved;};
struct sd_boot_record {uint32_t dest,file_bytes,mem_bytes,offset;};

#endif
