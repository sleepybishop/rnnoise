#ifndef MMAPRING_H_
#define MMAPRING_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
  uint8_t *base;
  off_t size;
  off_t used;
  off_t read_offset;
  off_t write_offset;
} mmapring_t;

mmapring_t *mmapring_new(off_t size);
void mmapring_free(mmapring_t *rng);
void mmapring_reset(mmapring_t *rng);
off_t mmapring_offer(mmapring_t *rng, const uint8_t *p, off_t len);
const uint8_t *mmapring_poll(mmapring_t *rng, off_t len);
const uint8_t *mmapring_peek(mmapring_t *rng, off_t len);
off_t mmapring_used(mmapring_t *rng);
off_t mmapring_unused(mmapring_t *rng);
off_t mmapring_size(mmapring_t *rng);

#ifdef __cplusplus
}
#endif

#endif // MMAPRING_H_
