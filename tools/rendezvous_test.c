#include "wiiu_imports.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); \
    result = 1; goto cleanup; } } while (0)

static u32 worker_returns[3], worker_done[3], wait_calls, sleep_calls;
static u32 audio_calls;
static u32 mutex_calls, mutex_done, mutex_return;
static bool test_worker_call(CPUState* cpu, u32 address) {
    if(address==0x06003000u) {
        mutex_return=cpu->lr;cpu->lr=0x06003004u;
        cpu->gpr[3]=cpu->gpr[4];cpu->pc=0x0434D900u;return true;
    }
    if(address==0x06003004u) {
        ++mutex_done;cpu->lr=mutex_return;cpu->pc=0x0434D910u;return true;
    }
    if(address==0x0434D900u)++mutex_calls;
    if (address == 0x06002000u) {
        ++audio_calls;
        cpu->pc = cpu->lr;
        return true;
    }
    if (address == 0x06001000u) {
        u32 id = cpu->gpr[3];
        if (id >= 3u) return false;
        cpu->gpr[31] = id;
        worker_returns[id] = cpu->lr;
        cpu->lr = 0x06001004u;
        if (id == 2u) {
            cpu->gpr[3] = 1u; cpu->gpr[4] = 5u;
            cpu->pc = 0x0434DD00u;
        } else {
            cpu->gpr[3] = cpu->gpr[4];
            cpu->pc = 0x0434DB00u;
        }
        return true;
    }
    if (address == 0x06001004u) {
        u32 id = cpu->gpr[31];
        ++worker_done[id];
        cpu->pc = worker_returns[id];
        return true;
    }
    if (address == 0x0434DB00u) ++wait_calls;
    if (address == 0x0434DD00u) ++sleep_calls;
    return wiiu_imports_host_call(cpu, address);
}

int main(void) {
    static RPXFile rpx;
    const u32 init = 0x0434C548u, wait = 0x0434CA98u;
    const u32 object = WIIU_GUEST_HEAP_BASE, caller = 0x02001000u;
    CPUState cpu = {0};
    WiiUMemory memory;
    int result = 0;
    rpx.symbol_count = 26;
    const char* audio_names[] = {"AXInit", "AXRegisterAppFrameCallback", "AXQuit"};
    for (u32 i = 0; i < 3; ++i) {
        strcpy(rpx.symbols[23+i].name, audio_names[i]);
        rpx.symbols[23+i].value = 0x0434DE00u + i*8u;
        rpx.symbols[23+i].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    const char* thread_names[] = {"OSSleepTicks", "OSCreateThread", "OSResumeThread"};
    for (u32 i = 0; i < 3; ++i) {
        strcpy(rpx.symbols[20+i].name, thread_names[i]);
        rpx.symbols[20+i].value = 0x0434DD00u + i*8u;
        rpx.symbols[20+i].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    const char* event_names[] = {"OSInitEvent", "OSWaitEvent",
        "OSSignalEvent", "OSResetEvent"};
    for (u32 i = 0; i < 4; ++i) {
        strcpy(rpx.symbols[i + 16].name, event_names[i]);
        rpx.symbols[i + 16].value = 0x0434DC00u + i * 8;
        rpx.symbols[i + 16].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    const char* semaphore_names[] = {"OSWaitSemaphore", "OSTryWaitSemaphore",
        "OSSignalSemaphore", "OSGetSemaphoreCount"};
    for (u32 i = 0; i < 4; ++i) {
        strcpy(rpx.symbols[i + 12].name, semaphore_names[i]);
        rpx.symbols[i + 12].value = 0x0434DB00u + i * 8;
        rpx.symbols[i + 12].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    strcpy(rpx.symbols[11].name, "DCZeroRange");
    rpx.symbols[11].value = 0x0434DA00u;
    rpx.symbols[11].section_index = RPX_SYMBOL_SECTION_ALIAS;
    strcpy(rpx.symbols[0].name, "OSInitRendezvous");
    rpx.symbols[0].value = init;
    rpx.symbols[0].section_index = RPX_SYMBOL_SECTION_ALIAS;
    strcpy(rpx.symbols[1].name, "OSWaitRendezvous");
    rpx.symbols[1].value = wait;
    rpx.symbols[1].section_index = RPX_SYMBOL_SECTION_ALIAS;
    const char* init_names[] = {"OSInitMutex", "OSInitMutexEx", "OSInitCond",
        "OSInitCondEx", "OSInitSemaphore", "OSInitSemaphoreEx"};
    for (u32 i = 0; i < 6; ++i) {
        strcpy(rpx.symbols[i + 2].name, init_names[i]);
        rpx.symbols[i + 2].value = 0x0434D800u + i * 8;
        rpx.symbols[i + 2].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    const char* lock_names[] = {"OSLockMutex", "OSTryLockMutex", "OSUnlockMutex"};
    for (u32 i = 0; i < 3; ++i) {
        strcpy(rpx.symbols[i + 8].name, lock_names[i]);
        rpx.symbols[i + 8].value = 0x0434D900u + i * 8;
        rpx.symbols[i + 8].section_index = RPX_SYMBOL_SECTION_ALIAS;
    }
    wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory, "barrier", object, 0x80000u, NULL, true));
    wiiu_memory_bind_cpu(&memory, &cpu);
    wiiu_imports_init_cpu_context(&cpu);
    CHECK(cpu.gqr[2] == 0x00040004u && cpu.gqr[3] == 0x00050005u);
    CHECK(cpu.gqr[4] == 0x00060006u && cpu.gqr[5] == 0x00070007u);
    cpu.hid2 = PPC_HID2_PSE | PPC_HID2_LSQE | PPC_HID2_LCE;
    cpu.msr |= 0x00002000u;
    cpu.fpr[0] = 42.75;
    mem_write8(&cpu, object + 9, 0xA5);
    mem_write8(&cpu, object + 11, 0xA5);
    CHECK(ppc_psq_store(&cpu, 0, object + 10, true, 2, false, 0x03C0669Cu));
    CHECK(cpu.exception == 0 && mem_read8(&cpu, object + 10) == 42);
    CHECK(mem_read8(&cpu, object + 9) == 0xA5 && mem_read8(&cpu, object + 11) == 0xA5);
    wiiu_imports_attach_rpx(&rpx);
    cpu.pc = init; cpu.lr = caller; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, init));
    CHECK(cpu.pc == caller && mem_read32(&cpu, object + 12) == object);
    cpu.pc = wait; cpu.gpr[3] = object; cpu.gpr[4] = 7;
    CHECK(wiiu_imports_host_call(&cpu, wait));
    CHECK(cpu.pc == wait && cpu.gpr[3] == object && cpu.gpr[4] == 7);
    CHECK(mem_read32(&cpu, object + 4) == 1); /* main runs on core 1 */
    mem_write32(&cpu, object, 1); /* core 0 arrives */
    CHECK(wiiu_imports_host_call(&cpu, wait) && cpu.pc == wait);
    mem_write32(&cpu, object + 8, 1); /* core 2 arrives */
    CHECK(wiiu_imports_host_call(&cpu, wait));
    CHECK(cpu.pc == caller && cpu.gpr[3] == 1);
    cpu.pc = init; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, init));
    CHECK(mem_read32(&cpu, object) == 0 && mem_read32(&cpu, object + 8) == 0);
    cpu.pc = wait; cpu.gpr[3] = object; cpu.gpr[4] = 2;
    CHECK(wiiu_imports_host_call(&cpu, wait));
    CHECK(cpu.pc == caller && cpu.gpr[3] == 1); /* only main required */
    for (u32 i = 0; i < 6; ++i) {
        u32 size = i < 2 ? 0x2Cu : i < 4 ? 0x1Cu : 0x20u;
        u32 tag = i < 2 ? 0x6D557458u : i < 4 ? 0x634E6456u : 0x73506852u;
        for (u32 offset = 0; offset < 80; ++offset)
            mem_write8(&cpu, object + offset, 0xA5);
        cpu.pc = rpx.symbols[i + 2].value;
        cpu.gpr[3] = object;
        cpu.gpr[4] = 7;
        cpu.gpr[5] = 0x12345678u;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
        CHECK(mem_read32(&cpu, object) == tag);
        CHECK(mem_read32(&cpu, object + 4) ==
              ((i & 1) ? (i < 4 ? 7u : 0x12345678u) : 0u));
        CHECK(mem_read32(&cpu, object + (i < 4 ? 20 : 24)) == object);
        if (i >= 4) CHECK(mem_read32(&cpu, object + 12) == 7);
        for (u32 offset = size; offset < 80; ++offset)
            CHECK(mem_read8(&cpu, object + offset) == 0xA5);
    }
    cpu.pc = rpx.symbols[2].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    for (u32 count = 1; count <= 2; ++count) {
        cpu.pc = rpx.symbols[8].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
        CHECK(mem_read32(&cpu, object + 0x1C) == WIIU_GUEST_THREAD);
        CHECK(mem_read32(&cpu, object + 0x20) == count);
    }
    for (u32 count = 2; count != 0; --count) {
        cpu.pc = rpx.symbols[10].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(mem_read32(&cpu, object + 0x20) == count - 1);
    }
    CHECK(mem_read32(&cpu, object + 0x1C) == 0);
    mem_write32(&cpu, object + 0x1C, 0x12340000u);
    mem_write32(&cpu, object + 0x20, 1);
    cpu.pc = rpx.symbols[8].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == rpx.symbols[8].value && cpu.gpr[3] == object);
    cpu.pc = rpx.symbols[9].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == caller && cpu.gpr[3] == 0);
    cpu.pc = rpx.symbols[10].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(mem_read32(&cpu, object + 0x1C) == 0x12340000u);
    mem_write32(&cpu, object + 0x1C, 0);
    mem_write32(&cpu, object + 0x20, 0);
    cpu.pc = rpx.symbols[9].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == caller && cpu.gpr[3] == 1);
    mem_write32(&cpu, object + 12, 0);
    cpu.pc = rpx.symbols[12].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == rpx.symbols[12].value && cpu.gpr[3] == object);
    cpu.pc = rpx.symbols[13].value;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.pc == caller && cpu.gpr[3] == 0);
    for (u32 count = 0; count < 2; ++count) {
        cpu.pc = rpx.symbols[14].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(cpu.gpr[3] == count && mem_read32(&cpu, object + 12) == count + 1);
    }
    cpu.pc = rpx.symbols[15].value; cpu.gpr[3] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(cpu.gpr[3] == 2 && mem_read32(&cpu, object + 12) == 2);
    for (u32 count = 2; count != 0; --count) {
        cpu.pc = rpx.symbols[12].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(cpu.pc == caller && cpu.gpr[3] == count);
        CHECK(mem_read32(&cpu, object + 12) == count - 1);
    }
    for (u32 mode = 0; mode < 2; ++mode) {
        cpu.pc = rpx.symbols[16].value;
        cpu.gpr[3] = object; cpu.gpr[4] = 0; cpu.gpr[5] = mode;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(mem_read32(&cpu, object + 0x18) == object);
        cpu.pc = rpx.symbols[17].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(cpu.pc == rpx.symbols[17].value && cpu.gpr[3] == object);
        cpu.pc = rpx.symbols[18].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(mem_read32(&cpu, object + 12) == 1);
        cpu.pc = rpx.symbols[17].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
        CHECK(mem_read32(&cpu, object + 12) == (mode ? 0u : 1u));
        cpu.pc = rpx.symbols[19].value; cpu.gpr[3] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        CHECK(mem_read32(&cpu, object + 12) == 0);
    }
    /* Zeroing is not a cache-flush no-op: it covers complete cache lines. */
    for (u32 offset = 0; offset < 128; ++offset)
        mem_write8(&cpu, object + offset, 0xA5);
    cpu.pc = rpx.symbols[11].value;
    cpu.gpr[3] = object + 35; cpu.gpr[4] = 32;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
    for (u32 offset = 0; offset < 128; ++offset)
        CHECK(mem_read8(&cpu, object + offset) ==
              (offset >= 32 && offset < 96 ? 0 : 0xA5));
    cpu.pc = rpx.symbols[11].value;
    cpu.gpr[3] = object; cpu.gpr[4] = 0;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(mem_read8(&cpu, object) == 0xA5);
    cpu.pc = rpx.symbols[11].value;
    cpu.gpr[3] = object; cpu.gpr[4] = UINT32_MAX;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    CHECK(mem_read8(&cpu, object) == 0xA5);
    /* BOTW's checked Yaz0 ABI is (destination, capacity, source). A literal
       followed by a distance-one match expands to 32 copies, even when the
       destination overlaps the compressed input. */
    const u8 yaz0[] = {'Y','a','z','0',0,0,0,32,0,0,0,0,0,0,0,0,0x80,'A',0,0,13};
    for (u32 mode = 0; mode < 2; ++mode) {
        for (u32 i = 0; i < sizeof(yaz0); ++i) mem_write8(&cpu, object + i, yaz0[i]);
        u32 destination = object + (mode ? 8u : 256u);
        cpu.pc = mode ? 0x04215480u : 0x042155D4u;
        cpu.gpr[3] = destination; cpu.gpr[4] = mode ? object : 32u; cpu.gpr[5] = object;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller && cpu.gpr[3] == 32u);
        for (u32 i = 0; i < 32u; ++i) CHECK(mem_read8(&cpu, destination+i) == 'A');
    }
    /* A distance-three repeat exercises non-power-of-two overlapping matches
       and a final match clipped to the declared output size. */
    const u8 repeated[]={'Y','a','z','0',0,0,0,32,0,0,0,0,0,0,0,0,0xe0,'A','B','C',0,2,255};
    for(u32 i=0;i<sizeof(repeated);++i)mem_write8(&cpu,object+i,repeated[i]);
    cpu.pc=0x042155D4u;cpu.gpr[3]=object+256u;cpu.gpr[4]=32;cpu.gpr[5]=object;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc) && cpu.gpr[3]==32);
    for(u32 i=0;i<32;++i)CHECK(mem_read8(&cpu,object+256u+i)=="ABC"[i%3]);
    for (u32 i = 0; i < sizeof(yaz0); ++i) mem_write8(&cpu, object + i, yaz0[i]);
    mem_write8(&cpu, object + 256u, 0xA5);
    cpu.pc = 0x042155D4u; cpu.gpr[3] = object+256u; cpu.gpr[4] = 31u; cpu.gpr[5] = object;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.gpr[3] == (u32)-2);
    CHECK(mem_read8(&cpu, object+256u) == 0xA5);
    mem_write8(&cpu, object+19u, 10u); /* invalid backwards reference */
    cpu.pc = 0x042155D4u; cpu.gpr[3] = object+256u; cpu.gpr[4] = 32u;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.gpr[3] == (u32)-2);
    CHECK(mem_read8(&cpu, object+256u) == 0xA5);
    mem_write8(&cpu, object, 0u);
    cpu.pc = 0x042155D4u; cpu.gpr[3] = object+256u;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.gpr[3] == (u32)-1);
    /* Streaming input may stop after a flags byte or inside a match. The
       same context is reinitialized and reused for the next resource. */
    for (u32 pass = 0; pass < 2u; ++pass) {
        for (u32 i = 0; i < sizeof(yaz0); ++i) mem_write8(&cpu, object+i, yaz0[i]);
        u32 stream = object+512u;
        for (u32 i = 0; i < 24u; ++i) mem_write8(&cpu, stream+i, 0);
        mem_write32(&cpu, stream, object+256u);
        mem_write32(&cpu, stream+8u, pass ? 0u : 32u);
        mem_write8(&cpu, stream+22u, 16u);
        for (u32 pos = 0; pos < sizeof(yaz0);) {
            u32 size = pos == 0u ? 17u : 1u;
            cpu.pc = 0x04215778u; cpu.gpr[3] = stream; cpu.gpr[4] = object+pos; cpu.gpr[5] = size;
            CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
            pos += size;
            CHECK(cpu.gpr[3] == (pos == sizeof(yaz0) ? 0u : pos == 17u ? 32u : 31u));
        }
        CHECK(mem_read32(&cpu, stream) == object+288u && mem_read32(&cpu, stream+4u) == 0u);
        for (u32 i = 0; i < 32u; ++i) CHECK(mem_read8(&cpu, object+256u+i) == 'A');
    }
    /* Retained main sleeps use the complete 64-bit tick argument and do not
       restart their deadline each time the scheduler retries the import. */
    cpu.timebase = 20u;
    cpu.pc = rpx.symbols[20].value; cpu.lr = caller;
    cpu.gpr[3] = 1u; cpu.gpr[4] = 5u;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == rpx.symbols[20].value);
    cpu.timebase += 0x100000004ull;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == rpx.symbols[20].value);
    ++cpu.timebase;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);
    cpu.pc = rpx.symbols[20].value; cpu.gpr[3] = 0u; cpu.gpr[4] = 0u;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.pc == caller);

    /* Exercise actual cooperative contexts: two semaphore waiters must stop
       dispatching, then consume exactly one token per signal. A sleeping
       third thread must resume its continuation only when its deadline is due. */
    mem_write32(&cpu, object + 12u, 0u);
    cpu.host_call = test_worker_call;
    for (u32 id = 0; id < 3u; ++id) {
        cpu.pc = rpx.symbols[21].value; cpu.lr = caller;
        cpu.gpr[3] = object + 4096u*(id+1u); cpu.gpr[4] = 0x06001000u;
        cpu.gpr[5] = id; cpu.gpr[6] = object;
        cpu.gpr[7] = object + 32768u + id*4096u; cpu.gpr[8] = 4096u;
        cpu.gpr[9] = 16u; cpu.gpr[10] = 2u;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.gpr[3] == 1u);
        cpu.pc = rpx.symbols[22].value; cpu.gpr[3] = object + 4096u*(id+1u);
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    }
    wiiu_imports_run_resumed_threads(&cpu, 8u);
    CHECK(wait_calls == 2u && sleep_calls == 1u);
    for (u32 i = 0; i < 10u; ++i) wiiu_imports_run_resumed_threads(&cpu, 8u);
    CHECK(wait_calls == 2u && sleep_calls == 1u);
    CHECK(worker_done[0] + worker_done[1] + worker_done[2] == 0u);
    for (u32 tokens = 1; tokens <= 2u; ++tokens) {
        cpu.pc = rpx.symbols[14].value; cpu.gpr[3] = object; cpu.lr = caller;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
        wiiu_imports_run_resumed_threads(&cpu, 8u);
        CHECK(worker_done[0] + worker_done[1] == tokens);
        CHECK(mem_read32(&cpu, object + 12u) == 0u);
    }
    wiiu_imports_advance_timebase(0x100000004ull);
    wiiu_imports_run_resumed_threads(&cpu, 8u);
    CHECK(worker_done[2] == 0u && sleep_calls == 1u);
    wiiu_imports_advance_timebase(1u);
    wiiu_imports_run_resumed_threads(&cpu, 8u);
    CHECK(worker_done[2] == 1u && sleep_calls == 1u);
    /* An ordinary mutex waiter must sleep across scheduler turns, stay asleep
       through recursive unlocks, then acquire and release on the final unlock. */
    cpu.pc=rpx.symbols[2].value;cpu.lr=caller;cpu.gpr[3]=object;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc));
    for(u32 lock=0;lock<2;++lock) {
        cpu.pc=0x0434D900u;cpu.gpr[3]=object;
        CHECK(wiiu_imports_host_call(&cpu,cpu.pc));
    }
    cpu.pc=rpx.symbols[21].value;cpu.gpr[3]=object+0x9000;
    cpu.gpr[4]=0x06003000u;cpu.gpr[5]=0;cpu.gpr[6]=object;
    cpu.gpr[7]=object+0xC000;cpu.gpr[8]=4096;cpu.gpr[9]=16;cpu.gpr[10]=2;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc) && cpu.gpr[3]==1);
    cpu.pc=rpx.symbols[22].value;cpu.gpr[3]=object+0x9000;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc));
    for(u32 i=0;i<100;++i)wiiu_imports_run_resumed_threads(&cpu,32);
    CHECK(mutex_calls==1 && mutex_done==0 && mem_read32(&cpu,object+0x20)==2);
    cpu.pc=0x0434D910u;cpu.gpr[3]=object;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc));
    for(u32 i=0;i<100;++i)wiiu_imports_run_resumed_threads(&cpu,32);
    CHECK(mutex_calls==1 && mutex_done==0 && mem_read32(&cpu,object+0x20)==1);
    cpu.pc=0x0434D910u;cpu.gpr[3]=object;
    CHECK(wiiu_imports_host_call(&cpu,cpu.pc));
    wiiu_imports_run_resumed_threads(&cpu,32);
    CHECK(mutex_calls==2 && mutex_done==1 && mem_read32(&cpu,object+0x20)==0);
    CHECK(mem_read32(&cpu,object+0x1C)==0);
    /* BOTW creates more than 32 threads before its first frame. Completed
       startup threads still occupy records, so do not silently reject the
       later services. These extra contexts remain suspended in this test. */
    for (u32 id = 0; id < 64u; ++id) {
        u32 thread_object = object + 0x10000u + id*0x800u;
        mem_write8(&cpu, thread_object + 0x6A0u, 0xA5u);
        cpu.pc = rpx.symbols[21].value; cpu.lr = caller;
        cpu.gpr[3] = thread_object; cpu.gpr[4] = 0x06001000u;
        cpu.gpr[5] = 0u; cpu.gpr[6] = object;
        cpu.gpr[7] = object + 0x80000u; cpu.gpr[8] = 4096u;
        cpu.gpr[9] = 16u; cpu.gpr[10] = 2u;
        CHECK(wiiu_imports_host_call(&cpu, cpu.pc) && cpu.gpr[3] == 1u);
        CHECK(mem_read8(&cpu, thread_object + 0x6A0u) == 0xA5u);
    }
    CHECK(wiiu_memory_add_segment(&memory, "audio", WIIU_GUEST_OS_BASE,
                                 WIIU_GUEST_OS_SIZE, NULL, true));
    cpu.pc = rpx.symbols[23].value; cpu.lr = caller;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    cpu.pc = rpx.symbols[24].value; cpu.gpr[3] = 0x06002000u;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
    cpu.timebase = 100u;
    /* Even callbacks split over multiple scheduler turns must finish;
       repeated turns at the same time must not start extra audio frames. */
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 1u);
    cpu.timebase += 186468u;
    wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 1u);
    ++cpu.timebase;
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 2u);
    cpu.timebase += 186469000u;
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 3u); /* no unbounded late-frame catch-up burst */
    u64 next_audio_tick = cpu.timebase + 186469u;
    cpu.timebase = next_audio_tick + 10000u; /* A slightly late scheduler turn. */
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 4u);
    cpu.timebase = next_audio_tick + 186469u; /* Original cadence, not late+3ms. */
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 5u);
    cpu.timebase += 2u * 186469u;
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 7u); /* Catch up a short stall, then stop at deadline. */
    cpu.timebase += 10u * 186469u;
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 17u); /* A 30 ms load stall fits inside the prebuffer. */
    cpu.timebase += 21u * 186469u;
    for (u32 i = 0; i < 100u; ++i) wiiu_imports_run_audio(&cpu, 1u);
    CHECK(audio_calls == 18u); /* A longer stall rebases instead of flooding. */
    cpu.pc = rpx.symbols[25].value; cpu.lr = caller;
    CHECK(wiiu_imports_host_call(&cpu, cpu.pc));
cleanup:
    wiiu_imports_attach_rpx(NULL);
    wiiu_memory_free(&memory);
    return result;
}
