/* NeuroCrunch R01: bounded, read-only, seekable lossless container.
 * License: GPL-3.0-or-later (intended for x1LiberNT).
 * Format is experimental. No ABI/format stability guarantee.
 */
#ifndef NEUROCRUNCH_H
#define NEUROCRUNCH_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NCZ_BLOCK_64K 65536U
#define NCZ_BLOCK_256K 262144U
#define NCZ_HEADER_SIZE 40U
#define NCZ_ENTRY_SIZE 24U
/* Callback must read *exactly* n bytes from file offset, returning 0 on success. */
typedef int (*ncz_read_at)(void *ctx, uint64_t offset, void *dst, size_t n);
typedef struct {
  ncz_read_at read;
  void *ctx;
  uint64_t physical_bytes;
  uint64_t logical_bytes;
  uint64_t block_count;
  uint64_t index_offset;
  uint32_t block_bytes;
} ncz_reader;
/* 0 = success. Errors are negative. */
enum { NCZ_OK=0, NCZ_EINVAL=-1, NCZ_EIO=-2, NCZ_EFORMAT=-3,
       NCZ_ECORRUPT=-4, NCZ_ERANGE=-5, NCZ_EWORKSPACE=-6 };
int ncz_open(ncz_reader *out, ncz_read_at fn, void *ctx, uint64_t physical_bytes);
/* Scratch must hold 2 * block_bytes bytes; does not allocate memory. */
int ncz_read_range(const ncz_reader *r, uint64_t offset, void *dst, size_t n,
                   uint8_t *scratch, size_t scratch_bytes);
#ifdef __cplusplus
}
#endif
#endif
