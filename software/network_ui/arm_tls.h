#ifndef Z7_ARM_TLS_H
#define Z7_ARM_TLS_H
#include <stdint.h>
/* 必须先由主机写入一次性随机种子和 UTC；种子不进入固件或日志。
   main 每毫秒调用 tick。只有证书、域名、日期均通过后才启动云端。 */
void arm_tls_init(void);
void arm_tls_tick(uint32_t now);
int arm_tls_ready(void);
int arm_tls_active(void);
int arm_tls_send(const uint8_t *data,uint32_t size);
int arm_tls_attach(void (*receive)(uint8_t));
#endif
