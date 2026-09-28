#include "wiiu_imports.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"Audio mix check failed at %d: %s\n",__LINE__,#x); result=1; goto cleanup; } } while (0)
static s32 captured[96][2];
static u32 captures;
static bool callback(CPUState* cpu, u32 address) {
    if (address != 0x06000000u) return wiiu_imports_host_call(cpu,address);
    u32 pointers = mem_read32(cpu,cpu->gpr[3]);
    for (u32 c=0;c<2;++c) {
        u32 samples=mem_read32(cpu,pointers+c*4u);
        for (u32 i=0;i<96;++i) captured[i][c]=(s32)mem_read32(cpu,samples+i*4u)/256;
    }
    ++captures;
    cpu->pc=cpu->lr;
    return true;
}

enum { INIT, ACQUIRE, OFFSETS, MIX, STATE, FINAL_CALLBACK, QUIT, SAMPLES_ADDR, IMPORT_COUNT };
static bool call(CPUState* cpu,u32 id) {
    cpu->pc=0x0434E000u+id*8u; cpu->lr=0x02001000u;
    return wiiu_imports_host_call(cpu,cpu->pc) && cpu->pc==0x02001000u;
}

int main(void) {
    static RPXFile rpx;
    const char* names[]={"AXInit","AXAcquireVoice","AXSetVoiceOffsets",
        "AXSetVoiceDeviceMix","AXSetVoiceState","AXRegisterDeviceFinalMixCallback","AXQuit","AXSetVoiceSamplesAddr"};
    rpx.symbol_count=IMPORT_COUNT;
    for(u32 i=0;i<IMPORT_COUNT;++i) {
        strcpy(rpx.symbols[i].name,names[i]);
        rpx.symbols[i].value=0x0434E000u+i*8u;
        rpx.symbols[i].section_index=RPX_SYMBOL_SECTION_ALIAS;
    }
    WiiUMemory memory; CPUState cpu={0}; int result=0;
    const u32 base=WIIU_GUEST_HEAP_BASE, mix=base+256, samples=base+1024;
    wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory,"samples",base,8192,NULL,true));
    CHECK(wiiu_memory_add_segment(&memory,"os",WIIU_GUEST_OS_BASE,WIIU_GUEST_OS_SIZE,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);
    cpu.host_call=callback;
    wiiu_imports_reset_stats(); wiiu_imports_attach_rpx(&rpx);
    CHECK(call(&cpu,INIT));
    cpu.gpr[3]=31;cpu.gpr[4]=cpu.gpr[5]=0; CHECK(call(&cpu,ACQUIRE));
    u32 voice=cpu.gpr[3]; CHECK(voice!=0);
    for(u32 i=0;i<128;++i) mem_write16(&cpu,samples+i*2u,10000);
    mem_write16(&cpu,base,10); mem_write16(&cpu,base+2,1);
    mem_write32(&cpu,base+4,0);mem_write32(&cpu,base+8,127);
    mem_write32(&cpu,base+12,0);mem_write32(&cpu,base+16,samples);
    cpu.gpr[3]=voice;cpu.gpr[4]=base;CHECK(call(&cpu,OFFSETS));
    cpu.gpr[3]=0;cpu.gpr[4]=0x06000000u;CHECK(call(&cpu,FINAL_CALLBACK));
    cpu.gpr[3]=voice;cpu.gpr[4]=1;CHECK(call(&cpu,STATE));
    for(u32 mode=0;mode<8;++mode) {
        for(u32 i=0;i<96;++i) mem_write8(&cpu,mix+i,0);
        /* Aux sends must not be confused with the next output channel. */
        mem_write16(&cpu,mix+4,0x7000);mem_write16(&cpu,mix+8,0x6000);
        s32 expected_left=0,expected_right=0;
        if(mode==0) { mem_write16(&cpu,mix,0x8000);mem_write16(&cpu,mix+16,0x4000);expected_left=10000;expected_right=5000; }
        if(mode==1) { mem_write16(&cpu,mix+16,0x8000);expected_right=10000; }
        if(mode==2) { mem_write16(&cpu,mix,0x8000);expected_left=10000; }
        if(mode==3) { mem_write16(&cpu,mix+64,0x8000);expected_left=expected_right=7070; }
        if(mode==4) { mem_write16(&cpu,mix+32,0x8000);expected_left=7070; }
        if(mode==5) { mem_write16(&cpu,mix+48,0x8000);expected_right=7070; }
        if(mode==6) { mem_write16(&cpu,mix+2,256);mem_write16(&cpu,mix+16,0x8000);mem_write16(&cpu,mix+18,(u16)-512);expected_right=10000; }
        if(mode==7) { mem_write16(&cpu,mix,0x8000);mem_write16(&cpu,mix+16,0x8000);expected_left=expected_right=10000; }
        cpu.gpr[3]=voice;cpu.gpr[4]=0;cpu.gpr[5]=0;cpu.gpr[6]=mix;
        CHECK(call(&cpu,MIX) && cpu.gpr[3]==0);
        u32 previous=captures;
        cpu.timebase += 186469u;
        for(u32 i=0;i<16 && captures==previous;++i) wiiu_imports_run_audio(&cpu,4);
        CHECK(captures==previous+1);
        CHECK(captured[0][0]==expected_left && captured[0][1]==expected_right);
        if(mode==6) CHECK(captured[95][0]==7421 && captured[95][1]==0);
        else CHECK(captured[95][0]==expected_left && captured[95][1]==expected_right);
    }
    cpu.gpr[3]=voice;cpu.gpr[4]=0;cpu.gpr[5]=1;cpu.gpr[6]=mix;
    CHECK(call(&cpu,MIX) && (s32)cpu.gpr[3]==-2);
    cpu.gpr[3]=voice;cpu.gpr[4]=3;cpu.gpr[5]=0;
    CHECK(call(&cpu,MIX) && (s32)cpu.gpr[3]==-1);
    cpu.gpr[3]=voice;cpu.gpr[4]=0;cpu.gpr[6]=base+8192-4;
    CHECK(call(&cpu,MIX) && (s32)cpu.gpr[3]==-3);
    cpu.gpr[3]=0;cpu.gpr[6]=mix;
    CHECK(call(&cpu,MIX) && (s32)cpu.gpr[3]==-4);
    /* Reasserting RUN or the same buffer must not discard the prefetched
       sample at each 3 ms boundary (a periodic discontinuity/click). */
    for(u32 i=0;i<384;++i)mem_write16(&cpu,samples+i*2,(u16)(i*64));
    mem_write16(&cpu,base+2,0);mem_write32(&cpu,base+8,383);mem_write32(&cpu,base+12,0);
    cpu.gpr[3]=voice;cpu.gpr[4]=base;CHECK(call(&cpu,OFFSETS));
    memset(captured,0,sizeof(captured));
    for(u32 i=0;i<96;++i)mem_write8(&cpu,mix+i,0);
    mem_write16(&cpu,mix,0x8000);mem_write16(&cpu,mix+16,0x8000);
    cpu.gpr[3]=voice;cpu.gpr[4]=0;cpu.gpr[5]=0;cpu.gpr[6]=mix;CHECK(call(&cpu,MIX));
    for(u32 block=0;block<3;++block) {
        cpu.gpr[3]=voice;cpu.gpr[4]=1;CHECK(call(&cpu,STATE));
        cpu.gpr[3]=voice;cpu.gpr[4]=samples;CHECK(call(&cpu,SAMPLES_ADDR));
        u32 previous=captures;cpu.timebase+=186469u;
        for(u32 i=0;i<16 && captures==previous;++i)wiiu_imports_run_audio(&cpu,4);
        CHECK(captures==previous+1);
        for(u32 i=0;i<96;++i)CHECK(captured[i][0]==(s32)((block*96+i)*64) && captured[i][1]==captured[i][0]);
    }
    CHECK(!memory.unmapped_read_count && !memory.unmapped_write_count);
    fprintf(stderr,"AX stereo: distinct L/R, centered mono, surround order, channel ramps and invalid matrices passed\n");
cleanup:
    call(&cpu,QUIT);
    wiiu_imports_attach_rpx(NULL);wiiu_memory_free(&memory);
    return result;
}
