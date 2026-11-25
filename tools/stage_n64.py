#!/usr/bin/env python3
"""Stage the pinned N64 sources for the bounded pure-interpreter console.

The existing CPU probe and its original upstream layout are left intact.
Only the new console uses compact TLB entries and omits unused cached/JIT data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import re

ROOT=Path(__file__).resolve().parents[1]


def block_remove(text, marker, count=None):
    removed=0
    while marker in text:
        start=text.index(marker);brace=text.index('{',start);end=brace+1;depth=1
        while depth:
            depth+=(text[end]=='{')-(text[end]=='}');end+=1
        text=text[:start]+text[end:];removed+=1
    if count is not None and removed!=count:raise ValueError(f'Changed upstream block: {marker} ({removed})')
    return text


def stage(output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    upstream=ROOT/'experiments/n64/mupen/src/device/r4300'
    pinned={'r4300_core.h':'a6d0e80d30da0361fa2c5d0e665a614c6709e8b827942a2e031289a20f625bf1',
            'tlb.h':'a2604e4bb72d425951a714d134b9d8d8085dab59880095a2e7b2f7d1a5fe7b01',
            'pure_interp.c':'6d699da7abaf654e04a2e8ae84a87d62593f54f0bb8ec3081752c0644effbcb1',
            'mips_instructions.def':'c0f7e42835386ecd8bc623fe6115bc1b2d7facd93799c0308a76bef138622a7d'}
    for name,digest in pinned.items():
        if hashlib.sha256((upstream/name).read_bytes()).hexdigest()!=digest:
            raise ValueError('Unreviewed Mupen source change: '+name)
    # Known source revision is also recorded in the source manifest.
    for p in upstream.iterdir():
        if p.is_file():shutil.copy2(p,output/p.name)
    header=(upstream/'r4300_core.h').read_text()
    a=header.index('    struct cached_interp cached_interp;')
    b=header.index('    unsigned int emumode;',a)
    header=header[:a]+header[b:]
    (output/'r4300_core.h').write_text(header)
    tlb=(upstream/'tlb.h').read_text()
    for line in ('    uint32_t LUT_r[0x100000];\n','    uint32_t LUT_w[0x100000];\n'):
        if tlb.count(line)!=1:raise ValueError('Changed upstream TLB layout')
        tlb=tlb.replace(line,'')
    (output/'tlb.h').write_text(tlb)
    instructions=block_remove((upstream/'mips_instructions.def').read_text(),
        '    if (r4300->emumode != EMUMODE_PURE_INTERPRETER)',2)
    instructions=instructions.replace('cp0_regs[CP0_INDEX_REG] & UINT32_C(0x3F)','cp0_regs[CP0_INDEX_REG] & UINT32_C(0x1F)')
    instructions=instructions.replace('static void TLBWrite(struct r4300_core* r4300, unsigned int idx)\n{',
        'static void TLBWrite(struct r4300_core* r4300, unsigned int idx)\n{\n    idx &= 31;')
    a=instructions.index('    case CP0_COMPARE_REG:',instructions.index('DECLARE_INSTRUCTION(MTC0)'))
    b=instructions.index('    case CP0_STATUS_REG:',a)
    instructions=instructions[:a]+'''    case CP0_COMPARE_REG:
        tab5_n64_set_compare(r4300, rrt32);
        break;
'''+instructions[b:]
    (output/'mips_instructions.def').write_text(instructions)
    pure=(upstream/'pure_interp.c').read_text()
    old='\tuint32_t op = *op_address;'
    if pure.count(old)!=1:raise ValueError('Changed upstream interpreter dispatch')
    pure=pure.replace(old,old+'\n\tif (!tab5_n64_trace(r4300)) return;')
    pure=pure.replace('static void InterpretOpcode(struct r4300_core* r4300);',
        'extern int tab5_n64_trace(struct r4300_core* r4300);\nextern void tab5_n64_set_compare(struct r4300_core*,uint32_t);\nextern uint32_t tab5_n64_peek_opcode(struct r4300_core*,uint32_t);\nstatic void InterpretOpcode(struct r4300_core* r4300);')
    pure=pure.replace('*fast_mem_access((r4300), (addr) + 4)','tab5_n64_peek_opcode((r4300), (addr) + 4)')
    pure=pure.replace('   *r4300_pc(r4300) = r4300->cp0.last_addr = r4300->start_address;',
                      '   /* Resume the current PC; initialize only at cartridge boot. */')
    (output/'pure_interp.c').write_text(pure)
    nested=output/'device/r4300';nested.mkdir(parents=True,exist_ok=True)
    for p in output.iterdir():
        if p.is_file():shutil.copy2(p,nested/p.name)
    for name in ('rsp','rdp'):
        src=ROOT/'experiments/n64/plugins'/name
        manifest=json.loads((src/'UPSTREAM.json').read_text())
        for file,digest in manifest['files'].items():
            if hashlib.sha256((src/file).read_bytes()).hexdigest()!=digest:
                raise ValueError('Unreviewed N64 dependency change: '+name+'/'+file)
    rsp=ROOT/'experiments/n64/plugins/rsp'
    shutil.copytree(rsp,output/'rsp',dirs_exist_ok=True)
    su=(rsp/'su.c').read_text()
    old='    for (;;) {\n        inst_word ='
    if su.count(old)!=1:raise ValueError('Changed RSP dispatch')
    su=su.replace(old,'    for (;;) {\n        if (!tab5_n64_rsp_poll()) goto RSP_halted_CPU_exit_point;\n        inst_word =')
    su=su.replace('#include "su.h"','#include "su.h"\nextern int tab5_n64_rsp_poll(void);')
    # CXD4 optimizes vector transfers through unaligned short pointers on
    # desktop hosts. Use byte copies on RV32 and wrap the 4 KiB DMEM boundary.
    token='*(pi16)(DMEM + '
    while token in su:
        a=su.index(token);b=a+len(token);end=b;level=1
        while level:
            level+=(su[end]=='(')-(su[end]==')');end+=1
        offset=su[b:end-1]
        if su[end:].startswith(' = '):
            stop=su.index(';',end)
            value=su[end+3:stop]
            su=su[:a]+'tab5_rsp_write16('+offset+', '+value+')'+su[stop:]
        else:
            su=su[:a]+'tab5_rsp_read16('+offset+')'+su[end:]
    anchor='void LSV('
    pos=su.index(anchor)
    su=su[:pos]+'''static i16 tab5_rsp_read16(u32 offset) {
    unsigned char bytes[2]={DMEM[offset & 4095],DMEM[(offset+1) & 4095]};
    i16 value; memcpy(&value,bytes,2); return value;
}
static void tab5_rsp_write16(u32 offset,i16 value) {
    unsigned char bytes[2]; memcpy(bytes,&value,2);
    DMEM[offset & 4095]=bytes[0];DMEM[(offset+1) & 4095]=bytes[1];
}
'''+su[pos:]
    (output/'rsp/su.c').write_text(su)
    shutil.copytree(ROOT/'experiments/n64/plugins/rdp/core',output/'rdp',dirs_exist_ok=True)
    # ESP's int32_t is signed long, whereas the renderer also uses signed
    # int temporaries for output pointers. Both are 32 bits, but are distinct
    # C pointer types. Normalize the renderer's signed arithmetic to int.
    for p in (output/'rdp').rglob('*.c'):
        p.write_text(re.sub(r'\bint32_t\b','int',p.read_text()))
    p=output/'rdp/n64video.c'
    p.write_text(p.read_text().replace('if (config.parallel)','if (0) /* single renderer worker on Tab5 */').replace('    switch (config.dp.compat) {','    switch (config.dp.compat) {\n        case DP_COMPAT_NUM: /* invalid sentinel: use the low compatibility mode */'))
    # Tab5 has its own bounded VI scanout. The unused desktop VI prescaler
    # otherwise reserves 1.6 MiB even when playing a different console.
    p=output/'rdp/n64video.c'
    text=p.read_text().replace('    vi_init();','    /* Tab5 uses its own VI scanout. */').replace('    vi_close();','    tab5_rdp_memory_release();')
    text=text.replace('#include "n64video.h"','#include "n64video.h"\nextern void tab5_rdp_memory_release(void);')
    p.write_text(text)
    p=output/'rdp/n64video/rdp/rdram.c';text=p.read_text()
    original='static uint8_t rdram_hidden[RDRAM_MAX_SIZE / 2];'
    if text.count(original)!=1:raise ValueError('Changed RDP hidden-bit storage')
    text=text.replace(original,'''static uint8_t *rdram_hidden;
bool tab5_rdp_memory_acquire(void) {
    if(!rdram_hidden)rdram_hidden=malloc(RDRAM_MAX_SIZE/2);
    return rdram_hidden!=NULL;
}
void tab5_rdp_memory_release(void) {free(rdram_hidden);rdram_hidden=NULL;}
''').replace('sizeof(rdram_hidden)', '(RDRAM_MAX_SIZE/2)')
    p.write_text(text)
    p=output/'rdp/parallel.h';p.write_text(p.read_text().replace('PARALLEL_MAX_WORKERS 64u','PARALLEL_MAX_WORKERS 1u'))
    (output/'rdp/version.h').write_text('#define VERSION "Tab5-N64-experimental"\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('output',type=Path);stage(p.parse_args().output)
