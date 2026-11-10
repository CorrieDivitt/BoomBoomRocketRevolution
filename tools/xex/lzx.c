#include <stdio.h>
#include <stdlib.h>
#include "mspack.h"
#include "lzx.h"
extern struct mspack_system *mspack_default_system;
int main(int c,char**v){int wb=atoi(v[3]);long outlen=atol(v[4]);
 struct mspack_system*s=mspack_default_system;
 struct mspack_file*in=s->open(s,v[1],MSPACK_SYS_OPEN_READ);
 struct mspack_file*out=s->open(s,v[2],MSPACK_SYS_OPEN_WRITE);
 struct lzxd_stream*l=lzxd_init(s,in,out,wb,0,0x8000,outlen,0);
 int r=lzxd_decompress(l,outlen); printf("lzx result %d\n",r); lzxd_free(l); s->close(in); s->close(out); return r;}
