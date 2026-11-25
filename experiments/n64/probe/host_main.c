// SPDX-License-Identifier: 0BSD
#include "probe.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
int main(int argc,char **argv){
 if(argc!=2)return 2;FILE *file=fopen(argv[1],"rb");if(!file)return 2;
 unsigned char code[512];size_t bytes=fread(code,1,sizeof(code),file);fclose(file);
 struct n64_probe_result r;int passed=n64_probe_run(code,bytes,&r);
 struct n64_probe_result negative;
 const unsigned char idle[]={0x10,0x00,0xff,0xff,0,0,0,0};
 const unsigned char loop[]={0x24,0x21,0,1,0x10,0,0xff,0xfe,0,0,0,0};
 const unsigned char outside[]={0x3c,0x08,0x80,0x01,0x8d,0x09,0,0,0,0,0,0x0d};
 const unsigned char tlb[]={0x42,0,0,0x02,0,0,0,0x0d};
 assert(!n64_probe_run(code,3,&negative) && !strcmp(negative.unsupported,"invalid test program"));
 assert(!n64_probe_run(loop,sizeof(loop),&negative) && !strcmp(negative.unsupported,"instruction budget"));
 assert(!n64_probe_run(idle,sizeof(idle),&negative) && !strcmp(negative.unsupported,"interrupt scheduler"));
 assert(!n64_probe_run(outside,sizeof(outside),&negative) && !strcmp(negative.unsupported,"outside KSEG scratch RAM"));
 assert(!n64_probe_run(tlb,sizeof(tlb),&negative) && negative.unsupported && strstr(negative.unsupported,"TLB"));
 printf("{\"passed\":%s,\"instructions\":%zu,\"core_bytes\":%zu,\"cause\":%u,\"epc\":%u,\"unsupported\":\"%s\"}\n",passed?"true":"false",r.instructions,r.core_bytes,r.cause,r.epc,r.unsupported?r.unsupported:"");
 if(!passed)for(int n=0;n<16;++n)printf("r%d=%016llx\n",n,(unsigned long long)r.regs[n]);
 return passed?0:1;
}
