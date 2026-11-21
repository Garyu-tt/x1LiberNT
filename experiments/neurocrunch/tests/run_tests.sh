#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT INT TERM
cc -std=c11 -Wall -Wextra -Werror -O2 -fsanitize=address,undefined -fno-omit-frame-pointer neurocrunch.c tests/test_neurocrunch.c -o "$tmp/test_ncz"
python3 - "$tmp/source" <<'PY'
import sys
n=262144+729
with open(sys.argv[1],'wb') as f:
    f.write(bytes(0xA5 if i<131072 else (i&255 if i<196608 else ((i*31)^(i>>7))&255) for i in range(n)))
PY
python3 pack.py "$tmp/source" "$tmp/source.ncz"
"$tmp/test_ncz" "$tmp/source" "$tmp/source.ncz"
python3 - "$tmp/source.ncz" "$tmp/bad.ncz" <<'PY'
import sys
b=bytearray(open(sys.argv[1],'rb').read())
b[50]^=0x40
open(sys.argv[2],'wb').write(b)
PY
cat > "$tmp/corrupt.c" <<'C'
#include "neurocrunch.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
static int rd(void *ctx,uint64_t off,void *dst,size_t len){FILE *f=ctx;return fseek(f,(long)off,SEEK_SET)||fread(dst,1,len,f)!=len;}
int main(int argc,char **argv){FILE *f;long sz;ncz_reader r;uint8_t *tmp,*out;int status;(void)argc;
 f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);sz=ftell(f);
 if(ncz_open(&r,rd,f,(uint64_t)sz))return 3;
 tmp=malloc(2*r.block_bytes);out=malloc(r.block_bytes);
 if(!tmp||!out)return 4;
 status=ncz_read_range(&r,0,out,r.block_bytes,tmp,2*r.block_bytes);free(tmp);free(out);fclose(f);
 return (status==NCZ_ECORRUPT || status==NCZ_EFORMAT)?0:1;}
C
cc -std=c11 -Wall -Wextra -Werror -O2 -I. neurocrunch.c "$tmp/corrupt.c" -o "$tmp/corrupt"
"$tmp/corrupt" "$tmp/bad.ncz"
echo 'PASS: altered payload rejected'
