#include "wiiu_cpu.h"
#include "wiiu_imports.h"
#include "generated.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"CPU fallback line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static u32 spr_instruction(u32 spr, u32 reg, u32 xo) {
    return (31u<<26)|(reg<<21)|((spr&31)<<16)|((spr>>5)<<11)|(xo<<1);
}
int main(void) {
    CPUState cpu={0};wiiu_imports_init_cpu_context(&cpu);
    const u32 cia=0x03E27924u;
    for(u32 i=0;i<8;++i) {
        cpu.gpr[12]=0x12340000u+i;
        cpu.msr=0x4000u; /* UGQR is accessible in user mode. */
        ppc_fallback_instruction(&cpu,spr_instruction(896+i,12,467),cia);
        CHECK(!cpu.exception && cpu.pc==cia+4 && cpu.gqr[i]==cpu.gpr[12]);
        ppc_fallback_instruction(&cpu,spr_instruction(896+i,11,339),cia);
        CHECK(!cpu.exception && cpu.gpr[11]==cpu.gpr[12]);
    }
    /* Reproduce the exact generated scene instruction that stopped boot. */
    cpu.pc=cia;cpu.gpr[12]=0x00040004u;cpu.downcount=100;
    CHECK(dolrecomp_call(&cpu,cia));
    CHECK(!cpu.exception && cpu.pc==cia+4 && cpu.gqr[2]==0x00040004u);
    WiiUMemory memory;wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory,"quantized",WIIU_GUEST_HEAP_BASE,4096,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);
    cpu.msr=0x2000u;cpu.hid2=PPC_HID2_PSE|PPC_HID2_LSQE;
    mem_write8(&cpu,WIIU_GUEST_HEAP_BASE,17);mem_write8(&cpu,WIIU_GUEST_HEAP_BASE+1,250);
    CHECK(ppc_psq_load(&cpu,3,WIIU_GUEST_HEAP_BASE,false,2,false,cia));
    CHECK(cpu.fpr[3]==17.0 && cpu.ps1[3]==250.0);
    wiiu_memory_free(&memory);
    const u32 invalid[]={0,0x7D82E3A7u,spr_instruction(895,12,467),spr_instruction(904,12,339)};
    for(u32 i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        cpu.exception=0;cpu.program_exception=0;
        ppc_fallback_instruction(&cpu,invalid[i],cia);
        CHECK(cpu.exception && cpu.program_exception==PPC_PROGRAM_ILLEGAL && cpu.srr0==cia);
    }
    puts("Espresso UGQR read/write, generated scene instruction, paired-single unpack and illegal fallback passed");
    return 0;
}
