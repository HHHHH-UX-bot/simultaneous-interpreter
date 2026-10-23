#include <stdint.h>
uint32_t speaker_test_read(uint32_t offset);
void speaker_test_write(uint32_t offset,uint32_t value);
#define SPEAKER_MMIO_READ(offset) speaker_test_read(offset)
#define SPEAKER_MMIO_WRITE(offset,value) speaker_test_write(offset,value)
