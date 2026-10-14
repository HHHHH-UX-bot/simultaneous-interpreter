#ifndef PS_WIFI_AT_PROBE_H
#define PS_WIFI_AT_PROBE_H
#include <stdint.h>
struct wifi_status {
    uint32_t magic,sequence,phase,error,at_ok,echo_off,firmware_ok,firmware_hash;
    uint32_t rx_received,tx_started,rx_overflow,framing_error,ticks,step,reserved0,reserved1;
};
typedef uint32_t (*wifi_read_fn)(uint32_t offset);
typedef void (*wifi_write_fn)(uint32_t offset,uint32_t value);
extern volatile struct wifi_status wifi_info;
void wifi_probe_init(wifi_read_fn read,wifi_write_fn write,uint32_t now);
void wifi_probe_tick(uint32_t now);
#endif
