/* Read-only debugger for a local BOTW test process. Never modifies guest RAM.
   Suspend only its busiest native thread long enough to copy registers, then
   resume it before examining memory or printing. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include "wiiu_memory.h"

static int read_process(HANDLE process, uintptr_t at, void* out, size_t size) {
    SIZE_T read = 0;
    return at && ReadProcessMemory(process, (void*)at, out, size, &read) && read == size;
}

static void native_location(HANDLE process,DWORD pid,uintptr_t ip) {
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
    MODULEENTRY32 module={sizeof(module)};
    if(Module32First(snapshot,&module))do {
        uintptr_t base=(uintptr_t)module.modBaseAddr;
        if(ip<base || ip-base>=module.modBaseSize)continue;
        printf("native module=%s offset=%llX\n",module.szModule,(unsigned long long)(ip-base));
        IMAGE_DOS_HEADER dos;IMAGE_NT_HEADERS64 nt;IMAGE_EXPORT_DIRECTORY exports;
        if(!read_process(process,base,&dos,sizeof(dos)) || dos.e_lfanew<0 ||
           !read_process(process,base+dos.e_lfanew,&nt,sizeof(nt)))break;
        DWORD rva=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
        if(!rva || !read_process(process,base+rva,&exports,sizeof(exports)) ||
           exports.NumberOfFunctions>65536 || exports.NumberOfNames>65536)break;
        DWORD* functions=malloc(exports.NumberOfFunctions*sizeof(DWORD));
        DWORD* names=malloc(exports.NumberOfNames*sizeof(DWORD));
        WORD* ordinals=malloc(exports.NumberOfNames*sizeof(WORD));
        if(functions && names && ordinals &&
           read_process(process,base+exports.AddressOfFunctions,functions,exports.NumberOfFunctions*sizeof(DWORD)) &&
           read_process(process,base+exports.AddressOfNames,names,exports.NumberOfNames*sizeof(DWORD)) &&
           read_process(process,base+exports.AddressOfNameOrdinals,ordinals,exports.NumberOfNames*sizeof(WORD))) {
            DWORD closest=0;unsigned index=0;
            for(unsigned i=0;i<exports.NumberOfFunctions;++i)
                if(functions[i]<=ip-base && functions[i]>closest){closest=functions[i];index=i;}
            if(closest && ip-base-closest<4096)for(unsigned i=0;i<exports.NumberOfNames;++i)if(ordinals[i]==index) {
                char name[128]={0};
                if(read_process(process,base+names[i],name,sizeof(name)-1))
                    printf("native export=%s+%llu\n",name,(unsigned long long)(ip-base-closest));
                break;
            }
        }
        free(functions);free(names);free(ordinals);break;
    }while(Module32Next(snapshot,&module));
    CloseHandle(snapshot);
}

static u32 guest_word(HANDLE process, const WiiUMemory* memory, u32 address) {
    if ((address & 0xE0000000u) == 0x80000000u) address &= 0x1FFFFFFFu;
    for (u32 i = 0; i < memory->segment_count; ++i) {
        const WiiUMemorySegment* s = &memory->segments[i];
        if (address >= s->base && (u64)address + 4 <= (u64)s->base + s->size) {
            u8 bytes[4];
            if (read_process(process, (uintptr_t)s->data + address - s->base, bytes, 4))
                return read_be32(bytes);
        }
    }
    return 0;
}

/* Find saved worker CPUs in the system DLL's writable data, without relying
   on private symbols or modifying the running scheduler. All pointers and
   guest stacks are validated against the already identified memory object. */
static void sample_workers(HANDLE process, DWORD pid, uintptr_t memory_address,
                           const WiiUMemory* memory) {
    HANDLE modules=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,pid);
    MODULEENTRY32 module={sizeof(module)};
    if(Module32First(modules,&module)) do {
        if(_stricmp(module.szModule,"botw_system.dll"))continue;
        IMAGE_DOS_HEADER dos; IMAGE_NT_HEADERS64 nt;
        uintptr_t base=(uintptr_t)module.modBaseAddr;
        if(!read_process(process,base,&dos,sizeof(dos)) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
           dos.e_lfanew<0 || !read_process(process,base+dos.e_lfanew,&nt,sizeof(nt)) ||
           nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections>96)break;
        uintptr_t headers=base+dos.e_lfanew+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+
                          nt.FileHeader.SizeOfOptionalHeader;
        for(unsigned section=0;section<nt.FileHeader.NumberOfSections;++section) {
            IMAGE_SECTION_HEADER h;
            if(!read_process(process,headers+section*sizeof(h),&h,sizeof(h)) ||
               !(h.Characteristics&IMAGE_SCN_MEM_WRITE) || h.Misc.VirtualSize>64*1024*1024 ||
               h.VirtualAddress>module.modBaseSize ||
               h.Misc.VirtualSize>module.modBaseSize-h.VirtualAddress)continue;
            u8* bytes=malloc(h.Misc.VirtualSize);
            if(!bytes)continue;
            if(read_process(process,base+h.VirtualAddress,bytes,h.Misc.VirtualSize))
            for(size_t offset=offsetof(CPUState,external_user_data);
                offset+sizeof(void*)<=h.Misc.VirtualSize;offset+=sizeof(void*)) {
                uintptr_t candidate; memcpy(&candidate,bytes+offset,sizeof(candidate));
                size_t start=offset - offsetof(CPUState,external_user_data);
                if(candidate!=memory_address || start+sizeof(CPUState)>h.Misc.VirtualSize)continue;
                CPUState cpu;memcpy(&cpu,bytes+start,sizeof(cpu));
                if(cpu.pc<0x02000000u || cpu.pc>0x04350000u || !cpu.gpr[1] ||
                   !guest_word(process,memory,cpu.gpr[1]))continue;
                printf("worker cpu=%p pc=%08X lr=%08X sp=%08X\n",
                    (void*)(base+h.VirtualAddress+start),cpu.pc,cpu.lr,cpu.gpr[1]);
                for(u32 r=3;r<32;++r)printf("r%u=%08X%c",r,cpu.gpr[r],r==31?'\n':' ');
                for(u32 sp=cpu.gpr[1],depth=0;sp&&depth<14;++depth) {
                    printf("worker stack sp=%08X lr=%08X\n",sp,guest_word(process,memory,sp+4));
                    u32 parent=guest_word(process,memory,sp);
                    if(parent<=sp || parent-sp>0x100000)break;
                    sp=parent;
                }
            }
            free(bytes);
        }
    } while(Module32Next(modules,&module));
    CloseHandle(modules);
}

int main(int argc, char** argv) {
    if (argc != 3 && argc != 5) { fprintf(stderr, "usage: guest_sample PID exact-executable-path [guest-address word-count]\n"); return 1; }
    unsigned long long requested=argc==5?strtoull(argv[3],NULL,0):0;
    unsigned words=argc==5?(unsigned)strtoul(argv[4],NULL,0):0;
    if(argc==5 && (!words || words>1024 || requested>UINT32_MAX || requested+words*4ull>0x100000000ull))return 1;
    DWORD pid = strtoul(argv[1], NULL, 10);
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    char path[4096]; DWORD size = sizeof(path);
    if (!process || !QueryFullProcessImageNameA(process, 0, path, &size) ||
        _stricmp(path, argv[2]) || !strstr(path, "botw_recomp.exe")) return 1;
    DWORD tid = 0; ULONGLONG best = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry = {sizeof(entry)};
    if (Thread32First(snapshot, &entry)) do {
        if (entry.th32OwnerProcessID != pid) continue;
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
        FILETIME created, exited, kernel, user;
        if (thread && GetThreadTimes(thread, &created, &exited, &kernel, &user)) {
            ULONGLONG ticks = ((ULONGLONG)user.dwHighDateTime << 32) | user.dwLowDateTime;
            if (ticks > best) { best = ticks; tid = entry.th32ThreadID; }
        }
        if (thread) CloseHandle(thread);
    } while (Thread32Next(snapshot, &entry));
    CloseHandle(snapshot);
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, tid);
    if (!thread) { CloseHandle(process); return 1; }
    int found = 0, workers_sampled = 0;
    for (u32 sample = 0; sample < 8; ++sample) {
        CONTEXT context = {0}; context.ContextFlags = CONTEXT_FULL;
        if (SuspendThread(thread) == (DWORD)-1) break;
        BOOL ok = GetThreadContext(thread, &context);
        ResumeThread(thread);
        if (!ok) break;
        uintptr_t candidates[] = {context.Rcx, context.Rbx, context.Rsi, context.Rdi,
            context.Rbp, context.R12, context.R13, context.R14, context.R15};
        for (u32 i = 0; i < sizeof(candidates)/sizeof(candidates[0]); ++i) {
            CPUState cpu; WiiUMemory memory;
            if (!read_process(process, candidates[i], &cpu, sizeof(cpu)) ||
                cpu.pc < 0x02000000u || cpu.pc > 0x04350000u ||
                !read_process(process, (uintptr_t)cpu.external_user_data, &memory, sizeof(memory)) ||
                !memory.segment_count || memory.segment_count > WIIU_MAX_SEGMENTS ||
                !memory.page_map) continue;
            found = 1;
            if(argc==5) {
                for(unsigned word=0;word<words;++word) {
                    u32 at=(u32)requested+word*4;
                    printf("guest %08X=%08X\n",at,guest_word(process,&memory,at));
                }
                CloseHandle(thread);CloseHandle(process);return 0;
            }
            if(!workers_sampled) {
                sample_workers(process,pid,(uintptr_t)cpu.external_user_data,&memory);
                workers_sampled=1;
            }
            printf("sample=%u cpu=%p pc=%08X lr=%08X sp=%08X host=%p\n", sample,
                (void*)candidates[i], cpu.pc, cpu.lr, cpu.gpr[1], (void*)context.Rip);
            native_location(process,pid,(uintptr_t)context.Rip);
            /* Windows shares system-DLL mappings across these local processes.
               Identify sampled syscall stubs without downloading symbols. */
            HMODULE ntdll=GetModuleHandleA("ntdll.dll");
            const char* syscalls[]={"NtQueryPerformanceCounter","NtWaitForSingleObject",
                "NtWaitForMultipleObjects","NtDelayExecution","NtQuerySystemTime",
                "NtWriteFile","NtReadFile","NtQueryInformationProcess"};
            for(unsigned s=0;s<sizeof(syscalls)/sizeof(syscalls[0]);++s) {
                uintptr_t entry=(uintptr_t)GetProcAddress(ntdll,syscalls[s]);
                if(entry && context.Rip>=entry && context.Rip-entry<32)
                    printf("native syscall=%s+%llu\n",syscalls[s],(unsigned long long)(context.Rip-entry));
            }
            for (u32 r = 3; r < 32; ++r) printf("r%u=%08X%c", r, cpu.gpr[r], r==31?'\n':' ');
            u32 mutex = 0x10A13D58u;
            printf("mutex=%08X owner=%08X count=%u\n", mutex,
                guest_word(process,&memory,mutex+0x1C), guest_word(process,&memory,mutex+0x20));
            for (u32 sp=cpu.gpr[1], depth=0; sp && depth<12; ++depth) {
                printf("stack sp=%08X lr=%08X\n",sp,guest_word(process,&memory,sp+4));
                u32 parent=guest_word(process,&memory,sp);
                if (parent<=sp || parent-sp>0x100000) break;
                sp=parent;
            }
            break;
        }
        Sleep(10);
    }
    CloseHandle(thread); CloseHandle(process);
    return found ? 0 : 1;
}
