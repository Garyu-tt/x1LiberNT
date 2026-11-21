#include "neurocrunch.h"
#include <string.h>

static uint16_t u16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t u32(const uint8_t *p) {
  return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p) | (uint64_t)u32(p+4)<<32; }
static uint32_t crc32_bytes(const uint8_t *p, size_t n) {
  uint32_t crc=0xffffffffU;
  for(size_t i=0;i<n;i++) { crc ^= p[i]; for(int k=0;k<8;k++) crc=(crc>>1) ^ (0xedb88320U & (0U-(crc&1U))); }
  return ~crc;
}
int ncz_open(ncz_reader *out, ncz_read_at fn, void *ctx, uint64_t physical_bytes) {
  uint8_t h[NCZ_HEADER_SIZE]; uint32_t bs; uint64_t logical,count,index,expect;
  if(!out || !fn) return NCZ_EINVAL;
  memset(out,0,sizeof(*out));
  if(physical_bytes<NCZ_HEADER_SIZE || fn(ctx,0,h,sizeof(h))) return NCZ_EIO;
  if(memcmp(h,"NCZ1",4) || u16(h+4)!=1 || u16(h+6)!=0 || u32(h+36)!=NCZ_ENTRY_SIZE) return NCZ_EFORMAT;
  bs=u32(h+8); logical=u64(h+12); count=u64(h+20); index=u64(h+28);
  if(bs!=NCZ_BLOCK_64K && bs!=NCZ_BLOCK_256K) return NCZ_EFORMAT;
  expect=logical/bs + ((logical%bs)!=0);
  if(count!=expect || index<NCZ_HEADER_SIZE || index>physical_bytes ||
     count>(physical_bytes-index)/NCZ_ENTRY_SIZE) return NCZ_EFORMAT;
  out->read=fn; out->ctx=ctx; out->physical_bytes=physical_bytes;
  out->logical_bytes=logical; out->block_count=count;
  out->index_offset=index; out->block_bytes=bs;
  return NCZ_OK;
}
static int decode_rle(const uint8_t *packed, size_t pn, uint8_t *out, size_t expected) {
  size_t i=0,o=0;
  while(i<pn) {
    uint8_t tag=packed[i++]; size_t n;
    if(tag&0x80) {
      n=(size_t)(tag&0x7f)+3;
      if(i==pn || n>expected-o) return NCZ_ECORRUPT;
      memset(out+o,packed[i++],n); o+=n;
    } else {
      n=(size_t)tag+1;
      if(n>pn-i || n>expected-o) return NCZ_ECORRUPT;
      memcpy(out+o,packed+i,n); i+=n; o+=n;
    }
  }
  return (o==expected) ? NCZ_OK : NCZ_ECORRUPT;
}
int ncz_read_range(const ncz_reader *r, uint64_t offset, void *dst, size_t n,
                   uint8_t *scratch, size_t scratch_bytes) {
  uint8_t entry[NCZ_ENTRY_SIZE]; uint8_t *output, *target=(uint8_t*)dst;
  if(!r || !r->read || (!dst&&n)) return NCZ_EINVAL;
  if(offset>r->logical_bytes || (uint64_t)n>r->logical_bytes-offset) return NCZ_ERANGE;
  if(!n) return NCZ_OK;
  if(!scratch || scratch_bytes<(size_t)r->block_bytes*2U) return NCZ_EWORKSPACE;
  output=scratch+r->block_bytes;
  while(n) {
    uint64_t bi=offset/r->block_bytes, pos, stored;
    uint32_t cn,un,crc,expected;
    size_t within, take; int status;
    uint8_t codec;
    if(bi>=r->block_count) return NCZ_EFORMAT;
    if(r->read(r->ctx,r->index_offset+bi*NCZ_ENTRY_SIZE,entry,sizeof(entry))) return NCZ_EIO;
    pos=u64(entry); cn=u32(entry+8); un=u32(entry+12); codec=entry[16]; crc=u32(entry+20);
    expected=(bi==r->block_count-1 && r->logical_bytes%r->block_bytes)
      ? (uint32_t)(r->logical_bytes%r->block_bytes) : r->block_bytes;
    if(entry[17]||entry[18]||entry[19] || un!=expected || cn==0 || cn>un ||
       (codec!=0 && codec!=1) || (codec==0 && cn!=un) || (codec==1 && cn>=un)) return NCZ_EFORMAT;
    if(pos<NCZ_HEADER_SIZE || pos>r->index_offset || cn>r->index_offset-pos) return NCZ_EFORMAT;
    if(r->read(r->ctx,pos,scratch,cn)) return NCZ_EIO;
    if(codec==0) memcpy(output,scratch,un);
    else { status=decode_rle(scratch,cn,output,un); if(status) return status; }
    if(crc32_bytes(output,un)!=crc) return NCZ_ECORRUPT;
    within=(size_t)(offset%r->block_bytes);
    if(within>=un) return NCZ_EFORMAT;
    take=(size_t)un-within; if(take>n) take=n;
    memcpy(target,output+within,take);
    target+=take; n-=take; stored=(uint64_t)take; offset+=stored;
  }
  return NCZ_OK;
}
