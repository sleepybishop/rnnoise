#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "mmapring.h"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

mmapring_t *mmapring_new(off_t size) {
  mmapring_t *rng = calloc(1, sizeof(mmapring_t));
  if (!rng)
    return 0;

  rng->size = sysconf(_SC_PAGE_SIZE);
  while (rng->size < size) {
    rng->size = rng->size << 1;
  }

  char path[] = "/tmp/mmapring-XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0) {
    free(rng);
    return 0;
  }
  // unlink(path);

  if (ftruncate(fd, rng->size) < 0) {
    free(rng);
    return 0;
  }

  rng->base = mmap(0, rng->size << 1, PROT_NONE, MAP_ANON | MAP_PRIVATE, -1, 0);
  if (rng->base == MAP_FAILED) {
    free(rng);
    close(fd);
    return 0;
  }

  uint8_t *base = mmap(rng->base, rng->size, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_SHARED, fd, 0);
  if (base != rng->base) {
    munmap(rng->base, rng->size << 1);

    free(rng);
    close(fd);
    return 0;
  }

  base = mmap(rng->base + rng->size, rng->size, PROT_READ | PROT_WRITE,
              MAP_FIXED | MAP_SHARED, fd, 0);
  if (base != (rng->base + rng->size)) {
    munmap(rng->base, rng->size << 1);

    free(rng);
    close(fd);
    return 0;
  }

  close(fd);

  return rng;
}

void mmapring_free(mmapring_t *rng) {
  if (!rng)
    return;
  munmap(rng->base, rng->size * 2);
  free(rng);
}

void mmapring_reset(mmapring_t *rng) {
  rng->read_offset = 0;
  rng->write_offset = 0;
}

off_t mmapring_offer(mmapring_t *rng, const uint8_t *p, off_t len) {
  off_t wlen = ((rng->size - rng->used) > len ? len : (rng->size - rng->used));

  memcpy(rng->base + rng->write_offset, p, wlen);
  rng->write_offset = (rng->write_offset + wlen) % rng->size;
  rng->used += wlen;
  return wlen;
}

const uint8_t *mmapring_poll(mmapring_t *rng, off_t len) {
  off_t rlen = (rng->used > len ? len : rng->used);
  const uint8_t *ret = rng->base + rng->read_offset;
  rng->read_offset = (rng->read_offset + rlen) % rng->size;
  rng->used -= rlen;
  return ret;
}

const uint8_t *mmapring_peek(mmapring_t *rng, off_t len) {
  return rng->base + rng->read_offset;
}

off_t mmapring_unused(mmapring_t *rng) { return rng->size - rng->used; }

off_t mmapring_used(mmapring_t *rng) { return rng->used; }

off_t mmapring_size(mmapring_t *rng) { return rng->size; }
