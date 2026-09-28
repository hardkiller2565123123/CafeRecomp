#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#endif

#include "cpu/cpu.h"
#include "rpx_runtime.h"
#include "generated.h"
#include "wiiu_imports.h"
#include "wiiu_memory.h"
#include "wiiu_window.h"

#ifndef BOTW_BASE_TITLE_DIR
#define BOTW_BASE_TITLE_DIR "game/base"
#endif

#ifndef BOTW_UPDATE_TITLE_DIR
#define BOTW_UPDATE_TITLE_DIR "game/update"
#endif

#ifndef BOTW_DLC_TITLE_DIR
#define BOTW_DLC_TITLE_DIR "game/dlc"
#endif

enum {
    WIIU_GUEST_LOADER_ARGS = WIIU_GUEST_OS_BASE + 0x8000u,
    WIIU_GUEST_LOADER_ARGV = WIIU_GUEST_OS_BASE + 0x8100u,
    WIIU_GUEST_LOADER_PATH = WIIU_GUEST_OS_BASE + 0x8200u,
};

static const u64 WIIU_TIMER_CLOCK = 62156250ull;
enum { GUEST_DISPATCH_CYCLE_BUDGET = 8192u };

/* Opt-in boot profiler. Sample irregularly to avoid aliasing with the fixed
   scheduler quantum; normal launches incur no timer calls per dispatch. */
typedef struct { u32 pc, samples; u64 ticks; } DispatchProfile;
static DispatchProfile g_dispatch_profile[8192];
static bool g_profile_dispatch;
static u32 g_profile_rng = 0x12345678u;
static u64 host_timebase_ticks(void);
static void report_dispatch_profile(void) {
    if (!g_profile_dispatch) return;
    for (u32 rank = 0; rank < 24; ++rank) {
        DispatchProfile* best = NULL;
        for (u32 i = 0; i < 8192; ++i)
            if (g_dispatch_profile[i].ticks &&
                (!best || g_dispatch_profile[i].ticks > best->ticks))
                best = &g_dispatch_profile[i];
        if (!best) break;
        fprintf(stderr, "profile: boot main pc=%08X samples=%u sampled_ms=%.3f\n",
                best->pc, best->samples,
                (double)best->ticks * 1000.0 / (double)WIIU_TIMER_CLOCK);
        best->ticks = 0;
    }
    g_profile_dispatch = false;
}

/* Generated C functions use downcount as a per-dispatch cycle budget.  It
   must be renewed before every chassis call; leaving the previous negative
   value in place forces later loops to return after only a few instructions.
   A multi-thousand-cycle quantum avoids a costly host round trip for each
   short native loop, while the small slice counts below retain fair pumping. */
static int run_guest_dispatches(CPUState* cpu, u32 max_blocks, u32 cycle_budget,
                                u32* executed) {
    *executed=0;
    for (u32 blocks = 0u; max_blocks == 0u || blocks < max_blocks; blocks++) {
        cpu->downcount = cycle_budget;
        bool sample = false;
        if (g_profile_dispatch) {
            g_profile_rng = g_profile_rng * 1664525u + 1013904223u;
            sample = (g_profile_rng >> 24) == 0;
        }
        u32 sampled_pc = cpu->pc;
        u64 start = sample ? host_timebase_ticks() : 0;
        int dispatched = dolrecomp_call(cpu, cpu->pc);
        ++*executed;
        if (sample) {
            u64 elapsed = host_timebase_ticks() - start;
            u32 slot = (sampled_pc >> 2) & 8191u;
            for (u32 probe = 0; probe < 8192; ++probe, slot = (slot+1)&8191u) {
                DispatchProfile* entry = &g_dispatch_profile[slot];
                if (!entry->pc || entry->pc == sampled_pc) {
                    entry->pc = sampled_pc; ++entry->samples; entry->ticks += elapsed;
                    break;
                }
            }
        }
        wiiu_memory_track_reservation(cpu);
        if (!dispatched || cpu->exception)
            return 0;
        /* A retained Cafe wait cannot complete until another cooperative
           context runs. Do not repeat that same import 32 times per slice. */
        if(cpu->pc==sampled_pc && cpu->pc>=0x04348000u && cpu->pc<0x04350000u)
            break;
    }
    return 1;
}

static u64 host_timebase_ticks(void) {
#if defined(_WIN32)
    static LARGE_INTEGER frequency;
    static LARGE_INTEGER origin;
    static bool initialized;
    LARGE_INTEGER now;

    if (!initialized) {
        if (!QueryPerformanceFrequency(&frequency) ||
            !QueryPerformanceCounter(&origin)) {
            return 0u;
        }
        initialized = true;
        return 0u;
    }
    if (!QueryPerformanceCounter(&now) || frequency.QuadPart <= 0)
        return 0u;

    u64 elapsed = (u64)(now.QuadPart - origin.QuadPart);
    u64 rate = (u64)frequency.QuadPart;
    return (elapsed / rate) * WIIU_TIMER_CLOCK +
           ((elapsed % rate) * WIIU_TIMER_CLOCK) / rate;
#else
    static clock_t origin;
    static bool initialized;
    clock_t now = clock();
    if (!initialized) {
        origin = now;
        initialized = true;
        return 0u;
    }
    return ((u64)(now - origin) * WIIU_TIMER_CLOCK) / CLOCKS_PER_SEC;
#endif
}

static void advance_guest_timebase(CPUState* cpu, u64* last_host_ticks) {
    u64 host_ticks = host_timebase_ticks();
    if (host_ticks <= *last_host_ticks)
        return;

    u64 elapsed = host_ticks - *last_host_ticks;
    cpu->timebase += elapsed;
    wiiu_imports_advance_timebase(elapsed);
    *last_host_ticks = host_ticks;
}

typedef struct {
    const char* rpx_path;
    const char* base_title_dir;
    const char* update_title_dir;
    const char* dlc_title_dir;
    u32 max_blocks;
} RuntimeOptions;

static void build_rpx_path(char* out, size_t out_size,
                           const char* update_title_dir) {
    snprintf(out, out_size, "%s/code/U-King.rpx", update_title_dir);
}

static void write_guest_string(CPUState* cpu, u32 address, const char* value) {
    if (!value)
        value = "";

    while (*value) {
        mem_write8(cpu, address++, (u8)*value++);
    }
    mem_write8(cpu, address, 0);
}

static u32 seed_loader_args(CPUState* cpu, const char* rpx_path) {
    for (u32 i = 0; i < 0x400u; i++)
        mem_write8(cpu, WIIU_GUEST_LOADER_ARGS + i, 0);

    write_guest_string(cpu, WIIU_GUEST_LOADER_PATH, rpx_path);

    mem_write32(cpu, WIIU_GUEST_LOADER_ARGV + 0u, WIIU_GUEST_LOADER_PATH);
    mem_write32(cpu, WIIU_GUEST_LOADER_ARGV + 4u, 0);
    mem_write32(cpu, WIIU_GUEST_LOADER_ARGS + 0u, 1);
    mem_write32(cpu, WIIU_GUEST_LOADER_ARGS + 4u, WIIU_GUEST_LOADER_ARGV);

    printf("loader args: magic=0xDEADF00D block=0x%08X argv=0x%08X\n",
           WIIU_GUEST_LOADER_ARGS, WIIU_GUEST_LOADER_ARGV);
    return WIIU_GUEST_LOADER_ARGS;
}

static void seed_small_data_registers(CPUState* cpu) {
    const RPXSymbol* sda = wiiu_imports_find_symbol_name("_SDA_BASE_");
    const RPXSymbol* sda2 = wiiu_imports_find_symbol_name("_SDA2_BASE_");

    if (sda)
        cpu->gpr[13] = sda->value;
    if (sda2)
        cpu->gpr[2] = sda2->value;

    printf("small data: r2=0x%08X r13=0x%08X\n", cpu->gpr[2], cpu->gpr[13]);
}

static void log_title_runtime_global(const RPXFile* rpx) {
    const u32 address = 0x1047BE88u;
    if (!rpx)
        return;

    for (u32 i = 0; i < rpx->load_section_count; i++) {
        const RPXLoadSection* section = &rpx->load_sections[i];
        if (!section->data || address < section->address ||
            address - section->address > section->size - 4u)
            continue;

        const u8* word = section->data + (address - section->address);
        u32 value = ((u32)word[0] << 24) | ((u32)word[1] << 16) |
                    ((u32)word[2] << 8) | (u32)word[3];
        fprintf(stderr,
                "loader: title runtime global 0x%08X = 0x%08X (%s+0x%X)\n",
                address, value, section->name, address - section->address);
        return;
    }

    fprintf(stderr, "loader: title runtime global 0x%08X is not mapped\n",
            address);
}

static bool parse_u32(const char* text, u32* value_out) {
    char* end = NULL;
    unsigned long value;
    if (!text || !text[0] || !value_out)
        return false;
    value = strtoul(text, &end, 0);
    if (!end || *end != 0)
        return false;
    *value_out = value > 0xFFFFFFFFul ? 0xFFFFFFFFu : (u32)value;
    return true;
}

static void print_usage(const char* executable) {
    printf("Usage: %s [options] [U-King.rpx] [block-count]\n", executable);
    printf("  --rpx <path>       Updated v208 U-King.rpx\n");
    printf("  --base <dir>       Extracted base title directory\n");
    printf("  --update <dir>     Extracted update title directory\n");
    printf("  --dlc <dir>        Extracted DLC title directory\n");
    printf("  --blocks <count>   Stop after this many generated blocks (0=unlimited)\n");
    printf("  --help             Show this help\n");
}

static bool parse_options(int argc, char** argv, RuntimeOptions* options,
                          char* default_rpx_path, size_t default_rpx_size) {
    bool positional_rpx_seen = false;
    bool positional_blocks_seen = false;

    options->base_title_dir = BOTW_BASE_TITLE_DIR;
    options->update_title_dir = BOTW_UPDATE_TITLE_DIR;
    options->dlc_title_dir = BOTW_DLC_TITLE_DIR;
    options->max_blocks = 0u;

    for (int i = 1; i < argc; i++) {
        const char* argument = argv[i];
        if (strcmp(argument, "--help") == 0 || strcmp(argument, "-h") == 0) {
            print_usage(argv[0]);
            return false;
        }
        if (strcmp(argument, "--rpx") == 0 || strcmp(argument, "--base") == 0 ||
            strcmp(argument, "--update") == 0 || strcmp(argument, "--dlc") == 0 ||
            strcmp(argument, "--blocks") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "missing value for %s\n", argument);
                return false;
            }
            if (strcmp(argument, "--rpx") == 0) {
                options->rpx_path = argv[i];
                positional_rpx_seen = true;
            } else if (strcmp(argument, "--base") == 0) {
                options->base_title_dir = argv[i];
            } else if (strcmp(argument, "--update") == 0) {
                options->update_title_dir = argv[i];
            } else if (strcmp(argument, "--dlc") == 0) {
                options->dlc_title_dir = argv[i];
            } else if (!parse_u32(argv[i], &options->max_blocks)) {
                fprintf(stderr, "invalid block count: %s\n", argv[i]);
                return false;
            }
            continue;
        }
        if (argument[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", argument);
            return false;
        }
        if (!positional_rpx_seen) {
            options->rpx_path = argument;
            positional_rpx_seen = true;
        } else if (!positional_blocks_seen &&
                   parse_u32(argument, &options->max_blocks)) {
            positional_blocks_seen = true;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argument);
            return false;
        }
    }

    build_rpx_path(default_rpx_path, default_rpx_size,
                   options->update_title_dir);
    if (!options->rpx_path)
        options->rpx_path = default_rpx_path;
    return true;
}

static int run_with_guest_threads(CPUState* cpu, u32 max_blocks) {
    const char* perf_setting = getenv("BOTW_TRACE_PERF");
    bool trace_performance = perf_setting && *perf_setting && strcmp(perf_setting, "0") != 0;
    const char* stop_pc_text = getenv("BOTW_STOP_AT_PC");
    u32 stop_pc = stop_pc_text && *stop_pc_text
                      ? (u32)strtoul(stop_pc_text, NULL, 0) : 0u;
    const char* stop_hits_text = getenv("BOTW_STOP_PC_HITS");
    u32 stop_hits = stop_hits_text && *stop_hits_text
                        ? (u32)strtoul(stop_hits_text, NULL, 0) : 1u;
    u32 observed_stop_hits = 0u;
    const char* snapshot_pc_text = getenv("BOTW_SNAPSHOT_AT_PC");
    u32 snapshot_pc = snapshot_pc_text && *snapshot_pc_text
                         ? (u32)strtoul(snapshot_pc_text, NULL, 0) : 0u;
    u32 snapshot_hits = 0u;
    const char* snapshot_after_text = getenv("BOTW_SNAPSHOT_AFTER_BLOCKS");
    u32 snapshot_after = snapshot_after_text && *snapshot_after_text
                            ? (u32)strtoul(snapshot_after_text, NULL, 0) : 0u;
    /* Title startup immediately begins running several resource workers.
       Keep their time slices small enough that the main thread can process
       their completion queues and pump the window between jobs.  A single
       262K-block worker slice can otherwise monopolize the host for minutes
       while the title shell remains white. */
    /* Match the already-used frame quantum during CPU-heavy initialization.
       Retained waits and the contended queue below still yield in 1-2 steps. */
    const u32 startup_main_slice = 32u;
    /* Blocked/sleeping workers now leave the runnable set. Give the remaining
       workers enough dispatches to complete jobs while main polls game-level
       completion flags as well as OS events. */
    const u32 startup_worker_slice = 32u;
    const u32 runtime_main_slice = 32u;
    /* A title frame is not a loading-complete signal. Resource preparation
       continues behind it; keep the startup worker budget instead of cutting
       it to a quarter as soon as a splash/title image reaches scan-out. */
    const u32 runtime_worker_slice = 32u;
    const u32 waiting_main_worker_slice = 32u;
    u32 event_wait_address = 0u;
    const u32 progress_interval = trace_performance ? 8000u : 1000000u;
    const u32 perf_interval = 4000u;
    u32 blocks = 0;
    u32 next_progress = progress_interval;
    u32 next_perf = perf_interval;
    clock_t main_ticks = 0;
    clock_t worker_ticks = 0;
    clock_t audio_ticks = 0;
    clock_t pump_ticks = 0;
    u32 perf_slices = 0;
    u64 last_host_ticks = host_timebase_ticks();
    u32 shader_bundle_entry_logs = 0;
    u32 material_bind_entry_logs = 0;

    while (max_blocks == 0u || blocks < max_blocks) {
        if (wiiu_window_is_closed()) {
            fprintf(stderr, "runner: game window closed; shutting down\n");
            return 1;
        }
        /* One-shot observation, without ending the run or modifying guest
           state. Allow the worker a few thousand turns before sampling. */
        if (snapshot_pc && blocks >= snapshot_after && cpu->pc == snapshot_pc &&
            ++snapshot_hits == 4096u) {
            fprintf(stderr, "runner: live snapshot pc=%08X blocks=%u\n", snapshot_pc, blocks);
            wiiu_imports_dump_threads(cpu);
            snapshot_pc = 0u;
        }
        if (stop_pc && cpu->pc == stop_pc && ++observed_stop_hits >= stop_hits) {
            fprintf(stderr, "runner: diagnostic stop at pc=%08X blocks=%u\n",
                    stop_pc, blocks);
            return 1;
        }
        advance_guest_timebase(cpu, &last_host_ticks);
        if (cpu->pc == 0x04218F54u && shader_bundle_entry_logs++ < 16u) {
            fprintf(stderr,
                    "runner: shader-bundle entry lr=0x%08X dst=0x%08X "
                    "shader=0x%08X count=%u arg6=0x%08X\n",
                    cpu->lr, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                    cpu->gpr[6]);
        }
        if (cpu->pc == 0x03C51D0Cu && material_bind_entry_logs++ < 16u) {
            fprintf(stderr,
                    "runner: material-bind entry lr=0x%08X manager=0x%08X "
                    "material=0x%08X\n",
                    cpu->lr, cpu->gpr[3], cpu->gpr[4]);
        }
        /* The title's startup cleanup list can contain one final node whose
           link was never wired to the end sentinel when Cafe static-module
           construction is absent.  Resume at the routine's normal unlock
           epilogue once that exact traversal reaches its impossible null
           node, instead of repeatedly dereferencing guest address zero. */
        if (cpu->pc == 0x037DE3C8u && cpu->lr == 0x037F2790u &&
            cpu->gpr[31] == 0u && cpu->gpr[30] != 0u) {
            fprintf(stderr,
                    "runner: recovered null startup cleanup link "
                    "end=0x%08X mutex=0x%08X\n",
                    cpu->gpr[30], cpu->gpr[27]);
            cpu->pc = 0x037F27B4u;
        }
        /* A second startup container is left as an all-zero intrusive list
           when its optional subsystem is unavailable.  The title mistakes
           the null head for an element and repeatedly calls its null vtable.
           Restore the empty sentinel links and resume at the same branch an
           initialized empty list takes.  The dispatcher may already have
           returned the null callback to LR, so accept either boundary. */
        if ((cpu->pc == 0u || cpu->pc == 0x030BCD34u) &&
            cpu->lr == 0x030BCD34u && cpu->gpr[3] == 0u &&
            cpu->gpr[8] == 0u && cpu->gpr[24] == 1u &&
            cpu->gpr[31] == 0u && cpu->gpr[26] != 0u &&
            cpu->gpr[30] == 0u &&
            cpu->gpr[25] == cpu->gpr[26] + 316u) {
            u32 sentinel = cpu->gpr[25];
            mem_write32(cpu, cpu->gpr[26] + 320u, sentinel);
            mem_write32(cpu, cpu->gpr[26] + 324u, sentinel);
            fprintf(stderr,
                    "runner: repaired empty startup container "
                    "owner=0x%08X sentinel=0x%08X offset=0x%08X\n",
                    cpu->gpr[26], cpu->gpr[25], cpu->gpr[30]);
            cpu->pc = 0x030BCD64u;
        }
        /* The render-state owner has the same absent-constructor shape: its
           intrusive list head is null while the end sentinel is embedded in
           the owner.  Do not run the per-node flag clear on address zero. */
        if (cpu->pc == 0x031A81A0u && cpu->lr == 0x031A9844u &&
            cpu->gpr[3] == 0u && cpu->gpr[30] == 0u &&
            cpu->gpr[29] != 0u) {
            fprintf(stderr,
                    "runner: skipped null render-state list node "
                    "owner=0x%08X end=0x%08X bias=0x%08X\n",
                    cpu->gpr[28], cpu->gpr[29], cpu->gpr[31]);
            cpu->pc = 0x031A9858u;
        }
        /* The scene-query pass obtains its candidate container through an
           optional global service.  With that service absent, the container
           is null; the original code assumes a constructed empty list and
           otherwise walks address zero forever.  Preserve the no-match result
           and continue after that list scan. */
        if (cpu->pc == 0x03628448u && cpu->gpr[3] == 0u &&
            cpu->gpr[28] == 0u &&
            (cpu->lr == 0x031508ACu || cpu->lr == 0x031508F4u)) {
            fprintf(stderr,
                    "runner: skipped absent scene-query container "
                    "global=0x1047C244 result=%u\n",
                    cpu->gpr[30]);
            cpu->pc = 0x03150904u;
        }
        bool active_frame = wiiu_gx2_has_active_frame();
        if (active_frame) report_dispatch_profile();
        u32 main_slice = active_frame ? runtime_main_slice
                                      : startup_main_slice;
        /* Retained waits cannot change during consecutive main dispatches:
           the worker that signals them has not had a turn yet. Retry once,
           then schedule workers before polling that same event again. */
        if (event_wait_address && cpu->pc == event_wait_address)
            main_slice = 1u;
        /* This title's save-manager loop polls an asynchronous worker flag.
           Keep executing the original loop, but yield earlier so the worker
           can initialize the save fields. No guest flags/PCs are changed. */
        u32 main_cycle_budget = GUEST_DISPATCH_CYCLE_BUDGET;
        if ((cpu->pc == 0x02E3D614u && cpu->lr == 0x02E3D6B0u) ||
            (cpu->pc == 0x02E3D63Cu && cpu->lr == 0x02E3D63Cu)) {
            main_slice = 1u;
            main_cycle_budget = 256u;
        }
        u32 worker_slice = active_frame ? runtime_worker_slice
                                        : startup_worker_slice;
        /* Job-queue consumers can release and reacquire the same spinlock
           within a 32-dispatch worker turn. A fixed quantum can repeatedly
           end with the lock held, starving main indefinitely. Main gets two
           dispatches to finish lwarx/cache-fallback/stwcx while each worker
           gets one, exposing the unlock window without stealing the lock.
           tools/job_lock_test.c executes this exact contended guest path. */
        if (cpu->pc >= 0x030DF674u && cpu->pc <= 0x030DF698u &&
            cpu->lr == 0x030DF624u) {
            main_slice = 2u;
            main_cycle_budget = 256u;
            worker_slice = 1u;
        }
        wiiu_imports_run_alarms(cpu, active_frame ? 16u : 4u);
        u32 slice = main_slice;
        if (max_blocks != 0u && slice > max_blocks - blocks)
            slice = max_blocks - blocks;
        clock_t phase_start = trace_performance ? clock() : 0;
        u32 executed=0;
        if (!run_guest_dispatches(cpu, slice, main_cycle_budget, &executed))
            return 0;
        if (trace_performance) main_ticks += clock() - phase_start;
        blocks += executed;
        /* A retained OSWaitEvent call has no main-thread work to perform.
           Give the resource workers bounded, useful turns instead of spending
           most dispatches retrying that same unsignaled event. */
        if (!event_wait_address && cpu->pc >= 0x04348000u && cpu->pc < 0x04350000u) {
            const RPXSymbol* symbol = wiiu_imports_find_symbol(cpu->pc);
            if (symbol && strcmp(symbol->name, "OSWaitEvent") == 0) {
                event_wait_address = cpu->pc;
                fprintf(stderr, "runner: event-wait worker slice=%u at %08X\n",
                        waiting_main_worker_slice, event_wait_address);
            }
        }
        if (event_wait_address && cpu->pc == event_wait_address)
            worker_slice = waiting_main_worker_slice;
        if (trace_performance) phase_start = clock();
        wiiu_imports_run_resumed_threads(cpu, worker_slice);
        if (trace_performance) worker_ticks += clock() - phase_start;
        if (blocks >= 18000000u) {
            if (trace_performance) phase_start = clock();
            wiiu_imports_run_audio(cpu, 32u);
            if (trace_performance) audio_ticks += clock() - phase_start;
        }
        if (trace_performance) phase_start = clock();
        wiiu_window_pump();
        if (trace_performance) pump_ticks += clock() - phase_start;
        advance_guest_timebase(cpu, &last_host_ticks);
        perf_slices++;

        if (blocks >= next_progress) {
            fprintf(stderr,
                    "runner: main progress blocks=%u pc=0x%08X lr=0x%08X "
                    "r0=0x%08X r3=0x%08X r8=0x%08X r9=0x%08X "
                    "r10=0x%08X r11=0x%08X r12=0x%08X\n",
                    blocks, cpu->pc, cpu->lr, cpu->gpr[0], cpu->gpr[3],
                    cpu->gpr[8], cpu->gpr[9], cpu->gpr[10], cpu->gpr[11],
                    cpu->gpr[12]);
            if (blocks >= 23000000u && blocks < 23032000u) {
                u32 sp = cpu->gpr[1];
                fprintf(stderr, "runner: late boot r25=%08X r26=%08X "
                        "r27=%08X r28=%08X r29=%08X r30=%08X r31=%08X\n",
                        cpu->gpr[25], cpu->gpr[26], cpu->gpr[27], cpu->gpr[28],
                        cpu->gpr[29], cpu->gpr[30], cpu->gpr[31]);
                WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
                for (u32 depth = 0; depth < 8u; ++depth) {
                    if (!wiiu_memory_find(memory, sp, 8u)) break;
                    u32 parent = mem_read32(cpu, sp);
                    fprintf(stderr, "runner: late stack sp=%08X return=%08X\n",
                            sp, mem_read32(cpu, sp + 4u));
                    if (parent <= sp) break;
                    sp = parent;
                }
            }
            if (cpu->pc == 0x037DE3C8u) {
                u32 node = cpu->gpr[31];
                u32 bias = cpu->gpr[29];
                u32 raw_node = node + bias;
                fprintf(stderr,
                        "runner: cleanup-list node=0x%08X raw=0x%08X "
                        "end=0x%08X bias=0x%08X next=0x%08X flags=0x%02X\n",
                        node, raw_node, cpu->gpr[30], bias,
                        mem_read32(cpu, raw_node + 4u),
                        mem_read8(cpu, node + 8u));
            }
            do {
                next_progress += progress_interval;
            } while (next_progress <= blocks &&
                      next_progress > progress_interval);
        }
        if (trace_performance && blocks >= next_perf) {
            double ticks_to_ms = 1000.0 / (double)CLOCKS_PER_SEC;
            fprintf(stderr,
                    "runner: perf slices=%u main=%.1fms workers=%.1fms "
                    "audio=%.1fms window=%.1fms\n",
                    perf_slices, (double)main_ticks * ticks_to_ms,
                    (double)worker_ticks * ticks_to_ms,
                    (double)audio_ticks * ticks_to_ms,
                    (double)pump_ticks * ticks_to_ms);
            main_ticks = 0;
            worker_ticks = 0;
            audio_ticks = 0;
            pump_ticks = 0;
            perf_slices = 0;
            do {
                next_perf += perf_interval;
            } while (next_perf <= blocks && next_perf > perf_interval);
        }
    }
    return 1;
}

static void dump_loop_list(CPUState* cpu) {
    if (cpu->pc != 0x02379C74u)
        return;

    u32 node = cpu->gpr[11];
    printf("loop list: target=0x%08X floor=0x%08X limit=0x%08X stride=0x%08X\n",
           cpu->gpr[8], node, cpu->gpr[4], cpu->gpr[12]);
    for (u32 i = 0; i < 8 && node != 0; i++) {
        u32 word0 = mem_read32(cpu, node + cpu->gpr[12] + 0u);
        u32 word4 = mem_read32(cpu, node + cpu->gpr[12] + 4u);
        u32 word8 = mem_read32(cpu, node + cpu->gpr[12] + 8u);
        printf("  node[%u] 0x%08X: +0=0x%08X +4=0x%08X +8=0x%08X\n",
               i, node, word0, word4, word8);
        if (word4 == node)
            break;
        node = word4 - cpu->gpr[12];
    }
}

static void configure_runtime_log(void) {
    const char* path = getenv("BOTW_LOG_PATH");
#if defined(_WIN32)
    char default_path[2048];
    bool default_log = !path || !*path;
    if (default_log) {
        const char* local = getenv("LOCALAPPDATA");
        if (local && *local) {
            int size = snprintf(default_path, sizeof(default_path),
                                "%s/BOTWRecomp", local);
            if (size > 0 && (size_t)size < sizeof(default_path)) {
                CreateDirectoryA(default_path, NULL);
                size = snprintf(default_path, sizeof(default_path),
                                "%s/BOTWRecomp/logs", local);
                if (size > 0 && (size_t)size < sizeof(default_path)) {
                    CreateDirectoryA(default_path, NULL);
                    size = snprintf(default_path, sizeof(default_path),
                                    "%s/BOTWRecomp/logs/botw_%lu.log", local,
                                    (unsigned long)GetCurrentProcessId());
                    if (size > 0 && (size_t)size < sizeof(default_path))
                        path = default_path;
                }
            }
        }
    }
#endif
    if (!path || !*path)
        return;

    FILE* log = freopen(path, "w", stderr);
    if (log) {
        setvbuf(log, NULL, _IONBF, 0);
#if defined(_WIN32)
        if (default_log) {
            /* Explorer launches must not depend on a diagnostic shell's
               redirection, or block when text is selected in a console. */
            _dup2(_fileno(stderr), _fileno(stdout));
            setvbuf(stdout, NULL, _IONBF, 0);
        }
        char executable[2048] = {0};
        char directory[2048] = {0};
        GetModuleFileNameA(NULL, executable, sizeof(executable));
        GetCurrentDirectoryA(sizeof(directory), directory);
        fprintf(stderr, "launcher: exe=%s cwd=%s log=%s\n", executable,
                directory, path);
#endif
    }
}

int main(int argc, char** argv) {
    const char* profile_dispatch = getenv("BOTW_PROFILE_DISPATCH");
    g_profile_dispatch = profile_dispatch && strcmp(profile_dispatch, "1") == 0;
    atexit(report_dispatch_profile);
    configure_runtime_log();
    char default_rpx_path[1024];
    RuntimeOptions options = {0};
    if (!parse_options(argc, argv, &options, default_rpx_path,
                       sizeof(default_rpx_path))) {
        return argc > 1 && (strcmp(argv[1], "--help") == 0 ||
                            strcmp(argv[1], "-h") == 0)
                   ? 0
                   : 1;
    }

    const char* rpx_path = options.rpx_path;
    u32 max_blocks = options.max_blocks;

    RPXFile rpx;
    if (!rpx_load(&rpx, rpx_path))
        return 1;

    log_title_runtime_global(&rpx);

    rpx_print_info(&rpx, "The Legend of Zelda: Breath of the Wild v208");
    wiiu_imports_attach_rpx(&rpx);
    wiiu_imports_set_title_paths(options.base_title_dir,
                                 options.update_title_dir,
                                 options.dlc_title_dir);
    wiiu_imports_reset_stats();

    WiiUMemory memory;
    wiiu_memory_init(&memory);
    if (!wiiu_memory_load_rpx(&memory, &rpx) ||
        !wiiu_memory_install_default_regions(&memory)) {
        wiiu_memory_free(&memory);
        rpx_free(&rpx);
        return 1;
    }
    wiiu_imports_seed_data_imports(&memory);

    CPUState cpu;
    if (!cpu_init(&cpu)) {
        wiiu_memory_free(&memory);
        rpx_free(&rpx);
        return 1;
    }

    cpu.ram_size = 0;
    cpu_alloc_mem2(&cpu, 64u * 1024u * 1024u);
    wiiu_memory_bind_cpu(&memory, &cpu);
    cpu.host_call = wiiu_imports_host_call;
    wiiu_imports_init_cpu_context(&cpu);
    cpu.hid2 = PPC_HID2_PSE | PPC_HID2_LSQE | PPC_HID2_LCE;
    /* Cafe OS enters an RPX with the paired-single/FPU facility available.
       The standalone CPU core resets MSR to zero, so establish the user-mode
       launch state before executing the title entry point. */
    cpu.msr |= 0x00002000u; /* MSR[FP] */
    cpu.pc = rpx.entry_point;
    cpu.gpr[3] = 0xDEADF00Du;
    cpu.gpr[1] = WIIU_GUEST_STACK_TOP - 0x100u;
    cpu.gpr[4] = seed_loader_args(&cpu, rpx_path);
    seed_small_data_registers(&cpu);

    wiiu_memory_dump_map(&memory);
    if (max_blocks == 0u)
        printf("starting at 0x%08X with no diagnostic limit\n", cpu.pc);
    else
        printf("starting at 0x%08X for %u blocks\n", cpu.pc, max_blocks);

    int completed = run_with_guest_threads(&cpu, max_blocks);
    wiiu_imports_dump_threads(&cpu);

    printf("run %s: pc=0x%08X lr=0x%08X exception=0x%08X gpr3=0x%08X\n",
           completed ? "paused" : "stopped", cpu.pc, cpu.lr, cpu.exception,
           cpu.gpr[3]);
    if (cpu.exception) {
        printf("exception detail: program=0x%08X srr0=0x%08X srr1=0x%08X "
               "dar=0x%08X dsisr=0x%08X msr=0x%08X\n",
               cpu.program_exception, cpu.srr0, cpu.srr1, cpu.dar,
               cpu.dsisr, cpu.msr);
    }
    printf("regs: r0=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X r7=0x%08X "
           "r8=0x%08X r9=0x%08X r10=0x%08X r11=0x%08X r12=0x%08X "
           "r18=0x%08X r30=0x%08X r31=0x%08X ctr=0x%08X\n",
           cpu.gpr[0], cpu.gpr[4], cpu.gpr[5], cpu.gpr[6], cpu.gpr[7],
           cpu.gpr[8], cpu.gpr[9], cpu.gpr[10], cpu.gpr[11], cpu.gpr[12],
           cpu.gpr[18], cpu.gpr[30], cpu.gpr[31], cpu.ctr);
    dump_loop_list(&cpu);
    printf("memory gaps: reads=%u writes=%u readonly_writes=%u imports=%u\n",
           memory.unmapped_read_count, memory.unmapped_write_count,
           memory.readonly_write_count, wiiu_imports_unknown_count());

    cpu_free(&cpu);
    wiiu_imports_shutdown();
    wiiu_memory_free(&memory);
    rpx_free(&rpx);
    return completed ? 0 : 2;
}
