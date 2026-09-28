#include "wiiu_guest_string.h"
#include "wiiu_memory.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

extern void func_033D0020(CPUState*);
extern void func_035CC020(CPUState*);
extern void func_030B0020(CPUState*);
extern void func_030F0020(CPUState*);
extern void func_030F4020(CPUState*);
extern void func_03130020(CPUState*);
extern void func_033CC020(CPUState*);
#define CHECK(x) do{if(!(x)){fprintf(stderr,"string test line %d: %s\n",__LINE__,#x);return 1;}}while(0)
enum {BASE=0x11000000u, TABLE=0x10263910u, STOP=0xfffffffcu};
static void put(CPUState* cpu,u32 at,const char* text) {
    do{mem_write8(cpu,at++,(u8)*text);}while(*text++);
}
static bool original(CPUState* cpu) {
    cpu->host_call=NULL;
    for(unsigned calls=0;calls<1000000 && cpu->pc!=STOP;++calls) {
        cpu->downcount=8192;
        switch(cpu->pc&0xffffc000u) {
        case 0x033d0000:func_033D0020(cpu);break;
        case 0x035cc000:func_035CC020(cpu);break;
        case 0x030b0000:func_030B0020(cpu);break;
        default:return false;
        }
        if(cpu->exception)return false;
    }
    return cpu->pc==STOP;
}
static int compare(CPUState* template_cpu,const char* hay,const char* needle,u32 address) {
    CPUState slow=*template_cpu,fast=*template_cpu,inlined=*template_cpu;
    put(&slow,BASE+0x1000,hay);put(&slow,BASE+0x2000,needle);
    slow.pc=fast.pc=inlined.pc=address;
    /* The adjacent callback is also a BLR, but remains dynamically dispatched.
       This provides an unoptimized reference for the guarded no-op calls. */
    mem_write32(&slow,TABLE+28,0x030B0C3C);
    CHECK(original(&slow));
    mem_write32(&slow,TABLE+28,0x030B0C38);
    CHECK(original(&inlined));
    CHECK(inlined.gpr[3]==slow.gpr[3] && inlined.pc==slow.pc && inlined.gpr[1]==slow.gpr[1]);
    for(unsigned i=14;i<32;++i)CHECK(inlined.gpr[i]==slow.gpr[i]);
    CHECK(wiiu_guest_string_contains(&fast,address));
    CHECK(fast.gpr[3]==slow.gpr[3] && fast.pc==slow.pc && fast.gpr[1]==slow.gpr[1]);
    for(unsigned i=14;i<32;++i)CHECK(fast.gpr[i]==slow.gpr[i]);
    CHECK(fast.gpr[3]==(strstr(hay,needle)!=NULL));
    return 0;
}
int main(void) {
    WiiUMemory memory;CPUState cpu={0};wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory,"strings",BASE,0x100000,NULL,true));
    CHECK(wiiu_memory_add_segment(&memory,"string globals",0x10263000,4096,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);
    cpu.lr=STOP;cpu.gpr[1]=BASE+0x800;cpu.gpr[3]=BASE+0x100;cpu.gpr[4]=BASE+0x108;
    for(unsigned i=14;i<32;++i)cpu.gpr[i]=i*0x123456u;
    mem_write32(&cpu,BASE+0x100,BASE+0x1000);mem_write32(&cpu,BASE+0x104,TABLE);
    mem_write32(&cpu,BASE+0x108,BASE+0x2000);mem_write32(&cpu,BASE+0x10c,TABLE);
    mem_write32(&cpu,TABLE+28,0x030B0C38);
    /* Each patched chunk must yield at the no-op continuation when its cycle
       budget expires, and still dispatch any different virtual callback. */
    const u32 sites[]={0x030F003C,0x030F405C,0x03130264,0x033CD0A4,0x033D14F4,0x035CC174};
    void (*chunks[])(CPUState*)={func_030F0020,func_030F4020,func_03130020,
        func_033CC020,func_033D0020,func_035CC020};
    for(unsigned i=0;i<6;++i)for(unsigned alternative=0;alternative<2;++alternative) {
        CPUState run=cpu;run.pc=sites[i];run.ctr=alternative?0x030B0C3C:0x030B0C38;
        run.downcount=1;chunks[i](&run);
        CHECK(run.lr==sites[i]+4 && run.pc==(alternative?run.ctr:run.lr));
        CHECK(!memcmp(run.gpr,cpu.gpr,sizeof(run.gpr)));
        CHECK(run.cr==cpu.cr && run.xer==cpu.xer);
    }
    const char* cases[][2]={{"",""},{"abc",""},{"","abc"},{"abc","abc"},{"abc","abcd"},
        {"abcabc","bca"},{"aaaaab","aaab"},{"aaa","b"},{"Link_Armor_001","_Armor_"},
        {"abc","a"},{"abc","c"},{"\xff\x80\x90","\x80"}};
    clock_t started=clock();unsigned checks=0;
    for(unsigned v=0;v<2;++v) {
        u32 address=v?0x035CF358u:0x033D2B74u;
        for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);++i){CHECK(!compare(&cpu,cases[i][0],cases[i][1],address));++checks;}
        u32 seed=0x12345678;char hay[97],needle[25];
        for(unsigned trial=0;trial<1000;++trial) {
            unsigned hn=trial%96,nn=trial%24;
            for(unsigned i=0;i<hn;++i){seed=seed*1664525u+1013904223u;hay[i]='a'+((seed>>24)%5);}
            hay[hn]=0;
            for(unsigned i=0;i<nn;++i){seed=seed*1664525u+1013904223u;needle[i]='a'+((seed>>24)%5);}
            if(trial%3==0 && hn>=nn)memcpy(needle,hay+hn-nn,nn);
            needle[nn]=0;CHECK(!compare(&cpu,hay,needle,address));++checks;
        }
    }
    /* Fast-path refusal must not mutate the CPU or perform unmapped reads. */
    CPUState before=cpu;
    CHECK(!wiiu_guest_string_contains(&cpu,0x12345678));CHECK(!memcmp(&cpu,&before,sizeof(cpu)));
    mem_write32(&cpu,TABLE+28,0x030B0C3C);
    CHECK(!wiiu_guest_string_contains(&cpu,0x033D2B74));CHECK(!memcmp(&cpu,&before,sizeof(cpu)));
    mem_write32(&cpu,TABLE+28,0x030B0C38);
    mem_write8(&cpu,0x10263A00,1);
    CHECK(!wiiu_guest_string_contains(&cpu,0x033D2B74));CHECK(!memcmp(&cpu,&before,sizeof(cpu)));
    mem_write8(&cpu,0x10263A00,0);
    mem_write32(&cpu,BASE+0x100,BASE+0xfffff);mem_write8(&cpu,BASE+0xfffff,'x');
    CHECK(!wiiu_guest_string_contains(&cpu,0x033D2B74));CHECK(!memcmp(&cpu,&before,sizeof(cpu)));
    mem_write32(&cpu,BASE+0x100,BASE+0x10000);
    for(unsigned i=0;i<=0x80000;++i)mem_write8(&cpu,BASE+0x10000+i,'x');
    CHECK(!wiiu_guest_string_contains(&cpu,0x033D2B74));CHECK(!memcmp(&cpu,&before,sizeof(cpu)));
    CHECK(!memory.unmapped_read_count && !memory.unmapped_write_count);
    /* Isolated callback-dispatch benchmark; not a boot/FPS measurement. */
    char hay[257];memset(hay,'a',255);hay[255]='b';hay[256]=0;
    put(&cpu,BASE+0x1000,hay);put(&cpu,BASE+0x2000,"b");
    mem_write32(&cpu,BASE+0x100,BASE+0x1000);
    for(unsigned mode=0;mode<2;++mode) {
        mem_write32(&cpu,TABLE+28,mode?0x030B0C38:0x030B0C3C);
        clock_t start=clock();
        for(unsigned i=0;i<2000;++i) {
            CPUState run=cpu;run.pc=0x033D2B74;
            CHECK(original(&run) && run.gpr[3]==1);
        }
        printf("String callback %s: %.1f ms\n",mode?"inlined":"dispatched",
            1000.0*(clock()-start)/CLOCKS_PER_SEC);
    }
    printf("%u string-search cases match both original guest routines (%.3f s)\n",checks,(double)(clock()-started)/CLOCKS_PER_SEC);
    wiiu_memory_free(&memory);return 0;
}
