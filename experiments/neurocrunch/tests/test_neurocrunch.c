#include "../neurocrunch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int read_file(void *ctx, uint64_t offset, void *dst, size_t n) {
  FILE *f=(FILE*)ctx;
  if(offset>0x7fffffffULL) return -1; /* small test fixture only */
  if(fseek(f,(long)offset,SEEK_SET)) return -1;
  return fread(dst,1,n,f)==n?0:-1;
}
static int check(const char *filename, const uint8_t *expected, size_t length) {
  FILE *f=fopen(filename,"rb"); ncz_reader r; uint8_t *buf=NULL,*scratch=NULL;
  long size; int status=1;
  if(!f) return 1;
  if(fseek(f,0,SEEK_END) || (size=ftell(f))<0 || ncz_open(&r,read_file,f,(uint64_t)size)) goto done;
  if(r.logical_bytes!=length) goto done;
  buf=malloc(length?length:1); scratch=malloc((size_t)r.block_bytes*2U);
  if(!buf||!scratch) goto done;
  if(ncz_read_range(&r,0,buf,length,scratch,(size_t)r.block_bytes*2U)) goto done;
  if(memcmp(buf,expected,length)) goto done;
  for(size_t off=0;off<length;off+=(off%233)+1) {
    size_t n=(off%8192)+1; if(n>length-off) n=length-off;
    memset(buf,0,n);
    if(ncz_read_range(&r,off,buf,n,scratch,(size_t)r.block_bytes*2U)) goto done;
    if(memcmp(buf,expected+off,n)) goto done;
  }
  if(ncz_read_range(&r,length,buf,0,NULL,0)!=NCZ_OK) goto done;
  if(ncz_read_range(&r,length,buf,1,scratch,(size_t)r.block_bytes*2U)!=NCZ_ERANGE) goto done;
  if(ncz_read_range(&r,0,buf,length,scratch,0)!=NCZ_EWORKSPACE && length) goto done;
  status=0;
 done:
  free(scratch);free(buf);fclose(f);return status;
}
int main(int argc,char **argv) {
  const size_t n=262144+729; uint8_t *expected=malloc(n); FILE *f;
  if(argc!=3 || !expected) return 2;
  for(size_t i=0;i<n;i++) expected[i]=(i<131072)?0xA5U:((i<196608)?(uint8_t)i:(uint8_t)((i*31U)^(i>>7)));
  f=fopen(argv[1],"wb");if(!f) return 3;
  if(fwrite(expected,1,n,f)!=n) return 4;
  fclose(f);
  if(check(argv[2],expected,n)) { fputs("FAIL: range reads / exact restore\n",stderr); return 5; }
  fputs("PASS: NCZ1 exact restore, random seeks, EOF, bounds\n",stdout);
  free(expected);return 0;
}
