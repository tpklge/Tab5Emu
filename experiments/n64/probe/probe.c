// SPDX-License-Identifier: GPL-2.0-or-later
// Board harness for Mupen64Plus's actual pure interpreter. KSEG0/1 scratch
// RAM only; unsupported TLB/devices/interrupt scheduling STOP with a reason.
// This is a bounded CPU integration probe, not an N64 console implementation.
#include "probe.h"
#include "device/r4300/r4300_core.h"
#include "device/r4300/pure_interp.h"
#include "api/callbacks.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
static struct r4300_core *active;
static struct n64_probe_result *result;
static uint32_t memory[1024];
static void unsupported(const char *name){result->unsupported=name;active->stop=1;}
void DebugMessage(int level,const char *message,...){(void)message;if(level==M64MSG_ERROR)unsupported("interpreter error");}
int n64_probe_trace(struct r4300_core *core,uint32_t pc,uint32_t op){
 (void)core;(void)pc;(void)op;if(++result->instructions>4096){unsupported("instruction budget");return 0;}return 1;
}
// Register accessors use the upstream non-dynarec layout unchanged.
int64_t *r4300_regs(struct r4300_core *r){return r->regs;}
int64_t *r4300_mult_hi(struct r4300_core *r){return &r->hi;}
int64_t *r4300_mult_lo(struct r4300_core *r){return &r->lo;}
uint32_t *r4300_pc(struct r4300_core *r){return &r->pc->addr;}
struct precomp_instr **r4300_pc_struct(struct r4300_core *r){return &r->pc;}
int *r4300_stop(struct r4300_core *r){return &r->stop;}
uint32_t *r4300_cp0_regs(struct cp0 *c){return c->regs;}
uint64_t *r4300_cp0_latch(struct cp0 *c){return &c->latch;}
int *r4300_cp0_cycle_count(struct cp0 *c){return &c->cycle_count;}
static uint32_t *word(struct r4300_core *r,uint32_t address){
 (void)r;if((address&0xc0000000)!=0x80000000 || (address&0x1fffffff)>=sizeof(memory)){unsupported("outside KSEG scratch RAM");return NULL;}
 return &memory[(address&0x1fffffff)/4];
}
uint32_t *fast_mem_access(struct r4300_core *r,uint32_t address){return word(r,address);}
int r4300_read_aligned_word(struct r4300_core *r,uint32_t address,uint32_t *value){uint32_t *p=word(r,address);if(!p)return 0;*value=*p;return 1;}
int r4300_read_aligned_dword(struct r4300_core *r,uint32_t a,uint64_t *v){uint32_t hi,lo;if(!r4300_read_aligned_word(r,a,&hi)||!r4300_read_aligned_word(r,a+4,&lo))return 0;*v=((uint64_t)hi<<32)|lo;return 1;}
int r4300_write_aligned_word(struct r4300_core *r,uint32_t a,uint32_t v,uint32_t mask){uint32_t *p=word(r,a);if(!p)return 0;*p=(*p&~mask)|(v&mask);return 1;}
int r4300_write_aligned_dword(struct r4300_core *r,uint32_t a,uint64_t v,uint64_t mask){return r4300_write_aligned_word(r,a,v>>32,mask>>32)&&r4300_write_aligned_word(r,a+4,v,mask);}
void generic_jump_to(struct r4300_core *r,unsigned int address){r->interp_PC.addr=address;}
void cp0_update_count(struct r4300_core *r){
 const uint32_t count=((r->pc->addr-r->cp0.last_addr)>>2)*r->cp0.count_per_op;
 r->cp0.regs[CP0_COUNT_REG]+=count;r->cp0.cycle_count+=count;r->cp0.last_addr=r->pc->addr;
}
void exception_general(struct r4300_core *r){
 r->cp0.regs[CP0_EPC_REG]=r->pc->addr-(r->delay_slot?4:0);
 if(r->delay_slot)r->cp0.regs[CP0_CAUSE_REG]|=CP0_CAUSE_BD;
 r->cp0.regs[CP0_STATUS_REG]|=CP0_STATUS_EXL;r->stop=1;
}
int check_cop1_unusable(struct r4300_core *r){if(r->cp0.regs[CP0_STATUS_REG]&CP0_STATUS_CU1)return 0;r->cp0.regs[CP0_CAUSE_REG]=CP0_CAUSE_EXCCODE_CPU|CP0_CAUSE_CE1;exception_general(r);return 1;}
int check_cop2_unusable(struct r4300_core *r){if(r->cp0.regs[CP0_STATUS_REG]&CP0_STATUS_CU2)return 0;r->cp0.regs[CP0_CAUSE_REG]=CP0_CAUSE_EXCCODE_CPU|CP0_CAUSE_CE2;exception_general(r);return 1;}
// Never silently claim success for devices absent from this harness.
void gen_interrupt(struct r4300_core *r){(void)r;unsupported("interrupt scheduler");}
void r4300_check_interrupt(struct r4300_core *r,uint32_t c,int set){(void)r;(void)c;(void)set;unsupported("interrupt controller");}
void tlb_map(struct tlb *t,size_t entry){(void)t;(void)entry;unsupported("TLB map");}
void tlb_unmap(struct tlb *t,size_t entry){(void)t;(void)entry;unsupported("TLB unmap");}
void translate_event_queue(struct cp0 *c,unsigned int base){(void)c;(void)base;unsupported("event queue");}
void remove_event(struct interrupt_queue *q,int type){(void)q;(void)type;unsupported("remove interrupt event");}
void add_interrupt_event_count(struct cp0 *c,int type,unsigned int count){(void)c;(void)type;(void)count;unsupported("add interrupt event");}
int n64_probe_run(const uint8_t *program,size_t size,struct n64_probe_result *out){
 memset(out,0,sizeof(*out));out->core_bytes=sizeof(struct r4300_core);
 if(!program || !size || size>512 || (size&3)){out->unsupported="invalid test program";return 0;}
#ifdef ESP_PLATFORM
 struct r4300_core *core=heap_caps_calloc(1,sizeof(*core),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
 struct r4300_core *core=calloc(1,sizeof(*core));
#endif
 if(!core){out->unsupported="core allocation failed";return 0;}
 active=core;result=out;memset(memory,0,sizeof(memory));
 for(size_t n=0;n<size/4;++n)memory[n]=((uint32_t)program[n*4]<<24)|((uint32_t)program[n*4+1]<<16)|((uint32_t)program[n*4+2]<<8)|program[n*4+3];
 core->emumode=EMUMODE_PURE_INTERPRETER;core->start_address=0x80000000;
 core->cp0.cycle_count=-0x10000000;core->cp0.count_per_op=2;core->cp0.regs[CP0_STATUS_REG]=CP0_STATUS_CU1|CP0_STATUS_FR;
 poweron_cp1(&core->cp1);set_fpr_pointers(&core->cp1,core->cp0.regs[CP0_STATUS_REG]);
 run_pure_interpreter(core);
 out->cause=core->cp0.regs[CP0_CAUSE_REG];out->epc=core->cp0.regs[CP0_EPC_REG];
 for(int n=0;n<32;++n)out->regs[n]=(uint64_t)core->regs[n];
 const uint64_t expected=((UINT64_C(0xffffffff)<<16)-1);
 out->passed=!out->unsupported && out->cause==CP0_CAUSE_EXCCODE_BP && out->epc==0x80000000+size-4 &&
  out->regs[0]==0 && out->regs[1]==0xffffffff && out->regs[2]==(UINT64_C(0xffffffff)<<16) &&
  out->regs[3]==UINT64_MAX && out->regs[4]==expected && out->regs[6]==expected && out->regs[7]==7 &&
  out->regs[9]==0x12cb && out->regs[12]==85 && out->regs[14]==0xabc && memory[0x200/4]==(expected>>32) && memory[0x204/4]==(uint32_t)expected;
 free(core);active=NULL;result=NULL;return out->passed;
}
