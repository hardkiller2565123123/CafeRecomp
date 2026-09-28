#include "wiiu_filesystem.h"
#include "wiiu_memory.h"
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #condition); \
    result = 1; goto cleanup; } } while (0)

static s32 read_file(CPUState* cpu, u32 handle, u32 dst, u32 size, u32 count) {
    cpu->gpr[5] = dst;
    cpu->gpr[6] = size;
    cpu->gpr[7] = count;
    cpu->gpr[8] = handle;
    if (!wiiu_filesystem_handle_import(cpu, "FSReadFile"))
        return -1;
    return (s32)cpu->gpr[3];
}

int main(int argc, char** argv) {
    if (argc != 3) return 1;
    int result = 0;
    WiiUMemory memory;
    CPUState cpu = {0};
    FILE* reference = NULL;
    const u32 base = WIIU_GUEST_HEAP_BASE;
    const u32 capacity = 96u * 1024u * 1024u;
    const u32 dst = base + 4096u;
    char path[2048];
    snprintf(path, sizeof(path), "%s/content/Pack/TitleBG.pack", argv[1]);
    wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory, "read_test", base, capacity, NULL, true));
    wiiu_memory_bind_cpu(&memory, &cpu);
    wiiu_filesystem_reset();
    wiiu_filesystem_set_title_paths(argv[1], argv[1], argv[1]);
    u8* data = memory.segments[0].data;
    strcpy((char*)data, "/vol/content/Pack/TitleBG.pack");
    strcpy((char*)data + 256, "r");
    cpu.gpr[5] = base;
    cpu.gpr[6] = base + 256;
    cpu.gpr[7] = base + 512;
    CHECK(wiiu_filesystem_handle_import(&cpu, "FSOpenFile") && cpu.gpr[3] == 0);
    u32 handle = mem_read32(&cpu, base + 512);
    CHECK(read_file(&cpu, handle, dst, 0xFFFFFFFFu, 2) < 0);
    CHECK(read_file(&cpu, handle, base + capacity - 4, 1, 8) < 0);
    memory.segments[0].writable = false;
    CHECK(read_file(&cpu, handle, dst, 1, 16) < 0);
    memory.segments[0].writable = true;
    s32 bytes = read_file(&cpu, handle, dst, 1, capacity - 4096);
    CHECK(bytes > 64 * 1024 * 1024);
    reference = fopen(path, "rb");
    CHECK(reference != NULL);
    for (u32 offset = 0; offset < (u32)bytes;) {
        u8 block[65536];
        size_t count = fread(block, 1, sizeof(block), reference);
        CHECK(count != 0 && count <= (u32)bytes - offset);
        CHECK(memcmp(block, data + 4096 + offset, count) == 0);
        offset += (u32)count;
    }
    CHECK(fgetc(reference) == EOF);
    CHECK(read_file(&cpu, handle, dst, 1, 16) == 0);
    wiiu_filesystem_reset();
    wiiu_filesystem_set_title_paths(argv[2], argv[2], argv[2]);
    for (u32 slot_test = 0; slot_test < 2; ++slot_test) {
        u32 slot = slot_test ? 0xFFu : 1u;
        strcpy((char*)data, "profile.dat");
        cpu.gpr[5] = slot;
        cpu.gpr[6] = base;
        cpu.gpr[7] = base + 256;
        cpu.gpr[8] = base + 512;
        CHECK(wiiu_filesystem_handle_import(&cpu, "SAVEOpenFile") && cpu.gpr[3] == 0);
        handle = mem_read32(&cpu, base + 512);
        CHECK(read_file(&cpu, handle, dst, 1, 6) == 6);
        CHECK(memcmp(data + 4096, slot_test ? "COMMON" : "ACCOUN", 6) == 0);
        cpu.gpr[5] = slot;
        cpu.gpr[6] = base;
        cpu.gpr[7] = base + 1024;
        CHECK(wiiu_filesystem_handle_import(&cpu, "SAVEGetStat") && cpu.gpr[3] == 0);
        CHECK(mem_read32(&cpu, base + 1024 + 0x10) >= 6);
        data[0] = 0;
        cpu.gpr[5] = slot;
        cpu.gpr[6] = base;
        cpu.gpr[7] = base + 512;
        CHECK(wiiu_filesystem_handle_import(&cpu, "SAVEOpenDir") && cpu.gpr[3] == 0);
    }
    const char* invalid_paths[] = {"content_only.dat", "../common/profile.dat",
                                  "..\\common\\profile.dat", "profile.dat"};
    for (u32 i = 0; i < 4; ++i) {
        strcpy((char*)data, invalid_paths[i]);
        cpu.gpr[5] = i == 3 ? 2 : 1;
        cpu.gpr[6] = base;
        cpu.gpr[7] = base + 256;
        cpu.gpr[8] = base + 512;
        mem_write32(&cpu, base + 512, 0xDEADBEEFu);
        CHECK(wiiu_filesystem_handle_import(&cpu, "SAVEOpenFile") && (s32)cpu.gpr[3] < 0);
        CHECK(mem_read32(&cpu, base + 512) == 0xDEADBEEFu);
    }
cleanup:
    if (reference) fclose(reference);
    wiiu_filesystem_shutdown();
    wiiu_memory_free(&memory);
    return result;
}
