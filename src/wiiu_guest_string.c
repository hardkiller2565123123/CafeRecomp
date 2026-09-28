#include "wiiu_guest_string.h"
#include "wiiu_memory.h"
#include <string.h>

enum { STRING_TABLE=0x10263910u, STRING_TERMINATOR=0x10263A00u,
       STRING_NOOP=0x030B0C38u, STRING_LIMIT=0x80000u };

static bool mapped(CPUState* cpu,u32 address,u32 size) {
    WiiUMemory* memory=cpu?(WiiUMemory*)cpu->external_user_data:NULL;
    return memory && address && wiiu_memory_find(memory,address,size)!=NULL;
}
static const char* read_string(CPUState* cpu,u32 object,size_t* length) {
    if(!mapped(cpu,object,8))return NULL;
    u32 table=mem_read32(cpu,object+4);
    if(table>UINT32_MAX-32 || !mapped(cpu,table+28,4) ||
       mem_read32(cpu,table+28)!=STRING_NOOP)return NULL;
    u32 address=mem_read32(cpu,object);
    WiiUMemory* memory=(WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* segment=wiiu_memory_find(memory,address,1);
    if(!address || !segment)return NULL;
    u32 offset=wiiu_memory_canonical_address(address)-segment->base;
    size_t available=segment->size-offset;
    if(available>STRING_LIMIT+1)available=STRING_LIMIT+1;
    const char* text=(const char*)segment->data+offset;
    const char* end=memchr(text,0,available);
    if(!end)return NULL; /* Includes overlong strings and segment boundaries. */
    *length=(size_t)(end-text);return text;
}
bool wiiu_guest_string_contains(CPUState* cpu,u32 address) {
    if(address!=0x033D2B74u && address!=0x035CF358u)return false;
    if(!mapped(cpu,STRING_TERMINATOR,1) || mem_read8(cpu,STRING_TERMINATOR)!=0 ||
       !mapped(cpu,STRING_TABLE+28,4) || mem_read32(cpu,STRING_TABLE+28)!=STRING_NOOP)
        return false;
    size_t hay_length,needle_length;
    const char* hay=read_string(cpu,cpu->gpr[3],&hay_length);
    const char* needle=read_string(cpu,cpu->gpr[4],&needle_length);
    if(!hay || !needle)return false;
    /* Both originals return a boolean, not a substring index. Their only
       virtual calls are the verified no-op terminator checks above. */
    cpu->gpr[3]=hay_length>=needle_length && strstr(hay,needle)!=NULL;
    cpu->pc=cpu->lr&~3u;
    return true;
}
