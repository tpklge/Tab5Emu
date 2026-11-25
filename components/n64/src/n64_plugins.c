// Portable static bridge. Desktop plugin loading and graphics APIs are absent.
#include "su.h"
#include "module.h"
#include "vu/vu.h"
#include "n64video.h"
#include <string.h>
RSP_INFO RSP_INFO_NAME;
p_func GBI_phase;
void message(const char*text){(void)text;}
void tab5_rsp_init(uint8_t*ram,uint8_t*mem,uint32_t*sp,uint32_t*pc,uint32_t*dp,uint32_t*mi,void(*interrupt)(void),void(*rdp)(void)){
  memset(&RSP_INFO_NAME,0,sizeof(RSP_INFO_NAME));memset(conf,0,32);
  DRAM=ram;DMEM=mem;IMEM=mem+4096;su_max_address=0x7fffff;
  RSP_INFO_NAME.RDRAM=DRAM;RSP_INFO_NAME.DMEM=DMEM;RSP_INFO_NAME.IMEM=IMEM;RSP_INFO_NAME.MI_INTR_REG=mi;
  RSP_INFO_NAME.SP_MEM_ADDR_REG=sp;RSP_INFO_NAME.SP_DRAM_ADDR_REG=sp+1;RSP_INFO_NAME.SP_RD_LEN_REG=sp+2;
  RSP_INFO_NAME.SP_WR_LEN_REG=sp+3;RSP_INFO_NAME.SP_STATUS_REG=sp+4;RSP_INFO_NAME.SP_DMA_FULL_REG=sp+5;
  RSP_INFO_NAME.SP_DMA_BUSY_REG=sp+6;RSP_INFO_NAME.SP_SEMAPHORE_REG=sp+7;RSP_INFO_NAME.SP_PC_REG=pc;
  RSP_INFO_NAME.DPC_START_REG=dp;RSP_INFO_NAME.DPC_END_REG=dp+1;RSP_INFO_NAME.DPC_CURRENT_REG=dp+2;
  RSP_INFO_NAME.DPC_STATUS_REG=dp+3;RSP_INFO_NAME.DPC_CLOCK_REG=dp+4;RSP_INFO_NAME.DPC_BUFBUSY_REG=dp+5;
  RSP_INFO_NAME.DPC_PIPEBUSY_REG=dp+6;RSP_INFO_NAME.DPC_TMEM_REG=dp+7;
  RSP_INFO_NAME.CheckInterrupts=interrupt;RSP_INFO_NAME.ProcessRdpList=rdp;
  for(unsigned i=0;i<8;++i)CR[i]=sp+i;
  for(unsigned i=0;i<8;++i)CR[i+8]=dp+i;
  memset(SR,0,32*sizeof(*SR));memset(VR,0,sizeof(VR));memset(VACC,0,sizeof(VACC));
  set_VCO(0);set_VCC(0);set_VCE(0);MF_SP_STATUS_TIMEOUT=32767;GBI_phase=rdp;
}
int tab5_rsp_run(void){
  if(*CR[4]&3)return 0;
#ifdef WAIT_FOR_CPU_HOST
  memset(MFC0_count,0,32*sizeof(*MFC0_count));
#endif
  run_task();
  if(GET_RCP_REG(MI_INTR_REG)&1)GET_RSP_INFO(CheckInterrupts)();
  if(*CR[4]&SP_STATUS_BROKE)return 0;
  // CXD4 temporarily halts while polling CPU-owned status. Resume only
  // after the CPU has had a scheduling turn, as its desktop bridge does.
  MF_SP_STATUS_TIMEOUT=16;*CR[4]&=~SP_STATUS_HALT;return 1;
}
void tab5_rdp_init(struct n64video_config*config,uint32_t*vi,uint32_t*dp){
  static uint32_t *vi_ptrs[14],*dp_ptrs[8];
  for(unsigned i=0;i<14;++i)vi_ptrs[i]=vi+i;
  for(unsigned i=0;i<8;++i)dp_ptrs[i]=dp+i;
  config->gfx.vi_reg=vi_ptrs;config->gfx.dp_reg=dp_ptrs;
  n64video_init(config);
}
