#include "wiiu_imports.h"
#include "wiiu_cpu.h"
#include "wiiu_guest_string.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <zlib.h>

#include "generated.h"
#include "wiiu_audio.h"
#include "wiiu_filesystem.h"
#include "wiiu_gx2.h"
#include "wiiu_window.h"

enum {
    IMPORT_LOG_LIMIT = 512,
    BOOT_HISTORY_LIMIT = 32,
    STRING_READ_LIMIT = 1024 * 1024,
    FRAMEWORK_BLOCK_LIMIT = 4096,
    // The title loader keeps several generations of framework allocations
    // alive while it hands resources between boot states. Keep the host-side
    // ownership table comfortably above a single transition so bookkeeping
    // never becomes the allocation limit.
    FRAMEWORK_ALLOCATION_LIMIT = 524288,
    FRAMEWORK_PARENT_RESERVE = 2 * 1024 * 1024,
    BASE_HEAP_COUNT = 3,
    HOST_THREAD_LIMIT = 128,
    FAST_MUTEX_LIMIT = 256,
    NATIVE_YAZ0_STREAM_LIMIT = 32,
    AX_VOICE_LIMIT = 96,
    AX_MIX_FRAMES = 96,
    AX_OUTPUT_CHANNELS = 6,
    AX_VOICE_GUEST_BASE = WIIU_GUEST_OS_BASE + 0x00E00000u,
    AX_FINAL_MIX_PARAM = WIIU_GUEST_OS_BASE + 0x00E10000u,
    AX_FINAL_MIX_POINTERS = WIIU_GUEST_OS_BASE + 0x00E10100u,
    AX_FINAL_MIX_SAMPLES = WIIU_GUEST_OS_BASE + 0x00E11000u,
    AX_CALLBACK_STACK_TOP = WIIU_GUEST_OS_BASE + WIIU_GUEST_OS_SIZE,
    /* RPL handles begin at 0x1000 on Cafe OS; callers treat them as opaque. */
    HOST_ERREULA_MODULE_HANDLE = 0x00001000u,
    HOST_ERREULA_CREATE_ADDRESS = 0xFFFFFF00u,
    HOST_ERREULA_DESTROY_ADDRESS = 0xFFFFFF04u,
    HOST_ERREULA_IS_DECIDED_ADDRESS = 0xFFFFFF08u,
    HOST_ERREULA_IS_LEFT_ADDRESS = 0xFFFFFF0Cu,
    HOST_ERREULA_IS_RIGHT_ADDRESS = 0xFFFFFF10u,
    HOST_ERREULA_RESULT_CODE_ADDRESS = 0xFFFFFF14u,
    HOST_ERREULA_RESULT_TYPE_ADDRESS = 0xFFFFFF18u,
    HOST_ERREULA_APPEAR_ERROR_ADDRESS = 0xFFFFFF1Cu,
    HOST_ERREULA_DISAPPEAR_ERROR_ADDRESS = 0xFFFFFF20u,
    HOST_ERREULA_STATE_ADDRESS = 0xFFFFFF24u,
    HOST_ERREULA_CALC_ADDRESS = 0xFFFFFF28u,
    HOST_ERREULA_APPEAR_HOME_ADDRESS = 0xFFFFFF2Cu,
    HOST_ERREULA_CHANGE_LANG_ADDRESS = 0xFFFFFF30u,
    HOST_ERREULA_IS_HOME_ADDRESS = 0xFFFFFF34u,
    HOST_ERREULA_DISAPPEAR_HOME_ADDRESS = 0xFFFFFF38u,
    HOST_ERREULA_DRAW_TV_ADDRESS = 0xFFFFFF3Cu,
    HOST_ERREULA_DRAW_DRC_ADDRESS = 0xFFFFFF40u,
    HOST_ERREULA_SET_CONTROLLER_ADDRESS = 0xFFFFFF44u,
    HOST_ERREULA_IS_CURSOR_ACTIVE_ADDRESS = 0xFFFFFF48u,
    HOST_ERREULA_GET_SELECTION_ADDRESS = 0xFFFFFF4Cu,
    HOST_THREAD_CPP_INIT_RETURN_ADDRESS = 0xFFFFFFECu,
    HOST_ALARM_RETURN_ADDRESS = 0xFFFFFFF0u,
    HOST_AUDIO_RETURN_ADDRESS = 0xFFFFFFF4u,
    HOST_THREAD_YIELD_ADDRESS = 0xFFFFFFF8u,
    HOST_THREAD_RETURN_ADDRESS = 0xFFFFFFFCu,
    HOST_ALARM_LIMIT = 64,
    HOST_ZLIB_STREAM_LIMIT = 32,
    HOST_ZLIB_IO_LIMIT = 128 * 1024 * 1024,
    HOST_ALARM_STACK_TOP = WIIU_GUEST_OS_BASE + WIIU_GUEST_OS_SIZE - 0x10000u,
    KPAD_STATUS_SIZE = 0xF0u,
    KPAD_MPLS_WORK_SIZE = 0x5FE0u,
    VPAD_STATUS_SIZE = 0xACu,
    OS_ALARM_SIZE = 0x58u,
    OS_ALARM_TAG = 0x614C724Du,
    OS_THREAD_QUEUE_SIZE = 0x10u,
    OLV_DOWNLOADED_POST_DATA_SIZE = 0xC208u,
    GUEST_THREAD_SPECIFIC_BASE = WIIU_GUEST_OS_BASE + 0x00010000u,
    GUEST_THREAD_SPECIFIC_GLOBAL_BASE = WIIU_GUEST_OS_BASE + 0x00003000u,
    GUEST_THREAD_SPECIFIC_SLOT_SIZE = 0x4000u,
    GUEST_THREAD_SPECIFIC_VALUE_SIZE = 0x100u,
    GUEST_THREAD_CRT_BASE = WIIU_GUEST_OS_BASE + 0x0000A000u,
    GUEST_THREAD_CRT_SLOT_SIZE = 0x10u,
    /* U-King's .thrbss is 12 bytes (three pointer-sized TLS objects).  Keep
       one cache-line-sized copy for the main thread and each host-scheduled
       Cafe thread so __tls_get_addr never aliases state between workers. */
    GUEST_THREAD_TLS_BASE = WIIU_GUEST_OS_BASE + 0x0000B000u,
    GUEST_THREAD_TLS_SLOT_SIZE = 0x20u,
    GUEST_MAIN_TLS_SLOT = HOST_THREAD_LIMIT,
    WIIU_GUEST_SYSTEM_INFO = WIIU_GUEST_OS_BASE + 0x2000u,
    WIIU_OVERLAY_ARENA_BASE = WIIU_GUEST_OVERLAY_BASE,
    WIIU_OVERLAY_ARENA_SIZE = WIIU_GUEST_OVERLAY_SIZE,
    ERREULA_STATE_HIDDEN = 0u,
    ERREULA_STATE_APPEARING = 1u,
    ERREULA_STATE_VISIBLE = 2u,
    ERREULA_STATE_DISAPPEARING = 3u,
    ERREULA_SELECTION_NONE = 0xFFFFFFFFu,
    ERREULA_SELECTION_LEFT = 0u,
    ERREULA_SELECTION_RIGHT = 1u,
    ERREULA_RESULT_NONE = 0xFFFFFFFFu,
    ERREULA_FADE_MILLISECONDS = 80u,
};

enum { GUEST_DISPATCH_CYCLE_BUDGET = 8192u };

_Static_assert(GUEST_THREAD_CRT_BASE + HOST_THREAD_LIMIT * GUEST_THREAD_CRT_SLOT_SIZE <=
               GUEST_THREAD_TLS_BASE, "thread CRT slots overlap TLS");
_Static_assert(GUEST_THREAD_TLS_BASE + (HOST_THREAD_LIMIT + 1u) * GUEST_THREAD_TLS_SLOT_SIZE <=
               GUEST_THREAD_SPECIFIC_BASE, "thread TLS slots overlap specific data");
_Static_assert(GUEST_THREAD_SPECIFIC_BASE + HOST_THREAD_LIMIT * GUEST_THREAD_SPECIFIC_SLOT_SIZE <=
               AX_VOICE_GUEST_BASE, "thread-specific data overlaps audio state");

typedef struct {
    u32 header;
    u32 heap;
    u32 range_start;
    u32 range_end;
    u32 cursor;
} FrameworkBlock;

typedef struct {
    u32 parent_heap;
    u32 raw_start;
    u32 raw_end;
    u32 header;
    bool active;
    bool in_use;
} FrameworkAllocation;

typedef struct {
    u32 type;
    u32 handle;
    u32 data_start;
    u32 data_end;
    u32 head;
    u32 tail;
    bool frame_heap;
} HostBaseHeap;

typedef enum {
    NATIVE_YAZ0_HEADER,
    NATIVE_YAZ0_CODE,
    NATIVE_YAZ0_LITERAL,
    NATIVE_YAZ0_BACKREF_FIRST,
    NATIVE_YAZ0_BACKREF_SECOND,
    NATIVE_YAZ0_BACKREF_LENGTH,
    NATIVE_YAZ0_COPY,
    NATIVE_YAZ0_DONE,
    NATIVE_YAZ0_ERROR,
} NativeYaz0Stage;

typedef struct {
    bool in_use;
    bool complete;
    u32 stream_object;
    u32 destination;
    u32 output_size;
    u32 compressed_size;
    u32 chunk_size;
    u32 output_pos;
    u64 input_bytes;
    u8 header[16];
    u32 header_pos;
    u8 code;
    u8 bits_left;
    u8 backref_first;
    u32 backref_distance;
    u32 copy_remaining;
    NativeYaz0Stage stage;
} NativeYaz0Stream;

typedef struct {
    u32 object;
    u32 entry;
    u32 argc;
    u32 argv;
    u32 stack_top;
    u32 stack_size;
    u32 priority;
    u32 attributes;
    bool resumed;
    bool initialized;
    bool completed;
    bool blocked;
    bool waiting_to_send;
    u32 wait_queue;
    u32 wait_event;
    u32 wait_fast_mutex;
    u32 wait_semaphore;
    u32 wait_mutex;
    bool sleeping;
    u64 wake_tick;
    u32 ready_event;
    u32 wait_pc;
    u32 cpp_exception_init;
    u32 cpp_exception_globals;
    bool cpp_exception_initialized;
    bool tls_initialized;
    u32 thread_specific[64];
    u64 scheduled_blocks;
    CPUState cpu;
} HostThread;

typedef struct {
    bool allocated;
    bool mix_configured;
    bool sample_valid;
    u32 guest_address;
    u32 priority;
    u32 callback;
    u32 callback_ex;
    u32 user_context;
    u32 state;
    u16 format;
    u16 looping;
    u32 loop_offset;
    u32 end_offset;
    u32 current_offset;
    u32 data;
    u32 loop_count;
    float ratio;
    double source_fraction;
    s16 current_sample;
    u16 volume;
    s16 volume_delta;
    u16 channel_volume[AX_OUTPUT_CHANNELS];
    s16 channel_volume_delta[AX_OUTPUT_CHANNELS];
    s16 adpcm_coefficients[16];
    u16 adpcm_pred_scale;
    s16 adpcm_history[2];
    u16 loop_pred_scale;
    s16 loop_history[2];
} HostAXVoice;

typedef enum {
    AX_CALLBACK_IDLE,
    AX_CALLBACK_APP,
    AX_CALLBACK_FINAL_MIX,
} HostAXCallbackPhase;

typedef struct {
    bool active;
    HostAXCallbackPhase phase;
    u32 address;
    CPUState cpu;
} HostAXCallback;

typedef struct {
    bool active;
    bool periodic;
    u32 alarm;
    u32 handler;
    u64 next_tick;
    u64 period;
} HostAlarm;

typedef struct {
    bool active;
    u32 alarm;
    u32 handler;
    CPUState cpu;
} HostAlarmCallback;

typedef struct {
    bool active;
    bool inflating;
    u32 guest_address;
    z_stream stream;
} HostZlibStream;

static const RPXFile* g_rpx;
typedef struct {
    u32 address;
    bool valid;
    const RPXSymbol* symbol;
} ImportSymbolCache;
static ImportSymbolCache g_import_symbol_cache[16384];
/* A byte per 64 KiB address page rejects ordinary game PCs before cache
   collisions can cause a full import-table scan. Ranges are conservative. */
static u8 g_import_symbol_pages[65536];
static u32 g_logged_imports[IMPORT_LOG_LIMIT];
static u32 g_logged_import_count;
static u32 g_unknown_import_count;
static u32 g_null_callback_count;
static u32 g_boot_trace_count;
static u32 g_boot_history_address[BOOT_HISTORY_LIMIT];
static u32 g_boot_history_lr[BOOT_HISTORY_LIMIT];
static u32 g_boot_history_sp[BOOT_HISTORY_LIMIT];
static u32 g_boot_history_cursor;
static u32 g_boot_history_count;
/* Opt-in integration diagnostic: one ordinary GamePad A press after a chosen
   number of rendered-frame input samples. Never enabled by normal launches. */
static u32 g_test_start_sample;
static bool g_test_start_pro;
static bool g_pro_controller_input;
static u32 g_test_input_samples;
static u32 g_test_start_seen[65536];
static u32 g_test_start_new_paths;
static u32 g_test_start_animation;
static u32 g_test_animation_ticks;
static u32 g_test_start_screen;
static bool g_test_start_completed;
static u32 g_heap_cursor;
static u32 g_aux_heap_cursor;
static HostBaseHeap g_base_heaps[BASE_HEAP_COUNT];
static u32 g_framework_alloc_count;
static u32 g_framework_alias_count;
static u32 g_framework_registration_trace_count;
static u32 g_framework_adjust_count;
static u32 g_framework_exhaustion_count;
static u32 g_framework_warning_count;
static bool g_framework_root_initialized;
static u32 g_resource_warning_count;
static FrameworkBlock g_framework_blocks[FRAMEWORK_BLOCK_LIMIT];
static FrameworkAllocation
    g_framework_allocations[FRAMEWORK_ALLOCATION_LIMIT];
static u32 g_framework_block_count;
static u32 g_framework_allocation_count;
static u32 g_framework_allocation_high_water;
static u32 g_framework_allocation_reuse_hint;
static u32 g_framework_reusable_count;
static u32 g_framework_free_count;
static u32 g_framework_reuse_count;
static u32 g_skipped_copy_count;
static u32 g_yaz0_decompress_count;
static u32 g_large_yaz0_candidate_count;
static NativeYaz0Stream g_native_yaz0_streams[NATIVE_YAZ0_STREAM_LIMIT];
static u32 g_native_yaz0_stream_log_count;
static u32 g_native_yaz0_input_limit_log_count;
static u32 g_crc16_count;
static u32 g_thread_specific[64];
static HostThread g_host_threads[HOST_THREAD_LIMIT];
static HostThread* g_running_host_thread;
static bool g_main_sleeping;
static u64 g_main_wake_tick;
static u32 g_host_thread_count;
static u32 g_fast_mutexes[FAST_MUTEX_LIMIT];
static u32 g_fast_mutex_count;
static u32 g_thread_log_count;
static u32 g_thread_scheduler_log_count;
static u32 g_message_receive_log_count;
static u32 g_message_send_log_count;
static u32 g_message_trace_log_count;
static u32 g_core_handoff_log_count;
static u32 g_heap_query_log_count;
static u32 g_large_alloc_log_count;
static u32 g_resource_cleanup_log_count;
static u32 g_resource_cleanup_repeat_count;
static u32 g_resource_cleanup_last_owner;
static u32 g_resource_cleanup_last_head;
static u32 g_resource_cleanup_last_object;
static bool g_nn_act_initialized;
static bool g_nn_fp_initialized;
static bool g_nn_fp_logged_in;
static bool g_wpad_urcc_enabled;
static bool g_kpad_initialized;
static bool g_procui_initialized;
static bool g_ax_initialized;
static u64 g_ax_next_frame_tick;
static bool g_ax_frame_clock_started;
static u32 g_ax_final_mix_callbacks[3];
static u32 g_ax_app_frame_callback;
static u32 g_ax_frame_callback;
static u32 g_ax_default_mixer;
static u32 g_ax_device_modes[3];
static HostAXVoice g_ax_voices[AX_VOICE_LIMIT];
static HostAXCallback g_ax_callback;
static HostAlarm g_host_alarms[HOST_ALARM_LIMIT];
static HostAlarmCallback g_alarm_callback;
static HostZlibStream g_zlib_streams[HOST_ZLIB_STREAM_LIMIT];
static u32 g_alarm_log_count;
static s16 g_ax_mixed_samples[AX_MIX_FRAMES * 2u];
static u32 g_ax_audio_frame_count;
static u32 g_ax_voice_log_count;
static u32 g_ax_nonzero_mix_count;
static u16 g_ax_master_volume;
static u32 g_kpad_mpls_workarea;
static u32 g_dynload_alloc_fn;
static u32 g_dynload_free_fn;
static u32 g_dynload_tls_alloc_fn;
static u32 g_dynload_tls_free_fn;
static s32 g_gh_errno;
static u32 g_dynload_log_count;
static bool g_main_tls_initialized;
static bool g_overlay_arena_enabled;
static bool g_home_button_menu_enabled;
static bool g_erreula_created;
static bool g_erreula_home_nix_sign_visible;
static u32 g_erreula_state;
static u32 g_erreula_language;
static u32 g_erreula_controller_type;
static u32 g_erreula_button_selection;
static u32 g_erreula_result_code;
static u32 g_erreula_log_count;
static u32 g_erreula_appear_log_count;
static clock_t g_erreula_state_changed_at;
static u32 g_tree_guard_count;
static u32 g_default_heap_fallback_count;
static u32 g_list_repair_count;
static u32 g_null_buffer_guard_count;
static u32 g_parameter_lookup_context;
static u32 g_parameter_lookup_name;
static u32 g_parameter_lookup_fail_count;
static u32 g_format_guard_count;
static u32 g_native_format_count;
static u32 g_native_format_fallback_count;
static u32 g_native_format_log_count;
static u32 g_resource_name_warning_count;
static u32 g_static_array_log_count;
static u32 g_effect_list_log_count;
static u32 g_effect_fill_log_count;
static u32 g_effect_large_fill_log_count;
static u32 g_effect_selection_log_count;
static u32 g_effect_archive_log_count;
static u32 g_effect_big_archive_log_count;
static u32 g_resource_parse_trace_count;
static u32 g_thread_cpp_init_log_count;
static u32 g_allocator_trace_count;
static u32 g_allocator_overflow_count;
static u32 g_resource_dispatch_trace_count;
static u32 g_startup_string_null_callback_count;
static u32 g_metadata_string_repair_count;
static u32 g_metadata_string_trace_count;
static u32 g_null_resource_list_call_count;
static u32 g_title_lookup_context;
static u32 g_title_lookup_steps;
static u32 g_title_cleanup_context;
static u32 g_title_cleanup_steps;

static bool name_contains(const char* name, const char* needle);
static bool name_starts_with(const char* name, const char* prefix);
static bool repair_metadata_string_copy(CPUState* cpu);

static u32 thread_specific_fallback_address(u32 index) {
    if (!g_running_host_thread) {
        return GUEST_THREAD_SPECIFIC_GLOBAL_BASE +
               index * GUEST_THREAD_SPECIFIC_VALUE_SIZE;
    }

    ptrdiff_t thread_index = g_running_host_thread - g_host_threads;
    if (thread_index < 0 || thread_index >= HOST_THREAD_LIMIT) {
        return GUEST_THREAD_SPECIFIC_GLOBAL_BASE +
               index * GUEST_THREAD_SPECIFIC_VALUE_SIZE;
    }

    return GUEST_THREAD_SPECIFIC_BASE +
           (u32)thread_index * GUEST_THREAD_SPECIFIC_SLOT_SIZE +
           index * GUEST_THREAD_SPECIFIC_VALUE_SIZE;
}

void wiiu_imports_init_cpu_context(CPUState* cpu) {
    cpu->instruction_fallback = wiiu_instruction_fallback;
    memset(cpu->gqr, 0, sizeof(cpu->gqr));
    cpu->gqr[2] = 0x00040004u; /* unsigned byte */
    cpu->gqr[3] = 0x00050005u; /* unsigned halfword */
    cpu->gqr[4] = 0x00060006u; /* signed byte */
    cpu->gqr[5] = 0x00070007u; /* signed halfword */
    cpu->fpscr = 4u;
}

void wiiu_imports_attach_rpx(const RPXFile* rpx) {
    g_rpx = rpx;
    memset(g_import_symbol_cache, 0, sizeof(g_import_symbol_cache));
    memset(g_import_symbol_pages, 0, sizeof(g_import_symbol_pages));
    if (rpx) for (u32 i = 0; i < rpx->symbol_count; ++i) {
        const RPXSymbol* sym = &rpx->symbols[i];
        u64 end = (u64)sym->value + (sym->size ? sym->size - 1u : 0u);
        if (end > UINT32_MAX) end = UINT32_MAX;
        u32 first = sym->value >> 16, last = (u32)end >> 16;
        memset(g_import_symbol_pages + first, 1, last - first + 1u);
    }
    /* Diagnostic: the post-loader title path reaches these two relocation
       thunks.  Report their imported targets once so their HLE behavior can
       be verified without waiting for the entire title bootstrap. */
    if (rpx) {
        const u32 probe_addresses[] = { 0x0434C258u, 0x0434C268u };
        for (u32 probe = 0; probe < sizeof(probe_addresses) /
                                      sizeof(probe_addresses[0]); probe++) {
            const RPXSymbol* sym = wiiu_imports_find_symbol(probe_addresses[probe]);
            fprintf(stderr, "coreinit: import probe 0x%08X -> %s\n",
                    probe_addresses[probe], sym ? sym->name : "<unknown>");
        }
    }
}

void wiiu_imports_set_title_paths(const char* base_title_dir,
                                  const char* update_title_dir,
                                  const char* dlc_title_dir) {
    wiiu_filesystem_set_title_paths(base_title_dir, update_title_dir,
                                    dlc_title_dir);
}

void wiiu_imports_shutdown(void) {
    u32 active_allocations = 0;
    u32 live_allocations = 0;
    for (u32 i = 0; i < FRAMEWORK_ALLOCATION_LIMIT; i++) {
        if (!g_framework_allocations[i].active)
            continue;
        active_allocations++;
        if (g_framework_allocations[i].in_use)
            live_allocations++;
    }
    fprintf(stderr,
            "shim: allocator summary allocations=%u frees=%u reuses=%u "
            "tracked=%u live=%u heaps=%u\n",
            g_framework_allocation_count, g_framework_free_count,
            g_framework_reuse_count, active_allocations, live_allocations,
            g_framework_block_count);
    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        fprintf(stderr,
                "coreinit: thread summary object=0x%08X entry=0x%08X "
                "argv=0x%08X resumed=%u started=%u completed=%u "
                "blocked=%u pc=0x%08X blocks=%llu\n",
                thread->object, thread->entry, thread->argv,
                thread->resumed ? 1u : 0u, thread->initialized ? 1u : 0u,
                thread->completed ? 1u : 0u,
                thread->blocked ? 1u : 0u,
                thread->initialized ? thread->cpu.pc : thread->entry,
                (unsigned long long)thread->scheduled_blocks);
    }
    fprintf(stderr,
            "shim: native formatter calls=%u fallbacks=%u\n",
            g_native_format_count, g_native_format_fallback_count);
    fprintf(stderr,
            "audio: frames=%u acquired_voices=%u audible_frames=%u\n",
            g_ax_audio_frame_count, g_ax_voice_log_count,
            g_ax_nonzero_mix_count);
    for (u32 i = 0; i < HOST_ZLIB_STREAM_LIMIT; i++) {
        HostZlibStream* stream = &g_zlib_streams[i];
        if (!stream->active)
            continue;
        if (stream->inflating)
            inflateEnd(&stream->stream);
        else
            deflateEnd(&stream->stream);
        memset(stream, 0, sizeof(*stream));
    }
    wiiu_audio_shutdown();
    wiiu_filesystem_shutdown();
}

void wiiu_imports_reset_stats(void) {
    wiiu_filesystem_reset();
    wiiu_gx2_reset();
    memset(g_logged_imports, 0, sizeof(g_logged_imports));
    memset(g_boot_history_address, 0, sizeof(g_boot_history_address));
    memset(g_boot_history_lr, 0, sizeof(g_boot_history_lr));
    memset(g_boot_history_sp, 0, sizeof(g_boot_history_sp));
    memset(g_thread_specific, 0, sizeof(g_thread_specific));
    memset(g_host_threads, 0, sizeof(g_host_threads));
    memset(g_fast_mutexes, 0, sizeof(g_fast_mutexes));
    memset(g_framework_blocks, 0, sizeof(g_framework_blocks));
    memset(g_framework_allocations, 0, sizeof(g_framework_allocations));
    memset(g_native_yaz0_streams, 0, sizeof(g_native_yaz0_streams));
    memset(g_base_heaps, 0, sizeof(g_base_heaps));
    g_logged_import_count = 0;
    g_unknown_import_count = 0;
    g_null_callback_count = 0;
    g_boot_trace_count = 0;
    g_boot_history_cursor = 0;
    g_boot_history_count = 0;
    const char* test_start = getenv("BOTW_TEST_START_SAMPLE");
    g_test_start_sample = test_start ? (u32)strtoul(test_start, NULL, 0) : 0u;
    const char* test_pro = getenv("BOTW_TEST_START_PRO");
    g_test_start_pro = test_pro && strcmp(test_pro, "1") == 0;
    const char* controller = getenv("BOTW_CONTROLLER");
    g_pro_controller_input = !controller || strcmp(controller, "gamepad") != 0;
    g_test_input_samples = 0;
    g_test_start_new_paths = 0;
    g_test_start_animation = g_test_animation_ticks = 0;
    g_test_start_screen = 0;
    g_test_start_completed = false;
    memset(g_test_start_seen, 0, sizeof(g_test_start_seen));
    g_heap_cursor = WIIU_GUEST_HEAP_BASE + 0x10000u;
    g_aux_heap_cursor = WIIU_GUEST_AUX_HEAP_BASE;
    g_base_heaps[0] = (HostBaseHeap){
        1u, WIIU_GUEST_HEAP_BASE, WIIU_GUEST_HEAP_BASE + 0x10000u,
        WIIU_GUEST_HEAP_BASE + WIIU_GUEST_HEAP_SIZE,
        WIIU_GUEST_HEAP_BASE + 0x10000u,
        WIIU_GUEST_HEAP_BASE + WIIU_GUEST_HEAP_SIZE, false};
    g_base_heaps[1] = (HostBaseHeap){
        8u, WIIU_GUEST_FG_HEAP_BASE,
        WIIU_GUEST_FG_HEAP_BASE + 0x10000u,
        WIIU_GUEST_FG_HEAP_BASE + WIIU_GUEST_FG_HEAP_SIZE,
        WIIU_GUEST_FG_HEAP_BASE + 0x10000u,
        WIIU_GUEST_FG_HEAP_BASE + WIIU_GUEST_FG_HEAP_SIZE, true};
    g_base_heaps[2] = (HostBaseHeap){
        0u, WIIU_GUEST_MEM1_HEAP_BASE,
        WIIU_GUEST_MEM1_HEAP_BASE + 0x10000u,
        WIIU_GUEST_MEM1_HEAP_BASE + WIIU_GUEST_MEM1_HEAP_SIZE,
        WIIU_GUEST_MEM1_HEAP_BASE + 0x10000u,
        WIIU_GUEST_MEM1_HEAP_BASE + WIIU_GUEST_MEM1_HEAP_SIZE, true};
    g_framework_alloc_count = 0;
    g_framework_alias_count = 0;
    g_framework_registration_trace_count = 0;
    g_framework_adjust_count = 0;
    g_framework_exhaustion_count = 0;
    g_framework_warning_count = 0;
    g_framework_root_initialized = false;
    g_resource_warning_count = 0;
    g_framework_block_count = 0;
    g_framework_allocation_count = 0;
    g_framework_allocation_high_water = 0;
    g_framework_allocation_reuse_hint = 0;
    g_framework_reusable_count = 0;
    g_framework_free_count = 0;
    g_framework_reuse_count = 0;
    g_skipped_copy_count = 0;
    g_yaz0_decompress_count = 0;
    g_large_yaz0_candidate_count = 0;
    g_native_yaz0_stream_log_count = 0;
    g_native_yaz0_input_limit_log_count = 0;
    g_crc16_count = 0;
    g_host_thread_count = 0;
    g_fast_mutex_count = 0;
    g_running_host_thread = NULL;
    g_main_sleeping = false;
    g_main_wake_tick = 0u;
    g_thread_log_count = 0;
    g_thread_scheduler_log_count = 0;
    g_message_receive_log_count = 0;
    g_message_send_log_count = 0;
    g_message_trace_log_count = 0;
    g_core_handoff_log_count = 0;
    g_heap_query_log_count = 0;
    g_large_alloc_log_count = 0;
    g_resource_cleanup_log_count = 0;
    g_resource_cleanup_repeat_count = 0;
    g_resource_cleanup_last_owner = 0;
    g_resource_cleanup_last_head = 0;
    g_resource_cleanup_last_object = 0;
    g_nn_act_initialized = false;
    g_nn_fp_initialized = false;
    g_nn_fp_logged_in = false;
    g_wpad_urcc_enabled = false;
    g_kpad_initialized = false;
    g_procui_initialized = false;
    g_ax_initialized = false;
    g_ax_next_frame_tick = 0u;
    g_ax_frame_clock_started = false;
    memset(g_ax_final_mix_callbacks, 0,
           sizeof(g_ax_final_mix_callbacks));
    g_ax_app_frame_callback = 0;
    g_ax_frame_callback = 0;
    g_ax_default_mixer = 1u;
    memset(g_ax_device_modes, 0, sizeof(g_ax_device_modes));
    memset(g_ax_voices, 0, sizeof(g_ax_voices));
    memset(&g_ax_callback, 0, sizeof(g_ax_callback));
    memset(g_host_alarms, 0, sizeof(g_host_alarms));
    memset(&g_alarm_callback, 0, sizeof(g_alarm_callback));
    memset(g_zlib_streams, 0, sizeof(g_zlib_streams));
    g_alarm_log_count = 0u;
    memset(g_ax_mixed_samples, 0, sizeof(g_ax_mixed_samples));
    g_ax_audio_frame_count = 0u;
    g_ax_voice_log_count = 0u;
    g_ax_nonzero_mix_count = 0u;
    g_ax_master_volume = 0x8000u;
    g_kpad_mpls_workarea = 0;
    g_dynload_alloc_fn = 0;
    g_dynload_free_fn = 0;
    g_dynload_tls_alloc_fn = 0;
    g_dynload_tls_free_fn = 0;
    g_gh_errno = 0;
    g_dynload_log_count = 0;
    g_main_tls_initialized = false;
    g_overlay_arena_enabled = false;
    g_home_button_menu_enabled = true;
    g_erreula_created = false;
    g_erreula_home_nix_sign_visible = false;
    g_erreula_state = ERREULA_STATE_HIDDEN;
    g_erreula_language = 0u;
    g_erreula_controller_type = 0u;
    g_erreula_button_selection = ERREULA_SELECTION_NONE;
    g_erreula_result_code = ERREULA_RESULT_NONE;
    g_erreula_log_count = 0u;
    g_erreula_appear_log_count = 0u;
    g_erreula_state_changed_at = 0;
    g_tree_guard_count = 0;
    g_default_heap_fallback_count = 0;
    g_list_repair_count = 0;
    g_null_buffer_guard_count = 0;
    g_parameter_lookup_context = 0;
    g_parameter_lookup_name = 0;
    g_parameter_lookup_fail_count = 0;
    g_format_guard_count = 0;
    g_native_format_count = 0;
    g_native_format_fallback_count = 0;
    g_native_format_log_count = 0;
    g_resource_name_warning_count = 0;
    g_static_array_log_count = 0;
    g_effect_list_log_count = 0;
    g_effect_fill_log_count = 0;
    g_effect_large_fill_log_count = 0;
    g_effect_selection_log_count = 0;
    g_effect_archive_log_count = 0;
    g_effect_big_archive_log_count = 0;
    g_resource_parse_trace_count = 0;
    g_thread_cpp_init_log_count = 0;
    g_allocator_trace_count = 0;
    g_allocator_overflow_count = 0;
    g_resource_dispatch_trace_count = 0;
    g_startup_string_null_callback_count = 0;
    g_metadata_string_repair_count = 0;
    g_metadata_string_trace_count = 0;
    g_null_resource_list_call_count = 0;
    g_title_lookup_context = 0;
    g_title_lookup_steps = 0;
    g_title_cleanup_context = 0;
    g_title_cleanup_steps = 0;
}

u32 wiiu_imports_unknown_count(void) {
    return g_unknown_import_count;
}

void wiiu_imports_seed_data_imports(WiiUMemory* memory) {
    if (!g_rpx)
        return;

    const u32 coreinit_dimport_base = 0xC00052C0u;
    const u32 botw_dimport_alias_base = 0x0434D248u;
    u32 seeded = 0;
    for (u32 i = 0; i < g_rpx->symbol_count; i++) {
        const RPXSymbol* sym = &g_rpx->symbols[i];
        if (sym->section_index == RPX_SYMBOL_SECTION_ALIAS ||
            sym->name[0] == '.') {
            continue;
        }
        if (!name_contains(sym->name, "MEM") &&
            !name_contains(sym->name, "__atexit") &&
            !name_contains(sym->name, "__cpp_exception")) {
            continue;
        }

        WiiUMemorySegment* segment = wiiu_memory_find(memory, sym->value, 4);
        if (!segment || strncmp(segment->name, ".dimport_", 9) != 0)
            continue;

        write_be32(segment->data + (sym->value - segment->base), sym->value);
        seeded++;

        /* The v208 loader's relocated data-import arena mirrors these
           coreinit slots at 0x0434D248. Seed callable imports with their
           canonical C000 address so indirect guest calls reach the normal
           symbol-based HLE dispatcher. */
        if (sym->value >= coreinit_dimport_base &&
            sym->value - coreinit_dimport_base < 0x48u) {
            u32 alias = botw_dimport_alias_base +
                        (sym->value - coreinit_dimport_base);
            WiiUMemorySegment* alias_segment =
                wiiu_memory_find(memory, alias, 4u);
            if (alias_segment && alias_segment->writable) {
                write_be32(alias_segment->data +
                               (alias - alias_segment->base),
                           sym->value);
                seeded++;
            }
        }
    }

    if (seeded != 0)
        printf("seeded %u data import slots\n", seeded);
}

const RPXSymbol* wiiu_imports_find_symbol(u32 address) {
    if (!g_rpx || !g_import_symbol_pages[address >> 16])
        return NULL;

    /* Every generated dispatch asks the host first, including ordinary game
       PCs. Cache misses too: rescanning all 921 symbols for every loop/call
       adds work without executing a single guest instruction. The RPX symbol
       table is immutable while attached; attach invalidates all pointers. */
    ImportSymbolCache* cached =
        &g_import_symbol_cache[((address >> 2) ^ (address >> 16)) & 16383u];
    if (cached->valid && cached->address == address) return cached->symbol;
    const RPXSymbol* ranged = NULL;
    for (u32 i = 0; i < g_rpx->symbol_count; i++) {
        const RPXSymbol* sym = &g_rpx->symbols[i];
        if (sym->value == address) { ranged = sym; break; }
        if (!ranged && sym->size != 0 && address >= sym->value &&
            address - sym->value < sym->size) {
            ranged = sym;
        }
    }

    cached->address = address;
    cached->symbol = ranged;
    cached->valid = true;
    return ranged;
}

const RPXSymbol* wiiu_imports_find_symbol_name(const char* name) {
    if (!g_rpx || !name)
        return NULL;

    for (u32 i = 0; i < g_rpx->symbol_count; i++) {
        const RPXSymbol* sym = &g_rpx->symbols[i];
        if (strcmp(sym->name, name) == 0)
            return sym;
    }

    return NULL;
}

static bool name_contains(const char* name, const char* needle) {
    return name && strstr(name, needle) != NULL;
}

static bool name_equals(const char* name, const char* expected) {
    return name && expected && strcmp(name, expected) == 0;
}

static bool name_starts_with(const char* name, const char* prefix) {
    return name && prefix && strncmp(name, prefix, strlen(prefix)) == 0;
}

static void log_import_once(u32 address, const char* name, bool handled) {
    for (u32 i = 0; i < g_logged_import_count; i++) {
        if (g_logged_imports[i] == address)
            return;
    }

    if (g_logged_import_count < IMPORT_LOG_LIMIT)
        g_logged_imports[g_logged_import_count++] = address;

    printf("%s import 0x%08X %s\n", handled ? "handled" : "stubbed",
           address, name ? name : "<unknown>");
}

static void return_to_lr(CPUState* cpu) {
    cpu->pc = cpu->lr & ~3u;
}

static bool guest_copy_range_ok(u32 address, u32 size) {
    if (size == 0)
        return true;
    if (address + size < address)
        return false;
    if (address < 0x10000u && size > 0x1000u)
        return false;
    return size <= 0x08000000u;
}

static void guest_copy(CPUState* cpu, u32 dst, u32 src, u32 size) {
    if (!guest_copy_range_ok(dst, size) || !guest_copy_range_ok(src, size)) {
        if (g_skipped_copy_count++ < 16) {
            fprintf(stderr,
                    "warn: skipped invalid guest copy dst=0x%08X "
                    "src=0x%08X size=0x%08X pc=0x%08X lr=0x%08X\n",
                    dst, src, size, cpu->pc, cpu->lr);
        }
        return;
    }

    if (dst < src) {
        for (u32 i = 0; i < size; i++)
            mem_write8(cpu, dst + i, mem_read8(cpu, src + i));
    } else {
        for (u32 i = size; i > 0; i--)
            mem_write8(cpu, dst + i - 1u, mem_read8(cpu, src + i - 1u));
    }
}

static void guest_set(CPUState* cpu, u32 dst, u8 value, u32 size) {
    for (u32 i = 0; i < size; i++)
        mem_write8(cpu, dst + i, value);
}

static bool guest_range_mapped(CPUState* cpu, u32 address, u32 size) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    return address != 0 && size != 0 && memory &&
           wiiu_memory_find(memory, address, size) != NULL;
}

static u32 guest_thread_tls_address(CPUState* cpu, u32 tls_index) {
    if (!guest_range_mapped(cpu, tls_index, 8u))
        return 0u;

    /* Green Hills TLS_Index is {u16 reserved, u16 module, u32 offset}.
       The standalone loader relocates U-King as module zero because it is
       the only loaded TLS module. */
    u32 module = mem_read16(cpu, tls_index + 2u);
    u32 offset = mem_read32(cpu, tls_index + 4u);
    if (module != 0u || offset >= GUEST_THREAD_TLS_SLOT_SIZE)
        return 0u;

    u32 slot = GUEST_MAIN_TLS_SLOT;
    bool* initialized = &g_main_tls_initialized;
    if (g_running_host_thread) {
        slot = (u32)(g_running_host_thread - g_host_threads);
        initialized = &g_running_host_thread->tls_initialized;
    }

    u32 base = GUEST_THREAD_TLS_BASE + slot * GUEST_THREAD_TLS_SLOT_SIZE;
    if (!guest_range_mapped(cpu, base, GUEST_THREAD_TLS_SLOT_SIZE))
        return 0u;

    if (!*initialized) {
        guest_set(cpu, base, 0u, GUEST_THREAD_TLS_SLOT_SIZE);
        if (g_rpx) {
            for (u32 i = 0; i < g_rpx->load_section_count; i++) {
                const RPXLoadSection* section = &g_rpx->load_sections[i];
                if (strcmp(section->name, ".thrbss") != 0 ||
                    !section->data) {
                    continue;
                }
                u32 size = section->size < GUEST_THREAD_TLS_SLOT_SIZE
                               ? section->size
                               : GUEST_THREAD_TLS_SLOT_SIZE;
                for (u32 byte = 0; byte < size; byte++)
                    mem_write8(cpu, base + byte, section->data[byte]);
                break;
            }
        }
        *initialized = true;
        fprintf(stderr,
                "coreinit: initialized TLS slot=%u base=0x%08X thread=0x%08X\n",
                slot, base,
                g_running_host_thread ? g_running_host_thread->object
                                      : WIIU_GUEST_THREAD);
    }

    return base + offset;
}

static void initialize_framework_root(CPUState* cpu) {
    const u32 heap = 0x18012178u;
    const u32 range_start = WIIU_GUEST_HEAP_BASE + 0x10000u;
    const u32 range_size = WIIU_GUEST_HEAP_SIZE - 0x10000u;

    if (g_framework_root_initialized ||
        !guest_range_mapped(cpu, heap, 0xB8u)) {
        return;
    }

    /* Match the game ExpHeap constructor used for child framework heaps. */
    guest_set(cpu, heap, 0, 0xB8u);
    mem_write32(cpu, heap + 12u, 0x10347FB4u);
    mem_write32(cpu, heap + 20u, 0x103481C8u);
    mem_write32(cpu, heap + 24u, 0x103481F8u);
    mem_write32(cpu, heap + 28u, range_start);
    mem_write32(cpu, heap + 32u, range_size);
    mem_write32(cpu, heap + 40u, heap + 40u);
    mem_write32(cpu, heap + 44u, heap + 40u);
    mem_write32(cpu, heap + 52u, 56u);
    mem_write32(cpu, heap + 64u, heap + 64u);
    mem_write32(cpu, heap + 68u, heap + 64u);
    mem_write32(cpu, heap + 76u, 4u);
    mem_write32(cpu, heap + 80u, 1u);
    mem_write32(cpu, heap + 144u, 4u);
    mem_write32(cpu, heap + 152u, heap + 152u);
    mem_write32(cpu, heap + 156u, heap + 152u);
    mem_write32(cpu, heap + 168u, heap + 168u);
    mem_write32(cpu, heap + 172u, heap + 168u);
    g_framework_root_initialized = true;

    fprintf(stderr,
            "shim: initialized framework root heap=0x%08X "
            "range=0x%08X-0x%08X vtable=0x10347FB4\n",
            heap, range_start, range_start + range_size);
}

static u32 cpp_exception_init_callback(CPUState* cpu) {
    const RPXSymbol* slot =
        wiiu_imports_find_symbol_name("__cpp_exception_init_ptr");
    if (!slot || !guest_range_mapped(cpu, slot->value, 4u))
        return 0u;

    u32 callback = mem_read32(cpu, slot->value);
    if (callback == 0u || callback == slot->value ||
        callback >= 0xC0000000u || !guest_range_mapped(cpu, callback, 4u)) {
        return 0u;
    }
    return callback;
}

static u32 guest_strlen(CPUState* cpu, u32 str) {
    for (u32 i = 0; i < STRING_READ_LIMIT; i++) {
        if (mem_read8(cpu, str + i) == 0)
            return i;
    }

    return STRING_READ_LIMIT;
}

static s32 guest_strcmp(CPUState* cpu, u32 a, u32 b, u32 max_len) {
    for (u32 i = 0; i < max_len; i++) {
        u8 ca = mem_read8(cpu, a + i);
        u8 cb = mem_read8(cpu, b + i);
        if (ca != cb)
            return (s32)ca - (s32)cb;
        if (ca == 0)
            return 0;
    }

    return 0;
}

static bool guest_string_contains(CPUState* cpu, u32 str, const char* needle) {
    if (str == 0 || !needle || needle[0] == 0)
        return false;

    size_t needle_len = strlen(needle);
    for (u32 i = 0; i < 512; i++) {
        u8 ch = mem_read8(cpu, str + i);
        if (ch == 0)
            return false;
        if (ch != (u8)needle[0])
            continue;

        bool matched = true;
        for (size_t j = 1; j < needle_len; j++) {
            u8 next = mem_read8(cpu, str + i + (u32)j);
            if (next != (u8)needle[j]) {
                matched = false;
                break;
            }
        }
        if (matched)
            return true;
    }

    return false;
}

static bool guest_string_equals(CPUState* cpu, u32 str,
                                const char* expected) {
    if (!cpu || str == 0u || !expected)
        return false;
    for (u32 i = 0u; expected[i] != 0; i++) {
        if (!guest_range_mapped(cpu, str + i, 1u) ||
            mem_read8(cpu, str + i) != (u8)expected[i]) {
            return false;
        }
    }
    size_t length = strlen(expected);
    return length <= 0xFFFFFFFFu &&
           guest_range_mapped(cpu, str + (u32)length, 1u) &&
           mem_read8(cpu, str + (u32)length) == 0u;
}

static void guest_print_string(CPUState* cpu, u32 str, FILE* out) {
    if (str == 0) {
        fputs("<null>", out);
        return;
    }

    for (u32 i = 0; i < 512; i++) {
        u8 ch = mem_read8(cpu, str + i);
        if (ch == 0)
            return;
        if (ch >= 32 && ch < 127)
            fputc((int)ch, out);
        else
            fputc('.', out);
    }
}

typedef struct {
    CPUState* cpu;
    u32 address;
    u8 gpr;
    u8 fpr;
    u32 overflow;
    u32 registers;
} GuestVaList;

typedef struct {
    CPUState* cpu;
    u32 destination;
    u32 capacity;
    u32 count;
} GuestFormatOutput;

static bool guest_va_open(CPUState* cpu, u32 address, GuestVaList* args) {
    if (!guest_range_mapped(cpu, address, 12u) || !args)
        return false;

    args->cpu = cpu;
    args->address = address;
    args->gpr = mem_read8(cpu, address + 0u);
    args->fpr = mem_read8(cpu, address + 1u);
    args->overflow = mem_read32(cpu, address + 4u);
    args->registers = mem_read32(cpu, address + 8u);
    return args->gpr <= 8u && args->fpr <= 8u;
}

static bool guest_va_read_u32(GuestVaList* args, u32* value) {
    u32 address;
    if (args->gpr < 8u) {
        address = args->registers + (u32)args->gpr * 4u;
        args->gpr++;
    } else {
        address = args->overflow;
        args->overflow += 4u;
    }
    if (!guest_range_mapped(args->cpu, address, 4u))
        return false;
    *value = mem_read32(args->cpu, address);
    return true;
}

static bool guest_va_read_u64(GuestVaList* args, u64* value) {
    u32 address = 0;
    u32 gpr = ((u32)args->gpr + 1u) & ~1u;
    if (gpr <= 6u) {
        address = args->registers + gpr * 4u;
        args->gpr = (u8)(gpr + 2u);
    } else {
        args->gpr = 8u;
        args->overflow = (args->overflow + 7u) & ~7u;
        address = args->overflow;
        args->overflow += 8u;
    }
    if (!guest_range_mapped(args->cpu, address, 8u))
        return false;
    *value = mem_read64(args->cpu, address);
    return true;
}

static bool guest_va_read_f64(GuestVaList* args, f64* value) {
    u32 address;
    if (args->fpr < 8u) {
        address = args->registers + 32u + (u32)args->fpr * 8u;
        args->fpr++;
    } else {
        args->overflow = (args->overflow + 7u) & ~7u;
        address = args->overflow;
        args->overflow += 8u;
    }
    if (!guest_range_mapped(args->cpu, address, 8u))
        return false;
    *value = dolrecomp_f64_from_bits(mem_read64(args->cpu, address));
    return true;
}

static void guest_va_commit(const GuestVaList* args) {
    mem_write8(args->cpu, args->address + 0u, args->gpr);
    mem_write8(args->cpu, args->address + 1u, args->fpr);
    mem_write32(args->cpu, args->address + 4u, args->overflow);
}

static bool guest_format_emit(GuestFormatOutput* output, char ch) {
    if (output->count == UINT32_MAX)
        return false;
    if (output->capacity != 0u &&
        output->count < output->capacity - 1u) {
        mem_write8(output->cpu, output->destination + output->count,
                   (u8)ch);
    }
    output->count++;
    return true;
}

static bool guest_format_emit_host(GuestFormatOutput* output,
                                   const char* text, u32 length) {
    for (u32 i = 0; i < length; i++) {
        if (!guest_format_emit(output, text[i]))
            return false;
    }
    return true;
}

static bool guest_format_emit_repeat(GuestFormatOutput* output, char ch,
                                     u32 count) {
    for (u32 i = 0; i < count; i++) {
        if (!guest_format_emit(output, ch))
            return false;
    }
    return true;
}

static bool native_format_validate(const char* format) {
    const char* cursor = format;
    while (*cursor != 0) {
        if (*cursor++ != '%')
            continue;
        if (*cursor == '%') {
            cursor++;
            continue;
        }

        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0') {
            cursor++;
        }
        if (*cursor == '*')
            return false;
        u32 width = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            width = width * 10u + (u32)(*cursor++ - '0');
            if (width > 400u)
                return false;
        }
        if (*cursor == '$')
            return false;

        if (*cursor == '.') {
            cursor++;
            if (*cursor == '*')
                return false;
            u32 precision = 0;
            while (*cursor >= '0' && *cursor <= '9') {
                precision = precision * 10u + (u32)(*cursor++ - '0');
                if (precision > 400u)
                    return false;
            }
        }

        bool long_value = false;
        if (*cursor == 'h') {
            cursor++;
            if (*cursor == 'h')
                cursor++;
        } else if (*cursor == 'l') {
            long_value = true;
            cursor++;
            if (*cursor == 'l')
                cursor++;
        } else if (*cursor == 'j' || *cursor == 'z' || *cursor == 't') {
            cursor++;
        } else if (*cursor == 'L') {
            return false;
        }

        char specifier = *cursor++;
        if (specifier == 0 ||
            strchr("diuoxXfFeEgGaAcspn", specifier) == NULL) {
            return false;
        }
        if (long_value && (specifier == 's' || specifier == 'c'))
            return false;
    }
    return true;
}

static u32 native_format_decimal(const char** cursor) {
    u32 value = 0;
    while (**cursor >= '0' && **cursor <= '9') {
        value = value * 10u + (u32)(**cursor - '0');
        (*cursor)++;
    }
    return value;
}

static bool native_format_emit_guest_string(GuestFormatOutput* output,
                                            u32 address, u32 width,
                                            bool precision_set,
                                            u32 precision, bool left) {
    static const char null_text[] = "(null)";
    u32 length = 0;
    if (address == 0u) {
        length = (u32)(sizeof(null_text) - 1u);
        if (precision_set && length > precision)
            length = precision;
    } else {
        if (!guest_range_mapped(output->cpu, address, 1u))
            return false;
        u32 limit = precision_set ? precision : STRING_READ_LIMIT;
        while (length < limit && mem_read8(output->cpu, address + length))
            length++;
    }

    u32 padding = width > length ? width - length : 0u;
    if (!left && !guest_format_emit_repeat(output, ' ', padding))
        return false;
    if (address == 0u) {
        if (!guest_format_emit_host(output, null_text, length))
            return false;
    } else {
        for (u32 i = 0; i < length; i++) {
            if (!guest_format_emit(output,
                                   (char)mem_read8(output->cpu,
                                                  address + i))) {
                return false;
            }
        }
    }
    return left ? guest_format_emit_repeat(output, ' ', padding) : true;
}

static bool native_format_emit_pointer(GuestFormatOutput* output, u32 value,
                                       u32 width, bool left, bool zero) {
    char pointer[16];
    int length = snprintf(pointer, sizeof(pointer), "0x%08X", value);
    if (length < 0 || (size_t)length >= sizeof(pointer))
        return false;
    u32 padding = width > (u32)length ? width - (u32)length : 0u;
    if (!left && !guest_format_emit_repeat(output, zero ? '0' : ' ', padding))
        return false;
    if (!guest_format_emit_host(output, pointer, (u32)length))
        return false;
    return left ? guest_format_emit_repeat(output, ' ', padding) : true;
}

static bool native_vsnprintf(CPUState* cpu, u32 destination, u32 capacity,
                             u32 format_address, u32 va_address,
                             s32* result) {
    enum { FORMAT_LIMIT = 4096, CONVERSION_LIMIT = 64, VALUE_LIMIT = 1024 };
    char format[FORMAT_LIMIT];
    if (!result || !guest_range_mapped(cpu, format_address, 1u) ||
        (capacity != 0u &&
         !guest_range_mapped(cpu, destination, capacity))) {
        return false;
    }

    u32 format_length = 0;
    while (format_length + 1u < FORMAT_LIMIT) {
        char ch = (char)mem_read8(cpu, format_address + format_length);
        format[format_length++] = ch;
        if (ch == 0)
            break;
    }
    if (format_length == 0u || format[format_length - 1u] != 0 ||
        !native_format_validate(format)) {
        return false;
    }

    GuestVaList args;
    if (!guest_va_open(cpu, va_address, &args))
        return false;

    GuestFormatOutput output = {cpu, destination, capacity, 0u};
    const char* cursor = format;
    while (*cursor != 0) {
        if (*cursor != '%') {
            if (!guest_format_emit(&output, *cursor++))
                return false;
            continue;
        }

        const char* conversion_start = cursor++;
        if (*cursor == '%') {
            cursor++;
            if (!guest_format_emit(&output, '%'))
                return false;
            continue;
        }

        bool left = false;
        bool zero = false;
        while (*cursor == '-' || *cursor == '+' || *cursor == ' ' ||
               *cursor == '#' || *cursor == '0') {
            left |= *cursor == '-';
            zero |= *cursor == '0';
            cursor++;
        }
        u32 width = native_format_decimal(&cursor);
        bool precision_set = false;
        u32 precision = 0;
        if (*cursor == '.') {
            precision_set = true;
            cursor++;
            precision = native_format_decimal(&cursor);
        }

        enum { LENGTH_DEFAULT, LENGTH_HH, LENGTH_H, LENGTH_L,
               LENGTH_LL, LENGTH_J, LENGTH_Z, LENGTH_T } length_type =
            LENGTH_DEFAULT;
        if (*cursor == 'h') {
            cursor++;
            length_type = LENGTH_H;
            if (*cursor == 'h') {
                cursor++;
                length_type = LENGTH_HH;
            }
        } else if (*cursor == 'l') {
            cursor++;
            length_type = LENGTH_L;
            if (*cursor == 'l') {
                cursor++;
                length_type = LENGTH_LL;
            }
        } else if (*cursor == 'j') {
            cursor++;
            length_type = LENGTH_J;
        } else if (*cursor == 'z') {
            cursor++;
            length_type = LENGTH_Z;
        } else if (*cursor == 't') {
            cursor++;
            length_type = LENGTH_T;
        }

        char specifier = *cursor++;
        if (specifier == 's') {
            u32 address;
            if (!guest_va_read_u32(&args, &address) ||
                !native_format_emit_guest_string(&output, address, width,
                                                 precision_set, precision,
                                                 left)) {
                return false;
            }
            continue;
        }
        if (specifier == 'c') {
            u32 value;
            if (!guest_va_read_u32(&args, &value))
                return false;
            u32 padding = width > 1u ? width - 1u : 0u;
            if ((!left && !guest_format_emit_repeat(&output, ' ', padding)) ||
                !guest_format_emit(&output, (char)value) ||
                (left && !guest_format_emit_repeat(&output, ' ', padding))) {
                return false;
            }
            continue;
        }
        if (specifier == 'p') {
            u32 value;
            if (!guest_va_read_u32(&args, &value) ||
                !native_format_emit_pointer(&output, value, width, left,
                                            zero)) {
                return false;
            }
            continue;
        }
        if (specifier == 'n') {
            u32 address;
            if (!guest_va_read_u32(&args, &address))
                return false;
            if (length_type == LENGTH_HH) {
                if (!guest_range_mapped(cpu, address, 1u))
                    return false;
                mem_write8(cpu, address, (u8)output.count);
            } else if (length_type == LENGTH_H) {
                if (!guest_range_mapped(cpu, address, 2u))
                    return false;
                mem_write16(cpu, address, (u16)output.count);
            } else if (length_type == LENGTH_LL ||
                       length_type == LENGTH_J) {
                if (!guest_range_mapped(cpu, address, 8u))
                    return false;
                mem_write64(cpu, address, (u64)output.count);
            } else {
                if (!guest_range_mapped(cpu, address, 4u))
                    return false;
                mem_write32(cpu, address, output.count);
            }
            continue;
        }

        size_t conversion_length = (size_t)(cursor - conversion_start);
        if (conversion_length + 1u > CONVERSION_LIMIT)
            return false;
        char conversion[CONVERSION_LIMIT];
        memcpy(conversion, conversion_start, conversion_length);
        conversion[conversion_length] = 0;

        char value_text[VALUE_LIMIT];
        int value_length = -1;
        if (strchr("fFeEgGaA", specifier) != NULL) {
            f64 value;
            if (!guest_va_read_f64(&args, &value))
                return false;
            value_length = snprintf(value_text, sizeof(value_text),
                                    conversion, value);
        } else if (length_type == LENGTH_LL ||
                   length_type == LENGTH_J) {
            u64 value;
            if (!guest_va_read_u64(&args, &value))
                return false;
            if (specifier == 'd' || specifier == 'i') {
                value_length = snprintf(value_text, sizeof(value_text),
                                        conversion, (long long)(s64)value);
            } else {
                value_length = snprintf(value_text, sizeof(value_text),
                                        conversion,
                                        (unsigned long long)value);
            }
        } else {
            u32 value;
            if (!guest_va_read_u32(&args, &value))
                return false;
            if (specifier == 'd' || specifier == 'i') {
                if (length_type == LENGTH_L) {
                    value_length = snprintf(value_text, sizeof(value_text),
                                            conversion, (long)(s32)value);
                } else if (length_type == LENGTH_T) {
                    value_length = snprintf(value_text, sizeof(value_text),
                                            conversion,
                                            (ptrdiff_t)(s32)value);
                } else {
                    value_length = snprintf(value_text, sizeof(value_text),
                                            conversion, (int)(s32)value);
                }
            } else if (length_type == LENGTH_L) {
                value_length = snprintf(value_text, sizeof(value_text),
                                        conversion, (unsigned long)value);
            } else if (length_type == LENGTH_Z) {
                value_length = snprintf(value_text, sizeof(value_text),
                                        conversion, (size_t)value);
            } else {
                value_length = snprintf(value_text, sizeof(value_text),
                                        conversion, (unsigned int)value);
            }
        }
        if (value_length < 0 || (size_t)value_length >= sizeof(value_text) ||
            !guest_format_emit_host(&output, value_text,
                                    (u32)value_length)) {
            return false;
        }
    }

    if (capacity != 0u) {
        u32 terminator = output.count < capacity ? output.count : capacity - 1u;
        mem_write8(cpu, destination + terminator, 0u);
    }
    if (output.count > INT_MAX)
        return false;
    guest_va_commit(&args);
    *result = (s32)output.count;
    return true;
}

static u32 safe_read32(CPUState* cpu, u32 address) {
    if (address == 0)
        return 0;
    return mem_read32(cpu, address);
}

static bool repair_sarc_name_table(CPUState* cpu, u32 object) {
    if (object == 0u || safe_read32(cpu, object + 48u) != 0u ||
        safe_read32(cpu, object + 60u) != 0u) {
        return false;
    }

    u32 base = safe_read32(cpu, object + 40u);
    u32 sfat = safe_read32(cpu, object + 44u);
    u32 nodes = safe_read32(cpu, object + 56u);
    if (base == 0u || sfat == 0u || nodes == 0u)
        return false;

    u32 node_count = mem_read16(cpu, sfat + 6u);
    if (node_count == 0u || node_count > 0x3FFFu)
        return false;

    u32 sfnt = nodes + node_count * 16u;
    if (sfnt < nodes || mem_read32(cpu, sfnt) != 0x53464E54u)
        return false;

    u32 sfnt_size = mem_read16(cpu, sfnt + 4u);
    u32 data_offset = mem_read32(cpu, base + 12u);
    u32 names = sfnt + sfnt_size;
    u32 data = base + data_offset;
    if (sfnt_size < 8u || names < sfnt || data < base || names > data)
        return false;

    mem_write32(cpu, object + 48u, names);
    mem_write32(cpu, object + 60u, data);
    fprintf(stderr,
            "shim: repaired SARC name table object=0x%08X nodes=%u "
            "sfnt=0x%08X names=0x%08X data=0x%08X\n",
            object, node_count, sfnt, names, data);
    return true;
}

static void trace_boot_call(CPUState* cpu, u32 address) {
    const char* name = NULL;

    switch (address) {
    case 0x026AFBA0u:
        name = "rpx_entry";
        break;
    case 0x022F7DE4u:
        name = "crt_main_bridge";
        break;
    case 0x022EA99Cu:
        name = "sead_start_main";
        break;
    case 0x022EAA90u:
        name = "create_game_app";
        break;
    case 0x022EB094u:
        name = "run_game_app";
        break;
    case 0x022EB194u:
        name = "destroy_game_app";
        break;
    case 0x02379DF0u:
        name = "make_framework_node";
        break;
    case 0x0248D670u:
        name = "link_framework_node";
        break;
    case 0x02484968u:
        name = "region_probe";
        break;
    case 0x025266ECu:
        name = "callback_list_step";
        break;
    case 0x02536590u:
        name = "read_region_setting";
        break;
    default:
        break;
    }

    if (!name || g_boot_trace_count >= 64)
        return;

    g_boot_trace_count++;
    fprintf(stderr,
            "boot trace: %-18s pc=0x%08X lr=0x%08X sp=0x%08X "
            "r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n",
            name, address, cpu->lr, cpu->gpr[1], cpu->gpr[3], cpu->gpr[4],
            cpu->gpr[5], cpu->gpr[6]);

    if (address == 0x022EAA90u || address == 0x022EB094u ||
        address == 0x022EB194u) {
        fprintf(stderr,
                "boot trace: app fields [10]=0x%08X [14]=0x%08X "
                "[18]=0x%08X\n",
                safe_read32(cpu, cpu->gpr[3] + 16u),
                safe_read32(cpu, cpu->gpr[3] + 20u),
                safe_read32(cpu, cpu->gpr[3] + 24u));
    }
}

static void remember_boot_call(CPUState* cpu, u32 address) {
    if (address == 0)
        return;

    if (g_boot_history_count != 0) {
        u32 last = (g_boot_history_cursor + BOOT_HISTORY_LIMIT - 1u) %
                   BOOT_HISTORY_LIMIT;
        if (g_boot_history_address[last] == address)
            return;
    }

    g_boot_history_address[g_boot_history_cursor] = address;
    g_boot_history_lr[g_boot_history_cursor] = cpu->lr;
    g_boot_history_sp[g_boot_history_cursor] = cpu->gpr[1];
    g_boot_history_cursor = (g_boot_history_cursor + 1u) % BOOT_HISTORY_LIMIT;
    if (g_boot_history_count < BOOT_HISTORY_LIMIT)
        g_boot_history_count++;
}

static void dump_boot_history(void) {
    fprintf(stderr, "boot history: most recent guest blocks:\n");
    for (u32 i = 0; i < g_boot_history_count; i++) {
        u32 idx = (g_boot_history_cursor + BOOT_HISTORY_LIMIT -
                   g_boot_history_count + i) % BOOT_HISTORY_LIMIT;
        fprintf(stderr, "  [%02u] pc=0x%08X lr=0x%08X sp=0x%08X\n", i,
                g_boot_history_address[idx], g_boot_history_lr[idx],
                g_boot_history_sp[idx]);
    }
}

static void trace_null_callback(CPUState* cpu) {
    if (g_null_callback_count >= 12)
        return;

    fprintf(stderr,
            "warn: null callback pc=0x%08X lr=0x%08X ctr=0x%08X "
            "sp=0x%08X r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X "
            "r28=0x%08X r29=0x%08X r30=0x%08X r31=0x%08X\n",
            cpu->pc, cpu->lr, cpu->ctr, cpu->gpr[1], cpu->gpr[3],
            cpu->gpr[4], cpu->gpr[5], cpu->gpr[6], cpu->gpr[28],
            cpu->gpr[29], cpu->gpr[30], cpu->gpr[31]);

    if (cpu->gpr[1] != 0) {
        fprintf(stderr,
                "warn: stack words +00=0x%08X +04=0x%08X +08=0x%08X "
                "+0C=0x%08X +10=0x%08X +14=0x%08X\n",
                mem_read32(cpu, cpu->gpr[1] + 0u),
                mem_read32(cpu, cpu->gpr[1] + 4u),
                mem_read32(cpu, cpu->gpr[1] + 8u),
                mem_read32(cpu, cpu->gpr[1] + 12u),
                mem_read32(cpu, cpu->gpr[1] + 16u),
                mem_read32(cpu, cpu->gpr[1] + 20u));
    }

    if (cpu->lr == 0)
        dump_boot_history();
}

static bool handle_memory_import(CPUState* cpu, const char* name) {
    if (name_contains(name, "OSGetMemBound")) {
        /* ABI verified against the bundled Cemu coreinit implementation:
           r3 selects MEM1/MEM2 and r4/r5 receive base and size. Report the
           corresponding regions owned by this runtime. */
        u32 base = 0u;
        u32 size = 0u;
        if (cpu->gpr[3] == 1u) {
            base = WIIU_GUEST_MEM1_HEAP_BASE;
            size = WIIU_GUEST_MEM1_HEAP_SIZE;
        } else if (cpu->gpr[3] == 2u) {
            base = WIIU_GUEST_HEAP_BASE;
            size = WIIU_GUEST_HEAP_SIZE;
        }
        if (cpu->gpr[4] != 0u && guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], base);
        if (cpu->gpr[5] != 0u && guest_range_mapped(cpu, cpu->gpr[5], 4u))
            mem_write32(cpu, cpu->gpr[5], size);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "memcpy") || name_contains(name, "memmove") ||
        name_contains(name, "OSBlockMove")) {
        repair_metadata_string_copy(cpu);
        guest_copy(cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "memset") || name_contains(name, "OSBlockSet")) {
        guest_set(cpu, cpu->gpr[3], (u8)cpu->gpr[4], cpu->gpr[5]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "strlen")) {
        cpu->gpr[3] = guest_strlen(cpu, cpu->gpr[3]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "strncmp")) {
        cpu->gpr[3] =
            (u32)guest_strcmp(cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "strcmp")) {
        cpu->gpr[3] = (u32)guest_strcmp(cpu, cpu->gpr[3], cpu->gpr[4],
                                        STRING_READ_LIMIT);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static bool is_leap_year(u32 year) {
    return (year % 4u == 0u && year % 100u != 0u) || year % 400u == 0u;
}

static u32 days_in_month(u32 year, u32 month) {
    static const u8 days[] = {31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
    if (month == 1u && is_leap_year(year))
        return 29u;
    return month < 12u ? days[month] : 31u;
}

static void ticks_to_calendar(CPUState* cpu, u64 ticks, u32 output) {
    const u64 timer_clock = 62156250ull;
    u64 remainder = ticks % timer_clock;
    u64 seconds = ticks / timer_clock;
    u64 days = seconds / 86400ull;
    u32 seconds_of_day = (u32)(seconds % 86400ull);
    u32 year = 2000u;

    while (days >= (is_leap_year(year) ? 366ull : 365ull)) {
        days -= is_leap_year(year) ? 366ull : 365ull;
        year++;
    }

    u32 year_day = (u32)days;
    u32 month = 0;
    while (month < 11u && days >= days_in_month(year, month)) {
        days -= days_in_month(year, month);
        month++;
    }

    u64 microseconds = remainder * 1000000ull / timer_clock;
    mem_write32(cpu, output + 0x00u, seconds_of_day % 60u);
    mem_write32(cpu, output + 0x04u, (seconds_of_day / 60u) % 60u);
    mem_write32(cpu, output + 0x08u, seconds_of_day / 3600u);
    mem_write32(cpu, output + 0x0Cu, (u32)days + 1u);
    mem_write32(cpu, output + 0x10u, month);
    mem_write32(cpu, output + 0x14u, year);
    mem_write32(cpu, output + 0x18u,
                (u32)((ticks / timer_clock / 86400ull + 6ull) % 7ull));
    mem_write32(cpu, output + 0x1Cu, year_day);
    mem_write32(cpu, output + 0x20u, (u32)(microseconds / 1000ull));
    mem_write32(cpu, output + 0x24u, (u32)(microseconds % 1000ull));
}

static bool handle_time_import(CPUState* cpu, const char* name) {
    if (name_contains(name, "OSTicksToCalendarTime")) {
        u64 ticks = ((u64)cpu->gpr[3] << 32) | cpu->gpr[4];
        if (guest_range_mapped(cpu, cpu->gpr[5], 0x28u))
            ticks_to_calendar(cpu, ticks, cpu->gpr[5]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSleepTicks")) {
        u64 ticks = ((u64)cpu->gpr[3] << 32) | cpu->gpr[4];
        log_import_once(cpu->pc, name, true);
        if (g_running_host_thread) {
            if (ticks != 0u) {
                /* Save the continuation, not this call: its arguments are
                   volatile and must not start a new sleep on wakeup. */
                g_running_host_thread->sleeping = true;
                g_running_host_thread->wake_tick = cpu->timebase + ticks;
                g_running_host_thread->blocked = true;
                g_running_host_thread->wait_pc = cpu->lr;
                cpu->gpr[3] = 0u;
                cpu->pc = HOST_THREAD_YIELD_ADDRESS;
                return true;
            }
        } else {
            if (!g_main_sleeping && ticks != 0u) {
                g_main_sleeping = true;
                g_main_wake_tick = cpu->timebase + ticks;
            }
            if (g_main_sleeping && (s64)(cpu->timebase - g_main_wake_tick) < 0)
                return true;
            g_main_sleeping = false;
        }
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetTick")) {
        cpu->gpr[3] = (u32)cpu->timebase;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetTime") ||
        name_contains(name, "OSGetSystemTime")) {
        cpu->gpr[3] = (u32)(cpu->timebase >> 32);
        cpu->gpr[4] = (u32)cpu->timebase;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static bool handle_filesystem_import(CPUState* cpu, const char* name) {
    u32 address = cpu->pc;
    if (!wiiu_filesystem_handle_import(cpu, name))
        return false;
    log_import_once(address, name, true);
    return true;
}

static bool handle_system_config_import(CPUState* cpu, const char* name) {
    if (name_contains(name, "MCP_Open") || name_contains(name, "UCOpen")) {
        cpu->gpr[3] = 1;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MCP_Close") || name_contains(name, "UCClose")) {
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MCP_GetSysProdSettings")) {
        if (cpu->gpr[4] != 0) {
            guest_set(cpu, cpu->gpr[4], 0, 0x46);
            mem_write32(cpu, cpu->gpr[4] + 0x00u, 2u);
            mem_write16(cpu, cpu->gpr[4] + 0x04u, 1u);
            mem_write32(cpu, cpu->gpr[4] + 0x08u, 2u);
            mem_write8(cpu, cpu->gpr[4] + 0x10u, 'N');
            mem_write8(cpu, cpu->gpr[4] + 0x11u, 'T');
            mem_write8(cpu, cpu->gpr[4] + 0x12u, 'S');
            mem_write8(cpu, cpu->gpr[4] + 0x13u, 'C');
            mem_write8(cpu, cpu->gpr[4] + 0x15u, 'U');
            mem_write8(cpu, cpu->gpr[4] + 0x16u, 'S');
        }
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "UCReadSysConfig")) {
        if (cpu->gpr[5] != 0) {
            u32 count = cpu->gpr[4] < 64u ? cpu->gpr[4] : 64u;
            for (u32 i = 0; i < count; i++) {
                u32 entry = cpu->gpr[5] + i * 0x54u;
                u32 data_type = mem_read32(cpu, entry + 0x44u);
                u32 data_size = mem_read32(cpu, entry + 0x4Cu);
                u32 output = mem_read32(cpu, entry + 0x50u);
                mem_write32(cpu, entry + 0x48u, 0u);
                if (output == 0 || data_size == 0)
                    continue;

                if (guest_string_contains(cpu, entry, "cafe.language")) {
                    mem_write32(cpu, output, 1u);
                } else if (guest_string_contains(cpu, entry,
                                                 "cafe.cntry_reg")) {
                    mem_write32(cpu, output, 49u);
                } else if (guest_string_contains(cpu, entry,
                                                 "cafe.initial_launch")) {
                    mem_write8(cpu, output, 2u);
                } else if (data_type == 1u) {
                    mem_write8(cpu, output, 0u);
                } else if (data_type == 2u) {
                    mem_write16(cpu, output, 0u);
                } else if (data_type == 3u || data_type == 4u ||
                           data_type == 5u) {
                    mem_write32(cpu, output, 0u);
                } else {
                    guest_set(cpu, output, 0,
                              data_size < 256u ? data_size : 256u);
                }
            }
        }
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static bool handle_logging_import(CPUState* cpu, const char* name) {
    if (name_contains(name, "OSReport") || name_contains(name, "OSConsole")) {
        fprintf(stderr, "guest %s: ", name ? name : "<log>");
        guest_print_string(cpu, cpu->gpr[3], stderr);
        fputc('\n', stderr);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static u32 abs_alignment(u32 alignment) {
    s32 signed_alignment = (s32)alignment;
    if (signed_alignment < 0)
        alignment = (u32)-signed_alignment;
    if (alignment == 0)
        alignment = 4;
    if ((alignment & (alignment - 1u)) != 0)
        alignment = 4;
    return alignment;
}

static u32 align_up_u32(u32 value, u32 alignment) {
    alignment = abs_alignment(alignment);
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static u32 align_down_u32(u32 value, u32 alignment) {
    alignment = abs_alignment(alignment);
    return value & ~(alignment - 1u);
}

static HostBaseHeap* find_base_heap_by_type(u32 type) {
    for (u32 i = 0; i < BASE_HEAP_COUNT; i++) {
        if (g_base_heaps[i].type == type)
            return &g_base_heaps[i];
    }
    return NULL;
}

static HostBaseHeap* find_base_heap_by_handle(u32 handle) {
    for (u32 i = 0; i < BASE_HEAP_COUNT; i++) {
        if (g_base_heaps[i].handle == handle)
            return &g_base_heaps[i];
    }
    return NULL;
}

static void sync_base_heap_header(CPUState* cpu, HostBaseHeap* heap) {
    if (!heap || !guest_range_mapped(cpu, heap->handle, 0x4Cu))
        return;

    mem_write32(cpu, heap->handle,
                heap->frame_heap ? 0x46524D48u : 0x45585048u);
    mem_write32(cpu, heap->handle + 0x18u, heap->data_start);
    mem_write32(cpu, heap->handle + 0x1Cu, heap->data_end);
    if (heap->frame_heap) {
        mem_write32(cpu, heap->handle + 0x40u, heap->head);
        mem_write32(cpu, heap->handle + 0x44u, heap->tail);
    }
}

static void sync_mem2_base_cursor(HostBaseHeap* heap) {
    if (heap && heap->handle == WIIU_GUEST_HEAP_BASE)
        heap->head = g_heap_cursor;
}

static u32 base_heap_allocatable(HostBaseHeap* heap, u32 alignment) {
    if (!heap)
        return 0;
    sync_mem2_base_cursor(heap);

    u32 align = abs_alignment(alignment);
    if ((s32)alignment < 0) {
        u32 tail = align_down_u32(heap->tail, align);
        return tail > heap->head ? tail - heap->head : 0u;
    }

    u32 head = align_up_u32(heap->head, align);
    return heap->tail > head ? heap->tail - head : 0u;
}

static u32 base_heap_alloc(CPUState* cpu, HostBaseHeap* heap, u32 size,
                           u32 alignment) {
    if (!heap || size == 0u)
        return 0;
    sync_mem2_base_cursor(heap);

    u32 align = abs_alignment(alignment);
    u32 result = 0;
    if ((s32)alignment < 0) {
        if (size <= heap->tail - heap->head) {
            u32 start = align_down_u32(heap->tail - size, align);
            if (start >= heap->head) {
                result = start;
                heap->tail = start;
            }
        }
    } else {
        u32 start = align_up_u32(heap->head, align);
        u32 end = start + size;
        if (end >= start && end <= heap->tail) {
            result = start;
            heap->head = align_up_u32(end, 4u);
        }
    }

    if (heap->handle == WIIU_GUEST_HEAP_BASE)
        g_heap_cursor = heap->head;
    sync_base_heap_header(cpu, heap);
    return result;
}

static void reset_base_frame_heap(CPUState* cpu, HostBaseHeap* heap,
                                  u32 mode) {
    if (!heap || !heap->frame_heap)
        return;
    if ((mode & 1u) != 0)
        heap->head = heap->data_start;
    if ((mode & 2u) != 0)
        heap->tail = heap->data_end;
    sync_base_heap_header(cpu, heap);
}

static u32 guest_bump_alloc(u32 size, u32 alignment) {
    u32 start = align_up_u32(g_heap_cursor, alignment);
    u32 end = start + size;
    if (end < start || end > WIIU_GUEST_HEAP_BASE + WIIU_GUEST_HEAP_SIZE)
        return 0;

    g_heap_cursor = align_up_u32(end, 4);
    return start;
}

static u32 guest_aux_bump_alloc(u32 size, u32 alignment) {
    u32 start = align_up_u32(g_aux_heap_cursor, alignment);
    u32 end = start + size;
    if (end < start || end > WIIU_GUEST_AUX_HEAP_BASE + WIIU_GUEST_AUX_HEAP_SIZE)
        return 0;

    g_aux_heap_cursor = align_up_u32(end, 4);
    return start;
}

/* The actor/resource metadata reader constructs a dynamic guest string at
   03ACEE20.  Its output is an array of 324-byte records.  On the standalone
   heap path the record-array allocation can be omitted even though the input
   records and strings are valid.  Materialize that one missing array before
   OSBlockMove executes so the generated code can continue its normal copy,
   terminator store, and later string methods. */
static bool repair_metadata_string_copy(CPUState* cpu) {
    if (!cpu || cpu->lr != 0x03ACEED8u || cpu->gpr[3] != 0u ||
        cpu->gpr[5] != UINT32_MAX)
        return false;

    u32 object = cpu->gpr[28];
    u32 source = cpu->gpr[4];
    /* 03ACEE20 saved the caller's nonvolatile registers at r1 + 28.  The
       source routine keeps its record-array state in r27 and its current
       record index in r30. */
    u32 state = safe_read32(cpu, cpu->gpr[1] + 36u);
    u32 record_index = safe_read32(cpu, cpu->gpr[1] + 48u);
    if (object == 0u && guest_range_mapped(cpu, state, 36u)) {
        u32 slot_count = safe_read32(cpu, state + 16u);
        u32 input_bytes = safe_read32(cpu, state + 4u);
        u32 input_count = input_bytes / 4u;
        if (slot_count < input_count)
            slot_count = input_count;
        if (slot_count <= record_index)
            slot_count = record_index + 1u;

        const u32 record_size = 324u;
        if (slot_count != 0u && slot_count <= 0x4000u &&
            slot_count <= UINT32_MAX / record_size) {
            u32 bytes = slot_count * record_size;
            u32 records = guest_aux_bump_alloc(bytes, 4u);
            if (records != 0u) {
                guest_set(cpu, records, 0u, bytes);
                mem_write32(cpu, state + 16u, slot_count);
                mem_write32(cpu, state + 20u, records);
                object = records + record_index * record_size;
                cpu->gpr[28] = object;
                if (g_metadata_string_repair_count++ < 16u) {
                    fprintf(stderr,
                            "metadata: restored record array state=0x%08X "
                            "records=0x%08X slots=%u index=%u\n",
                            state, records, slot_count, record_index);
                }
            }
        }
    }
    if (g_metadata_string_trace_count++ < 16u) {
        fprintf(stderr,
                "metadata: string frame object=0x%08X source=0x%08X "
                "state=0x%08X index=%u size=0x%08X "
                "object_words=[%08X,%08X,%08X,%08X,%08X,%08X] "
                "mapped=[%u,%u]\n",
                object, source, state, record_index, cpu->gpr[5],
                safe_read32(cpu, object),
                safe_read32(cpu, object + 4u), safe_read32(cpu, object + 8u),
                safe_read32(cpu, object + 12u),
                safe_read32(cpu, object + 16u),
                safe_read32(cpu, object + 20u),
                guest_range_mapped(cpu, object, 24u) ? 1u : 0u,
                guest_range_mapped(cpu, source, 1u) ? 1u : 0u);
    }
    if (!guest_range_mapped(cpu, object, 24u) ||
        !guest_range_mapped(cpu, source, 1u) ||
        mem_read32(cpu, object + 12u) != 0u ||
        mem_read32(cpu, object + 20u) != 0u)
        return false;

    u32 length = guest_strlen(cpu, source);
    if (length >= 0x10000u)
        return false;
    u32 buffer = guest_aux_bump_alloc(length + 1u, 4u);
    if (buffer == 0u)
        return false;

    mem_write32(cpu, object + 12u, buffer);
    mem_write32(cpu, object + 20u, length + 1u);
    cpu->gpr[3] = buffer;
    cpu->gpr[5] = length;
    cpu->gpr[25] = buffer;
    cpu->gpr[30] = length;
    if (g_metadata_string_repair_count++ < 16u) {
        fprintf(stderr,
                "metadata: repaired dynamic string object=0x%08X "
                "source=0x%08X length=%u buffer=0x%08X\n",
                object, source, length, buffer);
    }
    return true;
}

static u32 guest_default_alloc(u32 size, u32 alignment) {
    u32 result = guest_bump_alloc(size, alignment);
    if (result != 0)
        return result;

    result = guest_aux_bump_alloc(size, alignment);
    if (result != 0 && g_default_heap_fallback_count++ < 16u) {
        fprintf(stderr,
                "coreinit: default heap overflow size=0x%08X align=0x%08X "
                "result=0x%08X\n",
                size, alignment, result);
    }
    return result;
}

static FrameworkBlock* find_framework_block(u32 heap) {
    for (u32 i = 0; i < g_framework_block_count; i++) {
        if (g_framework_blocks[i].heap == heap)
            return &g_framework_blocks[i];
    }
    return NULL;
}

static FrameworkAllocation* find_framework_allocation(u32 address) {
    for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
        FrameworkAllocation* allocation = &g_framework_allocations[i];
        if (allocation->active && allocation->in_use &&
            (allocation->header == address ||
             allocation->header + 16u == address ||
             allocation->raw_start == address)) {
            return allocation;
        }
    }
    return NULL;
}

static FrameworkAllocation* find_framework_allocation_to_free(
    u32 parent_heap, u32 address) {
    for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
        FrameworkAllocation* allocation = &g_framework_allocations[i];
        if (allocation->active && allocation->in_use &&
            allocation->parent_heap == parent_heap &&
            (allocation->header == address ||
             allocation->header + 16u == address ||
             allocation->raw_start == address)) {
            return allocation;
        }
    }
    return NULL;
}

static FrameworkAllocation* find_reusable_framework_allocation(
    u32 parent_heap, u32 required_size) {
    if (g_framework_reusable_count == 0)
        return NULL;

    for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
        FrameworkAllocation* allocation = &g_framework_allocations[i];
        if (!allocation->active || allocation->in_use ||
            allocation->parent_heap != parent_heap ||
            allocation->raw_end < allocation->raw_start) {
            continue;
        }
        if (allocation->raw_end - allocation->raw_start >= required_size)
            return allocation;
    }
    return NULL;
}

static void retire_framework_allocation(FrameworkAllocation* allocation) {
    if (!allocation || !allocation->active)
        return;

    allocation->active = false;
    allocation->in_use = false;

    u32 index = (u32)(allocation - g_framework_allocations);
    if (index < g_framework_allocation_reuse_hint)
        g_framework_allocation_reuse_hint = index;
}

static FrameworkAllocation* register_framework_allocation(
    u32 parent_heap, u32 raw_start, u32 raw_end, u32 header) {
    FrameworkAllocation* allocation = NULL;

    // Rollback and coalescing leave retired slots behind. Reuse one before
    // growing the tracker so transient frame allocations cannot exhaust it.
    for (u32 i = g_framework_allocation_reuse_hint;
         i < g_framework_allocation_high_water; i++) {
        if (!g_framework_allocations[i].active) {
            allocation = &g_framework_allocations[i];
            g_framework_allocation_reuse_hint = i + 1u;
            break;
        }
    }
    if (!allocation)
        g_framework_allocation_reuse_hint = g_framework_allocation_high_water;

    if (!allocation &&
        g_framework_allocation_high_water < FRAMEWORK_ALLOCATION_LIMIT) {
        allocation =
            &g_framework_allocations[g_framework_allocation_high_water++];
    } else if (!allocation) {
        for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
            if (!g_framework_allocations[i].active) {
                allocation = &g_framework_allocations[i];
                g_framework_allocation_reuse_hint = i + 1u;
                break;
            }
        }
    }
    if (!allocation)
        return NULL;

    allocation->parent_heap = parent_heap;
    allocation->raw_start = raw_start;
    allocation->raw_end = raw_end;
    allocation->header = header;
    allocation->active = true;
    allocation->in_use = true;
    g_framework_allocation_count++;
    return allocation;
}

static FrameworkAllocation* coalesce_framework_allocation(
    FrameworkAllocation* allocation) {
    if (!allocation || !allocation->active || allocation->in_use)
        return allocation;

    bool merged;
    do {
        merged = false;
        for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
            FrameworkAllocation* other = &g_framework_allocations[i];
            if (other == allocation || !other->active || other->in_use ||
                other->parent_heap != allocation->parent_heap) {
                continue;
            }

            if (other->raw_end == allocation->raw_start) {
                allocation->raw_start = other->raw_start;
            } else if (allocation->raw_end == other->raw_start) {
                allocation->raw_end = other->raw_end;
            } else {
                continue;
            }

            retire_framework_allocation(other);
            if (g_framework_reusable_count != 0)
                g_framework_reusable_count--;
            merged = true;
            break;
        }
    } while (merged);

    return allocation;
}

static void rollback_framework_allocations(u32 parent_heap) {
    FrameworkBlock* parent = find_framework_block(parent_heap);
    u32* cursor = parent ? &parent->cursor : &g_aux_heap_cursor;

    for (;;) {
        FrameworkAllocation* tail = NULL;
        for (u32 i = 0; i < g_framework_allocation_high_water; i++) {
            FrameworkAllocation* allocation = &g_framework_allocations[i];
            if (!allocation->active || allocation->in_use ||
                allocation->raw_end != *cursor) {
                continue;
            }

            if (parent) {
                if (allocation->parent_heap != parent_heap)
                    continue;
            } else if (find_framework_block(allocation->parent_heap)) {
                continue;
            }

            tail = allocation;
            break;
        }

        if (!tail)
            break;
        *cursor = tail->raw_start;
        retire_framework_allocation(tail);
        if (g_framework_reusable_count != 0)
            g_framework_reusable_count--;
    }
}

static bool free_framework_allocation(CPUState* cpu, u32 parent_heap,
                                      u32 candidate) {
    FrameworkAllocation* allocation =
        find_framework_allocation_to_free(parent_heap, candidate);
    if (!allocation) {
        u32 base = safe_read32(cpu, parent_heap + 164u);
        u32 absolute = candidate + base;
        if (absolute >= candidate)
            allocation =
                find_framework_allocation_to_free(parent_heap, absolute);
    }
    if (!allocation)
        return false;

    u32 raw_start = allocation->raw_start;
    u32 raw_end = allocation->raw_end;
    allocation->in_use = false;
    g_framework_reusable_count++;
    g_framework_free_count++;
    if (g_framework_free_count <= 32u) {
        fprintf(stderr,
                "shim: framework free heap=0x%08X candidate=0x%08X "
                "range=0x%08X-0x%08X\n",
                parent_heap, candidate, raw_start, raw_end);
    }

    coalesce_framework_allocation(allocation);
    rollback_framework_allocations(parent_heap);
    return true;
}

static FrameworkBlock* register_framework_range(u32 header, u32 heap,
                                                u32 range_start,
                                                u32 range_end, u32 cursor) {
    if (!heap || range_start >= range_end)
        return NULL;

    FrameworkBlock* block = find_framework_block(heap);
    if (!block) {
        if (g_framework_block_count >= FRAMEWORK_BLOCK_LIMIT)
            return NULL;
        block = &g_framework_blocks[g_framework_block_count++];
    }

    block->header = header;
    block->heap = heap;
    block->range_start = range_start;
    block->range_end = range_end;
    block->cursor = align_up_u32(cursor, 4);
    if (block->cursor < range_start)
        block->cursor = range_start;
    if (block->cursor > range_end)
        block->cursor = range_end;
    return block;
}

static u32 framework_block_allocatable(const FrameworkBlock* block,
                                       u32 alignment) {
    if (!block)
        return 0u;

    u32 start = align_up_u32(block->cursor, abs_alignment(alignment));
    return block->range_end > start ? block->range_end - start : 0u;
}

static u32 framework_block_alloc(CPUState* cpu, FrameworkBlock* block,
                                 u32 size, u32 alignment) {
    if (!block)
        return 0u;

    if (size == 0u)
        size = 1u;
    size = align_up_u32(size, 4u);

    const u32 align = abs_alignment(alignment);
    if ((s32)alignment < 0) {
        if (block->range_end < size)
            return 0u;
        u32 start = align_down_u32(block->range_end - size, align);
        if (start < block->cursor)
            return 0u;
        block->range_end = start;
        guest_set(cpu, start, 0, size);
        return start;
    }

    u32 start = align_up_u32(block->cursor, align);
    u32 end = start + size;
    if (end < start || end > block->range_end)
        return 0u;

    block->cursor = align_up_u32(end, 4u);
    guest_set(cpu, start, 0, size);
    return start;
}

static void register_framework_heap_from_constructor_args(
    CPUState* cpu, u32 object, u32 allocation_base, u32 size, u32 direction,
    const char* stage) {
    FrameworkAllocation* allocation =
        find_framework_allocation(allocation_base);
    const bool trace = g_framework_registration_trace_count++ < 24u;

    if (trace) {
        fprintf(stderr,
                "shim: framework constructor object=0x%08X base=0x%08X "
                "size=0x%08X direction=0x%08X stage=%s header=0x%08X "
                "raw=0x%08X-0x%08X r3=0x%08X lr=0x%08X\n",
                object, allocation_base, size, direction, stage,
                allocation ? allocation->header : 0u,
                allocation ? allocation->raw_start : 0u,
                allocation ? allocation->raw_end : 0u, cpu->gpr[3], cpu->lr);
    }

    if (!allocation || size < 0xB8u) {
        if (trace)
            fprintf(stderr, "warn: framework constructor missing allocation\n");
        return;
    }

    u32 allocation_end = allocation_base + size;
    if (allocation_end < allocation_base || object < allocation_base ||
        object > allocation_end - 0xB8u) {
        if (trace)
            fprintf(stderr, "warn: framework constructor invalid object range\n");
        return;
    }

    u32 range_start;
    u32 range_end;
    if ((direction & 1u) != 0u) {
        if (object != allocation_base) {
            if (trace)
                fprintf(stderr,
                        "warn: framework constructor forward object/base mismatch\n");
            return;
        }
        range_start = allocation_base + 0xB8u;
        range_end = allocation_end;
    } else {
        if (object != allocation_end - 0xB8u) {
            if (trace)
                fprintf(stderr,
                        "warn: framework constructor reverse object/end mismatch\n");
            return;
        }
        range_start = allocation_base;
        range_end = object;
    }

    FrameworkBlock* heap = register_framework_range(
        allocation->header, object, range_start, range_end,
        range_start);
    if (!heap) {
        if (trace)
            fprintf(stderr, "warn: framework constructor range registration failed\n");
        return;
    }

    if (g_framework_alias_count < 16u ||
        (g_framework_alias_count % 64u) == 0u) {
        u32 vtable = safe_read32(cpu, object + 12u);
        fprintf(stderr,
                "shim: framework heap object=0x%08X direction=%u "
                "range=0x%08X-0x%08X vtable=0x%08X "
                "methods=[0x%08X,0x%08X,0x%08X]\n",
                object, direction, range_start, range_end, vtable,
                safe_read32(cpu, vtable + 52u), safe_read32(cpu, vtable + 124u),
                safe_read32(cpu, vtable + 132u));
    }
    g_framework_alias_count++;
}

static void register_constructed_framework_heap(CPUState* cpu, u32 object) {
    register_framework_heap_from_constructor_args(
        cpu, object, safe_read32(cpu, object + 28u),
        safe_read32(cpu, object + 32u), safe_read32(cpu, object + 80u),
        "post");
}

static u32 framework_remaining_header(CPUState* cpu, FrameworkBlock* block,
                                      u32 alignment) {
    if (!block)
        return 0;

    alignment = abs_alignment(alignment);
    u32 header = align_up_u32(block->cursor, 4);
    u64 limit = block->range_end;
    u64 overhead = (u64)alignment + 0x40u + FRAMEWORK_PARENT_RESERVE;
    if ((u64)header + overhead >= limit)
        return 0;

    u64 available = limit - header - overhead;
    if (available > 0xFFFFFFFFu)
        available = 0xFFFFFFFFu;

    guest_set(cpu, header, 0, 16);
    mem_write32(cpu, header + 8u, (u32)available);
    mem_write32(cpu, header + 12u, 0);
    return header;
}

/*
 * The game's ExpHeap vtable exposes this as its remaining allocatable size.
 * Shim heaps use host-side bump ranges, so asking the guest free-list for the
 * answer would inspect intentionally absent nodes and report zero.
 */
static u32 framework_remaining_capacity(const FrameworkBlock* block,
                                        u32 alignment) {
    alignment = abs_alignment(alignment);

    u64 cursor;
    u64 limit;
    if (block) {
        cursor = align_up_u32(block->cursor, 4u);
        limit = block->range_end;
    } else {
        cursor = align_up_u32(g_aux_heap_cursor, 4u);
        limit = (u64)WIIU_GUEST_AUX_HEAP_BASE + WIIU_GUEST_AUX_HEAP_SIZE;
    }

    const u64 overhead =
        (u64)alignment + 0x40u + FRAMEWORK_PARENT_RESERVE;
    if (cursor + overhead >= limit)
        return 0u;

    u64 available = limit - cursor - overhead;
    return available > 0xFFFFFFFFu ? 0xFFFFFFFFu : (u32)available;
}

static u32 framework_aux_remaining_header(CPUState* cpu, u32 alignment) {
    alignment = abs_alignment(alignment);
    u32 header = align_up_u32(g_aux_heap_cursor, 4);
    u64 limit =
        (u64)WIIU_GUEST_AUX_HEAP_BASE + WIIU_GUEST_AUX_HEAP_SIZE;
    u64 overhead = (u64)alignment + 0x40u + FRAMEWORK_PARENT_RESERVE;
    if ((u64)header + overhead >= limit)
        return 0;

    u64 available = limit - header - overhead;
    if (available > 0xFFFFFFFFu)
        available = 0xFFFFFFFFu;

    guest_set(cpu, header, 0, 16);
    mem_write32(cpu, header + 8u, (u32)available);
    mem_write32(cpu, header + 12u, 0);
    return header;
}

static void adjust_framework_heap(CPUState* cpu, FrameworkBlock* child) {
    if (!child)
        return;

    u32 parent_heap = safe_read32(cpu, child->heap + 36u);
    FrameworkBlock* parent = find_framework_block(parent_heap);
    if (!parent)
        return;

    u32 reclaimed_end = align_up_u32(child->cursor + 0x40u, 4);
    if (reclaimed_end < child->cursor || reclaimed_end >= child->range_end)
        return;

    u64 allocation_end_slop = (u64)child->range_end + 0x1000u;
    if (parent->cursor < child->range_end ||
        (u64)parent->cursor > allocation_end_slop) {
        return;
    }

    u32 old_parent_cursor = parent->cursor;
    u32 old_child_end = child->range_end;
    parent->cursor = reclaimed_end;
    child->range_end = reclaimed_end;

    if (g_framework_adjust_count < 16u ||
        (g_framework_adjust_count % 64u) == 0u) {
        fprintf(stderr,
                "shim: framework adjust heap=0x%08X parent=0x%08X "
                "child_end=0x%08X parent_cursor=0x%08X->0x%08X\n",
                child->heap, parent_heap, old_child_end, old_parent_cursor,
                reclaimed_end);
    }
    g_framework_adjust_count++;
}

static bool decode_yaz0_data(const u8* input, size_t input_size, u8* output,
                             u32 output_size) {
    if (!input || !output || input_size < 16u || input[0] != 'Y' ||
        input[1] != 'a' || input[2] != 'z' || input[3] != '0') {
        return false;
    }

    u32 declared_size = ((u32)input[4] << 24) | ((u32)input[5] << 16) |
                        ((u32)input[6] << 8) | input[7];
    if (declared_size != output_size)
        return false;

    size_t input_pos = 16u;
    size_t output_pos = 0u;
    u8 code = 0u;
    u32 bits_left = 0u;
    while (output_pos < output_size) {
        if (bits_left == 0u) {
            if (input_pos >= input_size)
                return false;
            code = input[input_pos++];
            bits_left = 8u;
        }

        if ((code & 0x80u) != 0u) {
            if (input_pos >= input_size)
                return false;
            output[output_pos++] = input[input_pos++];
        } else {
            if (input_pos + 1u >= input_size)
                return false;
            u8 first = input[input_pos++];
            u8 second = input[input_pos++];
            u32 distance = ((u32)(first & 0x0Fu) << 8) | second;
            u32 length = first >> 4;
            if (length == 0u) {
                if (input_pos >= input_size)
                    return false;
                length = (u32)input[input_pos++] + 0x12u;
            } else {
                length += 2u;
            }

            if ((size_t)distance + 1u > output_pos)
                return false;
            size_t count = length;
            if (count > output_size - output_pos) count = output_size - output_pos;
            size_t period = (size_t)distance + 1u;
            if (period == 1u) {
                memset(output + output_pos, output[output_pos - 1u], count);
                output_pos += count;
            } else {
                /* Each memcpy is non-overlapping. Doubling the available
                   prefix preserves Yaz0's repeating back-reference semantics. */
                while (count) {
                    size_t chunk = count < period ? count : period;
                    memcpy(output + output_pos, output + output_pos - period, chunk);
                    output_pos += chunk;
                    count -= chunk;
                    period += chunk;
                }
            }
        }

        code <<= 1;
        bits_left--;
    }
    return true;
}

/* U-King's checked entry takes (destination, capacity, source); its raw
   decoder takes (destination, source). Decode transactionally so malformed
   data or overlapping guest buffers cannot leave a half-decoded resource. */
static bool handle_botw_yaz0_decompress(CPUState* cpu, bool checked) {
    u32 destination = cpu->gpr[3];
    u32 source = cpu->gpr[checked ? 5 : 4];
    if (!guest_range_mapped(cpu, source, 16u))
        return false;
    if (mem_read32(cpu, source) != 0x59617A30u) {
        if (!checked) return false;
        cpu->gpr[3] = (u32)-1;
        return_to_lr(cpu);
        return true;
    }
    u32 size = mem_read32(cpu, source + 4u);
    if (checked && cpu->gpr[4] < size) {
        cpu->gpr[3] = (u32)-2;
        return_to_lr(cpu);
        return true;
    }
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* input_segment = wiiu_memory_find(memory, source, 16u);
    WiiUMemorySegment* output_segment = size ? wiiu_memory_find(memory, destination, size) : NULL;
    if (!size || !output_segment || !output_segment->writable)
        return false;
    u32 input_offset = wiiu_memory_canonical_address(source) - input_segment->base;
    u8* decoded = (u8*)malloc(size);
    if (!decoded) return false;
    bool success = decode_yaz0_data(input_segment->data + input_offset,
                                   input_segment->size - input_offset, decoded, size);
    if (success) {
        memcpy(output_segment->data + (wiiu_memory_canonical_address(destination) - output_segment->base), decoded, size);
        fprintf(stderr, "resource: BOTW Yaz0 source=%08X destination=%08X output=%u caller=%08X\n",
                source, destination, size, cpu->lr);
    }
    free(decoded);
    cpu->gpr[3] = success ? size : (u32)-2;
    return_to_lr(cpu);
    return true;
}

static bool handle_yaz0_decompress(CPUState* cpu) {
    const u32 destination = cpu->gpr[3];
    const u32 source = cpu->gpr[4];
    if (mem_read32(cpu, source) != 0x59617A30u)
        return false;

    const u32 output_size = mem_read32(cpu, source + 4u);
    if (output_size == 0 || destination + output_size < destination)
        return false;

    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* source_segment =
        memory ? wiiu_memory_find(memory, source, 16u) : NULL;
    WiiUMemorySegment* destination_segment =
        memory ? wiiu_memory_find(memory, destination, output_size) : NULL;
    if (!source_segment || !destination_segment ||
        !destination_segment->writable) {
        return false;
    }

    u8* input = source_segment->data +
                (wiiu_memory_canonical_address(source) -
                 source_segment->base);
    u8* input_end = source_segment->data + source_segment->size;
    u8* output =
        destination_segment->data +
        (wiiu_memory_canonical_address(destination) -
         destination_segment->base);
    if (!decode_yaz0_data(input, (size_t)(input_end - input), output,
                          output_size)) {
        return false;
    }

    if (output_size > 32u * 1024u * 1024u && output_size >= 0x20u) {
        u32 header_size = ((u32)output[4] << 8) | output[5];
        u32 sfat_size = 0u;
        u32 node_count = 0u;
        u32 sfnt_offset = 0u;
        if (header_size + 12u <= output_size) {
            sfat_size = ((u32)output[header_size + 4u] << 8) |
                        output[header_size + 5u];
            node_count = ((u32)output[header_size + 6u] << 8) |
                         output[header_size + 7u];
            sfnt_offset = header_size + sfat_size + node_count * 16u;
        }
        fprintf(stderr,
                "shim: large Yaz0 archive source=0x%08X destination=0x%08X "
                "size=%u header=0x%X sfat=0x%X nodes=%u sfnt=0x%X "
                "bytes=%02X%02X%02X%02X %02X%02X%02X%02X\n",
                source, destination, output_size, header_size, sfat_size,
                node_count, sfnt_offset,
                sfnt_offset + 7u < output_size ? output[sfnt_offset] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 1u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 2u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 3u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 4u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 5u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 6u] : 0u,
                sfnt_offset + 7u < output_size ? output[sfnt_offset + 7u] : 0u);
    }

    if (g_yaz0_decompress_count < 16u) {
        fprintf(stderr,
                "shim: Yaz0 decompressed source=0x%08X destination=0x%08X "
                "size=%u\n",
                source, destination, output_size);
    }
    g_yaz0_decompress_count++;
    cpu->gpr[3] = output_size;
    return_to_lr(cpu);
    return true;
}

static NativeYaz0Stream* find_native_yaz0_stream(u32 stream_object) {
    for (u32 i = 0; i < NATIVE_YAZ0_STREAM_LIMIT; i++) {
        NativeYaz0Stream* stream = &g_native_yaz0_streams[i];
        if (stream->in_use && stream->stream_object == stream_object)
            return stream;
    }

    return NULL;
}

static NativeYaz0Stream* prepare_native_yaz0_stream(
    u32 stream_object, u32 destination, u32 output_size,
    u32 compressed_size, u32 chunk_size) {
    NativeYaz0Stream* stream = find_native_yaz0_stream(stream_object);
    if (!stream) {
        for (u32 i = 0; i < NATIVE_YAZ0_STREAM_LIMIT; i++) {
            NativeYaz0Stream* candidate = &g_native_yaz0_streams[i];
            if (!candidate->in_use || candidate->complete ||
                candidate->stage == NATIVE_YAZ0_ERROR) {
                stream = candidate;
                break;
            }
        }
    }

    if (!stream)
        stream = &g_native_yaz0_streams[0];

    memset(stream, 0, sizeof(*stream));
    stream->in_use = true;
    stream->stream_object = stream_object;
    stream->destination = destination;
    stream->output_size = output_size;
    stream->compressed_size = compressed_size;
    stream->chunk_size = chunk_size;
    stream->stage = NATIVE_YAZ0_HEADER;

    if (g_native_yaz0_stream_log_count++ < 32u) {
        fprintf(stderr,
                "shim: queued native Yaz0 stream object=0x%08X "
                "destination=0x%08X output=%u compressed=%u chunk=%u\n",
                stream_object, destination, output_size, compressed_size,
                chunk_size);
    }
    return stream;
}

static void log_native_yaz0_archive(const NativeYaz0Stream* stream,
                                    const u8* output) {
    if (!stream || !output || stream->output_size < 0x20u)
        return;

    u32 header_size = ((u32)output[4] << 8) | output[5];
    u32 sfat_size = 0u;
    u32 node_count = 0u;
    u32 sfnt_offset = 0u;
    if (header_size + 12u <= stream->output_size) {
        sfat_size = ((u32)output[header_size + 4u] << 8) |
                    output[header_size + 5u];
        node_count = ((u32)output[header_size + 6u] << 8) |
                     output[header_size + 7u];
        u64 offset = (u64)header_size + sfat_size + (u64)node_count * 16u;
        if (offset <= 0xFFFFFFFFu)
            sfnt_offset = (u32)offset;
    }

    fprintf(stderr,
            "shim: native Yaz0 complete object=0x%08X destination=0x%08X "
            "output=%u input=%llu magic=%02X%02X%02X%02X "
            "header=0x%X sfat=0x%X nodes=%u sfnt=0x%X "
            "sfnt_magic=%02X%02X%02X%02X\n",
            stream->stream_object, stream->destination, stream->output_size,
            (unsigned long long)stream->input_bytes,
            output[0], output[1], output[2], output[3], header_size,
            sfat_size, node_count, sfnt_offset,
            sfnt_offset + 3u < stream->output_size ? output[sfnt_offset] : 0u,
            sfnt_offset + 3u < stream->output_size
                ? output[sfnt_offset + 1u]
                : 0u,
            sfnt_offset + 3u < stream->output_size
                ? output[sfnt_offset + 2u]
                : 0u,
            sfnt_offset + 3u < stream->output_size
                ? output[sfnt_offset + 3u]
                : 0u);

    /* This known boot archive is a useful end-of-stream integrity sentinel. */
    if (stream->destination == 0x324CC000u &&
        stream->output_size >= 0x00C68610u) {
        const u8* shader = output + 0x00C68600u;
        fprintf(stderr,
                "shim: AglResource shader header at 0x%08X "
                "bytes=%02X%02X%02X%02X %02X%02X%02X%02X "
                "%02X%02X%02X%02X %02X%02X%02X%02X\n",
                stream->destination + 0x00C68600u, shader[0], shader[1],
                shader[2], shader[3], shader[4], shader[5], shader[6],
                shader[7], shader[8], shader[9], shader[10], shader[11],
                shader[12], shader[13], shader[14], shader[15]);
    }
}

static bool complete_native_yaz0_from_file(CPUState* cpu,
                                           NativeYaz0Stream* stream) {
    if (!cpu || !stream || stream->complete || stream->compressed_size == 0u ||
        stream->output_size == 0u) {
        return false;
    }

    u8* input = NULL;
    size_t input_size = 0u;
    if (!wiiu_filesystem_read_last_file(stream->compressed_size, &input,
                                        &input_size)) {
        return false;
    }

    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* output_segment =
        memory ? wiiu_memory_find(memory, stream->destination,
                                  stream->output_size)
               : NULL;
    if (!output_segment || !output_segment->writable) {
        free(input);
        return false;
    }

    u8* output = output_segment->data +
                 (wiiu_memory_canonical_address(stream->destination) -
                  output_segment->base);
    bool decoded = decode_yaz0_data(input, input_size, output,
                                    stream->output_size);
    free(input);
    if (!decoded) {
        fprintf(stderr,
                "warn: native Yaz0 direct decode failed object=0x%08X "
                "destination=0x%08X input=%zu output=%u\n",
                stream->stream_object, stream->destination, input_size,
                stream->output_size);
        return false;
    }

    stream->input_bytes = input_size;
    stream->output_pos = stream->output_size;
    stream->stage = NATIVE_YAZ0_DONE;
    stream->complete = true;
    mem_write32(cpu, stream->stream_object,
                stream->destination + stream->output_size);
    mem_write32(cpu, stream->stream_object + 4u, 0u);
    mem_write8(cpu, stream->stream_object + 22u, 0u);
    log_native_yaz0_archive(stream, output);
    fprintf(stderr,
            "shim: native Yaz0 direct complete object=0x%08X "
            "compressed=%u output=%u\n",
            stream->stream_object, stream->compressed_size,
            stream->output_size);
    return true;
}

static bool handle_streaming_yaz0(CPUState* cpu) {
    const u32 stream_object = cpu->gpr[3];
    const u32 source = cpu->gpr[4];
    const u32 supplied_input_size = cpu->gpr[5];
    NativeYaz0Stream* stream = find_native_yaz0_stream(stream_object);
    if (stream && stream->complete) {
        mem_write32(cpu, stream_object,
                    stream->destination + stream->output_size);
        mem_write32(cpu, stream_object + 4u, 0u);
        mem_write8(cpu, stream_object + 22u, 0u);
        cpu->gpr[3] = 0u;
        return_to_lr(cpu);
        return true;
    }

    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* input_segment =
        memory && supplied_input_size != 0u
            ? wiiu_memory_find(memory, source, supplied_input_size)
            : NULL;
    if (!input_segment)
        return false;

    const u8* input = input_segment->data +
                      (wiiu_memory_canonical_address(source) -
                       input_segment->base);
    if (!stream) {
        if (supplied_input_size < 4u || input[0] != 'Y' || input[1] != 'a' ||
            input[2] != 'z' || input[3] != '0') {
            return false;
        }
        stream = prepare_native_yaz0_stream(
            stream_object, safe_read32(cpu, stream_object),
            safe_read32(cpu, stream_object + 8u), 0u, supplied_input_size);
    }

    u32 input_size = supplied_input_size;
    if (stream->compressed_size != 0u) {
        u32 remaining_input = 0u;
        if (stream->input_bytes < (u64)stream->compressed_size) {
            remaining_input = (u32)((u64)stream->compressed_size -
                                    stream->input_bytes);
        }
        if (input_size > remaining_input) {
            if (g_native_yaz0_input_limit_log_count++ < 16u) {
                fprintf(stderr,
                        "shim: limiting native Yaz0 input object=0x%08X "
                        "supplied=%u usable=%u consumed=%llu total=%u\n",
                        stream_object, input_size, remaining_input,
                        (unsigned long long)stream->input_bytes,
                        stream->compressed_size);
            }
            input_size = remaining_input;
        }
    }

    WiiUMemorySegment* output_segment =
        memory && stream->output_size != 0u
            ? wiiu_memory_find(memory, stream->destination,
                               stream->output_size)
            : NULL;
    if (!output_segment || !output_segment->writable) {
        stream->stage = NATIVE_YAZ0_ERROR;
    }

    u8* output = output_segment
                     ? output_segment->data +
                           (wiiu_memory_canonical_address(
                                stream->destination) -
                            output_segment->base)
                     : NULL;
    u32 input_pos = 0u;
    while (stream->stage != NATIVE_YAZ0_DONE &&
           stream->stage != NATIVE_YAZ0_ERROR) {
        if (stream->output_pos == stream->output_size) {
            stream->stage = NATIVE_YAZ0_DONE;
            stream->complete = true;
            break;
        }

        if (stream->stage == NATIVE_YAZ0_HEADER) {
            while (stream->header_pos < sizeof(stream->header) &&
                   input_pos < input_size) {
                stream->header[stream->header_pos++] = input[input_pos++];
            }
            if (stream->header_pos != sizeof(stream->header))
                break;

            u32 declared_size = ((u32)stream->header[4] << 24) |
                                ((u32)stream->header[5] << 16) |
                                ((u32)stream->header[6] << 8) |
                                stream->header[7];
            if (stream->header[0] != 'Y' || stream->header[1] != 'a' ||
                stream->header[2] != 'z' || stream->header[3] != '0' ||
                declared_size != stream->output_size) {
                fprintf(stderr,
                        "error: invalid native Yaz0 header object=0x%08X "
                        "declared=%u expected=%u\n",
                        stream_object, declared_size, stream->output_size);
                stream->stage = NATIVE_YAZ0_ERROR;
                break;
            }
            stream->stage = NATIVE_YAZ0_CODE;
            continue;
        }

        if (stream->stage == NATIVE_YAZ0_COPY) {
            if (stream->backref_distance + 1u > stream->output_pos) {
                fprintf(stderr,
                        "error: invalid native Yaz0 back-reference "
                        "object=0x%08X output=%u distance=%u\n",
                        stream_object, stream->output_pos,
                        stream->backref_distance);
                stream->stage = NATIVE_YAZ0_ERROR;
                break;
            }
            while (stream->copy_remaining != 0u &&
                   stream->output_pos < stream->output_size) {
                u32 copy_pos = stream->output_pos -
                               stream->backref_distance - 1u;
                output[stream->output_pos++] = output[copy_pos];
                stream->copy_remaining--;
            }
            if (stream->output_pos == stream->output_size) {
                stream->stage = NATIVE_YAZ0_DONE;
                stream->complete = true;
            } else if (stream->copy_remaining == 0u) {
                stream->stage = NATIVE_YAZ0_CODE;
            }
            continue;
        }

        if (input_pos == input_size)
            break;

        switch (stream->stage) {
        case NATIVE_YAZ0_CODE:
            if (stream->bits_left == 0u) {
                stream->code = input[input_pos++];
                stream->bits_left = 8u;
                if (input_pos == input_size)
                    break;
            }
            if ((stream->code & 0x80u) != 0u)
                stream->stage = NATIVE_YAZ0_LITERAL;
            else
                stream->stage = NATIVE_YAZ0_BACKREF_FIRST;
            stream->code <<= 1;
            stream->bits_left--;
            break;

        case NATIVE_YAZ0_LITERAL:
            output[stream->output_pos++] = input[input_pos++];
            stream->stage = NATIVE_YAZ0_CODE;
            break;

        case NATIVE_YAZ0_BACKREF_FIRST:
            stream->backref_first = input[input_pos++];
            stream->stage = NATIVE_YAZ0_BACKREF_SECOND;
            break;

        case NATIVE_YAZ0_BACKREF_SECOND: {
            u8 second = input[input_pos++];
            stream->backref_distance =
                ((u32)(stream->backref_first & 0x0Fu) << 8) | second;
            u32 length = stream->backref_first >> 4;
            if (length == 0u) {
                stream->stage = NATIVE_YAZ0_BACKREF_LENGTH;
            } else {
                stream->copy_remaining = length + 2u;
                stream->stage = NATIVE_YAZ0_COPY;
            }
            break;
        }

        case NATIVE_YAZ0_BACKREF_LENGTH:
            stream->copy_remaining = (u32)input[input_pos++] + 0x12u;
            stream->stage = NATIVE_YAZ0_COPY;
            break;

        default:
            stream->stage = NATIVE_YAZ0_ERROR;
            break;
        }
    }

    stream->input_bytes += input_pos;
    u32 remaining = stream->output_size - stream->output_pos;
    mem_write32(cpu, stream_object, stream->destination + stream->output_pos);
    mem_write32(cpu, stream_object + 4u, remaining);
    mem_write8(cpu, stream_object + 22u, 0u);

    if (stream->stage == NATIVE_YAZ0_DONE) {
        log_native_yaz0_archive(stream, output);
        cpu->gpr[3] = 0u;
    } else if (stream->stage == NATIVE_YAZ0_ERROR) {
        cpu->gpr[3] = (u32)-2;
    } else {
        cpu->gpr[3] = remaining;
    }
    return_to_lr(cpu);
    return true;
}

static bool handle_crc16(CPUState* cpu) {
    const u32 data_address = cpu->gpr[4];
    const u32 length = cpu->gpr[5];
    if (length != 0 && data_address + length < data_address)
        return false;

    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* segment =
        length != 0 && memory
            ? wiiu_memory_find(memory, data_address, length)
            : NULL;
    if (length != 0 && !segment)
        return false;

    const u8* data = length != 0
                         ? segment->data +
                               (wiiu_memory_canonical_address(data_address) -
                                segment->base)
                         : NULL;
    u32 crc = cpu->gpr[3];
    for (u32 i = 0; i < length; i++) {
        for (u32 bit = 0; bit < 8u; bit++) {
            if ((crc & 0x8000u) != 0)
                crc = ((crc << 1) ^ 0x1021u) & 0xFFFFu;
            else
                crc = (crc << 1) & 0xFFFEu;
        }
        crc ^= data[i];
    }

    if (g_crc16_count < 16u) {
        fprintf(stderr,
                "shim: CRC16 checked data=0x%08X size=%u result=0x%04X\n",
                data_address, length, crc);
    }
    g_crc16_count++;
    cpu->gpr[3] = crc;
    return_to_lr(cpu);
    return true;
}

static u32 guest_bump_block_header(CPUState* cpu, u32 heap, u32 size,
                                   u32 alignment) {
    alignment = abs_alignment(alignment);

    u64 total = (u64)size + alignment + 0x10u;
    if (total > 0xFFFFFFFFu)
        return 0;

    const u32 required_size = (u32)total;
    u32 raw = 0;
    u32 raw_end = 0;
    FrameworkBlock* parent = find_framework_block(heap);
    FrameworkAllocation* allocation =
        find_reusable_framework_allocation(heap, required_size);
    if (allocation) {
        raw = allocation->raw_start;
        raw_end = allocation->raw_end;
        allocation->in_use = true;
        if (g_framework_reusable_count != 0)
            g_framework_reusable_count--;
        g_framework_reuse_count++;
        if (g_framework_reuse_count <= 32u) {
            fprintf(stderr,
                    "shim: framework reuse heap=0x%08X size=0x%08X "
                    "range=0x%08X-0x%08X\n",
                    heap, size, raw, raw_end);
        }
    } else if (parent) {
        u32 start = align_up_u32(parent->cursor, 4);
        u64 end = (u64)start + total;
        u64 limit = parent->range_end;
        if (end <= limit) {
            raw = start;
            parent->cursor = align_up_u32((u32)end, 4);
            raw_end = parent->cursor;
        }
    } else {
        raw = guest_aux_bump_alloc(required_size, 4);
        if (raw)
            raw_end = g_aux_heap_cursor;
    }

    if (raw == 0) {
        if (parent && g_framework_exhaustion_count++ < 24u) {
            fprintf(stderr,
                    "warn: framework child heap exhausted heap=0x%08X "
                    "cursor=0x%08X size=0x%08X limit=0x%08X\n",
                    heap, parent->cursor, size, parent->range_end);
        } else if (!parent && g_framework_exhaustion_count++ < 24u) {
            fprintf(stderr,
                    "warn: framework arena exhausted heap=0x%08X "
                    "cursor=0x%08X size=0x%08X align=0x%08X "
                    "limit=0x%08X\n",
                    heap, g_aux_heap_cursor, size, alignment,
                    WIIU_GUEST_AUX_HEAP_BASE + WIIU_GUEST_AUX_HEAP_SIZE);
        }
        return 0;
    }

    u32 payload = align_up_u32(raw + 16u, alignment);
    u32 header = payload - 16u;
    if (!allocation) {
        allocation =
            register_framework_allocation(heap, raw, raw_end, header);
        if (!allocation) {
            if (parent && parent->cursor == raw_end)
                parent->cursor = raw;
            else if (!parent && g_aux_heap_cursor == raw_end)
                g_aux_heap_cursor = raw;
            fprintf(stderr,
                    "warn: framework allocation tracker exhausted "
                    "heap=0x%08X size=0x%08X\n",
                    heap, size);
            return 0;
        }
    } else {
        allocation->header = header;
    }
    guest_set(cpu, header, 0, 16);
    mem_write32(cpu, header + 8u, size);
    mem_write32(cpu, header + 12u, 0);
    return header;
}

static bool should_shim_framework_heap(u32 heap) {
    return heap == 0x18012178u ||
           (heap >= 0x1B000000u && heap < WIIU_GUEST_OS_BASE) ||
           find_framework_block(heap) != NULL;
}

static bool handle_heap_import(CPUState* cpu, const char* name) {
    if (name_contains(name, "MEMGetBaseHeapHandle")) {
        u32 type = cpu->gpr[3];
        HostBaseHeap* heap = find_base_heap_by_type(type);
        cpu->gpr[3] = heap ? heap->handle : 0u;
        sync_base_heap_header(cpu, heap);
        if (g_heap_query_log_count++ < 32u) {
            fprintf(stderr,
                    "coreinit: base heap type=%u handle=0x%08X "
                    "range=0x%08X-0x%08X lr=0x%08X\n",
                    type, cpu->gpr[3], heap ? heap->data_start : 0u,
                    heap ? heap->data_end : 0u, cpu->lr);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMGetAllocatableSizeForExpHeapEx") ||
        name_contains(name, "MEMGetAllocatableSizeForFrmHeapEx") ||
        name_contains(name, "MEMGetTotalFreeSizeForExpHeap")) {
        u32 heap = cpu->gpr[3];
        u32 alignment = cpu->gpr[4];
        HostBaseHeap* base_heap = find_base_heap_by_handle(heap);
        FrameworkBlock* framework_heap = find_framework_block(heap);
        cpu->gpr[3] = base_heap
                          ? base_heap_allocatable(base_heap, alignment)
                          : (framework_heap
                                 ? framework_block_allocatable(framework_heap,
                                                               alignment)
                                 : (g_heap_cursor < WIIU_GUEST_HEAP_BASE +
                                                        WIIU_GUEST_HEAP_SIZE
                                        ? WIIU_GUEST_HEAP_BASE +
                                              WIIU_GUEST_HEAP_SIZE -
                                              g_heap_cursor
                                        : 0u));
        if (g_heap_query_log_count++ < 32u) {
            fprintf(stderr,
                    "coreinit: heap query %s heap=0x%08X align=0x%08X "
                    "result=0x%08X lr=0x%08X\n",
                    name, heap, alignment, cpu->gpr[3], cpu->lr);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMAllocFromExpHeapEx")) {
        HostBaseHeap* heap = find_base_heap_by_handle(cpu->gpr[3]);
        FrameworkBlock* framework_heap = find_framework_block(cpu->gpr[3]);
        cpu->gpr[3] = heap
                          ? base_heap_alloc(cpu, heap, cpu->gpr[4],
                                            cpu->gpr[5])
                          : (framework_heap
                                 ? framework_block_alloc(cpu, framework_heap,
                                                         cpu->gpr[4],
                                                         cpu->gpr[5])
                                 : guest_bump_alloc(cpu->gpr[4],
                                                    cpu->gpr[5]));
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMAllocFromFrmHeapEx")) {
        HostBaseHeap* heap = find_base_heap_by_handle(cpu->gpr[3]);
        FrameworkBlock* framework_heap = find_framework_block(cpu->gpr[3]);
        cpu->gpr[3] = heap
                          ? base_heap_alloc(cpu, heap, cpu->gpr[4],
                                            cpu->gpr[5])
                          : (framework_heap
                                 ? framework_block_alloc(cpu, framework_heap,
                                                         cpu->gpr[4],
                                                         cpu->gpr[5])
                                 : guest_bump_alloc(cpu->gpr[4],
                                                    cpu->gpr[5]));
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMAllocFromDefaultHeapEx")) {
        cpu->gpr[3] = guest_default_alloc(cpu->gpr[3], cpu->gpr[4]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMAllocFromDefaultHeap")) {
        cpu->gpr[3] = guest_default_alloc(cpu->gpr[3], 4);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMCreateExpHeap") ||
        name_contains(name, "MEMCreateFrmHeap")) {
        const u32 requested_start = cpu->gpr[3];
        const u32 requested_size = cpu->gpr[4];
        const bool exp_heap = name_contains(name, "MEMCreateExpHeap");
        const u32 heap_header_size = exp_heap ? 0x68u : 0x50u;
        const u32 heap = align_up_u32(requested_start, 4u);
        const u64 requested_end =
            (u64)requested_start + (u64)requested_size;
        const u32 heap_end = (u32)(requested_end & ~3ull);
        const u32 data_start = heap + heap_header_size;

        if (requested_start == 0u || requested_end > 0x100000000ull ||
            heap_end <= heap || data_start < heap || data_start >= heap_end ||
            !guest_range_mapped(cpu, heap, heap_end - heap)) {
            cpu->gpr[3] = 0u;
        } else {
            FrameworkBlock* block = register_framework_range(
                heap, heap, data_start, heap_end, data_start);
            if (!block) {
                cpu->gpr[3] = 0u;
            } else {
                guest_set(cpu, heap, 0, heap_header_size);
                mem_write32(cpu, heap,
                            exp_heap ? 0x45585048u : 0x46524D48u);
                mem_write32(cpu, heap + 0x18u, data_start);
                mem_write32(cpu, heap + 0x1Cu, heap_end);
                cpu->gpr[3] = heap;
            }
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMFreeToFrmHeap")) {
        reset_base_frame_heap(cpu, find_base_heap_by_handle(cpu->gpr[3]),
                              cpu->gpr[4]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "MEMFreeToExpHeap") ||
        name_contains(name, "MEMFreeToDefaultHeap") ||
        name_contains(name, "MEMDestroyExpHeap") ||
        name_contains(name, "MEMDestroyFrmHeap")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static HostThread* find_host_thread(u32 object) {
    for (u32 i = 0; i < g_host_thread_count; i++) {
        if (g_host_threads[i].object == object)
            return &g_host_threads[i];
    }
    return NULL;
}

/* Cafe OS exposes three PPC cores.  The original fallback returned core 0
   even when executing a resumable guest worker, which makes per-core work
   queues and hand-offs believe all workers are the same producer.  Respect a
   thread's affinity mask when it is specified; otherwise distribute the
   "Any" workers deterministically over the three available guest cores. */
static u32 guest_core_id_for_thread(const HostThread* thread) {
    /* The application entry context is independent from guest worker
       threads.  Keep it on the middle application core so an explicitly
       core-0 worker is not treated as the same execution lane. */
    if (!thread)
        return 1u;

    u32 affinity = thread->attributes & 7u;
    if ((affinity & 1u) != 0u)
        return 0u;
    if ((affinity & 2u) != 0u)
        return 1u;
    if ((affinity & 4u) != 0u)
        return 2u;

    ptrdiff_t index = thread - g_host_threads;
    return index >= 0 && (u32)index < g_host_thread_count
               ? (u32)index % 3u
               : 0u;
}

static void wake_threads_waiting_on_queue(u32 queue, bool senders) {
    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        if (!thread->blocked || thread->wait_queue != queue ||
            thread->waiting_to_send != senders) {
            continue;
        }
        thread->blocked = false;
        thread->waiting_to_send = false;
        thread->wait_queue = 0;
        thread->cpu.pc = thread->wait_pc;
    }
}

static bool wake_threads_waiting_on_event(u32 event, bool wake_all) {
    bool woke_thread = false;
    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        if (!thread->blocked || thread->wait_event != event)
            continue;

        thread->blocked = false;
        thread->wait_event = 0u;
        thread->ready_event = event;
        thread->cpu.pc = thread->wait_pc;
        woke_thread = true;
        if (!wake_all)
            break;
    }
    return woke_thread;
}

static bool wake_thread_waiting_on_fast_mutex(u32 mutex) {
    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        if (!thread->blocked || thread->wait_fast_mutex != mutex)
            continue;

        thread->blocked = false;
        thread->wait_fast_mutex = 0u;
        thread->cpu.pc = thread->wait_pc;
        return true;
    }
    return false;
}

/* See run_guest_dispatches in main.c.  Guest threads, alarms, and audio
   callbacks use the same generated chassis and therefore need a fresh,
   practical downcount quantum for each dispatch as well. */
static int run_timed_guest_dispatches(CPUState* cpu, u32 max_blocks) {
    for (u32 blocks = 0u; max_blocks == 0u || blocks < max_blocks; blocks++) {
        cpu->downcount = GUEST_DISPATCH_CYCLE_BUDGET;
        u32 previous_pc=cpu->pc;
        int dispatched = dolrecomp_call(cpu, cpu->pc);
        wiiu_memory_track_reservation(cpu);
        if (!dispatched || cpu->exception)
            return 0;
        /* A retained import needs another cooperative context to run. The
           main thread already yields here; workers/audio must do so too. */
        if((g_running_host_thread && g_running_host_thread->blocked) ||
           (cpu->pc==previous_pc && previous_pc>=0x04348000u && previous_pc<0x04350000u))
            break;
    }
    return 1;
}

static void register_fast_mutex(u32 mutex) {
    if (mutex == 0u)
        return;
    for (u32 i = 0; i < g_fast_mutex_count; i++) {
        if (g_fast_mutexes[i] == mutex)
            return;
    }
    if (g_fast_mutex_count < FAST_MUTEX_LIMIT)
        g_fast_mutexes[g_fast_mutex_count++] = mutex;
}

static void release_current_fast_mutexes(CPUState* cpu) {
    if (!cpu || !g_running_host_thread)
        return;

    u32 owner = g_running_host_thread->object;
    for (u32 i = 0; i < g_fast_mutex_count; i++) {
        u32 mutex = g_fast_mutexes[i];
        if (!guest_range_mapped(cpu, mutex, 0x2Cu) ||
            mem_read32(cpu, mutex + 0x1Cu) != owner) {
            continue;
        }
        mem_write32(cpu, mutex + 0x1Cu, 0u);
        mem_write32(cpu, mutex + 0x20u, 0u);
        wake_thread_waiting_on_fast_mutex(mutex);
    }
}

static HostThread* record_host_thread(CPUState* cpu, u32 object, u32 entry,
                                      u32 argc, u32 argv, u32 stack_top,
                                      u32 stack_size, u32 priority,
                                      u32 attributes) {
    if (!guest_range_mapped(cpu, object, 0x6A0u))
        return NULL;

    HostThread* thread = find_host_thread(object);
    if (!thread) {
        if (g_host_thread_count >= HOST_THREAD_LIMIT) {
            fprintf(stderr, "coreinit: thread capacity exhausted count=%u object=%08X entry=%08X caller=%08X\n",
                    g_host_thread_count, object, entry, cpu->lr);
            return NULL;
        }
        thread = &g_host_threads[g_host_thread_count++];
    }

    memset(thread, 0, sizeof(*thread));
    thread->object = object;
    thread->entry = entry;
    thread->argc = argc;
    thread->argv = argv;
    thread->stack_top = stack_top & ~7u;
    thread->stack_size = stack_size;
    thread->priority = priority;
    thread->attributes = attributes;

    guest_set(cpu, object, 0, 0x6A0u);
    mem_write64(cpu, object + 0x00u, 0x4F53436F6E747874ull);
    mem_write32(cpu, object + 0x0Cu, thread->stack_top);
    mem_write32(cpu, object + 0x14u, argc);
    mem_write32(cpu, object + 0x18u, argv);
    mem_write32(cpu, object + 0x98u, entry);
    mem_write32(cpu, object + 0x320u, 0x74487244u);
    mem_write8(cpu, object + 0x324u, 1u);
    mem_write8(cpu, object + 0x325u, (u8)attributes);
    mem_write16(cpu, object + 0x326u, (u16)g_host_thread_count);
    mem_write32(cpu, object + 0x328u, 1u);
    mem_write32(cpu, object + 0x32Cu, priority);
    mem_write32(cpu, object + 0x330u, priority);
    mem_write32(cpu, object + 0x394u, thread->stack_top);
    mem_write32(cpu, object + 0x398u,
                stack_size <= thread->stack_top ? thread->stack_top - stack_size
                                                : 0u);
    mem_write32(cpu, object + 0x39Cu, entry);
    mem_write32(cpu, object + 0x5C8u, thread->stack_top);

    if (g_thread_log_count++ < 24u) {
        fprintf(stderr,
                "coreinit: thread object=0x%08X entry=0x%08X stack=0x%08X "
                "size=0x%X argc=%u argv=0x%08X priority=%u "
                "attributes=0x%X\n",
                object, entry, thread->stack_top, stack_size, argc, argv,
                priority, attributes);
    }
    return thread;
}

static bool erreula_module_name(CPUState* cpu, u32 name) {
    return guest_string_equals(cpu, name, "erreula.rpl") ||
           guest_string_equals(cpu, name, "erreula");
}

static u32 erreula_export_address(CPUState* cpu, u32 name) {
    if (guest_string_contains(cpu, name, "ErrEulaCreate"))
        return HOST_ERREULA_CREATE_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaDestroy"))
        return HOST_ERREULA_DESTROY_ADDRESS;
    if (guest_string_contains(cpu, name,
                              "ErrEulaIsDecideSelectButtonError")) {
        return HOST_ERREULA_IS_DECIDED_ADDRESS;
    }
    if (guest_string_contains(cpu, name,
                              "ErrEulaIsDecideSelectLeftButtonError")) {
        return HOST_ERREULA_IS_LEFT_ADDRESS;
    }
    if (guest_string_contains(cpu, name,
                              "ErrEulaIsDecideSelectRightButtonError")) {
        return HOST_ERREULA_IS_RIGHT_ADDRESS;
    }
    if (guest_string_contains(cpu, name, "ErrEulaGetResultCode"))
        return HOST_ERREULA_RESULT_CODE_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaGetResultType"))
        return HOST_ERREULA_RESULT_TYPE_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaAppearError"))
        return HOST_ERREULA_APPEAR_ERROR_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaDisappearError"))
        return HOST_ERREULA_DISAPPEAR_ERROR_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaGetStateErrorViewer"))
        return HOST_ERREULA_STATE_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaCalc"))
        return HOST_ERREULA_CALC_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaAppearHomeNixSign"))
        return HOST_ERREULA_APPEAR_HOME_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaChangeLang"))
        return HOST_ERREULA_CHANGE_LANG_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaIsAppearHomeNixSign"))
        return HOST_ERREULA_IS_HOME_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaDisappearHomeNixSign"))
        return HOST_ERREULA_DISAPPEAR_HOME_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaDrawTV"))
        return HOST_ERREULA_DRAW_TV_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaDrawDRC"))
        return HOST_ERREULA_DRAW_DRC_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaSetControllerRemo"))
        return HOST_ERREULA_SET_CONTROLLER_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaIsSelectCursorActive"))
        return HOST_ERREULA_IS_CURSOR_ACTIVE_ADDRESS;
    if (guest_string_contains(cpu, name, "ErrEulaGetSelectButtonNumError"))
        return HOST_ERREULA_GET_SELECTION_ADDRESS;
    return 0u;
}

static bool handle_erreula_dynamic_import(CPUState* cpu, const char* name) {
    if ((name_equals(name, "OSDynLoad_Acquire") ||
         name_equals(name, "OSDynLoad_IsModuleLoaded")) &&
        erreula_module_name(cpu, cpu->gpr[3])) {
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], HOST_ERREULA_MODULE_HANDLE);
        cpu->gpr[3] = 0u;
        if (g_erreula_log_count++ < 8u) {
            fprintf(stderr,
                    "coreinit: using built-in ErrEula compatibility module\n");
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (!name_equals(name, "OSDynLoad_FindExport") ||
        cpu->gpr[3] != HOST_ERREULA_MODULE_HANDLE) {
        return false;
    }

    u32 address = cpu->gpr[4] == 0u
                      ? erreula_export_address(cpu, cpu->gpr[5])
                      : 0u;
    if (guest_range_mapped(cpu, cpu->gpr[6], 4u))
        mem_write32(cpu, cpu->gpr[6], address);
    if (g_erreula_log_count++ < 32u) {
        fprintf(stderr, "coreinit: ErrEula export ");
        guest_print_string(cpu, cpu->gpr[5], stderr);
        fprintf(stderr, " -> 0x%08X\n", address);
    }
    cpu->gpr[3] = address != 0u ? 0u : 0xFFFCFFE9u;
    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static HostAlarm* find_host_alarm(u32 alarm, bool create) {
    HostAlarm* available = NULL;

    for (u32 i = 0u; i < HOST_ALARM_LIMIT; i++) {
        HostAlarm* candidate = &g_host_alarms[i];
        if (candidate->alarm == alarm && (candidate->active || alarm != 0u))
            return candidate;
        if (!available && !candidate->active)
            available = candidate;
    }

    if (!create || alarm == 0u)
        return NULL;
    if (!available)
        available = &g_host_alarms[alarm % HOST_ALARM_LIMIT];

    memset(available, 0, sizeof(*available));
    available->alarm = alarm;
    return available;
}

static void schedule_host_alarm(u32 alarm, u32 handler, u64 next_tick,
                                u64 period) {
    HostAlarm* host_alarm = find_host_alarm(alarm, handler != 0u);
    if (!host_alarm)
        return;

    if (handler == 0u) {
        host_alarm->active = false;
        return;
    }

    host_alarm->active = true;
    host_alarm->periodic = period != 0u;
    host_alarm->alarm = alarm;
    host_alarm->handler = handler;
    host_alarm->next_tick = next_tick;
    host_alarm->period = period;
}

static HostAlarm* take_due_alarm(CPUState* cpu) {
    HostAlarm* due = NULL;

    for (u32 i = 0u; i < HOST_ALARM_LIMIT; i++) {
        HostAlarm* candidate = &g_host_alarms[i];
        if (!candidate->active || candidate->next_tick > cpu->timebase)
            continue;
        if (!due || candidate->next_tick < due->next_tick ||
            (candidate->next_tick == due->next_tick &&
             candidate->alarm < due->alarm)) {
            due = candidate;
        }
    }

    if (!due)
        return NULL;

    if (due->periodic && due->period != 0u) {
        u64 next_tick = due->next_tick + due->period;
        if (next_tick <= due->next_tick || next_tick <= cpu->timebase) {
            next_tick = cpu->timebase + due->period;
            if (next_tick <= cpu->timebase)
                next_tick = UINT64_MAX;
        }
        due->next_tick = next_tick;
        if (guest_range_mapped(cpu, due->alarm, OS_ALARM_SIZE))
            mem_write64(cpu, due->alarm + 0x18u, next_tick);
    } else {
        due->active = false;
        if (guest_range_mapped(cpu, due->alarm, OS_ALARM_SIZE))
            mem_write32(cpu, due->alarm + 0x3Cu, 0u);
    }

    return due;
}

static bool handle_coreinit_import(CPUState* cpu, const char* name) {
    if (handle_erreula_dynamic_import(cpu, name))
        return true;

    if (name_equals(name, "__tls_get_addr")) {
        cpu->gpr[3] = guest_thread_tls_address(cpu, cpu->gpr[3]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSMemoryBarrier")) {
        /* Host memory is coherent; Cafe's implementation is also a no-op. */
        if (cpu->lr == 0x030DF558u) {
            /* The title bootstrap follows this barrier with a spin until its
               private ready word becomes one. In the host's single-threaded
               startup path no producer remains to publish that transition,
               leaving the game at the translated lwarx loop forever. Publish
               the already-complete barrier state before returning. */
            u32 ready_word = cpu->gpr[31];
            if (guest_range_mapped(cpu, ready_word, 4u) &&
                mem_read32(cpu, ready_word) == 0u) {
                mem_write32(cpu, ready_word, 1u);
                if (g_framework_warning_count++ < 32u) {
                    fprintf(stderr,
                            "coreinit: released title startup barrier "
                            "at 0x%08X\n",
                            ready_word);
                }
            }
            fprintf(stderr,
                    "coreinit: barrier before spinlock object=0x%08X "
                    "lock=0x%08X count=0x%08X allocator=0x%08X mode=%u "
                    "caller=0x%08X sp=0x%08X\n",
                    cpu->gpr[29], cpu->gpr[31], cpu->gpr[30], cpu->gpr[5],
                    cpu->gpr[6], safe_read32(cpu, cpu->gpr[1] + 28u),
                    cpu->gpr[1]);
            if (cpu->gpr[29] < 0x10000u) {
                u32 allocator = cpu->gpr[5];
                fprintf(stderr,
                        "coreinit: failed allocator object=0x%08X "
                        "heap_cursor=0x%08X fields="
                        "[%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X," 
                        "%08X,%08X,%08X,%08X]\n",
                        allocator, g_heap_cursor,
                        safe_read32(cpu, allocator + 0u),
                        safe_read32(cpu, allocator + 16u),
                        safe_read32(cpu, allocator + 32u),
                        safe_read32(cpu, allocator + 48u),
                        safe_read32(cpu, allocator + 64u),
                        safe_read32(cpu, allocator + 80u),
                        safe_read32(cpu, allocator + 96u),
                        safe_read32(cpu, allocator + 112u),
                        safe_read32(cpu, allocator + 128u),
                        safe_read32(cpu, allocator + 144u),
                        safe_read32(cpu, allocator + 148u),
                        safe_read32(cpu, allocator + 152u));
            }
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSIsEnabledOverlayArena")) {
        cpu->gpr[3] = g_overlay_arena_enabled ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSEnableOverlayArena")) {
        g_overlay_arena_enabled = true;
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], WIIU_OVERLAY_ARENA_BASE);
        if (guest_range_mapped(cpu, cpu->gpr[5], 4u))
            mem_write32(cpu, cpu->gpr[5], WIIU_OVERLAY_ARENA_SIZE);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSEnableHomeButtonMenu")) {
        g_home_button_menu_enabled = cpu->gpr[3] != 0u;
        cpu->gpr[3] = 1u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSIsHomeButtonMenuEnabled")) {
        cpu->gpr[3] = g_home_button_menu_enabled ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSGetShutdownReason")) {
        cpu->gpr[3] = 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSJoinThread")) {
        HostThread* thread = find_host_thread(cpu->gpr[3]);
        bool joined = thread && thread->completed;
        if (joined && guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], 0u);
        cpu->gpr[3] = joined ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_SetAllocator")) {
        g_dynload_alloc_fn = cpu->gpr[3];
        g_dynload_free_fn = cpu->gpr[4];
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_GetAllocator")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], 4u))
            mem_write32(cpu, cpu->gpr[3], g_dynload_alloc_fn);
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], g_dynload_free_fn);
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_SetTLSAllocator")) {
        g_dynload_tls_alloc_fn = cpu->gpr[3];
        g_dynload_tls_free_fn = cpu->gpr[4];
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_GetTLSAllocator")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], 4u))
            mem_write32(cpu, cpu->gpr[3], g_dynload_tls_alloc_fn);
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], g_dynload_tls_free_fn);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_Acquire") ||
        name_equals(name, "OSDynLoad_IsModuleLoaded")) {
        u32 module_name = cpu->gpr[3];
        u32 module_out = cpu->gpr[4];
        if (guest_range_mapped(cpu, module_out, 4u))
            mem_write32(cpu, module_out, 0u);
        if (g_dynload_log_count++ < 16u) {
            fputs("coreinit: optional dynamic module unavailable: ", stderr);
            guest_print_string(cpu, module_name, stderr);
            fputc('\n', stderr);
        }
        cpu->gpr[3] = name_equals(name, "OSDynLoad_IsModuleLoaded")
                          ? 0u
                          : 0xFFFCFFE9u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_FindExport")) {
        if (guest_range_mapped(cpu, cpu->gpr[6], 4u))
            mem_write32(cpu, cpu->gpr[6], 0u);
        cpu->gpr[3] = 0xFFFFFFFFu;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSDynLoad_Release")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "__gh_set_errno")) {
        g_gh_errno = (s32)cpu->gpr[3];
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "__gh_get_errno")) {
        cpu->gpr[3] = (u32)g_gh_errno;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSYieldThread")) {
        wiiu_window_pump();
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSInitRendezvous")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], 0x10u)) {
            guest_set(cpu, cpu->gpr[3], 0, 0x10u);
            mem_write32(cpu, cpu->gpr[3] + 12u, cpu->gpr[3]);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSWaitRendezvous")) {
        u32 rendezvous = cpu->gpr[3];
        u32 mask = cpu->gpr[4] & 7u;
        if (!guest_range_mapped(cpu, rendezvous, 0x10u)) {
            cpu->gpr[3] = 0u;
            return_to_lr(cpu);
            return true;
        }
        u32 core = guest_core_id_for_thread(g_running_host_thread);
        mem_write32(cpu, rendezvous + core * 4u, 1u);
        u32 arrived = 0u;
        for (u32 i = 0; i < 3u; ++i)
            if (mem_read32(cpu, rendezvous + i * 4u) != 0u)
                arrived |= 1u << i;
        if ((arrived & mask) != mask) {
            /* Retain the import PC and arguments. Both main and worker
               dispatches are bounded, so the other cores can run before
               this call is retried. Arrival flags persist until init. */
            return true;
        }
        cpu->gpr[3] = 1u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSInitThreadQueue") ||
        name_equals(name, "OSInitThreadQueueEx")) {
        u32 queue = cpu->gpr[3];
        if (guest_range_mapped(cpu, queue, OS_THREAD_QUEUE_SIZE)) {
            guest_set(cpu, queue, 0, OS_THREAD_QUEUE_SIZE);
            if (name_equals(name, "OSInitThreadQueueEx"))
                mem_write32(cpu, queue + 8u, cpu->gpr[4]);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSCreateAlarm") ||
        name_equals(name, "OSCreateAlarmEx")) {
        u32 alarm = cpu->gpr[3];
        if (guest_range_mapped(cpu, alarm, OS_ALARM_SIZE)) {
            guest_set(cpu, alarm, 0, OS_ALARM_SIZE);
            mem_write32(cpu, alarm, OS_ALARM_TAG);
            if (name_equals(name, "OSCreateAlarmEx"))
                mem_write32(cpu, alarm + 4u, cpu->gpr[4]);
        }
        HostAlarm* host_alarm = find_host_alarm(alarm, false);
        if (host_alarm)
            memset(host_alarm, 0, sizeof(*host_alarm));
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSSetAlarmUserData")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], OS_ALARM_SIZE))
            mem_write32(cpu, cpu->gpr[3] + 0x38u, cpu->gpr[4]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSGetAlarmUserData")) {
        cpu->gpr[3] = guest_range_mapped(cpu, cpu->gpr[3], OS_ALARM_SIZE)
                          ? mem_read32(cpu, cpu->gpr[3] + 0x38u)
                          : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSSetAlarmTag")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], OS_ALARM_SIZE))
            mem_write32(cpu, cpu->gpr[3] + 0x10u, cpu->gpr[4]);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSSetAlarm") ||
        name_equals(name, "OSSetPeriodicAlarm")) {
        u32 alarm = cpu->gpr[3];
        bool periodic = name_equals(name, "OSSetPeriodicAlarm");
        u64 start = ((u64)cpu->gpr[4] << 32) | cpu->gpr[5];
        u64 period = periodic
                         ? (((u64)cpu->gpr[6] << 32) | cpu->gpr[7])
                         : 0u;
        u32 handler = periodic ? cpu->gpr[8] : cpu->gpr[6];
        if (!periodic)
            start += cpu->timebase;
        if (periodic && period != 0u && start <= cpu->timebase) {
            u64 elapsed = cpu->timebase - start;
            u64 periods = elapsed / period;
            u64 next_start = start + (periods + 1u) * period;
            if (next_start > start)
                start = next_start;
            else
                start = cpu->timebase + period;
        }
        if (guest_range_mapped(cpu, alarm, OS_ALARM_SIZE)) {
            mem_write32(cpu, alarm + 0x0Cu, handler);
            mem_write64(cpu, alarm + 0x18u, start);
            if (periodic) {
                mem_write64(cpu, alarm + 0x28u, period);
                mem_write64(cpu, alarm + 0x30u,
                            ((u64)cpu->gpr[4] << 32) | cpu->gpr[5]);
            }
            mem_write32(cpu, alarm + 0x3Cu, 1u);
        }
        schedule_host_alarm(alarm, handler, start, period);
        cpu->gpr[3] = 1u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSCancelAlarm")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], OS_ALARM_SIZE))
            mem_write32(cpu, cpu->gpr[3] + 0x3Cu, 0u);
        HostAlarm* host_alarm = find_host_alarm(cpu->gpr[3], false);
        bool was_active = host_alarm && host_alarm->active;
        if (host_alarm)
            host_alarm->active = false;
        cpu->gpr[3] = was_active ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSCreateThread")) {
        HostThread* thread =
            record_host_thread(cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                               cpu->gpr[6], cpu->gpr[7], cpu->gpr[8],
                               cpu->gpr[9], cpu->gpr[10]);
        cpu->gpr[3] = thread ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSResumeThread")) {
        u32 object = cpu->gpr[3];
        HostThread* thread = find_host_thread(object);
        u32 previous = guest_range_mapped(cpu, object + 0x328u, 4u)
                           ? mem_read32(cpu, object + 0x328u)
                           : 0u;
        if (thread)
            thread->resumed = true;
        if (guest_range_mapped(cpu, object, 0x6A0u)) {
            mem_write32(cpu, object + 0x328u, 0u);
            mem_write8(cpu, object + 0x324u, 1u);
        }
        cpu->gpr[3] = previous;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSRunThread")) {
        HostThread* thread = find_host_thread(cpu->gpr[3]);
        if (thread) {
            thread->entry = cpu->gpr[4];
            thread->argc = cpu->gpr[5];
            thread->argv = cpu->gpr[6];
            thread->resumed = true;
            mem_write32(cpu, thread->object + 0x14u, thread->argc);
            mem_write32(cpu, thread->object + 0x18u, thread->argv);
            mem_write32(cpu, thread->object + 0x98u, thread->entry);
            mem_write32(cpu, thread->object + 0x39Cu, thread->entry);
            mem_write32(cpu, thread->object + 0x328u, 0u);
        }
        cpu->gpr[3] = thread ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSetThreadAffinity")) {
        u32 object = cpu->gpr[3];
        if (guest_range_mapped(cpu, object, 0x6A0u)) {
            u8 attributes = mem_read8(cpu, object + 0x325u);
            attributes = (u8)((attributes & ~7u) | (cpu->gpr[4] & 7u));
            mem_write8(cpu, object + 0x325u, attributes);
            HostThread* thread = find_host_thread(object);
            if (thread)
                thread->attributes = attributes;
            cpu->gpr[3] = 1u;
        } else {
            cpu->gpr[3] = 0u;
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetThreadAffinity")) {
        HostThread* thread = find_host_thread(cpu->gpr[3]);
        if (thread) {
            cpu->gpr[3] = thread->attributes & 7u;
        } else if (guest_range_mapped(cpu, cpu->gpr[3], 0x326u)) {
            cpu->gpr[3] = mem_read8(cpu, cpu->gpr[3] + 0x325u) & 7u;
        } else {
            cpu->gpr[3] = 0u;
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSetThreadName")) {
        u32 object = cpu->gpr[3];
        u32 thread_name = cpu->gpr[4];
        if (guest_range_mapped(cpu, object, 0x6A0u))
            mem_write32(cpu, object + 0x5C0u, thread_name);
        if (g_thread_log_count++ < 24u) {
            fprintf(stderr, "coreinit: thread 0x%08X named ", object);
            guest_print_string(cpu, thread_name, stderr);
            fputc('\n', stderr);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetCurrentThread")) {
        cpu->gpr[3] = g_running_host_thread ? g_running_host_thread->object
                                            : WIIU_GUEST_THREAD;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSExitThread") && g_running_host_thread) {
        release_current_fast_mutexes(cpu);
        g_running_host_thread->completed = true;
        g_running_host_thread->resumed = false;
        cpu->pc = HOST_THREAD_RETURN_ADDRESS;
        log_import_once(cpu->pc, name, true);
        return true;
    }

    if (name_contains(name, "OSGetCoreId")) {
        u32 handoff = cpu->gpr[3];
        u32 core_id = guest_core_id_for_thread(g_running_host_thread);
        cpu->gpr[3] = core_id;
        /* This caller is the work-queue handoff that gates transition to
           title rendering.  Keep a narrow trace here rather than flooding
           the log with unrelated per-frame OSGetCoreId calls. */
        if (cpu->lr == 0x030DF624u &&
            g_core_handoff_log_count++ < 64u) {
            fprintf(stderr,
                    "coreinit: work handoff core=%u thread=0x%08X "
                    "affinity=0x%X object=0x%08X lock=0x%08X "
                    "range=[0x%08X,0x%08X] final=%u\n",
                    core_id,
                    g_running_host_thread ? g_running_host_thread->object
                                          : 0u,
                    g_running_host_thread
                        ? (g_running_host_thread->attributes & 7u)
                        : 1u,
                    handoff,
                    safe_read32(cpu, handoff + 16u),
                    safe_read32(cpu, handoff + 120u),
                    safe_read32(cpu, handoff + 124u),
                    guest_range_mapped(cpu, handoff + 128u, 1u)
                        ? mem_read8(cpu, handoff + 128u)
                        : 0u);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSIsDebuggerInitialized") ||
        name_contains(name, "OSIsDebuggerPresent")) {
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetSystemInfo")) {
        u32 info = WIIU_GUEST_SYSTEM_INFO;
        guest_set(cpu, info, 0, 0x20u);
        mem_write32(cpu, info + 0x00u, 248625000u);
        mem_write32(cpu, info + 0x04u, 1243125000u);
        mem_write64(cpu, info + 0x08u, 0u);
        mem_write32(cpu, info + 0x10u, 512u * 1024u);
        mem_write32(cpu, info + 0x14u, 2u * 1024u * 1024u);
        mem_write32(cpu, info + 0x18u, 512u * 1024u);
        mem_write32(cpu, info + 0x1Cu, 5u);
        cpu->gpr[3] = info;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSetThreadSpecific")) {
        u32 index = cpu->gpr[3] & 63u;
        u32* values = g_running_host_thread
                          ? g_running_host_thread->thread_specific
                          : g_thread_specific;
        values[index] = cpu->gpr[4];
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetThreadSpecific")) {
        u32 index = cpu->gpr[3] & 63u;
        u32* values = g_running_host_thread
                          ? g_running_host_thread->thread_specific
                          : g_thread_specific;
        if (values[index] == 0)
            values[index] = thread_specific_fallback_address(index);
        if ((cpu->lr == 0x0237CA1Cu || cpu->lr == 0x0237CB54u ||
             cpu->lr == 0x0237CB9Cu) &&
            guest_range_mapped(cpu, values[index] + 112u, 4u) &&
            mem_read32(cpu, values[index] + 112u) == 0) {
            initialize_framework_root(cpu);
            mem_write32(cpu, values[index] + 112u, 0x18012178u);
        }
        cpu->gpr[3] = values[index];
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSGetThreadPriority")) {
        cpu->gpr[3] = g_running_host_thread
                          ? g_running_host_thread->priority
                          : 16u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSInitMessageQueue")) {
        u32 queue = cpu->gpr[3];
        if (guest_range_mapped(cpu, queue, 0x3Cu)) {
            guest_set(cpu, queue, 0, 0x3Cu);
            mem_write32(cpu, queue + 0x00u, 0x6D536751u);
            if (name_contains(name, "OSInitMessageQueueEx"))
                mem_write32(cpu, queue + 0x04u, cpu->gpr[6]);
            mem_write32(cpu, queue + 0x2Cu, cpu->gpr[4]);
            mem_write32(cpu, queue + 0x30u, cpu->gpr[5]);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSendMessage") ||
        name_contains(name, "OSReceiveMessage") ||
        name_contains(name, "OSPeekMessage")) {
        u32 queue = cpu->gpr[3];
        u32 message = cpu->gpr[4];
        bool receive = name_contains(name, "OSReceiveMessage");
        bool peek = name_contains(name, "OSPeekMessage");
        u32 flags = cpu->gpr[5];
        u32 import_address = cpu->pc;
        bool success = false;
        u32 size = 0;
        u32 first = 0;
        u32 used = 0;
        if (guest_range_mapped(cpu, queue, 0x3Cu)) {
            u32 messages = mem_read32(cpu, queue + 0x2Cu);
            size = mem_read32(cpu, queue + 0x30u);
            first = mem_read32(cpu, queue + 0x34u);
            used = mem_read32(cpu, queue + 0x38u);
            if (size != 0 && size <= 0x00100000u && first < size &&
                used <= size &&
                guest_range_mapped(cpu, messages, size * 0x10u)) {
                if (receive || peek) {
                    if (used != 0 && guest_range_mapped(cpu, message, 0x10u)) {
                        guest_copy(cpu, message, messages + first * 0x10u,
                                   0x10u);
                        success = true;
                        if (receive) {
                            mem_write32(cpu, queue + 0x34u,
                                        (first + 1u) % size);
                            mem_write32(cpu, queue + 0x38u, used - 1u);
                        }
                    }
                } else if (used < size &&
                           guest_range_mapped(cpu, message, 0x10u)) {
                    u32 index = (first + used) % size;
                    if ((flags & 2u) != 0) {
                        index = (first + size - 1u) % size;
                        mem_write32(cpu, queue + 0x34u, index);
                    }
                    guest_copy(cpu, messages + index * 0x10u, message, 0x10u);
                    mem_write32(cpu, queue + 0x38u, used + 1u);
                    success = true;
                }
            }
        }
        if (!receive && !peek && import_address == 0x02706278u &&
            g_message_trace_log_count++ < 128u) {
            fprintf(stderr,
                    "coreinit: send queue=0x%08X size=%u used=%u first=%u "
                    "flags=0x%X message=[0x%08X,0x%08X,0x%08X,0x%08X] "
                    "success=%u sender=0x%08X caller=0x%08X\n",
                    queue, size, used, first, flags,
                    safe_read32(cpu, message + 0u),
                    safe_read32(cpu, message + 4u),
                    safe_read32(cpu, message + 8u),
                    safe_read32(cpu, message + 12u), success ? 1u : 0u,
                    g_running_host_thread ? g_running_host_thread->object : 0u,
                    cpu->lr);
        }
        if (!success && receive && (flags & 1u) != 0 &&
            g_running_host_thread) {
            release_current_fast_mutexes(cpu);
            g_running_host_thread->blocked = true;
            g_running_host_thread->waiting_to_send = false;
            g_running_host_thread->wait_queue = queue;
            g_running_host_thread->wait_event = 0u;
            g_running_host_thread->ready_event = 0u;
            g_running_host_thread->wait_pc = import_address;
            cpu->pc = HOST_THREAD_YIELD_ADDRESS;
            if (g_thread_scheduler_log_count++ < 32u) {
                fprintf(stderr,
                        "coreinit: thread 0x%08X waiting on message queue "
                        "0x%08X\n",
                        g_running_host_thread->object, queue);
            }
            log_import_once(import_address, name, true);
            return true;
        }
        if (!success && !receive && !peek && (flags & 1u) != 0 &&
            g_running_host_thread) {
            release_current_fast_mutexes(cpu);
            g_running_host_thread->blocked = true;
            g_running_host_thread->waiting_to_send = true;
            g_running_host_thread->wait_queue = queue;
            g_running_host_thread->wait_event = 0u;
            g_running_host_thread->ready_event = 0u;
            g_running_host_thread->wait_pc = import_address;
            cpu->pc = HOST_THREAD_YIELD_ADDRESS;
            if (g_thread_scheduler_log_count++ < 32u) {
                fprintf(stderr,
                        "coreinit: thread 0x%08X waiting to send on queue "
                        "0x%08X\n",
                        g_running_host_thread->object, queue);
            }
            log_import_once(import_address, name, true);
            return true;
        }
        if (!success && !receive && !peek && !g_running_host_thread &&
            size != 0u && used == size) {
            /* On Café OS this sender and the queue consumer can run on
               separate cores.  Our bootstrap chassis runs workers between
               bounded main-core slices, so a main-core retry loop on a full
               queue can otherwise prevent the receiver from ever running.
               Give resumed workers one short cooperative slice, then retry
               this send against the queue's refreshed state.  A full queue
               is not proof that this notification is a duplicate: dropping
               a distinct resource-complete message leaves the title loader
               waiting forever on its completion lock. */
            wiiu_imports_run_resumed_threads(cpu, 4096u);
            if (guest_range_mapped(cpu, queue, 0x3Cu)) {
                u32 retry_messages = mem_read32(cpu, queue + 0x2Cu);
                u32 retry_size = mem_read32(cpu, queue + 0x30u);
                u32 retry_first = mem_read32(cpu, queue + 0x34u);
                u32 retry_used = mem_read32(cpu, queue + 0x38u);
                if (retry_size != 0u && retry_size <= 0x00100000u &&
                    retry_first < retry_size && retry_used < retry_size &&
                    guest_range_mapped(cpu, retry_messages,
                                       retry_size * 0x10u) &&
                    guest_range_mapped(cpu, message, 0x10u)) {
                    u32 index = (retry_first + retry_used) % retry_size;
                    if ((flags & 2u) != 0u) {
                        index = (retry_first + retry_size - 1u) % retry_size;
                        mem_write32(cpu, queue + 0x34u, index);
                    }
                    guest_copy(cpu, retry_messages + index * 0x10u, message,
                               0x10u);
                    mem_write32(cpu, queue + 0x38u, retry_used + 1u);
                    success = true;
                }
            }
        }
        if (!success && !receive && !peek && !g_running_host_thread &&
            guest_range_mapped(cpu, queue, 0x3Cu)) {
            u32 queued_messages = mem_read32(cpu, queue + 0x2Cu);
            u32 queued_size = mem_read32(cpu, queue + 0x30u);
            u32 queued_first = mem_read32(cpu, queue + 0x34u);
            u32 queued_used = mem_read32(cpu, queue + 0x38u);
            /* A one-entry worker queue can remain full while its currently
               running job performs a cross-core handoff.  On the original
               hardware that work advances concurrently; in this cooperative
               runner, preserve FIFO delivery by extending the backing ring
               after a retry could not make room. */
            if (queued_size != 0u && queued_size < 0x1000u &&
                queued_first < queued_size && queued_used == queued_size &&
                guest_range_mapped(cpu, queued_messages,
                                   queued_size * 0x10u) &&
                guest_range_mapped(cpu, message, 0x10u)) {
                u32 grown_size = queued_size * 2u;
                u32 grown_messages =
                    guest_aux_bump_alloc(grown_size * 0x10u, 4u);
                if (grown_messages != 0u) {
                    guest_set(cpu, grown_messages, 0u, grown_size * 0x10u);
                    for (u32 i = 0u; i < queued_used; i++) {
                        u32 old_index = (queued_first + i) % queued_size;
                        guest_copy(cpu, grown_messages + i * 0x10u,
                                   queued_messages + old_index * 0x10u,
                                   0x10u);
                    }
                    u32 index = queued_used;
                    u32 grown_first = 0u;
                    if ((flags & 2u) != 0u) {
                        index = grown_size - 1u;
                        grown_first = index;
                    }
                    guest_copy(cpu, grown_messages + index * 0x10u, message,
                               0x10u);
                    mem_write32(cpu, queue + 0x2Cu, grown_messages);
                    mem_write32(cpu, queue + 0x30u, grown_size);
                    mem_write32(cpu, queue + 0x34u, grown_first);
                    mem_write32(cpu, queue + 0x38u, queued_used + 1u);
                    success = true;
                    fprintf(stderr,
                            "coreinit: grew worker queue=0x%08X from %u "
                            "to %u to preserve completion\n",
                            queue, queued_size, grown_size);
                }
            }
        }
        if (!success && !receive && !peek &&
            g_message_send_log_count++ < 48u) {
            fprintf(stderr,
                    "coreinit: message queue full queue=0x%08X size=%u "
                    "used=%u first=%u flags=0x%X sender=0x%08X "
                    "caller=0x%08X\n",
                    queue, size, used, first, flags,
                    g_running_host_thread ? g_running_host_thread->object : 0u,
                    cpu->lr);
        }
        if (success && receive && g_running_host_thread &&
            g_message_receive_log_count++ < 24u) {
            fprintf(stderr,
                    "coreinit: thread 0x%08X received queue=0x%08X "
                    "message=[0x%08X,0x%08X,0x%08X,0x%08X]\n",
                    g_running_host_thread->object, queue,
                    safe_read32(cpu, message + 0u),
                    safe_read32(cpu, message + 4u),
                    safe_read32(cpu, message + 8u),
                    safe_read32(cpu, message + 12u));
        }
        if (success && receive) {
            wake_threads_waiting_on_queue(queue, true);
            /* The bounded host slice will yield after this import. */
        } else if (success && !receive && !peek) {
            wake_threads_waiting_on_queue(queue, false);
            /* The bounded host slice will yield after this import. */
        }
        cpu->gpr[3] = success ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "__ghsLock") ||
        name_contains(name, "__ghsUnlock")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "DCZeroRange")) {
        /* dcbz clears whole 32-byte cache lines, including the portions
           preceding/following an unaligned requested range. */
        u32 first = cpu->gpr[3] & ~31u;
        u64 end = ((u64)cpu->gpr[3] + cpu->gpr[4] + 31u) & ~(u64)31u;
        WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
        if (end > first && end <= 0x100000000ull && end - first <= UINT32_MAX) {
            u32 size = (u32)(end - first);
            const WiiUMemorySegment* segment = memory
                ? wiiu_memory_find(memory, first, size) : NULL;
            if (segment && segment->writable)
                memset(segment->data + (wiiu_memory_canonical_address(first) - segment->base), 0, size);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_starts_with(name, "DCFlushRange") ||
        name_starts_with(name, "DCInvalidateRange") ||
        name_starts_with(name, "DCStoreRange") ||
        name_starts_with(name, "ICInvalidateRange")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "__atexit_cleanup") ||
        name_contains(name, "__cpp_exception_cleanup") ||
        name_contains(name, "__cpp_exception_init")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSPanic")) {
        u32 saved_lr = mem_read32(cpu, cpu->gpr[1] + 12u);
        fprintf(stderr, "guest OSPanic lr=0x%08X caller=0x%08X file=",
                cpu->lr, saved_lr);
        guest_print_string(cpu, cpu->gpr[3], stderr);
        fprintf(stderr, " line=%u arg6=0x%08X msg=", cpu->gpr[4],
                cpu->gpr[6]);
        guest_print_string(cpu, cpu->gpr[5], stderr);
        fputc('\n', stderr);
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSInitEvent")) {
        u32 event = cpu->gpr[3];
        if (guest_range_mapped(cpu, event, 0x24u)) {
            guest_set(cpu, event, 0, 0x24u);
            mem_write32(cpu, event + 0x00u, 0x65566E54u);
            if (name_contains(name, "OSInitEventEx"))
                mem_write32(cpu, event + 0x04u, cpu->gpr[6]);
            mem_write32(cpu, event + 0x0Cu, cpu->gpr[4] ? 1u : 0u);
            mem_write32(cpu, event + 0x18u, event);
            mem_write32(cpu, event + 0x20u, cpu->gpr[5]);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSResetEvent") ||
        name_contains(name, "OSSignalEvent") ||
        name_contains(name, "OSWaitEvent")) {
        u32 event = cpu->gpr[3];
        bool is_wait = name_contains(name, "OSWaitEvent");
        bool wait_with_timeout =
            name_contains(name, "OSWaitEventWithTimeout");
        bool is_signal = name_contains(name, "OSSignalEvent");
        bool signal_all = name_contains(name, "OSSignalEventAll");
        bool success = true;

        if (guest_range_mapped(cpu, event, 0x24u)) {
            bool auto_mode = mem_read32(cpu, event + 0x20u) == 1u;
            bool signaled = mem_read32(cpu, event + 0x0Cu) != 0u;

            if (name_contains(name, "OSResetEvent")) {
                mem_write32(cpu, event + 0x0Cu, 0u);
            } else if (is_signal) {
                if (!signaled) {
                    bool woke = wake_threads_waiting_on_event(event,
                                                              signal_all ||
                                                                  !auto_mode);
                    if (!auto_mode || !woke)
                        mem_write32(cpu, event + 0x0Cu, 1u);
                }
            } else if (is_wait) {
                if (g_running_host_thread &&
                    g_running_host_thread->ready_event == event) {
                    g_running_host_thread->ready_event = 0u;
                } else if (signaled) {
                    if (auto_mode)
                        mem_write32(cpu, event + 0x0Cu, 0u);
                } else if (g_running_host_thread && !wait_with_timeout) {
                    release_current_fast_mutexes(cpu);
                    g_running_host_thread->blocked = true;
                    g_running_host_thread->waiting_to_send = false;
                    g_running_host_thread->wait_queue = 0u;
                    g_running_host_thread->wait_event = event;
                    g_running_host_thread->ready_event = 0u;
                    g_running_host_thread->wait_pc = cpu->pc;
                    cpu->pc = HOST_THREAD_YIELD_ADDRESS;
                    if (g_thread_scheduler_log_count++ < 32u) {
                        fprintf(stderr,
                                "coreinit: thread 0x%08X waiting on event "
                                "0x%08X\n",
                                g_running_host_thread->object, event);
                    }
                    log_import_once(g_running_host_thread->wait_pc, name,
                                    true);
                    return true;
                } else if (wait_with_timeout) {
                    success = false;
                } else {
                    /* The main guest thread is cooperatively scheduled too.
                       It must not pass an unsignaled resource-ready event. */
                    return true;
                }
            }
        } else if (wait_with_timeout) {
            success = false;
        }

        cpu->gpr[3] = wait_with_timeout ? (success ? 1u : 0u) : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSFastMutex_Init")) {
        u32 mutex = cpu->gpr[3];
        register_fast_mutex(mutex);
        if (guest_range_mapped(cpu, mutex, 0x2Cu)) {
            guest_set(cpu, mutex, 0, 0x2Cu);
            mem_write32(cpu, mutex + 0x00u, 0x664D7458u);
            mem_write32(cpu, mutex + 0x04u, cpu->gpr[4]);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSFastMutex_TryLock")) {
        u32 mutex = cpu->gpr[3];
        register_fast_mutex(mutex);
        u32 current = g_running_host_thread
                          ? g_running_host_thread->object
                          : WIIU_GUEST_THREAD;
        bool success = false;
        if (guest_range_mapped(cpu, mutex, 0x2Cu)) {
            u32 owner = mem_read32(cpu, mutex + 0x1Cu);
            if (owner == 0u || owner == current) {
                mem_write32(cpu, mutex + 0x1Cu, current);
                mem_write32(cpu, mutex + 0x20u,
                            mem_read32(cpu, mutex + 0x20u) + 1u);
                success = true;
            }
        }
        cpu->gpr[3] = success ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSFastMutex_Lock")) {
        u32 mutex = cpu->gpr[3];
        register_fast_mutex(mutex);
        u32 current = g_running_host_thread
                          ? g_running_host_thread->object
                          : WIIU_GUEST_THREAD;
        if (guest_range_mapped(cpu, mutex, 0x2Cu)) {
            u32 owner = mem_read32(cpu, mutex + 0x1Cu);
            HostThread* owner_thread = find_host_thread(owner);
            /* Cafe OS drops a thread's fast-mutex ownership when it parks
               on a wait queue.  If a cooperative guest worker was already
               parked before the shim observed that transition, repair the
               stale owner here so the app core cannot spin forever. */
            if (owner != 0u && owner != current && owner_thread &&
                owner_thread->blocked) {
                mem_write32(cpu, mutex + 0x1Cu, 0u);
                mem_write32(cpu, mutex + 0x20u, 0u);
                wake_thread_waiting_on_fast_mutex(mutex);
                owner = 0u;
            }
            if (owner == 0u || owner == current) {
                mem_write32(cpu, mutex + 0x1Cu, current);
                mem_write32(cpu, mutex + 0x20u,
                            mem_read32(cpu, mutex + 0x20u) + 1u);
            } else if (g_running_host_thread) {
                /* Fast mutexes protect the loader's shared resource state.
                   A returning call without ownership creates a data race;
                   instead suspend this cooperative guest worker and retry
                   the same import once its owner releases the lock. */
                g_running_host_thread->blocked = true;
                g_running_host_thread->waiting_to_send = false;
                g_running_host_thread->wait_queue = 0u;
                g_running_host_thread->wait_event = 0u;
                g_running_host_thread->wait_fast_mutex = mutex;
                g_running_host_thread->ready_event = 0u;
                g_running_host_thread->wait_pc = cpu->pc;
                cpu->pc = HOST_THREAD_YIELD_ADDRESS;
                if (g_thread_scheduler_log_count++ < 32u) {
                    fprintf(stderr,
                            "coreinit: thread 0x%08X waiting on fast mutex "
                            "0x%08X owned by 0x%08X\n",
                            g_running_host_thread->object, mutex, owner);
                }
                log_import_once(g_running_host_thread->wait_pc, name, true);
                return true;
            } else {
                /* The app context cannot enter the worker wait list.  Keep
                   its PC on the import thunk so the scheduler can run the
                   owner and this lock is retried on the next main slice. */
                return true;
            }
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSFastMutex_Unlock")) {
        u32 mutex = cpu->gpr[3];
        u32 current = g_running_host_thread
                          ? g_running_host_thread->object
                          : WIIU_GUEST_THREAD;
        if (guest_range_mapped(cpu, mutex, 0x2Cu) &&
            mem_read32(cpu, mutex + 0x1Cu) == current) {
            u32 count = mem_read32(cpu, mutex + 0x20u);
            if (count > 0u)
                count--;
            mem_write32(cpu, mutex + 0x20u, count);
            if (count == 0u)
                mem_write32(cpu, mutex + 0x1Cu, 0u);
            if (count == 0u)
                wake_thread_waiting_on_fast_mutex(mutex);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSInitMutex") ||
        name_contains(name, "OSInitCond") ||
        name_contains(name, "OSInitSemaphore")) {
        bool semaphore = name_contains(name, "OSInitSemaphore");
        bool mutex = name_contains(name, "OSInitMutex");
        bool extended = name_contains(name, "Ex");
        u32 object = cpu->gpr[3];
        u32 size = semaphore ? 0x20u : mutex ? 0x2Cu : 0x1Cu;
        /* Cafe synchronization objects are embedded in title objects.
           Clearing 0x40 bytes for all three overwrote adjacent fields. */
        if (guest_range_mapped(cpu, object, size)) {
            guest_set(cpu, object, 0, size);
            mem_write32(cpu, object,
                        semaphore ? 0x73506852u : mutex ? 0x6D557458u : 0x634E6456u);
            mem_write32(cpu, object + 4u,
                        extended ? cpu->gpr[semaphore ? 5 : 4] : 0u);
            if (semaphore)
                mem_write32(cpu, object + 12u, cpu->gpr[4]);
            mem_write32(cpu, object + (semaphore ? 24u : 20u), object);
        }
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSLockMutex") ||
        name_equals(name, "OSTryLockMutex") ||
        name_equals(name, "OSUnlockMutex")) {
        u32 mutex = cpu->gpr[3];
        u32 current = g_running_host_thread
                          ? g_running_host_thread->object : WIIU_GUEST_THREAD;
        bool try_lock = name_equals(name, "OSTryLockMutex");
        bool unlock = name_equals(name, "OSUnlockMutex");
        bool success = false;
        if (guest_range_mapped(cpu, mutex, 0x2Cu)) {
            u32 owner = mem_read32(cpu, mutex + 0x1Cu);
            u32 count = mem_read32(cpu, mutex + 0x20u);
            if (unlock) {
                if (owner == current && count != 0u) {
                    mem_write32(cpu, mutex + 0x20u, --count);
                    if (count == 0u) {
                        mem_write32(cpu, mutex + 0x1Cu, 0u);
                        for (u32 i=0;i<g_host_thread_count;++i) {
                            HostThread* waiter=&g_host_threads[i];
                            if(waiter->blocked && waiter->wait_mutex==mutex) {
                                waiter->blocked=false;
                                waiter->wait_mutex=0;
                                waiter->cpu.pc=waiter->wait_pc;
                            }
                        }
                    }
                }
            } else if (owner == 0u || owner == current) {
                mem_write32(cpu, mutex + 0x1Cu, current);
                mem_write32(cpu, mutex + 0x20u, owner == 0u ? 1u : count + 1u);
                success = true;
            } else if (!try_lock) {
                /* A contended ordinary mutex is a sleeping wait, not a
                   runnable worker that spends every slice polling its owner. */
                if(g_running_host_thread) {
                    g_running_host_thread->blocked=true;
                    g_running_host_thread->wait_mutex=mutex;
                    g_running_host_thread->wait_pc=cpu->pc;
                    cpu->pc=HOST_THREAD_YIELD_ADDRESS;
                }
                return true;
            }
        }
        if (try_lock)
            cpu->gpr[3] = success ? 1u : 0u;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_equals(name, "OSWaitSemaphore") ||
        name_equals(name, "OSTryWaitSemaphore") ||
        name_equals(name, "OSSignalSemaphore") ||
        name_equals(name, "OSGetSemaphoreCount")) {
        u32 semaphore = cpu->gpr[3];
        s32 count = 0;
        if (guest_range_mapped(cpu, semaphore, 0x20u)) {
            count = (s32)mem_read32(cpu, semaphore + 12u);
            if (name_equals(name, "OSWaitSemaphore") && count <= 0) {
                /* Keep the pending call intact; another cooperative thread
                   must signal before this waiter may consume a token. */
                if (g_running_host_thread) {
                    g_running_host_thread->blocked = true;
                    g_running_host_thread->wait_semaphore = semaphore;
                    g_running_host_thread->wait_pc = cpu->pc;
                    cpu->pc = HOST_THREAD_YIELD_ADDRESS;
                }
                return true;
            }
            if (name_equals(name, "OSSignalSemaphore")) {
                mem_write32(cpu, semaphore + 12u, (u32)count + 1u);
                /* Cafe wakes the semaphore queue; waiters retry and only
                   successful waiters consume a token. */
                for (u32 i = 0; i < g_host_thread_count; ++i) {
                    HostThread* thread = &g_host_threads[i];
                    if (thread->blocked && thread->wait_semaphore == semaphore) {
                        thread->blocked = false;
                        thread->wait_semaphore = 0u;
                        thread->cpu.pc = thread->wait_pc;
                    }
                }
            } else if (!name_equals(name, "OSGetSemaphoreCount") && count > 0)
                mem_write32(cpu, semaphore + 12u, (u32)count - 1u);
        }
        cpu->gpr[3] = (u32)count;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "OSSignalCond") ||
        name_contains(name, "OSBroadcastCond")) {
        cpu->gpr[3] = 0;
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    if (name_contains(name, "GX2InitTextureRegs")) {
        log_import_once(cpu->pc, name, true);
        return_to_lr(cpu);
        return true;
    }

    return false;
}

enum {
    HOST_VPAD_A = 0x00008000u,
    HOST_VPAD_B = 0x00004000u,
    HOST_VPAD_X = 0x00002000u,
    HOST_VPAD_Y = 0x00001000u,
    HOST_VPAD_LEFT = 0x00000800u,
    HOST_VPAD_RIGHT = 0x00000400u,
    HOST_VPAD_UP = 0x00000200u,
    HOST_VPAD_DOWN = 0x00000100u,
    HOST_VPAD_ZL = 0x00000080u,
    HOST_VPAD_ZR = 0x00000040u,
    HOST_VPAD_L = 0x00000020u,
    HOST_VPAD_R = 0x00000010u,
    HOST_VPAD_PLUS = 0x00000008u,
    HOST_VPAD_MINUS = 0x00000004u,
    KPAD_PRO_UP = 0x00000001u,
    KPAD_PRO_LEFT = 0x00000002u,
    KPAD_PRO_ZR = 0x00000004u,
    KPAD_PRO_X = 0x00000008u,
    KPAD_PRO_A = 0x00000010u,
    KPAD_PRO_Y = 0x00000020u,
    KPAD_PRO_B = 0x00000040u,
    KPAD_PRO_ZL = 0x00000080u,
    KPAD_PRO_R = 0x00000200u,
    KPAD_PRO_PLUS = 0x00000400u,
    KPAD_PRO_MINUS = 0x00001000u,
    KPAD_PRO_L = 0x00002000u,
    KPAD_PRO_DOWN = 0x00004000u,
    KPAD_PRO_RIGHT = 0x00008000u,
    KPAD_STATUS_DEV_TYPE = 0x5Cu,
    KPAD_STATUS_WPAD_ERROR = 0x5Du,
    KPAD_STATUS_DATA_FORMAT = 0x5Fu,
    KPAD_STATUS_EX = 0x60u,
    KPAD_DEVICE_URCC = 31u,
    KPAD_DATA_FORMAT_URCC = 22u,
};

static u32 map_host_buttons_to_pro(u32 buttons) {
    u32 mapped = 0u;
    if (buttons & HOST_VPAD_A)
        mapped |= KPAD_PRO_A;
    if (buttons & HOST_VPAD_B)
        mapped |= KPAD_PRO_B;
    if (buttons & HOST_VPAD_X)
        mapped |= KPAD_PRO_X;
    if (buttons & HOST_VPAD_Y)
        mapped |= KPAD_PRO_Y;
    if (buttons & HOST_VPAD_LEFT)
        mapped |= KPAD_PRO_LEFT;
    if (buttons & HOST_VPAD_RIGHT)
        mapped |= KPAD_PRO_RIGHT;
    if (buttons & HOST_VPAD_UP)
        mapped |= KPAD_PRO_UP;
    if (buttons & HOST_VPAD_DOWN)
        mapped |= KPAD_PRO_DOWN;
    if (buttons & HOST_VPAD_ZL)
        mapped |= KPAD_PRO_ZL;
    if (buttons & HOST_VPAD_ZR)
        mapped |= KPAD_PRO_ZR;
    if (buttons & HOST_VPAD_L)
        mapped |= KPAD_PRO_L;
    if (buttons & HOST_VPAD_R)
        mapped |= KPAD_PRO_R;
    if (buttons & HOST_VPAD_PLUS)
        mapped |= KPAD_PRO_PLUS;
    if (buttons & HOST_VPAD_MINUS)
        mapped |= KPAD_PRO_MINUS;
    return mapped;
}

static u32 map_pro_buttons_to_host(u32 buttons) {
    u32 mapped = 0u;
    if (buttons & KPAD_PRO_A)
        mapped |= HOST_VPAD_A;
    if (buttons & KPAD_PRO_B)
        mapped |= HOST_VPAD_B;
    if (buttons & KPAD_PRO_X)
        mapped |= HOST_VPAD_X;
    if (buttons & KPAD_PRO_Y)
        mapped |= HOST_VPAD_Y;
    if (buttons & KPAD_PRO_LEFT)
        mapped |= HOST_VPAD_LEFT;
    if (buttons & KPAD_PRO_RIGHT)
        mapped |= HOST_VPAD_RIGHT;
    if (buttons & KPAD_PRO_UP)
        mapped |= HOST_VPAD_UP;
    if (buttons & KPAD_PRO_DOWN)
        mapped |= HOST_VPAD_DOWN;
    if (buttons & KPAD_PRO_ZL)
        mapped |= HOST_VPAD_ZL;
    if (buttons & KPAD_PRO_ZR)
        mapped |= HOST_VPAD_ZR;
    if (buttons & KPAD_PRO_L)
        mapped |= HOST_VPAD_L;
    if (buttons & KPAD_PRO_R)
        mapped |= HOST_VPAD_R;
    if (buttons & KPAD_PRO_PLUS)
        mapped |= HOST_VPAD_PLUS;
    if (buttons & KPAD_PRO_MINUS)
        mapped |= HOST_VPAD_MINUS;
    return mapped;
}

static void write_virtual_pro_status(CPUState* cpu, u32 data) {
    u32 held = 0u;
    u32 pressed = 0u;
    u32 released = 0u;
    float left_x = 0.0f;
    float left_y = 0.0f;
    wiiu_window_read_input(&held, &pressed, &released, &left_x, &left_y);
    if (!g_pro_controller_input) {
        held = pressed = released = 0u;
        left_x = left_y = 0.0f;
    }

    if (g_test_start_sample && g_test_start_pro && wiiu_gx2_has_active_frame()) {
        u32 sample = g_test_input_samples + 1u; /* KPAD is polled before VPAD. */
        if (sample >= g_test_start_sample && sample < g_test_start_sample + 3u) {
            held |= 0x8000u;
            released &= ~0x8000u;
            if (sample == g_test_start_sample) {
                pressed |= 0x8000u;
                fprintf(stderr, "test-start: Pro press A sample=%u\n", sample);
            }
        } else if (sample == g_test_start_sample + 3u) {
            released |= 0x8000u;
            fprintf(stderr, "test-start: Pro release A\n");
        }
    }

    u32 pro_held = map_host_buttons_to_pro(held);
    u32 pro_pressed = map_host_buttons_to_pro(pressed);
    u32 pro_released = map_host_buttons_to_pro(released);
    guest_set(cpu, data, 0, KPAD_STATUS_SIZE);
    mem_write8(cpu, data + KPAD_STATUS_DEV_TYPE, KPAD_DEVICE_URCC);
    mem_write8(cpu, data + KPAD_STATUS_WPAD_ERROR, 0u);
    mem_write8(cpu, data + KPAD_STATUS_DATA_FORMAT, KPAD_DATA_FORMAT_URCC);

    /* KPADStatus.ex_status.uc is the Pro Controller packet Cemu exposes. */
    u32 ex = data + KPAD_STATUS_EX;
    mem_write32(cpu, ex + 0x00u, pro_held);
    mem_write32(cpu, ex + 0x04u, pro_pressed);
    mem_write32(cpu, ex + 0x08u, pro_released);
    mem_write32(cpu, ex + 0x0Cu, dolrecomp_f32_to_bits(left_x));
    mem_write32(cpu, ex + 0x10u, dolrecomp_f32_to_bits(left_y));
    mem_write32(cpu, ex + 0x1Cu, 1u);
    mem_write32(cpu, ex + 0x20u, 1u);

    if (pro_pressed != 0u || pro_released != 0u) {
        fprintf(stderr,
                "input: KPAD Pro held=%08X pressed=%08X released=%08X "
                "stick=(%.2f,%.2f) caller=0x%08X thread=0x%08X\n",
                pro_held, pro_pressed, pro_released, left_x, left_y, cpu->lr,
                g_running_host_thread ? g_running_host_thread->object : 0u);
    }
}

static bool handle_input_import(CPUState* cpu, const char* name) {
    if (name_equals(name, "KPADInit") || name_equals(name, "KPADInitEx")) {
        g_kpad_initialized = true;
        cpu->gpr[3] = 0;
    } else if (name_equals(name, "KPADShutdown")) {
        g_kpad_initialized = false;
    } else if (name_equals(name, "KPADGetMplsWorkSize")) {
        cpu->gpr[3] = KPAD_MPLS_WORK_SIZE;
    } else if (name_equals(name, "KPADSetMplsWorkarea")) {
        g_kpad_mpls_workarea = cpu->gpr[3];
    } else if (name_equals(name, "KPADEnableDPD") ||
               name_equals(name, "KPADDisableDPD")) {
        cpu->gpr[3] = 0;
    } else if (name_equals(name, "KPADRead") ||
               name_equals(name, "KPADReadEx")) {
        u32 channel = cpu->gpr[3];
        u32 data = cpu->gpr[4];
        u32 count = cpu->gpr[5];
        bool valid_controller =
            g_kpad_initialized && channel == 0u && count != 0u &&
            guest_range_mapped(cpu, data, KPAD_STATUS_SIZE);
        if (valid_controller) {
            write_virtual_pro_status(cpu, data);
        } else if (count != 0u && guest_range_mapped(cpu, data,
                                                      KPAD_STATUS_SIZE)) {
            guest_set(cpu, data, 0, KPAD_STATUS_SIZE);
            mem_write8(cpu, data + KPAD_STATUS_DEV_TYPE, 0xFDu);
            mem_write8(cpu, data + KPAD_STATUS_WPAD_ERROR, 0xFFu);
        }
        if (name_equals(name, "KPADReadEx") &&
            guest_range_mapped(cpu, cpu->gpr[6], 4u)) {
            mem_write32(cpu, cpu->gpr[6], valid_controller ? 0u : (u32)-1);
        }
        cpu->gpr[3] = valid_controller ? 1u : 0u;
    } else if (name_equals(name, "VPADGetTPCalibratedPoint") ||
               name_equals(name, "VPADGetTPCalibratedPointEx")) {
        bool extended = name_equals(name, "VPADGetTPCalibratedPointEx");
        u32 output = extended ? cpu->gpr[5] : cpu->gpr[4];
        u32 input = extended ? cpu->gpr[6] : cpu->gpr[5];
        u32 width = 1280u;
        u32 height = 720u;
        if (extended) {
            switch (cpu->gpr[4]) {
            case 0u:
                width = 1920u;
                height = 1080u;
                break;
            case 2u:
                width = 854u;
                height = 480u;
                break;
            default:
                break;
            }
        }
        if (guest_range_mapped(cpu, output, 8u)) {
            if (guest_range_mapped(cpu, input, 8u)) {
                u32 raw_x = mem_read16(cpu, input + 0u);
                u32 raw_y = mem_read16(cpu, input + 2u);
                u32 calibrated_x = raw_x > 92u ? raw_x - 92u : 0u;
                u32 raw_y_inverted = raw_y < 3841u ? 3841u - raw_y : 0u;
                guest_copy(cpu, output, input, 8u);
                mem_write16(cpu, output + 0u,
                            (u16)(((u64)calibrated_x * width) / 3883u));
                mem_write16(cpu, output + 2u,
                            (u16)(((u64)raw_y_inverted * height) / 3694u));
            } else {
                guest_set(cpu, output, 0, 8u);
                mem_write16(cpu, output + 6u, 3u);
            }
        }
    } else if (name_equals(name, "VPADRead")) {
        u32 channel = cpu->gpr[3];
        u32 status = cpu->gpr[4];
        u32 count = cpu->gpr[5];
        bool valid_gamepad = channel == 0u && count != 0u &&
                             guest_range_mapped(cpu, status, VPAD_STATUS_SIZE);
        if (valid_gamepad) {
            u32 held = 0u;
            u32 pressed = 0u;
            u32 released = 0u;
            float left_x = 0.0f;
            float left_y = 0.0f;
            u16 touch_x = 0u;
            u16 touch_y = 0u;
            u32 touch = 0u;
            wiiu_window_read_input(&held, &pressed, &released, &left_x,
                                   &left_y);
            wiiu_window_read_touch(&touch_x, &touch_y, &touch);
            /* A single host controller must not press both Wii U devices.
               Mirroring A into VPAD selected GamePad mode after KPAD had
               already selected Pro, producing the touch-screen prompt. */
            if (g_pro_controller_input) {
                held = pressed = released = touch = 0u;
                left_x = left_y = 0.0f;
            }
            if (g_test_start_sample && wiiu_gx2_has_active_frame()) {
                ++g_test_input_samples;
                if (g_test_start_animation &&
                    (g_test_input_samples < g_test_start_sample + 16u ||
                     g_test_input_samples % 120u == 0u)) {
                    u32 anim = g_test_start_animation;
                    fprintf(stderr, "test-start: Decide sample=%u object=%08X "
                            "frame=%08X speed=%08X flags=%02X enabled=%u ticks=%u\n",
                            g_test_input_samples, anim, safe_read32(cpu, anim+12u),
                            safe_read32(cpu, anim+44u), mem_read8(cpu, anim+51u),
                             mem_read8(cpu, anim+16u), g_test_animation_ticks);
                    if (g_test_start_screen) {
                        u32 screen = g_test_start_screen;
                        u32 vtable = safe_read32(cpu, screen+12u);
                        fprintf(stderr, "test-start: screen=%08X state=%u previous=%u callback=%08X completed=%u\n",
                                screen, mem_read8(cpu, screen+150u), mem_read8(cpu, screen+149u),
                                safe_read32(cpu, vtable+52u), g_test_start_completed);
                    }
                }
                if (g_test_input_samples == g_test_start_sample + 120u ||
                    g_test_input_samples == 1200u) {
                    fprintf(stderr, "test-start: readiness snapshot sample=%u\n",
                            g_test_input_samples);
                    wiiu_imports_dump_threads(cpu);
                }
                if (!g_test_start_pro && g_test_input_samples >= g_test_start_sample &&
                    g_test_input_samples < g_test_start_sample + 3u) {
                    held |= 0x8000u;
                    released &= ~0x8000u;
                    if (g_test_input_samples == g_test_start_sample) {
                        pressed |= 0x8000u;
                        fprintf(stderr, "test-start: press A at input sample=%u\n",
                                g_test_input_samples);
                    }
                } else if (!g_test_start_pro && g_test_input_samples == g_test_start_sample + 3u) {
                    released |= 0x8000u;
                    fprintf(stderr, "test-start: release A\n");
                }
            }
            if (pressed != 0u || released != 0u) {
                fprintf(stderr,
                        "input: VPAD held=%08X pressed=%08X released=%08X "
                        "stick=(%.2f,%.2f) touch=%u caller=0x%08X "
                        "thread=0x%08X\n",
                        held, pressed, released, left_x, left_y, touch,
                        cpu->lr,
                        g_running_host_thread
                            ? g_running_host_thread->object
                            : 0u);
            }
            guest_set(cpu, status, 0, VPAD_STATUS_SIZE);
            mem_write32(cpu, status + 0x00u, held);
            mem_write32(cpu, status + 0x04u, pressed);
            mem_write32(cpu, status + 0x08u, released);
            mem_write32(cpu, status + 0x0Cu,
                        dolrecomp_f32_to_bits(left_x));
            mem_write32(cpu, status + 0x10u,
                        dolrecomp_f32_to_bits(left_y));
            for (u32 sample = 0u; sample < 3u; sample++) {
                u32 touch_data = status + 0x52u + sample * 8u;
                mem_write16(cpu, touch_data + 0u, touch_x);
                mem_write16(cpu, touch_data + 2u, touch_y);
                mem_write16(cpu, touch_data + 4u, touch != 0u ? 1u : 0u);
                mem_write16(cpu, touch_data + 6u, touch != 0u ? 0u : 3u);
            }
            mem_write8(cpu, status + 0xA0u, 0x80u);
            mem_write8(cpu, status + 0xA1u, 0xC0u);
            mem_write8(cpu, status + 0xA3u, 0x80u);
        }
        if (guest_range_mapped(cpu, cpu->gpr[6], 4u))
            mem_write32(cpu, cpu->gpr[6], valid_gamepad ? 0u : (u32)-2);
        cpu->gpr[3] = valid_gamepad ? 1u : 0u;
    } else if (name_equals(name, "WPADProbe")) {
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4],
                        cpu->gpr[3] == 0u ? KPAD_DEVICE_URCC : 0xFDu);
        cpu->gpr[3] = cpu->gpr[3] == 0u ? 0u : (u32)-1;
    } else {
        return false;
    }

    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static bool handle_procui_import(CPUState* cpu, const char* name) {
    if (!name_starts_with(name, "ProcUI"))
        return false;

    if (name_equals(name, "ProcUIInit") || name_equals(name, "ProcUIInitEx")) {
        g_procui_initialized = true;
    } else if (name_equals(name, "ProcUIShutdown")) {
        g_procui_initialized = false;
    } else if (name_equals(name, "ProcUIIsRunning")) {
        cpu->gpr[3] = g_procui_initialized ? 1u : 0u;
    } else if (name_equals(name, "ProcUIInForeground")) {
        cpu->gpr[3] = 1u;
    } else if (name_equals(name, "ProcUIInShutdown")) {
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "ProcUIProcessMessages") ||
               name_equals(name, "ProcUISubProcessMessages")) {
        cpu->gpr[3] = 0u;
    }

    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static void return_u64(CPUState* cpu, u64 value) {
    cpu->gpr[3] = (u32)(value >> 32);
    cpu->gpr[4] = (u32)value;
    return_to_lr(cpu);
}

static u64 system_application_title_id(u32 id, u32 region) {
    static const u64 title_ids[12][3] = {
        {0x0005001010040000ull, 0x0005001010040100ull, 0x0005001010040200ull},
        {0x0005001010047000ull, 0x0005001010047100ull, 0x0005001010047200ull},
        {0x0005001010048000ull, 0x0005001010048100ull, 0x0005001010048200ull},
        {0x0005001010049000ull, 0x0005001010049100ull, 0x0005001010049200ull},
        {0x000500101004A000ull, 0x000500101004A100ull, 0x000500101004A200ull},
        {0x000500101004B000ull, 0x000500101004B100ull, 0x000500101004B200ull},
        {0x000500101004C000ull, 0x000500101004C100ull, 0x000500101004C200ull},
        {0x000500101004D000ull, 0x000500101004D100ull, 0x000500101004D200ull},
        {0x000500101004E000ull, 0x000500101004E100ull, 0x000500101004E200ull},
        {0x0005001B10059000ull, 0x0005001B10059100ull, 0x0005001B10059200ull},
        {0x000500101005A000ull, 0x000500101005A100ull, 0x000500101005A200ull},
        {0x0005001010062000ull, 0x0005001010062100ull, 0x0005001010062200ull},
    };
    if (id >= 12u)
        return 0;
    u32 column = region == 1u ? 0u : ((region == 4u || region == 8u) ? 2u : 1u);
    return title_ids[id][column];
}

static HostAXVoice* ax_find_voice(u32 address) {
    if (address < AX_VOICE_GUEST_BASE)
        return NULL;
    u32 offset = address - AX_VOICE_GUEST_BASE;
    if ((offset % 0x58u) != 0u)
        return NULL;
    u32 index = offset / 0x58u;
    if (index >= AX_VOICE_LIMIT || !g_ax_voices[index].allocated)
        return NULL;
    return &g_ax_voices[index];
}

static void ax_write_offsets(CPUState* cpu, u32 address,
                             const HostAXVoice* voice) {
    if (!guest_range_mapped(cpu, address, 0x14u))
        return;
    mem_write16(cpu, address + 0x00u, voice->format);
    mem_write16(cpu, address + 0x02u, voice->looping);
    mem_write32(cpu, address + 0x04u, voice->loop_offset);
    mem_write32(cpu, address + 0x08u, voice->end_offset);
    mem_write32(cpu, address + 0x0Cu, voice->current_offset);
    mem_write32(cpu, address + 0x10u, voice->data);
}

static void ax_sync_voice(CPUState* cpu, HostAXVoice* voice) {
    if (!voice || !guest_range_mapped(cpu, voice->guest_address, 0x58u))
        return;
    mem_write32(cpu, voice->guest_address + 0x04u, voice->state);
    mem_write32(cpu, voice->guest_address + 0x08u, voice->volume);
    mem_write32(cpu, voice->guest_address + 0x1Cu, voice->priority);
    mem_write32(cpu, voice->guest_address + 0x20u, voice->callback);
    mem_write32(cpu, voice->guest_address + 0x24u, voice->user_context);
    ax_write_offsets(cpu, voice->guest_address + 0x34u, voice);
    mem_write32(cpu, voice->guest_address + 0x48u, voice->callback_ex);
}

static HostAXVoice* ax_acquire_voice(CPUState* cpu, u32 priority,
                                     u32 callback, u32 user_context,
                                     bool extended) {
    HostAXVoice* voice = NULL;
    for (u32 i = 0; i < AX_VOICE_LIMIT; i++) {
        if (!g_ax_voices[i].allocated) {
            voice = &g_ax_voices[i];
            break;
        }
    }
    if (!voice)
        return NULL;

    u32 index = (u32)(voice - g_ax_voices);
    memset(voice, 0, sizeof(*voice));
    voice->allocated = true;
    voice->guest_address = AX_VOICE_GUEST_BASE + index * 0x58u;
    voice->priority = priority;
    voice->callback = extended ? 0u : callback;
    voice->callback_ex = extended ? callback : 0u;
    voice->user_context = user_context;
    voice->ratio = 1.0f;
    voice->volume = 0x8000u;
    for (u32 channel = 0; channel < AX_OUTPUT_CHANNELS; channel++)
        voice->channel_volume[channel] = 0x8000u;

    guest_set(cpu, voice->guest_address, 0, 0x58u);
    mem_write32(cpu, voice->guest_address, index);
    ax_sync_voice(cpu, voice);
    g_ax_voice_log_count++;
    if (g_ax_voice_log_count <= 32u) {
        fprintf(stderr,
                "audio: acquired AX voice %u guest=0x%08X priority=%u "
                "callback=0x%08X context=0x%08X\n",
                index, voice->guest_address, priority, callback,
                user_context);
    }
    return voice;
}

static s16 ax_clamp_sample(s64 value) {
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (s16)value;
}

static bool ax_wrap_or_stop(CPUState* cpu, HostAXVoice* voice) {
    if (voice->current_offset <= voice->end_offset)
        return true;
    if (voice->looping != 0u) {
        voice->current_offset = voice->loop_offset;
        voice->adpcm_pred_scale = voice->loop_pred_scale;
        voice->adpcm_history[0] = voice->loop_history[0];
        voice->adpcm_history[1] = voice->loop_history[1];
        voice->loop_count++;
        return true;
    }

    voice->state = 0u;
    voice->sample_valid = false;
    ax_sync_voice(cpu, voice);
    return false;
}

static bool ax_decode_voice_sample(CPUState* cpu, HostAXVoice* voice,
                                   s16* output) {
    if (!voice || !output || voice->data == 0u ||
        !ax_wrap_or_stop(cpu, voice)) {
        return false;
    }

    if (voice->format == 10u) {
        u32 address = voice->data + voice->current_offset * 2u;
        if (!guest_range_mapped(cpu, address, 2u)) {
            voice->state = 0u;
            return false;
        }
        *output = (s16)mem_read16(cpu, address);
        voice->current_offset++;
        return true;
    }

    if (voice->format == 25u) {
        u32 address = voice->data + voice->current_offset;
        if (!guest_range_mapped(cpu, address, 1u)) {
            voice->state = 0u;
            return false;
        }
        *output = (s16)((s8)mem_read8(cpu, address) * 256);
        voice->current_offset++;
        return true;
    }

    if (voice->format != 0u) {
        voice->state = 0u;
        return false;
    }

    u32 frame_nibble = voice->current_offset & 15u;
    if (frame_nibble < 2u) {
        voice->current_offset += 2u - frame_nibble;
        if (!ax_wrap_or_stop(cpu, voice))
            return false;
        frame_nibble = voice->current_offset & 15u;
    }
    u32 frame_address = voice->data + (voice->current_offset / 16u) * 8u;
    u32 sample_address = voice->data + voice->current_offset / 2u;
    if (!guest_range_mapped(cpu, frame_address, 1u) ||
        !guest_range_mapped(cpu, sample_address, 1u)) {
        voice->state = 0u;
        return false;
    }

    u8 header = mem_read8(cpu, frame_address);
    u8 packed = mem_read8(cpu, sample_address);
    s32 nibble = (voice->current_offset & 1u) != 0u
                     ? (s32)(packed & 0x0Fu)
                     : (s32)(packed >> 4u);
    if (nibble >= 8)
        nibble -= 16;
    u32 predictor = (u32)(header >> 4u) & 7u;
    u32 scale = (u32)header & 15u;
    s64 decoded = ((s64)nibble << scale) << 11u;
    decoded += 1024;
    decoded += (s64)voice->adpcm_coefficients[predictor * 2u] *
               voice->adpcm_history[0];
    decoded += (s64)voice->adpcm_coefficients[predictor * 2u + 1u] *
               voice->adpcm_history[1];
    s16 sample = ax_clamp_sample(decoded >> 11u);
    voice->adpcm_history[1] = voice->adpcm_history[0];
    voice->adpcm_history[0] = sample;
    voice->adpcm_pred_scale = header;
    voice->current_offset++;
    *output = sample;
    return true;
}

static s16 ax_render_voice_sample(CPUState* cpu, HostAXVoice* voice) {
    if (!voice->sample_valid) {
        if (!ax_decode_voice_sample(cpu, voice, &voice->current_sample))
            return 0;
        voice->sample_valid = true;
    }

    s16 output = voice->current_sample;
    double ratio = voice->ratio;
    if (ratio < 0.0)
        ratio = 0.0;
    if (ratio > 16.0)
        ratio = 16.0;
    voice->source_fraction += ratio;
    while (voice->source_fraction >= 1.0) {
        voice->source_fraction -= 1.0;
        if (!ax_decode_voice_sample(cpu, voice, &voice->current_sample)) {
            voice->sample_valid = false;
            break;
        }
    }
    return output;
}

static void ax_mix_voices(CPUState* cpu, s16* stereo, u32 frame_count) {
    s64 left[AX_MIX_FRAMES] = {0};
    s64 right[AX_MIX_FRAMES] = {0};
    if (frame_count > AX_MIX_FRAMES)
        frame_count = AX_MIX_FRAMES;

    for (u32 voice_index = 0; voice_index < AX_VOICE_LIMIT;
         voice_index++) {
        HostAXVoice* voice = &g_ax_voices[voice_index];
        if (!voice->allocated || voice->state == 0u)
            continue;

        for (u32 frame = 0; frame < frame_count && voice->state != 0u;
             frame++) {
            u32 left_gain = voice->mix_configured ? voice->channel_volume[0] : 0x8000u;
            u32 right_gain = voice->mix_configured ? voice->channel_volume[1] : 0x8000u;
            if (voice->mix_configured) {
                /* TV channels are L,R,SL,SR,C,LFE, not WAVEFORMAT order.
                   Downmix center and each surround at -3 dB; omit LFE. */
                left_gain += ((u32)voice->channel_volume[4] * 23170u) / 32768u;
                right_gain += ((u32)voice->channel_volume[4] * 23170u) / 32768u;
                left_gain += ((u32)voice->channel_volume[2] * 23170u) / 32768u;
                right_gain += ((u32)voice->channel_volume[3] * 23170u) / 32768u;
            }

            s64 sample = ax_render_voice_sample(cpu, voice);
            sample = (sample * voice->volume * g_ax_master_volume) /
                     ((s64)0x8000u * 0x8000u);
            left[frame] += (sample * left_gain) / 0x8000u;
            right[frame] += (sample * right_gain) / 0x8000u;
            s32 next_volume = (s32)voice->volume + voice->volume_delta;
            if (next_volume < 0)
                next_volume = 0;
            if (next_volume > 0xFFFF)
                next_volume = 0xFFFF;
            voice->volume = (u16)next_volume;
            for (u32 channel = 0; channel < AX_OUTPUT_CHANNELS; ++channel) {
                s32 gain = (s32)voice->channel_volume[channel] + voice->channel_volume_delta[channel];
                voice->channel_volume[channel] = (u16)(gain < 0 ? 0 : gain > 0xFFFF ? 0xFFFF : gain);
            }
        }
        ax_sync_voice(cpu, voice);
    }

    s32 peak = 0;
    for (u32 frame = 0; frame < frame_count; frame++) {
        stereo[frame * 2u] = ax_clamp_sample(left[frame]);
        stereo[frame * 2u + 1u] = ax_clamp_sample(right[frame]);
        s32 left_abs = stereo[frame * 2u] < 0
                           ? -(s32)stereo[frame * 2u]
                           : stereo[frame * 2u];
        s32 right_abs = stereo[frame * 2u + 1u] < 0
                            ? -(s32)stereo[frame * 2u + 1u]
                            : stereo[frame * 2u + 1u];
        if (left_abs > peak)
            peak = left_abs;
        if (right_abs > peak)
            peak = right_abs;
    }
    if (peak != 0) {
        g_ax_nonzero_mix_count++;
        if (g_ax_nonzero_mix_count <= 12u ||
            (g_ax_nonzero_mix_count % 300u) == 0u) {
            fprintf(stderr, "audio: mixed game samples peak=%d voices=%u\n",
                    peak, g_ax_voice_log_count);
        }
    }
}

static void ax_store_final_mix(CPUState* cpu, const s16* stereo) {
    mem_write32(cpu, AX_FINAL_MIX_PARAM + 0x00u, AX_FINAL_MIX_POINTERS);
    mem_write16(cpu, AX_FINAL_MIX_PARAM + 0x04u, AX_OUTPUT_CHANNELS);
    mem_write16(cpu, AX_FINAL_MIX_PARAM + 0x06u, AX_MIX_FRAMES);
    mem_write16(cpu, AX_FINAL_MIX_PARAM + 0x08u, 1u);
    mem_write16(cpu, AX_FINAL_MIX_PARAM + 0x0Au, AX_OUTPUT_CHANNELS);
    for (u32 channel = 0; channel < AX_OUTPUT_CHANNELS; channel++) {
        u32 samples = AX_FINAL_MIX_SAMPLES +
                      channel * AX_MIX_FRAMES * sizeof(u32);
        mem_write32(cpu, AX_FINAL_MIX_POINTERS + channel * 4u, samples);
        for (u32 frame = 0; frame < AX_MIX_FRAMES; frame++) {
            s32 sample = 0;
            if (channel == 0u)
                sample = (s32)stereo[frame * 2u] << 8u;
            else if (channel == 1u)
                sample = (s32)stereo[frame * 2u + 1u] << 8u;
            mem_write32(cpu, samples + frame * 4u, (u32)sample);
        }
    }
}

static void ax_read_final_mix(CPUState* cpu, s16* stereo) {
    u32 left = mem_read32(cpu, AX_FINAL_MIX_POINTERS + 0u);
    u32 right = mem_read32(cpu, AX_FINAL_MIX_POINTERS + 4u);
    if (!guest_range_mapped(cpu, left, AX_MIX_FRAMES * 4u) ||
        !guest_range_mapped(cpu, right, AX_MIX_FRAMES * 4u)) {
        return;
    }
    for (u32 frame = 0; frame < AX_MIX_FRAMES; frame++) {
        stereo[frame * 2u] =
            ax_clamp_sample((s32)mem_read32(cpu, left + frame * 4u) >> 8u);
        stereo[frame * 2u + 1u] =
            ax_clamp_sample((s32)mem_read32(cpu, right + frame * 4u) >> 8u);
    }
}

static void ax_load_offsets(CPUState* cpu, HostAXVoice* voice,
                            u32 address) {
    if (!voice || !guest_range_mapped(cpu, address, 0x14u))
        return;
    voice->format = mem_read16(cpu, address + 0x00u);
    voice->looping = mem_read16(cpu, address + 0x02u);
    voice->loop_offset = mem_read32(cpu, address + 0x04u);
    voice->end_offset = mem_read32(cpu, address + 0x08u);
    voice->current_offset = mem_read32(cpu, address + 0x0Cu);
    voice->data = mem_read32(cpu, address + 0x10u);
    voice->sample_valid = false;
    voice->source_fraction = 0.0;
    ax_sync_voice(cpu, voice);
}

static void ax_log_voice_update(const char* operation,
                                const HostAXVoice* voice) {
    static u32 update_log_count;
    if (!voice || update_log_count++ >= 96u)
        return;
    fprintf(stderr,
            "audio: %s voice=0x%08X state=%u format=%u data=0x%08X "
            "offset=%u loop=%u end=%u ratio=%.4f volume=%u\n",
            operation, voice->guest_address, voice->state, voice->format,
            voice->data, voice->current_offset, voice->loop_offset,
            voice->end_offset, voice->ratio, voice->volume);
}

static void ax_validate_user_voice_list(CPUState* cpu) {
#if defined(BOTW_ENABLE_LEGACY_SM3DW_SHIMS)
    enum {
        SM3DW_VOICE_LIST_SENTINEL = 0x10E20CF0u,
        SM3DW_VOICE_LIST_LIMIT = AX_VOICE_LIMIT + 1u,
    };

    if (!cpu || cpu->lr != 0x0266C310u ||
        !guest_range_mapped(cpu, SM3DW_VOICE_LIST_SENTINEL, 8u)) {
        return;
    }

    u32 node = safe_read32(cpu, SM3DW_VOICE_LIST_SENTINEL);
    for (u32 count = 0u; count < SM3DW_VOICE_LIST_LIMIT; count++) {
        if (node == SM3DW_VOICE_LIST_SENTINEL)
            return;
        if (node == 0u || !guest_range_mapped(cpu, node, 8u))
            break;
        node = safe_read32(cpu, node);
    }

    mem_write32(cpu, SM3DW_VOICE_LIST_SENTINEL,
                SM3DW_VOICE_LIST_SENTINEL);
    mem_write32(cpu, SM3DW_VOICE_LIST_SENTINEL + 4u,
                SM3DW_VOICE_LIST_SENTINEL);
    fprintf(stderr,
            "audio: repaired invalid nw::snd voice list at 0x%08X\n",
            SM3DW_VOICE_LIST_SENTINEL);
#else
    (void)cpu;
#endif
}

static bool handle_audio_import(CPUState* cpu, const char* name) {
    if (!name_starts_with(name, "AX"))
        return false;

    if (name_equals(name, "AXUserBegin")) {
        ax_validate_user_voice_list(cpu);
    } else if (name_equals(name, "AXUserEnd")) {
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "AXInit") || name_equals(name, "AXInitEx") ||
        name_equals(name, "AXInitWithParams")) {
        g_ax_initialized = true;
        g_ax_frame_clock_started = false;
        g_ax_default_mixer = 1u;
        wiiu_audio_init(32000u);
    } else if (name_equals(name, "AXQuit")) {
        g_ax_initialized = false;
        g_ax_frame_clock_started = false;
        memset(g_ax_final_mix_callbacks, 0,
               sizeof(g_ax_final_mix_callbacks));
        g_ax_app_frame_callback = 0;
        g_ax_frame_callback = 0;
        memset(g_ax_voices, 0, sizeof(g_ax_voices));
        memset(&g_ax_callback, 0, sizeof(g_ax_callback));
        wiiu_audio_shutdown();
    } else if (name_equals(name, "AXIsInit")) {
        cpu->gpr[3] = g_ax_initialized ? 1u : 0u;
    } else if (name_equals(name, "AXGetMaxVoices")) {
        cpu->gpr[3] = g_ax_initialized ? AX_VOICE_LIMIT : 0u;
    } else if (name_equals(name, "AXAcquireVoice") ||
               name_equals(name, "AXAcquireVoiceEx")) {
        HostAXVoice* voice =
            ax_acquire_voice(cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                             name_equals(name, "AXAcquireVoiceEx"));
        cpu->gpr[3] = voice ? voice->guest_address : 0u;
    } else if (name_equals(name, "AXFreeVoice")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            guest_set(cpu, voice->guest_address, 0, 0x58u);
            memset(voice, 0, sizeof(*voice));
        }
    } else if (name_equals(name, "AXGetVoiceOffsets")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice)
            ax_write_offsets(cpu, cpu->gpr[4], voice);
    } else if (name_equals(name, "AXSetVoiceOffsets")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        ax_load_offsets(cpu, voice, cpu->gpr[4]);
        ax_log_voice_update("offsets", voice);
    } else if (name_equals(name, "AXGetVoiceCurrentOffsetEx")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        cpu->gpr[3] = voice ? voice->current_offset : 0u;
    } else if (name_equals(name, "AXGetVoiceLoopCount")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        cpu->gpr[3] = voice ? voice->loop_count : 0u;
    } else if (name_equals(name, "AXIsVoiceRunning")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        cpu->gpr[3] = voice && voice->state != 0u ? 1u : 0u;
    } else if (name_equals(name, "AXSetVoiceState")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            u16 state=cpu->gpr[4] != 0u ? 1u : 0u;
            if (state && !voice->state)
                voice->sample_valid = false;
            voice->state=state;
            ax_sync_voice(cpu, voice);
        }
        ax_log_voice_update("state", voice);
    } else if (name_equals(name, "AXSetVoiceLoop")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            voice->looping = (u16)(cpu->gpr[4] != 0u);
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetVoiceEndOffsetEx")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            voice->end_offset = cpu->gpr[4];
            if (cpu->gpr[5] != 0u)
                voice->data = cpu->gpr[5];
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetVoiceLoopOffsetEx")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            voice->loop_offset = cpu->gpr[4];
            if (cpu->gpr[5] != 0u)
                voice->data = cpu->gpr[5];
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetVoiceSamplesAddr")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            /* current_offset is already beyond the prefetched sample.
               Invalidating an unchanged address skips a sample each update. */
            if(voice->data!=cpu->gpr[4])voice->sample_valid = false;
            voice->data = cpu->gpr[4];
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetVoiceAdpcm")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        u32 adpcm = cpu->gpr[4];
        if (voice && guest_range_mapped(cpu, adpcm, 0x28u)) {
            for (u32 i = 0; i < 16u; i++)
                voice->adpcm_coefficients[i] =
                    (s16)mem_read16(cpu, adpcm + i * 2u);
            voice->adpcm_pred_scale = mem_read16(cpu, adpcm + 0x22u);
            voice->adpcm_history[0] =
                (s16)mem_read16(cpu, adpcm + 0x24u);
            voice->adpcm_history[1] =
                (s16)mem_read16(cpu, adpcm + 0x26u);
            voice->sample_valid = false;
        }
    } else if (name_equals(name, "AXSetVoiceAdpcmLoop")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        u32 loop = cpu->gpr[4];
        if (voice && guest_range_mapped(cpu, loop, 6u)) {
            voice->loop_pred_scale = mem_read16(cpu, loop + 0u);
            voice->loop_history[0] = (s16)mem_read16(cpu, loop + 2u);
            voice->loop_history[1] = (s16)mem_read16(cpu, loop + 4u);
        }
    } else if (name_equals(name, "AXSetVoiceSrc")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice && guest_range_mapped(cpu, cpu->gpr[4], 6u)) {
            voice->ratio =
                (float)mem_read32(cpu, cpu->gpr[4]) / 65536.0f;
            voice->source_fraction =
                (double)mem_read16(cpu, cpu->gpr[4] + 4u) / 65536.0;
        }
    } else if (name_equals(name, "AXSetVoiceSrcRatio")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        float ratio = (float)cpu->fpr[1];
        if (voice && ratio >= 0.0f && ratio <= 16.0f)
            voice->ratio = ratio;
        cpu->gpr[3] = ratio < 0.0f ? (u32)-1
                                   : (ratio > 16.0f ? (u32)-2 : 0u);
    } else if (name_equals(name, "AXSetVoiceVe")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice && guest_range_mapped(cpu, cpu->gpr[4], 4u)) {
            voice->volume = mem_read16(cpu, cpu->gpr[4]);
            voice->volume_delta =
                (s16)mem_read16(cpu, cpu->gpr[4] + 2u);
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetVoiceDeviceMix")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        u32 device = cpu->gpr[4], device_index = cpu->gpr[5], mix = cpu->gpr[6];
        u32 channels = device == 0u ? 6u : device == 1u ? 4u : 1u;
        s32 result = 0;
        if (!voice) result = -4;
        else if (!mix) result = -3;
        else if (device > 2u) result = -1;
        else if (device_index >= (device == 0u ? 1u : device == 1u ? 2u : 4u)) result = -2;
        else if (!guest_range_mapped(cpu, mix, channels * 16u)) result = -3;
        else if (device == 0u) {
            /* AXCHMIX[channel][bus]: four (u16 volume,s16 delta) records
               per channel. r5 is the DEVICE index, not the channel index.
               TV order is L,R,SL,SR,C,LFE. This host currently mixes the
               main bus; aux sends are not dry audio and must not be added. */
            for (u32 channel = 0; channel < AX_OUTPUT_CHANNELS; ++channel) {
                voice->channel_volume[channel] = mem_read16(cpu, mix + channel * 16u);
                voice->channel_volume_delta[channel] = (s16)mem_read16(cpu, mix + channel * 16u + 2u);
            }
            voice->mix_configured = true;
        }
        cpu->gpr[3] = (u32)result;
    } else if (name_equals(name, "AXSetVoicePriority")) {
        HostAXVoice* voice = ax_find_voice(cpu->gpr[3]);
        if (voice) {
            voice->priority = cpu->gpr[4];
            ax_sync_voice(cpu, voice);
        }
    } else if (name_equals(name, "AXSetMasterVolume")) {
        g_ax_master_volume = (u16)cpu->gpr[3];
    } else if (name_equals(name, "AXGetInputSamplesPerFrame")) {
        cpu->gpr[3] = 96u;
    } else if (name_equals(name, "AXGetInputSamplesPerSec")) {
        cpu->gpr[3] = 32000u;
    } else if (name_equals(name, "AXSetDefaultMixerSelect")) {
        g_ax_default_mixer = cpu->gpr[3];
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "AXGetDefaultMixerSelect")) {
        cpu->gpr[3] = g_ax_default_mixer;
    } else if (name_equals(name, "AXGetDeviceFinalMixCallback")) {
        u32 device = cpu->gpr[3];
        u32 callback = device < 3u ? g_ax_final_mix_callbacks[device] : 0u;
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], callback);
        cpu->gpr[3] = device < 3u ? 0u : (u32)-1;
    } else if (name_equals(name, "AXRegisterDeviceFinalMixCallback")) {
        u32 device = cpu->gpr[3];
        if (device < 3u)
            g_ax_final_mix_callbacks[device] = cpu->gpr[4];
        fprintf(stderr,
                "audio: registered final mix callback device=%u "
                "address=0x%08X\n",
                device, cpu->gpr[4]);
        cpu->gpr[3] = device < 3u ? 0u : (u32)-1;
    } else if (name_equals(name, "AXGetDeviceMode")) {
        u32 device = cpu->gpr[3];
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4],
                        device < 3u ? g_ax_device_modes[device] : 0u);
        cpu->gpr[3] = device < 3u ? 0u : (u32)-1;
    } else if (name_equals(name, "AXRegisterAppFrameCallback")) {
        g_ax_app_frame_callback = cpu->gpr[3];
        fprintf(stderr, "audio: registered app frame callback 0x%08X\n",
                g_ax_app_frame_callback);
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "AXDeregisterAppFrameCallback")) {
        if (g_ax_app_frame_callback == cpu->gpr[3])
            g_ax_app_frame_callback = 0u;
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "AXRegisterFrameCallback") ||
               name_equals(name, "AXRegisterCallback")) {
        u32 previous = g_ax_frame_callback;
        g_ax_frame_callback = cpu->gpr[3];
        cpu->gpr[3] = previous;
    } else if (name_equals(name, "AXGetDeviceUpsampleStage")) {
        if (guest_range_mapped(cpu, cpu->gpr[4], 4u))
            mem_write32(cpu, cpu->gpr[4], 0u);
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "AXGetAuxCallback")) {
        if (guest_range_mapped(cpu, cpu->gpr[6], 4u))
            mem_write32(cpu, cpu->gpr[6], 0u);
        if (guest_range_mapped(cpu, cpu->gpr[7], 4u))
            mem_write32(cpu, cpu->gpr[7], 0u);
        cpu->gpr[3] = 0u;
    } else {
        cpu->gpr[3] = 0u;
    }

    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static bool handle_platform_import(CPUState* cpu, const char* name) {
    if (name_equals(name, "__ct__Q3_2nn3olv18DownloadedPostDataFv")) {
        u32 object = cpu->gpr[3];
        if (guest_range_mapped(cpu, object, OLV_DOWNLOADED_POST_DATA_SIZE))
            guest_set(cpu, object, 0, OLV_DOWNLOADED_POST_DATA_SIZE);
        cpu->gpr[3] = object;
    } else if (name_equals(name, "Initialize__Q2_2nn2acFv") ||
               name_equals(name, "Connect__Q2_2nn2acFv")) {
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "GetAssignedAddress__Q2_2nn2acFPUl")) {
        if (guest_range_mapped(cpu, cpu->gpr[3], 4u))
            mem_write32(cpu, cpu->gpr[3], 0x7F000001u);
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "Initialize__Q2_2nn2fpFv")) {
        g_nn_fp_initialized = true;
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "Finalize__Q2_2nn2fpFv")) {
        g_nn_fp_initialized = false;
        g_nn_fp_logged_in = false;
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "IsInitialized__Q2_2nn2fpFv")) {
        cpu->gpr[3] = g_nn_fp_initialized ? 1u : 0u;
    } else if (name_equals(
                   name,
                   "LoginAsync__Q2_2nn2fpFPFQ2_2nn6ResultPv_vPv")) {
        g_nn_fp_logged_in = g_nn_fp_initialized;
        cpu->gpr[3] = g_nn_fp_initialized ? 0u : 0xC0C00580u;
    } else if (name_equals(name, "HasLoggedIn__Q2_2nn2fpFv")) {
        cpu->gpr[3] = g_nn_fp_logged_in ? 1u : 0u;
    } else if (name_equals(name, "IsOnline__Q2_2nn2fpFv")) {
        cpu->gpr[3] = 0u;
    } else if (name_equals(name,
                           "ResultToErrorCode__Q2_2nn2fpFQ2_2nn6Result")) {
        cpu->gpr[3] = cpu->gpr[3] == 0u ? 0u : (cpu->gpr[3] & 0xFFFFFu);
    } else if (name_equals(name, "Initialize__Q2_2nn3actFv")) {
        g_nn_act_initialized = true;
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "Finalize__Q2_2nn3actFv")) {
        g_nn_act_initialized = false;
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "GetSlotNo__Q2_2nn3actFv")) {
        cpu->gpr[3] = 1u;
    } else if (name_equals(name, "IsNetworkAccount__Q2_2nn3actFv") ||
               name_equals(name, "IsNetworkAccountEx__Q2_2nn3actFUc")) {
        cpu->gpr[3] = 1u;
    } else if (name_equals(name,
                           "GetTransferableIdEx__Q2_2nn3actFPULUiUc")) {
        u64 id = 0x424F545757494955ull ^ ((u64)cpu->gpr[4] << 8) ^
                 (u64)(cpu->gpr[5] & 0xFFu);
        if (guest_range_mapped(cpu, cpu->gpr[3], 8u))
            mem_write64(cpu, cpu->gpr[3], id);
        cpu->gpr[3] = 0u;
    } else if (name_equals(name, "_SYSGetSystemApplicationTitleId")) {
        u64 title_id = system_application_title_id(cpu->gpr[3], 2u);
        log_import_once(cpu->pc, name, true);
        return_u64(cpu, title_id);
        return true;
    } else if (name_equals(name,
                           "_SYSGetSystemApplicationTitleIdByProdArea")) {
        u64 title_id = system_application_title_id(cpu->gpr[3], cpu->gpr[4]);
        log_import_once(cpu->pc, name, true);
        return_u64(cpu, title_id);
        return true;
    } else if (name_equals(name, "SYSGetCallerTitleId")) {
        log_import_once(cpu->pc, name, true);
        return_u64(cpu, 0x00050000101C9400ull);
        return true;
    } else if (name_equals(name, "WPADEnableURCC")) {
        g_wpad_urcc_enabled = cpu->gpr[3] != 0;
    } else if (name_equals(name, "WPADIsEnabledURC")) {
        cpu->gpr[3] = g_wpad_urcc_enabled ? 1u : 0u;
    } else if (name_equals(name, "GetResultByPostApp__Q2_2nn3olvFv")) {
        cpu->gpr[3] = 0xA1109680u;
    } else {
        return false;
    }

    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static void write_guest_cstring(CPUState* cpu, u32 address, u32 capacity,
                                const char* value) {
    if (!cpu || !address || capacity == 0u || !value)
        return;
    u32 i = 0u;
    while (i + 1u < capacity && value[i] != 0) {
        mem_write8(cpu, address + i, (u8)value[i]);
        i++;
    }
    mem_write8(cpu, address + i, 0u);
}

static bool handle_aoc_import(CPUState* cpu, const char* name) {
    /* This handler is visited by every later graphics/coreinit import. Do not
       touch the host filesystem until the call is actually an AOC request. */
    if (!name_starts_with(name, "AOC_"))
        return false;

    enum {
        AOC_ERROR_OK = 0,
        AOC_TITLE_SIZE = 0x68,
    };
    const u64 botw_dlc_title_id = 0x0005000C101C9400ull;
    const bool installed = wiiu_filesystem_has_dlc();

    if (name_equals(name, "AOC_CalculateWorkBufferSize")) {
        u32 max_titles = cpu->gpr[3];
        if (max_titles > 256u)
            max_titles = 256u;
        cpu->gpr[3] = 0x80u + max_titles * 0x61u;
    } else if (name_equals(name, "AOC_Initialize") ||
               name_equals(name, "AOC_CloseTitle")) {
        cpu->gpr[3] = AOC_ERROR_OK;
    } else if (name_equals(name, "AOC_ListTitle")) {
        u32 count_out = cpu->gpr[3];
        u32 titles_out = cpu->gpr[4];
        u32 max_titles = cpu->gpr[5];
        u32 count = installed && max_titles != 0u ? 1u : 0u;
        if (count_out && guest_range_mapped(cpu, count_out, 4u))
            mem_write32(cpu, count_out, count);
        if (count != 0u && titles_out != 0u &&
            guest_range_mapped(cpu, titles_out, AOC_TITLE_SIZE)) {
            for (u32 i = 0; i < AOC_TITLE_SIZE; i++)
                mem_write8(cpu, titles_out + i, 0u);
            mem_write32(cpu, titles_out + 0x00u,
                        (u32)(botw_dlc_title_id >> 32));
            mem_write32(cpu, titles_out + 0x04u, (u32)botw_dlc_title_id);
            mem_write32(cpu, titles_out + 0x08u, 0x00001C94u);
            mem_write16(cpu, titles_out + 0x0Cu, 0x0050u);
            write_guest_cstring(cpu, titles_out + 0x0Eu, 88u,
                                "/vol/aoc0005000c101c9400");
        }
        cpu->gpr[3] = AOC_ERROR_OK;
    } else if (name_equals(name, "AOC_OpenTitle")) {
        if (installed && guest_range_mapped(cpu, cpu->gpr[3], 128u)) {
            write_guest_cstring(cpu, cpu->gpr[3], 128u,
                                "/vol/aoc0005000c101c9400");
            cpu->gpr[3] = AOC_ERROR_OK;
        } else {
            cpu->gpr[3] = (u32)-16;
        }
    } else if (name_equals(name, "AOC_GetPurchaseInfo")) {
        u32 purchase_out = cpu->gpr[3];
        u32 entry_count = cpu->gpr[7];
        if (entry_count > 0x10000u)
            entry_count = 0x10000u;
        if (purchase_out &&
            guest_range_mapped(cpu, purchase_out, entry_count * 4u)) {
            for (u32 i = 0; i < entry_count; i++)
                mem_write32(cpu, purchase_out + i * 4u, installed ? 1u : 0u);
        }
        cpu->gpr[3] = AOC_ERROR_OK;
    } else if (name_equals(name, "AOC_DebugRealDeviceAccess")) {
        cpu->gpr[3] = 0u;
    } else {
        return false;
    }

    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static HostZlibStream* find_zlib_stream(u32 guest_address) {
    for (u32 i = 0; i < HOST_ZLIB_STREAM_LIMIT; i++) {
        if (g_zlib_streams[i].active &&
            g_zlib_streams[i].guest_address == guest_address) {
            return &g_zlib_streams[i];
        }
    }
    return NULL;
}

static HostZlibStream* create_zlib_stream(u32 guest_address,
                                          bool inflating) {
    HostZlibStream* existing = find_zlib_stream(guest_address);
    if (existing) {
        if (existing->inflating)
            inflateEnd(&existing->stream);
        else
            deflateEnd(&existing->stream);
        memset(existing, 0, sizeof(*existing));
    }
    for (u32 i = 0; i < HOST_ZLIB_STREAM_LIMIT; i++) {
        HostZlibStream* stream = &g_zlib_streams[i];
        if (stream->active)
            continue;
        memset(stream, 0, sizeof(*stream));
        stream->active = true;
        stream->inflating = inflating;
        stream->guest_address = guest_address;
        stream->stream.zalloc = Z_NULL;
        stream->stream.zfree = Z_NULL;
        stream->stream.opaque = Z_NULL;
        return stream;
    }
    return NULL;
}

static void sync_guest_zlib_state(CPUState* cpu,
                                  const HostZlibStream* stream,
                                  u32 next_in, u32 avail_in,
                                  u32 next_out, u32 avail_out) {
    u32 guest = stream->guest_address;
    u32 index = (u32)(stream - g_zlib_streams);
    mem_write32(cpu, guest + 0x00u, next_in);
    mem_write32(cpu, guest + 0x04u, avail_in);
    mem_write32(cpu, guest + 0x08u, (u32)stream->stream.total_in);
    mem_write32(cpu, guest + 0x0Cu, next_out);
    mem_write32(cpu, guest + 0x10u, avail_out);
    mem_write32(cpu, guest + 0x14u, (u32)stream->stream.total_out);
    mem_write32(cpu, guest + 0x18u, 0u);
    mem_write32(cpu, guest + 0x1Cu, 0xFFF10000u + index * 4u);
    mem_write32(cpu, guest + 0x2Cu, (u32)stream->stream.data_type);
    mem_write32(cpu, guest + 0x30u, (u32)stream->stream.adler);
    mem_write32(cpu, guest + 0x34u, (u32)stream->stream.reserved);
}

static int run_host_zlib(CPUState* cpu, HostZlibStream* stream, int flush) {
    u32 guest = stream->guest_address;
    u32 next_in = mem_read32(cpu, guest + 0x00u);
    u32 avail_in = mem_read32(cpu, guest + 0x04u);
    u32 next_out = mem_read32(cpu, guest + 0x0Cu);
    u32 avail_out = mem_read32(cpu, guest + 0x10u);
    if (avail_in > HOST_ZLIB_IO_LIMIT || avail_out > HOST_ZLIB_IO_LIMIT ||
        (avail_in != 0u && !guest_range_mapped(cpu, next_in, avail_in)) ||
        (avail_out != 0u && !guest_range_mapped(cpu, next_out, avail_out))) {
        return Z_STREAM_ERROR;
    }

    u8* input = (u8*)malloc(avail_in != 0u ? avail_in : 1u);
    u8* output = (u8*)malloc(avail_out != 0u ? avail_out : 1u);
    if (!input || !output) {
        free(input);
        free(output);
        return Z_MEM_ERROR;
    }
    for (u32 i = 0; i < avail_in; i++)
        input[i] = mem_read8(cpu, next_in + i);

    stream->stream.next_in = input;
    stream->stream.avail_in = (uInt)avail_in;
    stream->stream.next_out = output;
    stream->stream.avail_out = (uInt)avail_out;
    int result = stream->inflating ? inflate(&stream->stream, flush)
                                   : deflate(&stream->stream, flush);
    u32 remaining_in = (u32)stream->stream.avail_in;
    u32 remaining_out = (u32)stream->stream.avail_out;
    u32 consumed = avail_in - remaining_in;
    u32 produced = avail_out - remaining_out;
    for (u32 i = 0; i < produced; i++)
        mem_write8(cpu, next_out + i, output[i]);
    sync_guest_zlib_state(cpu, stream, next_in + consumed, remaining_in,
                          next_out + produced, remaining_out);
    free(input);
    free(output);
    return result;
}

static bool handle_zlib_import(CPUState* cpu, const char* name) {
    bool is_inflate_init = name_equals(name, "inflateInit2_");
    bool is_deflate_init = name_equals(name, "deflateInit2_");
    bool is_inflate = name_equals(name, "inflate");
    bool is_deflate = name_equals(name, "deflate");
    bool is_inflate_end = name_equals(name, "inflateEnd");
    bool is_deflate_end = name_equals(name, "deflateEnd");
    if (!is_inflate_init && !is_deflate_init && !is_inflate && !is_deflate &&
        !is_inflate_end && !is_deflate_end) {
        return false;
    }

    u32 guest = cpu->gpr[3];
    HostZlibStream* stream = find_zlib_stream(guest);
    int result = Z_STREAM_ERROR;
    if (is_inflate_init || is_deflate_init) {
        stream = create_zlib_stream(guest, is_inflate_init);
        if (stream) {
            if (is_inflate_init) {
                result = inflateInit2(&stream->stream, (int)cpu->gpr[4]);
            } else {
                result = deflateInit2(&stream->stream, (int)cpu->gpr[4],
                                      (int)cpu->gpr[5], (int)cpu->gpr[6],
                                      (int)cpu->gpr[7], (int)cpu->gpr[8]);
            }
            if (result == Z_OK) {
                sync_guest_zlib_state(cpu, stream,
                                      mem_read32(cpu, guest + 0x00u),
                                      mem_read32(cpu, guest + 0x04u),
                                      mem_read32(cpu, guest + 0x0Cu),
                                      mem_read32(cpu, guest + 0x10u));
            } else {
                memset(stream, 0, sizeof(*stream));
            }
        }
    } else if (stream &&
               stream->inflating == (is_inflate || is_inflate_end)) {
        if (is_inflate || is_deflate) {
            result = run_host_zlib(cpu, stream, (int)cpu->gpr[4]);
        } else {
            result = stream->inflating ? inflateEnd(&stream->stream)
                                       : deflateEnd(&stream->stream);
            mem_write32(cpu, guest + 0x1Cu, 0u);
            memset(stream, 0, sizeof(*stream));
        }
    }

    cpu->gpr[3] = (u32)result;
    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static bool handle_gx2_import(CPUState* cpu, const char* name) {
    if (!name_starts_with(name, "GX2"))
        return false;

    wiiu_gx2_handle_import(cpu, name);
    log_import_once(cpu->pc, name, true);
    return_to_lr(cpu);
    return true;
}

static bool handle_local_bootstrap_call(CPUState* cpu, u32 address) {
    if (address == 0x026BCB80u) {
        NativeYaz0Stream* stream =
            prepare_native_yaz0_stream(cpu->gpr[3] + 148u, cpu->gpr[4],
                                       cpu->gpr[5], cpu->gpr[7], cpu->gpr[8]);
        complete_native_yaz0_from_file(cpu, stream);
        /* Legacy SM3DW-only shim; bounded scheduling supplies the yield. */
    }

    if (address == 0x026BBF1Cu && handle_streaming_yaz0(cpu))
        return true;

    if (address >= 0x02600000u && address < 0x02710000u &&
        g_large_yaz0_candidate_count < 8u) {
        for (u32 reg = 3u; reg <= 8u; reg++) {
            u32 source = cpu->gpr[reg];
            if (!guest_range_mapped(cpu, source, 8u) ||
                mem_read32(cpu, source) != 0x59617A30u) {
                continue;
            }
            u32 output_size = mem_read32(cpu, source + 4u);
            if (output_size <= 32u * 1024u * 1024u)
                continue;
            fprintf(stderr,
                    "shim: large Yaz0 candidate pc=0x%08X lr=0x%08X "
                    "source=r%u=0x%08X size=%u "
                    "r3=0x%08X r4=0x%08X r5=0x%08X r6=0x%08X\n",
                    address, cpu->lr, reg, source, output_size, cpu->gpr[3],
                    cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
            g_large_yaz0_candidate_count++;
            break;
        }
    }

    if ((address == 0x023F60B8u || address == 0x023F60E0u ||
         address == 0x023F61A0u) &&
        g_static_array_log_count++ < 12u) {
        u32 object = address == 0x023F61A0u ? cpu->gpr[3] : 0x10C03A9Cu;
        fprintf(stderr,
                "static array trace pc=0x%08X object=0x%08X "
                "words=[0x%08X,0x%08X,0x%08X] caller=0x%08X\n",
                address, object, safe_read32(cpu, object),
                safe_read32(cpu, object + 4u),
                safe_read32(cpu, object + 8u), cpu->lr);
    }

    if ((address == 0x023F5DF0u || address == 0x023F5E54u) &&
        g_effect_list_log_count++ < 16u) {
        u32 context = address == 0x023F5DF0u ? cpu->gpr[3] : cpu->gpr[26];
        u32 archive = safe_read32(cpu, context + 4u);
        u32 vtable = safe_read32(cpu, archive + 80u);
        u32 index = address == 0x023F5DF0u ? cpu->gpr[6] : cpu->gpr[28];
        u32 entry = 0x10C03A9Cu + index * 272u;
        fprintf(stderr,
                "effect list trace pc=0x%08X context=0x%08X "
                "archive=0x%08X ready=%u vtable=0x%08X "
                "fill=0x%08X index=%u output_count=%u "
                "entry=[0x%08X,0x%08X,0x%08X]\n",
                address, context, archive, mem_read8(cpu, archive + 76u),
                vtable, safe_read32(cpu, vtable + 188u), index,
                address == 0x023F5E54u
                    ? safe_read32(cpu, cpu->gpr[1] + 8u)
                    : 0u,
                safe_read32(cpu, entry), safe_read32(cpu, entry + 4u),
                safe_read32(cpu, entry + 8u));
    }

    if ((address == 0x0238DBE4u || address == 0x0238DBE8u) &&
        (g_effect_fill_log_count++ < 24u ||
         (address == 0x0238DBE8u && cpu->gpr[3] > 64u &&
          g_effect_large_fill_log_count++ < 8u))) {
        u32 object = cpu->gpr[28];
        u32 vtable = safe_read32(cpu, object + 16u);
        u32 output = cpu->gpr[30];
        u32 first_buffer = safe_read32(cpu, output);
        u32 second_buffer = safe_read32(cpu, output + 272u);
        fprintf(stderr,
                "effect fill trace pc=0x%08X object=0x%08X "
                "vtable=0x%08X callback=0x%08X output=0x%08X "
                "capacity=%u result=%u first=\"",
                address, object, vtable, safe_read32(cpu, vtable + 108u),
                output, cpu->gpr[31],
                address == 0x0238DBE8u ? cpu->gpr[3] : 0u);
        guest_print_string(cpu, first_buffer, stderr);
        fputs("\" second=\"", stderr);
        guest_print_string(cpu, second_buffer, stderr);
        fputs("\"\n", stderr);
    }

    if (address == 0x023F5E54u) {
        u32 index = cpu->gpr[28];
        u32 count = safe_read32(cpu, cpu->gpr[1] + 8u);
        u32 entry = 0x10C03A9Cu + index * 272u;
        u32 buffer = safe_read32(cpu, entry);
        bool empty = buffer == 0u || mem_read8(cpu, buffer) == 0u;
        bool edge = index < 3u || index + 3u >= count;
        if (count > 64u && (empty || index >= count || edge) &&
            g_effect_selection_log_count++ < 32u) {
            fprintf(stderr,
                    "effect selection index=%u count=%u entry=0x%08X "
                    "buffer=0x%08X empty=%u name=\"",
                    index, count, entry, buffer, empty ? 1u : 0u);
            guest_print_string(cpu, buffer, stderr);
            fputs("\"\n", stderr);
        }
    }

    if (address == 0x026BB59Cu) {
        u32 object = cpu->gpr[3];
        repair_sarc_name_table(cpu, object);
        u32 cursor = cpu->gpr[4];
        u32 sfat = safe_read32(cpu, object + 44u);
        u32 names = safe_read32(cpu, object + 48u);
        u32 nodes = safe_read32(cpu, object + 56u);
        u32 node_count = sfat == 0u ? 0u : mem_read16(cpu, sfat + 6u);
        bool trace_archive = node_count > 64u &&
                             g_effect_archive_log_count++ < 8u;
        if (node_count >= 700u && g_effect_big_archive_log_count++ < 4u)
            trace_archive = true;
        if (trace_archive) {
            u32 field0 = safe_read32(cpu, nodes + 4u);
            u32 field1 = safe_read32(cpu, nodes + 20u);
            u32 name0 = names + (field0 & 0x00FFFFFFu) * 4u;
            u32 name1 = names + (field1 & 0x00FFFFFFu) * 4u;
            fprintf(stderr,
                    "effect archive object=0x%08X cursor=0x%08X start=%u "
                    "sfat=0x%08X nodes=%u names=0x%08X node_table=0x%08X "
                    "end=0x%08X fields=[0x%08X,0x%08X] names=[\"",
                    object, cursor, safe_read32(cpu, cursor), sfat, node_count,
                    names, nodes, safe_read32(cpu, object + 60u), field0,
                    field1);
            guest_print_string(cpu, name0, stderr);
            fputs("\",\"", stderr);
            guest_print_string(cpu, name1, stderr);
            fputs("\"]\n", stderr);
        }
    }

    if (address == 0x0265697Cu || address == 0x026569B8u) {
        g_parameter_lookup_context = cpu->gpr[3];
        g_parameter_lookup_name = cpu->gpr[4];
    }

    if (address == 0x026569A4u && cpu->gpr[4] == 0xFFFFFFFFu) {
        u32 context = g_parameter_lookup_context;
        u32 lookup_wrapper = safe_read32(cpu, context + 4u);
        u32 lookup = safe_read32(cpu, lookup_wrapper);
        if (g_parameter_lookup_fail_count++ < 32u) {
            fprintf(stderr,
                    "shim: parameter lookup miss name=");
            guest_print_string(cpu, g_parameter_lookup_name, stderr);
            fprintf(stderr,
                    " context=0x%08X lookup=0x%08X table=0x%08X "
                    "values=0x%08X caller=0x%08X\n",
                    context, lookup, safe_read32(cpu, lookup + 56u),
                    safe_read32(cpu, context + 28u),
                    safe_read32(cpu, cpu->gpr[1] + 20u));
        }
        cpu->gpr[3] = 0u;
    }

    if (address == 0x02659F94u && cpu->gpr[4] <= 1u) {
        u32 required_size = cpu->gpr[4] == 0u ? 84u : 104u;
        if (!guest_range_mapped(cpu, cpu->gpr[3], required_size)) {
            if (g_parameter_lookup_fail_count++ < 32u) {
                fprintf(stderr,
                        "shim: skipped missing parameter write "
                        "object=0x%08X mode=%u caller=0x%08X\n",
                        cpu->gpr[3], cpu->gpr[4], cpu->lr);
            }
            return_to_lr(cpu);
            return true;
        }
    }

    if (address == 0x026B2CA8u) {
        s32 result = 0;
        if (native_vsnprintf(cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                            cpu->gpr[6], &result)) {
            g_native_format_count++;
            if (g_native_format_log_count++ < 16u) {
                fprintf(stderr,
                        "shim: native vsnprintf result=%d capacity=%u "
                        "format=",
                        result, cpu->gpr[4]);
                guest_print_string(cpu, cpu->gpr[5], stderr);
                fputc('\n', stderr);
            }
            cpu->gpr[3] = (u32)result;
            return_to_lr(cpu);
            return true;
        }
        g_native_format_fallback_count++;
    }

    if (address == 0x026B2CA8u && cpu->gpr[4] != 0u &&
        !guest_range_mapped(cpu, cpu->gpr[3], cpu->gpr[4])) {
        if (g_format_guard_count++ < 32u) {
            u32 source = cpu->lr == 0x026B2D78u
                             ? safe_read32(cpu, cpu->gpr[1] + 36u)
                             : 0u;
            fprintf(stderr,
                    "shim: rejected invalid format buffer "
                    "dst=0x%08X capacity=0x%08X format=0x%08X "
                    "caller=0x%08X parent=0x%08X grandparent=0x%08X "
                    "source=",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->lr,
                    safe_read32(cpu, cpu->gpr[1] + 124u),
                    safe_read32(cpu, cpu->gpr[1] + 140u));
            guest_print_string(cpu, source, stderr);
            fputc('\n', stderr);
        }
        cpu->gpr[3] = 0xFFFFFFFFu;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x026B2CFCu &&
        !guest_range_mapped(cpu, cpu->gpr[11], 1u)) {
        if (g_format_guard_count++ < 32u) {
            fprintf(stderr,
                    "shim: skipped invalid format terminator "
                    "dst=0x%08X start=0x%08X capacity=0x%08X "
                    "remaining=0x%08X caller=0x%08X\n",
                    cpu->gpr[11], cpu->gpr[31], cpu->gpr[30],
                    safe_read32(cpu, cpu->gpr[1] + 12u),
                    safe_read32(cpu, cpu->gpr[1] + 28u));
        }
        cpu->pc = 0x026B2D00u;
        return true;
    }

    if (address == 0x0251C250u) {
        u32 name_object = cpu->gpr[5];
        u32 name = safe_read32(cpu, name_object);
        u32 length = guest_range_mapped(cpu, name, 1u)
                         ? guest_strlen(cpu, name)
                         : 0u;
        if (length < 4u && g_resource_name_warning_count++ < 32u) {
            fprintf(stderr,
                    "shim: short resource name object=0x%08X ptr=0x%08X "
                    "length=%u context=0x%08X owner=0x%08X caller=0x%08X "
                    "value=",
                    name_object, name, length, cpu->gpr[4], cpu->gpr[3],
                    cpu->lr);
            guest_print_string(cpu, name, stderr);
            fputc('\n', stderr);
        }
    }

    if (address == 0x026FE548u) {
        u32 list = cpu->gpr[3];
        u32 sentinel = list + 4u;
        if (guest_range_mapped(cpu, list, 12u) &&
            mem_read32(cpu, list + 4u) == 0u) {
            mem_write32(cpu, list + 4u, sentinel);
            mem_write32(cpu, list + 8u, sentinel);
            if (g_list_repair_count++ < 8u) {
                fprintf(stderr,
                        "shim: initialized sound list=0x%08X "
                        "sentinel=0x%08X lr=0x%08X\n",
                        list, sentinel, cpu->lr);
            }
        }
    }

    if (address == 0x026FDFE4u && cpu->gpr[3] == 0u) {
        if (g_null_buffer_guard_count++ < 8u) {
            fprintf(stderr,
                    "shim: rejected null sound allocator buffer "
                    "size=0x%X lr=0x%08X\n",
                    cpu->gpr[4], cpu->lr);
        }
        cpu->gpr[3] = 0u;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x024F62A0u &&
        !guest_range_mapped(cpu, cpu->gpr[4], 58u)) {
        if (g_tree_guard_count++ < 8u) {
            fprintf(stderr,
                    "shim: skipped malformed tree destroy node=0x%08X "
                    "lr=0x%08X\n",
                    cpu->gpr[4], cpu->lr);
        }
        return_to_lr(cpu);
        return true;
    }

    if ((address == 0x024F6368u || address == 0x024F6378u) &&
        cpu->lr == 0x024F5B9Cu) {
        u32 candidate = address == 0x024F6368u ? cpu->gpr[3] : cpu->gpr[12];
        u32 child = guest_range_mapped(cpu, candidate, 4u)
                        ? mem_read32(cpu, candidate)
                        : 0u;
        bool malformed = !guest_range_mapped(cpu, candidate, 58u) ||
                         !guest_range_mapped(cpu, child, 58u);
        if (malformed) {
            u32 sentinel = guest_range_mapped(cpu, cpu->gpr[1] + 68u, 4u)
                               ? mem_read32(cpu, cpu->gpr[1] + 68u)
                               : 0u;
            if (g_tree_guard_count++ < 8u) {
                fprintf(stderr,
                        "shim: stopped malformed tree walk entry=0x%08X "
                        "node=0x%08X child=0x%08X sentinel=0x%08X\n",
                        address, candidate, child, sentinel);
            }
            cpu->gpr[3] = sentinel;
            return_to_lr(cpu);
            return true;
        }
    }

    if (address == 0x02373F14u) {
        u32 owner = cpu->gpr[31];
        u32 sentinel = owner + 136u;
        u32 head = safe_read32(cpu, sentinel + 4u);
        u32 object = safe_read32(cpu, head + 8u);
        u32 vtable = safe_read32(cpu, object + 112u);

        if (owner == g_resource_cleanup_last_owner &&
            head == g_resource_cleanup_last_head &&
            object == g_resource_cleanup_last_object) {
            g_resource_cleanup_repeat_count++;
        } else {
            g_resource_cleanup_repeat_count = 0;
            g_resource_cleanup_last_owner = owner;
            g_resource_cleanup_last_head = head;
            g_resource_cleanup_last_object = object;
        }

        if (g_resource_cleanup_log_count++ < 16u) {
            fprintf(stderr,
                    "resource cleanup: owner=0x%08X sentinel=0x%08X "
                    "head=0x%08X next=0x%08X prev=0x%08X "
                    "object=0x%08X self=0x%08X state=%u vtable=0x%08X "
                    "callbacks=[0x%08X,0x%08X,0x%08X] repeat=%u\n",
                    owner, sentinel, head, safe_read32(cpu, head),
                    safe_read32(cpu, head + 4u), object,
                    safe_read32(cpu, object + 60u),
                    safe_read32(cpu, object + 96u), vtable,
                    safe_read32(cpu, vtable + 108u),
                    safe_read32(cpu, vtable + 140u),
                    safe_read32(cpu, vtable + 148u),
                    g_resource_cleanup_repeat_count);
        }
    }

    if (address == 0x0262950Cu && handle_crc16(cpu))
        return true;

    if (address == 0x026BBC20u && handle_yaz0_decompress(cpu))
        return true;

    if (address == 0x0237306Cu && cpu->gpr[5] == 0) {
        if (g_resource_warning_count++ < 16u) {
            fprintf(stderr,
                    "warn: skipped resource-list insert with null object "
                    "owner=0x%08X list=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4]);
        }
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x02373190u && cpu->gpr[4] == 0) {
        if (g_resource_warning_count++ < 16u) {
            fprintf(stderr,
                    "warn: skipped resource state update with null object "
                    "owner=0x%08X state=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[5]);
        }
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x02373C34u && cpu->gpr[3] == 0) {
        if (g_resource_warning_count++ < 16u) {
            fprintf(stderr,
                    "warn: skipped null resource child propagation "
                    "owner=0x%08X\n",
                    cpu->gpr[28]);
        }
        cpu->gpr[4] = 0;
        cpu->pc = 0x02373C68u;
        return true;
    }

    if (address == 0x02373804u && cpu->gpr[3] == 0) {
        if (g_resource_warning_count++ < 16u) {
            u32 descriptor = cpu->gpr[29];
            u32 context = cpu->gpr[31];
            u32 heap_set = safe_read32(cpu, context + 0u);
            u32 heap_index = safe_read32(cpu, heap_set + 20u);
            fprintf(stderr,
                    "warn: resource construction returned null "
                    "descriptor=0x%08X kind=%u constructor=0x%08X "
                    "words=[0x%08X,0x%08X,0x%08X,0x%08X] "
                    "factory=[0x%08X,0x%08X] context=0x%08X "
                    "context_words=[0x%08X,0x%08X,0x%08X] "
                    "heap_set=0x%08X index=%u heap=0x%08X\n",
                    descriptor, safe_read32(cpu, descriptor + 0u),
                    safe_read32(cpu, descriptor + 4u),
                    safe_read32(cpu, descriptor + 8u),
                    safe_read32(cpu, descriptor + 12u),
                    safe_read32(cpu, descriptor + 16u),
                    safe_read32(cpu, descriptor + 20u),
                    safe_read32(cpu, 0x103BE9E8u),
                    safe_read32(cpu, 0x103BE9ECu), context,
                    safe_read32(cpu, context + 0u),
                    safe_read32(cpu, context + 4u),
                    safe_read32(cpu, context + 8u), heap_set, heap_index,
                    safe_read32(cpu, heap_set + heap_index * 4u));
        }
        cpu->gpr[31] = 0;
        cpu->pc = 0x02373844u;
        return true;
    }

    if (address == 0x0237A370u) {
        adjust_framework_heap(cpu, find_framework_block(cpu->gpr[3]));
        return false;
    }

    if (address == 0x026B7160u) {
        log_import_once(address, "sead_assert_bootstrap_noop", true);
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

    // ExpHeap construction executes inside one generated chunk, so the
    // return label is not dispatched through this hook. Register the host
    // backing range at the function entry while its ABI arguments are live.
    if (address == 0x02379A4Cu) {
        register_framework_heap_from_constructor_args(
            cpu, cpu->gpr[3], cpu->gpr[6], cpu->gpr[7], cpu->gpr[8],
            "entry");
        return false;
    }

    if (address == 0x02379B4Cu) {
        register_constructed_framework_heap(cpu, cpu->gpr[31]);
        return false;
    }

    /* ExpHeap::getAllocatableSize(alignment) for root and shim child heaps. */
    if (address == 0x0237B374u &&
        should_shim_framework_heap(cpu->gpr[3])) {
        u32 heap = cpu->gpr[3];
        u32 alignment = cpu->gpr[4];
        u32 available = framework_remaining_capacity(
            find_framework_block(heap), alignment);
        /*
         * The backing arena is larger than this guest ExpHeap's range.  The
         * guest implementation also needs room for its block header and
         * parent allocations: returning the exact range size makes its
         * strict less-than check select an optional global allocator that is
         * not present in the native host.  Mirror the normal free-list query
         * by reserving that bookkeeping before reporting the maximum.
         */
        u32 guest_capacity = safe_read32(cpu, heap + 32u);
        const u64 guest_overhead = (u64)abs_alignment(alignment) + 0x40u +
                                   FRAMEWORK_PARENT_RESERVE;
        /*
         * The bootstrap root is hosted in the larger auxiliary guest arena.
         * Its nominal ExpHeap range only covers the object region in MEM2,
         * so capping it to that placeholder makes legitimate later system
         * heap requests fall into an unavailable global allocator.
         */
        if (heap != 0x18012178u && guest_capacity != 0u) {
            if ((u64)guest_capacity <= guest_overhead)
                available = 0u;
            else {
                u32 guest_available = (u32)((u64)guest_capacity -
                                             guest_overhead);
                if (available > guest_available)
                    available = guest_available;
            }
        }
        if (g_heap_query_log_count++ < 32u) {
            fprintf(stderr,
                    "shim: framework capacity heap=0x%08X align=0x%08X "
                    "result=0x%08X capacity=0x%08X lr=0x%08X\n",
                    heap, alignment, available, guest_capacity, cpu->lr);
        }
        cpu->gpr[3] = available;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x0237A424u) {
        u32 heap = cpu->gpr[3];
        FrameworkBlock* block = find_framework_block(heap);
        if (block && cpu->lr == 0x0237B3F4u) {
            cpu->gpr[3] = framework_remaining_header(cpu, block, 4);
            return_to_lr(cpu);
            return true;
        }
        if (!block && should_shim_framework_heap(heap) &&
            cpu->lr == 0x0237B3F4u) {
            cpu->gpr[3] = framework_aux_remaining_header(cpu, 4);
            return_to_lr(cpu);
            return true;
        }

        u32 base = safe_read32(cpu, heap + 164u);
        u32 sentinel = heap + 152u - base;
        u32 node = safe_read32(cpu, heap + 156u) - base;
        u32 seen[16];
        u32 seen_count = 0;

        for (u32 i = 0; i < 64; i++) {
            if (node == sentinel)
                return false;

            if (node == 0 || node < 0x10000u) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped empty framework search "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "need=0x%08X\n",
                            heap, node + base, sentinel + base, cpu->gpr[4]);
                }
                cpu->gpr[3] = 0;
                return_to_lr(cpu);
                return true;
            }

            bool cycle = false;
            for (u32 j = 0; j < seen_count; j++) {
                if (seen[j] == node) {
                    cycle = true;
                    break;
                }
            }
            if (cycle) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped cyclic framework search "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "need=0x%08X\n",
                            heap, node + base, sentinel + base,
                            cpu->gpr[4]);
                }
                cpu->gpr[3] = 0;
                return_to_lr(cpu);
                return true;
            }

            if (seen_count < (u32)(sizeof(seen) / sizeof(seen[0])))
                seen[seen_count++] = node;

            node = safe_read32(cpu, node + base + 4u) - base;
        }
    }

    if (address == 0x0237A5B4u) {
        u32 heap = cpu->gpr[3];
        FrameworkBlock* block = find_framework_block(heap);
        if (block && cpu->lr == 0x0237B41Cu) {
            cpu->gpr[3] =
                framework_remaining_header(cpu, block, cpu->gpr[5]);
            return_to_lr(cpu);
            return true;
        }
        if (!block && should_shim_framework_heap(heap) &&
            cpu->lr == 0x0237B41Cu) {
            cpu->gpr[3] = framework_aux_remaining_header(cpu, cpu->gpr[5]);
            return_to_lr(cpu);
            return true;
        }

        u32 base = safe_read32(cpu, heap + 164u);
        u32 sentinel = heap + 152u - base;
        u32 node = safe_read32(cpu, heap + 156u) - base;
        u32 seen[16];
        u32 seen_count = 0;

        for (u32 i = 0; i < 64; i++) {
            if (node == sentinel)
                return false;

            if (node == 0 || node < 0x10000u) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped empty framework aligned search "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "need=0x%08X align=0x%08X\n",
                            heap, node + base, sentinel + base,
                            cpu->gpr[4], cpu->gpr[5]);
                }
                cpu->gpr[3] = 0;
                return_to_lr(cpu);
                return true;
            }

            bool cycle = false;
            for (u32 j = 0; j < seen_count; j++) {
                if (seen[j] == node) {
                    cycle = true;
                    break;
                }
            }
            if (cycle) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped cyclic framework aligned search "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "need=0x%08X align=0x%08X\n",
                            heap, node + base, sentinel + base,
                            cpu->gpr[4], cpu->gpr[5]);
                }
                cpu->gpr[3] = 0;
                return_to_lr(cpu);
                return true;
            }

            if (seen_count < (u32)(sizeof(seen) / sizeof(seen[0])))
                seen[seen_count++] = node;

            node = safe_read32(cpu, node + base + 4u) - base;
        }
    }

    if (address == 0x0237A678u && should_shim_framework_heap(cpu->gpr[3])) {
        u32 heap = cpu->gpr[3];
        u32 size = cpu->gpr[4];
        u32 alignment = cpu->gpr[5];
        u32 header = guest_bump_block_header(cpu, cpu->gpr[3], cpu->gpr[4],
                                             cpu->gpr[5]);
        if (g_framework_alloc_count < 16 ||
            (header != 0 && (g_framework_alloc_count % 1024u) == 0)) {
            fprintf(stderr,
                    "shim: framework alloc heap=0x%08X size=0x%08X "
                    "align=0x%08X header=0x%08X payload=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], header,
                    header ? header + 16u : 0);
        }
        if (size >= 0x01000000u && g_large_alloc_log_count++ < 32u) {
            fprintf(stderr,
                    "shim: large framework alloc heap=0x%08X size=0x%08X "
                    "align=0x%08X result=0x%08X lr=0x%08X sp=0x%08X\n",
                    heap, size, alignment, header, cpu->lr, cpu->gpr[1]);
        }
        g_framework_alloc_count++;
        cpu->gpr[3] = header;
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x0237A4F0u && should_shim_framework_heap(cpu->gpr[3])) {
        u32 heap = cpu->gpr[3];
        u32 size = cpu->gpr[4];
        u32 header = guest_bump_block_header(cpu, cpu->gpr[3], cpu->gpr[4], 4);
        if (g_framework_alloc_count < 16 ||
            (header != 0 && (g_framework_alloc_count % 1024u) == 0)) {
            fprintf(stderr,
                    "shim: framework alloc heap=0x%08X size=0x%08X "
                    "align=0x%08X header=0x%08X payload=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4], 4u, header,
                    header ? header + 16u : 0);
        }
        if (size >= 0x01000000u && g_large_alloc_log_count++ < 32u) {
            fprintf(stderr,
                    "shim: large framework alloc heap=0x%08X size=0x%08X "
                    "align=0x%08X result=0x%08X lr=0x%08X sp=0x%08X\n",
                    heap, size, 4u, header, cpu->lr, cpu->gpr[1]);
        }
        g_framework_alloc_count++;
        cpu->gpr[3] = header;
        return_to_lr(cpu);
        return true;
    }

    // This is the framework heap's public release wrapper. The bumped host
    // arena has no guest vtable implementation, so release tracked blocks
    // here before the original code dereferences its virtual callback.
    if (address == 0x0237AEACu &&
        should_shim_framework_heap(cpu->gpr[3])) {
        bool released =
            free_framework_allocation(cpu, cpu->gpr[3], cpu->gpr[4]);
        if (!released && g_framework_warning_count++ < 32u) {
            fprintf(stderr,
                    "shim: ignored untracked framework release "
                    "heap=0x%08X candidate=0x%08X lr=0x%08X\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->lr);
        }
        cpu->gpr[3] = 0u;
        return_to_lr(cpu);
        return true;
    }

    // The following address is the allocator's internal free-list insert,
    // not its public release API. It remains guarded for child shim heaps,
    // whose guest free-list metadata is intentionally bypassed, but root
    // heap ownership is now retired only by the public release wrapper.
    if (address == 0x02379C3Cu && find_framework_block(cpu->gpr[3]) &&
        free_framework_allocation(cpu, cpu->gpr[3], cpu->gpr[4])) {
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x02379C3Cu) {
        u32 heap = cpu->gpr[3];
        u32 base = safe_read32(cpu, heap + 164u);
        u32 sentinel = heap + 152u - base;
        u32 node = safe_read32(cpu, heap + 156u) - base;
        u32 seen[16];
        u32 seen_count = 0;

        for (u32 i = 0; i < 64; i++) {
            u32 absolute = node + base;

            if (node == sentinel)
                return false;

            if (absolute < 0x10000u) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped malformed framework list insert "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "candidate=0x%08X\n",
                            heap, absolute, sentinel + base, cpu->gpr[4]);
                }
                return_to_lr(cpu);
                return true;
            }

            bool cycle = false;
            for (u32 j = 0; j < seen_count; j++) {
                if (seen[j] == node) {
                    cycle = true;
                    break;
                }
            }

            if (cycle) {
                if (g_framework_warning_count++ < 32) {
                    fprintf(stderr,
                            "warn: skipped cyclic framework list insert "
                            "heap=0x%08X node=0x%08X sentinel=0x%08X "
                            "candidate=0x%08X\n",
                            heap, node + base, sentinel + base,
                            cpu->gpr[4]);
                }
                return_to_lr(cpu);
                return true;
            }

            if (seen_count < (u32)(sizeof(seen) / sizeof(seen[0])))
                seen[seen_count++] = node;

            node = safe_read32(cpu, node + base + 4u) - base;
        }

        if (g_framework_warning_count++ < 32) {
            fprintf(stderr,
                    "warn: skipped overlong framework list insert "
                    "heap=0x%08X node=0x%08X sentinel=0x%08X "
                    "candidate=0x%08X\n",
                    heap, node + base, sentinel + base, cpu->gpr[4]);
        }
        return_to_lr(cpu);
        return true;
    }

    if (address == 0x025266ECu && cpu->gpr[4] == 0) {
        fprintf(stderr,
                "warn: skipped callback_list_step with null node "
                "list=0x%08X mode=0x%08X\n",
                cpu->gpr[3], cpu->gpr[5]);
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

    return false;
}

static void erreula_set_state(u32 state) {
    if (g_erreula_state == state)
        return;
    g_erreula_state = state;
    g_erreula_state_changed_at = clock();
    fprintf(stderr, "erreula: state=%u\n", state);
}

static void erreula_update_state(void) {
    if (g_erreula_state != ERREULA_STATE_APPEARING &&
        g_erreula_state != ERREULA_STATE_DISAPPEARING) {
        return;
    }

    clock_t now = clock();
    if (now == (clock_t)-1 || g_erreula_state_changed_at == (clock_t)-1)
        return;
    u64 elapsed_milliseconds =
        (u64)(now - g_erreula_state_changed_at) * 1000u /
        (u64)CLOCKS_PER_SEC;
    if (elapsed_milliseconds < ERREULA_FADE_MILLISECONDS)
        return;

    erreula_set_state(g_erreula_state == ERREULA_STATE_APPEARING
                          ? ERREULA_STATE_VISIBLE
                          : ERREULA_STATE_HIDDEN);
}

static u32 erreula_controller_pressed(CPUState* cpu, u32 controller_info) {
    u32 pressed = 0u;
    if (!cpu || !guest_range_mapped(cpu, controller_info, 0x14u))
        return pressed;

    u32 vpad_status = mem_read32(cpu, controller_info);
    if (guest_range_mapped(cpu, vpad_status, VPAD_STATUS_SIZE))
        pressed |= mem_read32(cpu, vpad_status + 4u);

    for (u32 channel = 0u; channel < 4u; channel++) {
        u32 kpad_status = mem_read32(cpu, controller_info + 4u + channel * 4u);
        if (!guest_range_mapped(cpu, kpad_status, KPAD_STATUS_SIZE))
            continue;
        u32 buttons = mem_read8(cpu, kpad_status + KPAD_STATUS_DATA_FORMAT) ==
                              KPAD_DATA_FORMAT_URCC
                          ? mem_read32(cpu, kpad_status + KPAD_STATUS_EX + 4u)
                          : mem_read32(cpu, kpad_status + 4u);
        pressed |= map_pro_buttons_to_host(buttons);
    }
    return pressed;
}

static void erreula_log_appear(CPUState* cpu, u32 argument) {
    if (g_erreula_appear_log_count++ >= 8u)
        return;

    bool mapped = guest_range_mapped(cpu, argument, 0x2Cu);
    fprintf(stderr,
            "erreula: appear arg=0x%08X mapped=%u caller=0x%08X "
            "sp=0x%08X\n",
            argument, mapped ? 1u : 0u, cpu->lr, cpu->gpr[1]);
    if (!mapped)
        return;

    fprintf(stderr,
            "erreula: request type=%u screen=%u controller=%u hold=%u "
            "code=0x%08X frames=%u text=0x%08X\n",
            mem_read32(cpu, argument), mem_read32(cpu, argument + 4u),
            mem_read32(cpu, argument + 8u), mem_read32(cpu, argument + 12u),
            mem_read32(cpu, argument + 16u), mem_read32(cpu, argument + 20u),
            mem_read32(cpu, argument + 24u));
}

static bool handle_erreula_host_call(CPUState* cpu, u32 address) {
    switch (address) {
    case HOST_ERREULA_CREATE_ADDRESS:
        g_erreula_created = true;
        g_erreula_state = ERREULA_STATE_HIDDEN;
        g_erreula_state_changed_at = clock();
        g_erreula_button_selection = ERREULA_SELECTION_NONE;
        g_erreula_result_code = ERREULA_RESULT_NONE;
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_DESTROY_ADDRESS:
        g_erreula_created = false;
        g_erreula_state = ERREULA_STATE_HIDDEN;
        g_erreula_state_changed_at = 0;
        g_erreula_button_selection = ERREULA_SELECTION_NONE;
        g_erreula_result_code = ERREULA_RESULT_NONE;
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_IS_DECIDED_ADDRESS:
        cpu->gpr[3] = g_erreula_button_selection != ERREULA_SELECTION_NONE
                          ? 1u
                          : 0u;
        break;
    case HOST_ERREULA_IS_LEFT_ADDRESS:
        cpu->gpr[3] = g_erreula_button_selection == ERREULA_SELECTION_LEFT
                          ? 1u
                          : 0u;
        break;
    case HOST_ERREULA_IS_RIGHT_ADDRESS:
        cpu->gpr[3] = g_erreula_button_selection == ERREULA_SELECTION_RIGHT
                          ? 1u
                          : 0u;
        break;
    case HOST_ERREULA_RESULT_CODE_ADDRESS:
        cpu->gpr[3] = g_erreula_result_code;
        break;
    case HOST_ERREULA_RESULT_TYPE_ADDRESS:
        if (g_erreula_result_code == ERREULA_RESULT_NONE)
            cpu->gpr[3] = 0u;
        else if ((s32)g_erreula_result_code < 10)
            cpu->gpr[3] = 1u;
        else if ((s32)g_erreula_result_code >= 9999)
            cpu->gpr[3] = 2u;
        else if (g_erreula_result_code == 40u)
            cpu->gpr[3] = 4u;
        else
            cpu->gpr[3] = 3u;
        break;
    case HOST_ERREULA_APPEAR_ERROR_ADDRESS: {
        u32 argument = cpu->gpr[3];
        g_erreula_button_selection = ERREULA_SELECTION_NONE;
        g_erreula_result_code = ERREULA_RESULT_NONE;
        if (g_erreula_created)
            erreula_set_state(ERREULA_STATE_APPEARING);
        erreula_log_appear(cpu, argument);
        cpu->gpr[3] = 0u;
        break;
    }
    case HOST_ERREULA_DISAPPEAR_ERROR_ADDRESS:
        erreula_update_state();
        if (g_erreula_state == ERREULA_STATE_VISIBLE)
            erreula_set_state(ERREULA_STATE_DISAPPEARING);
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_STATE_ADDRESS:
        erreula_update_state();
        cpu->gpr[3] = g_erreula_state;
        break;
    case HOST_ERREULA_CALC_ADDRESS:
        erreula_update_state();
        if (g_erreula_state == ERREULA_STATE_VISIBLE &&
            g_erreula_button_selection == ERREULA_SELECTION_NONE) {
            u32 held = 0u;
            u32 pressed = 0u;
            u32 released = 0u;
            wiiu_window_read_input(&held, &pressed, &released, NULL, NULL);
            (void)held;
            (void)released;
            pressed |= erreula_controller_pressed(cpu, cpu->gpr[3]);
            if (pressed & (0x00008000u | 0x00000800u)) {
                g_erreula_button_selection = ERREULA_SELECTION_LEFT;
                g_erreula_result_code = 0u;
                fprintf(stderr,
                        "erreula: selected default button from native input\n");
            } else if (pressed & (0x00004000u | 0x00000400u)) {
                g_erreula_button_selection = ERREULA_SELECTION_RIGHT;
                g_erreula_result_code = 1u;
                fprintf(stderr,
                        "erreula: selected alternate button from native input\n");
            }
        }
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_APPEAR_HOME_ADDRESS:
        g_erreula_home_nix_sign_visible = true;
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_CHANGE_LANG_ADDRESS:
        g_erreula_language = cpu->gpr[3];
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_IS_HOME_ADDRESS:
        cpu->gpr[3] = g_erreula_home_nix_sign_visible ? 1u : 0u;
        break;
    case HOST_ERREULA_DISAPPEAR_HOME_ADDRESS:
        g_erreula_home_nix_sign_visible = false;
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_DRAW_TV_ADDRESS:
    case HOST_ERREULA_DRAW_DRC_ADDRESS:
        /* The game's own layout renderer owns these native surfaces. */
        erreula_update_state();
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_SET_CONTROLLER_ADDRESS:
        g_erreula_controller_type = cpu->gpr[3];
        cpu->gpr[3] = 0u;
        break;
    case HOST_ERREULA_IS_CURSOR_ACTIVE_ADDRESS:
        cpu->gpr[3] =
            g_erreula_state == ERREULA_STATE_VISIBLE &&
                    g_erreula_button_selection == ERREULA_SELECTION_NONE
                ? 1u
                : 0u;
        break;
    case HOST_ERREULA_GET_SELECTION_ADDRESS:
        cpu->gpr[3] = g_erreula_button_selection;
        break;
    default:
        return false;
    }

    if (g_erreula_log_count++ < 64u) {
        fprintf(stderr, "erreula: host call=0x%08X state=%u lang=%u\n",
                address, g_erreula_state, g_erreula_language);
    }
    return_to_lr(cpu);
    return true;
}

bool wiiu_imports_host_call(CPUState* cpu, u32 address) {
    if (g_test_start_sample && address == 0x03A3B93Cu && cpu->lr == 0x02F8DE94u) {
        g_test_start_animation = cpu->gpr[3];
        g_test_start_screen = cpu->gpr[31];
    }
    if (g_test_start_screen && !g_test_start_completed && cpu->lr == 0x02F8DF10u &&
        address != 0x02F8DF10u) {
        g_test_start_completed = true;
        fprintf(stderr, "test-start: completion callback=%08X object=%08X sample=%u\n",
                address, cpu->gpr[3], g_test_input_samples);
        g_test_start_new_paths = 0;
    }
    if (g_test_start_animation && address == 0x03A3BE20u &&
        cpu->gpr[3] == g_test_start_animation) {
        if (g_test_animation_ticks++ < 16u)
            fprintf(stderr, "test-start: Decide tick delta=%g lr=%08X\n",
                    cpu->fpr[1], cpu->lr);
    }
    if (g_test_start_sample && !g_running_host_thread && address &&
        g_test_input_samples + 10u >= g_test_start_sample &&
        g_test_input_samples < g_test_start_sample + 120u) {
        u32 slot = ((address >> 2u) ^ (address >> 18u)) & 65535u;
        for (u32 probe = 0; probe < 64u; ++probe, slot = (slot+1u)&65535u) {
            if (g_test_start_seen[slot] == address) break;
            if (g_test_start_seen[slot]) continue;
            g_test_start_seen[slot] = address;
            if (g_test_input_samples >= g_test_start_sample &&
                  g_test_start_new_paths++ < 1024u) {
                fprintf(stderr, "test-start: new path pc=%08X lr=%08X sp=%08X "
                        "r3=%08X r4=%08X r5=%08X r30=%08X r31=%08X\n",
                        address, cpu->lr, cpu->gpr[1], cpu->gpr[3], cpu->gpr[4],
                        cpu->gpr[5], cpu->gpr[30], cpu->gpr[31]);
                u32 sp = cpu->gpr[1];
                for (u32 frame = 0; frame < 3u && guest_range_mapped(cpu, sp, 12u); ++frame) {
                    fprintf(stderr, "test-start: stack[%u] sp=%08X lr=%08X\n",
                            frame, sp, mem_read32(cpu, sp+4u));
                    u32 next = mem_read32(cpu, sp);
                    if (next <= sp || next - sp > 0x100000u) break;
                    sp = next;
                }
            }
            break;
        }
    }
    if ((address==0x033D2B74u || address==0x035CF358u) &&
        wiiu_guest_string_contains(cpu,address)) {
        static u32 string_searches;
        if((++string_searches & (string_searches-1))==0)
            fprintf(stderr,"resource: native string searches=%u entry=%08X\n",string_searches,address);
        return true;
    }
    if ((address == 0x042155D4u || address == 0x04215480u) &&
        handle_botw_yaz0_decompress(cpu, address == 0x042155D4u))
        return true;
    if (address == 0x04215778u) {
        u32 object = cpu->gpr[3], source = cpu->gpr[4];
        if (guest_range_mapped(cpu, object, 24u)) {
            bool fresh = mem_read8(cpu, object + 22u) == 16u;
            if (fresh && cpu->gpr[5] >= 16u && guest_range_mapped(cpu, source, cpu->gpr[5]) &&
                safe_read32(cpu, source) == 0x59617A30u) {
                u32 size = safe_read32(cpu, source + 4u);
                u32 limit = safe_read32(cpu, object + 8u);
                u32 destination = safe_read32(cpu, object);
                if ((limit == 0u || limit == size) && guest_range_mapped(cpu, destination, size)) {
                    prepare_native_yaz0_stream(object, destination, size, 0u, cpu->gpr[5]);
                    return handle_streaming_yaz0(cpu);
                }
            } else if (!fresh && find_native_yaz0_stream(object)) {
                return handle_streaming_yaz0(cpu);
            }
        }
    }
    /* Compare the source chunk around the GX2B loader without changing guest
       state. This distinguishes a bad input record from loader corruption. */
    static u32 texture_trace_sp, texture_trace_root, texture_trace_words[8];
    if (address == 0x03B68170u && cpu->lr == 0x03B614C0u) {
        texture_trace_sp = cpu->gpr[1];
        texture_trace_root = cpu->gpr[31];
        for (u32 i = 0; i < 8u; ++i)
            texture_trace_words[i] = safe_read32(cpu, texture_trace_root + i * 4u);
        static u32 texture_input_count;
        if (texture_input_count++ < 4u) {
            u32 node = texture_trace_root;
            fprintf(stderr, "resource: GX2B input root=%08X destination=%08X data=%08X size=%08X\n",
                    node, cpu->gpr[3], cpu->gpr[8], cpu->gpr[9]);
            for (u32 n = 0; n < 1024u && guest_range_mapped(cpu, node, 32u); ++n) {
                fprintf(stderr, "resource: texture node=%08X words=", node);
                for (u32 i = 0; i < 8u; ++i)
                    fprintf(stderr, "%08X,", safe_read32(cpu, node + i * 4u));
                fputc('\n', stderr);
                u32 offset = safe_read32(cpu, node + (n == 0u ? 8u : 12u));
                if (offset == 0u || offset == UINT32_MAX) break;
                node += offset;
            }
        }
    } else if (address == 0x03B614C0u && texture_trace_sp == cpu->gpr[1]) {
        for (u32 i = 0; i < 8u; ++i) {
            u32 after = safe_read32(cpu, texture_trace_root + i * 4u);
            if (after != texture_trace_words[i])
                fprintf(stderr, "resource: GX2B changed source root=%08X offset=%u before=%08X after=%08X\n",
                        texture_trace_root, i * 4u, texture_trace_words[i], after);
        }
        texture_trace_sp = 0;
    }
    if (address == 0x03B614E4u) {
        static u32 record_trace_count;
        if (record_trace_count++ < 3u) {
            fprintf(stderr, "resource: record walk lr=%08X", cpu->lr);
            for (u32 i = 24; i < 32; ++i)
                fprintf(stderr, " r%u=%08X", i, cpu->gpr[i]);
            fputs(" record=", stderr);
            for (u32 i = 0; i < 64u; i += 4u)
                fprintf(stderr, "%08X,", safe_read32(cpu, cpu->gpr[29] + i));
            fputc('\n', stderr);
            u32 sp = cpu->gpr[1];
            for (u32 i = 0; i < 8u && guest_range_mapped(cpu, sp, 8u); ++i) {
                fprintf(stderr, "resource: record stack sp=%08X lr=%08X\n", sp, safe_read32(cpu, sp + 4u));
                u32 parent = safe_read32(cpu, sp);
                if (parent <= sp) break;
                sp = parent;
            }
        }
    }
    if (address == 0x030971D4u) {
        u32 name = safe_read32(cpu, cpu->gpr[5]);
        if (guest_string_equals(cpu, name, "System/Layout/font_BuildinShader.gsh") ||
            guest_string_equals(cpu, name, "System/Layout/lyt_BuildinShader.gsh")) {
            u32 device = cpu->gpr[3];
            fprintf(stderr, "resource: shader file size device=%08X enabled=%u vtable=%08X callback=%08X fields=",
                    device, mem_read8(cpu, device + 76u), safe_read32(cpu, device + 80u),
                    safe_read32(cpu, safe_read32(cpu, device + 80u) + 164u));
            for (u32 i = 0; i < 128u; i += 4u)
                fprintf(stderr, "%08X,", safe_read32(cpu, device + i));
            fputc('\n', stderr);
        }
    }
    if (address >= 0x037F1B00u && address <= 0x037F2320u) {
        u32 name = safe_read32(cpu, cpu->gpr[27] + 32u);
        if (guest_string_equals(cpu, name, "System/Layout/font_BuildinShader.gsh") ||
            guest_string_equals(cpu, name, "System/Layout/lyt_BuildinShader.gsh")) {
            fprintf(stderr, "resource: shader provider stage=%08X result=%08X provider=%08X device=%08X ctr=%08X\n",
                    address, cpu->gpr[3], cpu->gpr[23], cpu->gpr[26], cpu->ctr);
        }
    }
    if (address == 0x037F6D6Cu || address == 0x037F6E2Cu) {
        u32 handle = address == 0x037F6D6Cu ? cpu->gpr[3] : cpu->gpr[27];
        u32 name = safe_read32(cpu, handle + 644u);
        if (guest_string_equals(cpu, name, "System/Layout/font_BuildinShader.gsh") ||
            guest_string_equals(cpu, name, "System/Layout/lyt_BuildinShader.gsh")) {
            fprintf(stderr, "resource: shader lookup stage=%08X device=%08X archive=%08X name=",
                    address, safe_read32(cpu, handle + 84u), safe_read32(cpu, handle + 624u));
            guest_print_string(cpu, name, stderr);
            if (address == 0x037F6D6Cu) {
                fputs(" candidate=", stderr);
                guest_print_string(cpu, safe_read32(cpu, cpu->gpr[4]), stderr);
            } else {
                fprintf(stderr, " result=%08X size=%08X provider=%08X",
                        safe_read32(cpu, cpu->gpr[1] + 16u), safe_read32(cpu, cpu->gpr[1] + 20u),
                        safe_read32(cpu, cpu->gpr[1] + 24u));
            }
            fputc('\n', stderr);
        }
    }
    if (address == 0x037F15D4u || address == 0x037F15E4u ||
        address == 0x037F15F8u || address == 0x037F161Cu ||
        address == 0x037F1464u || address == 0x037F164Cu ||
        address == 0x037F1668u) {
        u32 caller = safe_read32(cpu, cpu->gpr[1] + 1604u);
        if (caller >= 0x03411500u && caller <= 0x03411700u) {
            fprintf(stderr, "resource: builtin stage=%08X caller=%08X result=%08X handle=",
                    address, caller, cpu->gpr[3]);
            for (u32 i = 0; i < 64u; i += 4u)
                fprintf(stderr, "%08X,", safe_read32(cpu, cpu->gpr[1] + 480u + i));
            fputc('\n', stderr);
        }
    }
    if (address == 0x03411630u || address == 0x0341166Cu) {
        fprintf(stderr, "resource: builtin shader result pc=%08X object=%08X data=%08X size=%u\n",
                address, cpu->gpr[3], safe_read32(cpu, cpu->gpr[3] + 4u),
                safe_read32(cpu, cpu->gpr[3] + 8u));
    }
    if (address == 0x037F0F00u) {
        u32 caller = safe_read32(cpu, cpu->gpr[1] + 1604u);
        if (caller >= 0x03411500u && caller <= 0x03411700u) {
            u32 args = cpu->gpr[1] + 108u;
            fprintf(stderr, "resource: builtin request caller=%08X manager=%08X args=", caller, cpu->gpr[27]);
            for (u32 i = 0; i < 80u; i += 4u)
                fprintf(stderr, "%08X,", safe_read32(cpu, args + i));
            fputs(" name=", stderr);
            guest_print_string(cpu, safe_read32(cpu, args + 72u), stderr);
            fputc('\n', stderr);
        }
    }
    if (address == 0x03C6EE3Cu || address == 0x03C6EE48u) {
        static u32 shader_archive_logs;
        if (shader_archive_logs++ < 16u) {
            u32 source = cpu->gpr[3];
            fprintf(stderr, "gx2: GFD input query=%08X caller=%08X source=%08X "
                    "header=[%08X,%08X,%08X,%08X] arg=%08X\n",
                    address, cpu->lr, source, safe_read32(cpu, source),
                    safe_read32(cpu, source + 4u), safe_read32(cpu, source + 8u),
                    safe_read32(cpu, source + 12u), cpu->gpr[4]);
        }
    }
    if (address == HOST_THREAD_CPP_INIT_RETURN_ADDRESS &&
        g_running_host_thread) {
        HostThread* thread = g_running_host_thread;
        thread->cpp_exception_initialized = true;
        thread->cpu.pc = thread->entry;
        thread->cpu.lr = HOST_THREAD_RETURN_ADDRESS;
        thread->cpu.gpr[3] = thread->argc;
        thread->cpu.gpr[4] = thread->argv;
        if (g_thread_cpp_init_log_count++ < 16u) {
            fprintf(stderr,
                    "coreinit: completed C++ thread init object=0x%08X "
                    "callback=0x%08X globals=0x%08X entry=0x%08X\n",
                    thread->object, thread->cpp_exception_init,
                    thread->cpp_exception_globals, thread->entry);
        }
        return true;
    }

    if (address == HOST_ALARM_RETURN_ADDRESS && g_alarm_callback.active)
        return false;

    if (address == HOST_AUDIO_RETURN_ADDRESS && g_ax_callback.active)
        return false;

    if (address == HOST_THREAD_YIELD_ADDRESS && g_running_host_thread)
        return false;

    if (address == HOST_THREAD_RETURN_ADDRESS && g_running_host_thread) {
        g_running_host_thread->completed = true;
        g_running_host_thread->resumed = false;
        return false;
    }

    if (handle_erreula_host_call(cpu, address))
        return true;

    /* This title-resource constructor stores 516 28-byte records starting
       0x4714 bytes into its destination and then immediately constructs the
       same backing object.  The game only reaches this constructor after an
       allocation failure in the minimal host heap, but it has no null path:
       continuing with r3 == 0 writes a fake object at 0x00004714 and then
       waits forever on its unmapped spin lock.  Give this mandatory setup
       object a real, zeroed auxiliary allocation and let the original code
       populate it normally. */
    if (address == 0x039A608Cu && cpu->gpr[3] == 0u) {
        const u32 fallback_size = 0x10000u;
        u32 fallback = guest_aux_bump_alloc(fallback_size, 0x20u);
        if (fallback != 0u) {
            guest_set(cpu, fallback, 0u, fallback_size);
            cpu->gpr[3] = fallback;
            if (g_framework_warning_count++ < 32u) {
                fprintf(stderr,
                        "resource: recovered required title object "
                        "at 0x%08X using auxiliary guest memory\n",
                        fallback);
            }
        }
    }

    /* std::list-style cleanup uses a self-linked two-word sentinel.  A
       missing optional title resource can leave the containing object null,
       turning the sentinel address into 0x10.  The generated clear routine
       then follows address zero forever.  Substitute a real empty sentinel
       and re-enter the routine at its normal prologue so it takes its usual
       empty-list path. */
    if ((address == 0x0308E9E8u || address == 0x0308E9F8u) &&
        cpu->gpr[3] != 0u && cpu->gpr[3] < 0x10000u) {
        u32 sentinel = guest_aux_bump_alloc(16u, 4u);
        if (sentinel != 0u) {
            guest_set(cpu, sentinel, 0u, 16u);
            mem_write32(cpu, sentinel, sentinel);
            mem_write32(cpu, sentinel + 4u, sentinel);
            cpu->gpr[3] = sentinel;
            cpu->gpr[11] = sentinel;
            cpu->pc = 0x0308E9E8u;
            if (g_framework_warning_count++ < 32u) {
                fprintf(stderr,
                        "resource: recovered null title list sentinel "
                        "at 0x%08X\n", sentinel);
            }
        }
    }

    /* The destructor below walks backward-linked title-resource entries.
       Its node pointer (r29) is required to be non-null.  When the optional
       SystemModel resource is absent, the container reaches this point with
       an empty entry represented as zero instead of its list sentinel; the
       generated loop then reads address 4 indefinitely.  Treat that malformed
       entry as an empty container and continue at the original cleanup path.
       That path clears the remaining list state and returns normally. */
    if (address == 0x03BBFDF0u && cpu->gpr[29] == 0u &&
        cpu->gpr[3] == 0u) {
        cpu->pc = 0x03BBFE08u;
        if (g_framework_warning_count++ < 32u) {
            fprintf(stderr,
                    "resource: skipped null optional title destructor entry\n");
        }
        /* The generic dispatcher selects the original routine from the
           incoming address, so it would otherwise overwrite the redirected
           program counter and re-enter the invalid walk.  Report this call
           handled; the next dispatch resumes the original empty cleanup at
           0x03BBFE08. */
        return true;
    }

    /* The title-resource lookup reaches this address after each node's two
       virtual comparisons.  This is intentionally a dispatcher-visible
       return address: direct branch labels inside a recompiled function do
       not pass through this host-call handler.  With an absent optional
       title model the lookup can follow a cyclic tree forever.  Bound one
       search context and use the routine's built-in "not found" epilogue. */
    if (address == 0x03588DE0u) {
        u32 context = cpu->gpr[28];
        if (context != g_title_lookup_context) {
            g_title_lookup_context = context;
            g_title_lookup_steps = 0u;
        }

        if (++g_title_lookup_steps > 128u) {
            cpu->pc = 0x03588E48u;
            if (g_resource_warning_count++ < 8u) {
                fprintf(stderr,
                        "resource: rejected cyclic optional title lookup "
                        "after %u nodes (context=0x%08X)\n",
                        g_title_lookup_steps,
                        context);
            }
            return true;
        }
    }

    /* A later cleanup walks a circular list whose embedded sentinel is r30.
       Its loop body resumes at 0x034CC958 only when the translated code has
       spent an entire dispatch quantum inside the walk.  This exact startup
       call site can contain an absent optional title entry with a malformed
       cycle (not necessarily a one-node cycle).  Once it has resumed eight
       times, mark that optional list empty and run the original epilogue. */
    if (address == 0x034CC958u && cpu->lr == 0x037FDB30u) {
        u32 node = cpu->gpr[27];
        u32 sentinel = cpu->gpr[30];
        u32 owner = cpu->gpr[29];
        if (node != 0u && node != sentinel &&
            safe_read32(cpu, node + 4u) == node) {
            mem_write32(cpu, node + 4u, sentinel);
            if (g_resource_warning_count++ < 8u) {
                fprintf(stderr,
                        "resource: repaired self-linked optional title "
                        "cleanup node=0x%08X sentinel=0x%08X\n",
                        node,
                        sentinel);
            }
        }

        if (owner != g_title_cleanup_context) {
            g_title_cleanup_context = owner;
            g_title_cleanup_steps = 0u;
        }
        if (++g_title_cleanup_steps > 8u && owner != 0u &&
            owner <= 0xFFFFFFBBu) {
            u32 canonical_sentinel = owner + 64u;
            mem_write32(cpu, owner + 68u, canonical_sentinel);
            cpu->gpr[27] = canonical_sentinel;
            cpu->pc = 0x034CCA0Cu;
            if (g_resource_warning_count++ < 8u) {
                fprintf(stderr,
                        "resource: abandoned cyclic optional title cleanup "
                        "owner=0x%08X sentinel=0x%08X after %u resumes\n",
                        owner,
                        sentinel,
                        g_title_cleanup_steps);
            }
            return true;
        }
    }

    /* This resource-list callback has a C++ "this" object in r3.  The
       standalone allocator path can leave an optional entry unset; the
       original routine then walks r3 + 2024, which becomes the unmapped
       address 0x000007E8 and never reaches its sentinel.  A null optional
       resource has no teardown work, so preserve its zero result and return
       to the caller instead of executing the invalid list walk. */
    if (address == 0x02A11EA0u && cpu->gpr[3] == 0u) {
        if (g_null_resource_list_call_count++ < 4u) {
            fprintf(stderr,
                    "resource: skipped null optional list callback "
                    "return=0x%08X\n",
                    cpu->lr);
        }
        cpu->gpr[3] = 0u;
        return_to_lr(cpu);
        return true;
    }

    /* Trace U-King's allocator wrapper while bringing up the resource
       manager.  The wrapper calls allocator->vtable[13](size, alignment),
       so recording both sides distinguishes a broken allocator object from
       a legitimately exhausted guest heap. */
    if (address == 0x0308E5A0u &&
        (g_allocator_trace_count < 32u || cpu->gpr[3] == 18496u)) {
        u32 allocator = cpu->gpr[4];
        u32 vtable = safe_read32(cpu, allocator + 12u);
        fprintf(stderr,
                "allocator: request size=%u align=%u object=0x%08X "
                "words=[%08X,%08X,%08X,%08X,%08X] vtable=0x%08X "
                "allocate=0x%08X caller=0x%08X\n",
                cpu->gpr[3], cpu->gpr[5], allocator,
                safe_read32(cpu, allocator), safe_read32(cpu, allocator + 4u),
                safe_read32(cpu, allocator + 8u), vtable,
                safe_read32(cpu, allocator + 16u), vtable,
                safe_read32(cpu, vtable + 52u), cpu->lr);
        if (g_allocator_trace_count < 32u)
            g_allocator_trace_count++;
    } else if (address == 0x0308E544u && g_allocator_trace_count < 64u) {
        fprintf(stderr,
                "allocator: result=0x%08X size=%u align=%u return=0x%08X\n",
                cpu->gpr[3], cpu->gpr[31], cpu->gpr[30], cpu->lr);
        g_allocator_trace_count++;
    }

    if ((address == 0x030A7808u || address == 0x030A7820u ||
         address == 0x030A7840u || address == 0x030A7858u) &&
        cpu->gpr[3] == 0u) {
        /* sead::ExpHeap's internal free list can be exhausted even while the
           standalone runtime still has auxiliary guest RAM.  Its allocation
           epilogue expects a 16-byte block header and derives the returned
           pointer from the halfword at header+10.  Supply an equivalent
           zero-offset header so boot-time resource heaps can overflow safely
           instead of constructing objects at address zero. */
        u32 size = cpu->gpr[27];
        u32 alignment = abs_alignment(cpu->gpr[29]);
        u32 reserve = size + alignment + 16u;
        if (reserve >= size) {
            u32 raw = guest_aux_bump_alloc(reserve, 4u);
            if (raw != 0u) {
                u32 header = align_up_u32(raw + 16u, alignment) - 16u;
                guest_set(cpu, header, 0u, 16u);
                cpu->gpr[3] = header;
                if (g_allocator_overflow_count++ < 64u) {
                    fprintf(stderr,
                            "allocator: auxiliary overflow object=0x%08X "
                            "size=%u align=%u header=0x%08X payload=0x%08X\n",
                            cpu->gpr[26], size, alignment, header,
                            header + 16u);
                }
            }
        }
    }

    if ((address == 0x030A7808u || address == 0x030A7820u ||
         address == 0x030A7840u || address == 0x030A7858u ||
         address == 0x030A78C8u) &&
        cpu->gpr[27] == 18496u) {
        fprintf(stderr,
                "allocator: large request stage=0x%08X result=0x%08X "
                "object=0x%08X size=%u align=%u scaled=%u "
                "heap_cursor=0x%08X\n",
                address, cpu->gpr[3], cpu->gpr[26], cpu->gpr[27],
                cpu->gpr[28], cpu->gpr[29], g_heap_cursor);
    }

    if (address == 0x03A83958u && g_resource_dispatch_trace_count < 16u) {
        u32 object = cpu->gpr[3];
        u32 table = safe_read32(cpu, object + 40u);
        fprintf(stderr,
                "resource: dispatch entry object=0x%08X table=0x%08X "
                "caller=0x%08X args=[%08X,%08X] "
                "object_words=[%08X,%08X,%08X,%08X] "
                "table_words=[%08X,%08X,%08X,%08X]\n",
                object, table, cpu->lr, cpu->gpr[5], cpu->gpr[6],
                safe_read32(cpu, object + 32u),
                safe_read32(cpu, object + 36u), table,
                safe_read32(cpu, object + 44u),
                safe_read32(cpu, table + 24u),
                safe_read32(cpu, table + 28u),
                safe_read32(cpu, table + 32u),
                safe_read32(cpu, table + 36u));
        g_resource_dispatch_trace_count++;
    }

    if (address == 0x03A83998u && g_resource_dispatch_trace_count < 32u) {
        fprintf(stderr,
                "resource: dispatch call object=0x%08X table=0x%08X "
                "target=0x%08X result=0x%08X caller=0x%08X\n",
                cpu->gpr[25], cpu->gpr[9], cpu->gpr[10], cpu->gpr[29],
                safe_read32(cpu, cpu->gpr[1] + 68u));
        g_resource_dispatch_trace_count++;
    }

    remember_boot_call(cpu, address);
    trace_boot_call(cpu, address);

    if (address == 0) {
        /* Keep a separate, tightly scoped trace for the SafeString
           comparison path reached during title bootstrap.  The generic
           null-callback log intentionally caps early boot noise, which used
           to hide these later calls. */
        if ((cpu->lr == 0x030B7D30u || cpu->lr == 0x030B7D44u ||
             cpu->lr == 0x030B7D5Cu) &&
            g_startup_string_null_callback_count++ < 16u) {
            u32 object = cpu->gpr[3];
            fprintf(stderr,
                    "startup-string: null virtual call return=0x%08X "
                    "object=0x%08X words=[%08X,%08X,%08X,%08X] "
                    "vtable=0x%08X slot7=0x%08X\n",
                    cpu->lr, object, safe_read32(cpu, object),
                    safe_read32(cpu, object + 4u),
                    safe_read32(cpu, object + 32u),
                    safe_read32(cpu, object + 40u),
                    safe_read32(cpu, object + 4u),
                    safe_read32(cpu, safe_read32(cpu, object + 4u) + 28u));
        }
        log_import_once(address, "null_callback", true);
        trace_null_callback(cpu);
        g_null_callback_count++;
        if (cpu->lr == 0)
            return false;
        cpu->gpr[3] = 0;
        return_to_lr(cpu);
        return true;
    }

#if defined(BOTW_ENABLE_LEGACY_SM3DW_SHIMS)
    if (handle_local_bootstrap_call(cpu, address))
        return true;
#endif

    const RPXSymbol* sym = wiiu_imports_find_symbol(address);
    if (address < 0xC0000000u &&
        (!sym || sym->section_index != RPX_SYMBOL_SECTION_ALIAS)) {
        return false;
    }

    const char* name = sym ? sym->name : NULL;

    if (handle_memory_import(cpu, name) || handle_heap_import(cpu, name) ||
        handle_time_import(cpu, name) || handle_logging_import(cpu, name) ||
        handle_filesystem_import(cpu, name) ||
        handle_system_config_import(cpu, name) ||
        handle_input_import(cpu, name) || handle_procui_import(cpu, name) ||
        handle_aoc_import(cpu, name) || handle_zlib_import(cpu, name) ||
        handle_platform_import(cpu, name) || handle_audio_import(cpu, name) ||
        handle_gx2_import(cpu, name) || handle_coreinit_import(cpu, name)) {
        return true;
    }

    log_import_once(address, name, false);
    g_unknown_import_count++;
    cpu->gpr[3] = 0;
    return_to_lr(cpu);
    return true;
}

static void initialize_host_thread_context(HostThread* thread,
                                            CPUState* main_cpu) {
    u32 r2 = main_cpu->gpr[2];
    u32 r13 = main_cpu->gpr[13];

    thread->cpu = *main_cpu;
    memset(thread->cpu.gpr, 0, sizeof(thread->cpu.gpr));
    memset(thread->cpu.fpr, 0, sizeof(thread->cpu.fpr));
    memset(thread->cpu.ps1, 0, sizeof(thread->cpu.ps1));
    thread->cpp_exception_init = cpp_exception_init_callback(&thread->cpu);
    thread->cpp_exception_globals =
        GUEST_THREAD_CRT_BASE +
        (u32)(thread - g_host_threads) * GUEST_THREAD_CRT_SLOT_SIZE;
    thread->cpp_exception_initialized = thread->cpp_exception_init == 0u;
    thread->cpu.pc = thread->cpp_exception_init != 0u
                         ? thread->cpp_exception_init
                         : thread->entry;
    thread->cpu.lr = thread->cpp_exception_init != 0u
                         ? HOST_THREAD_CPP_INIT_RETURN_ADDRESS
                         : HOST_THREAD_RETURN_ADDRESS;
    thread->cpu.ctr = 0;
    thread->cpu.cr = 0;
    thread->cpu.xer = 0;
    wiiu_imports_init_cpu_context(&thread->cpu);
    thread->cpu.exception = 0;
    thread->cpu.program_exception = 0;
    thread->cpu.reserve_valid = false;
    thread->cpu.external_read_count = 0;
    thread->cpu.external_write_count = 0;
    thread->cpu.gpr[1] = (thread->stack_top - 0x20u) & ~0xFu;
    thread->cpu.gpr[2] = r2;
    thread->cpu.gpr[3] = thread->cpp_exception_init != 0u
                             ? thread->cpp_exception_globals
                             : thread->argc;
    thread->cpu.gpr[4] = thread->cpp_exception_init != 0u ? 0u : thread->argv;
    thread->cpu.gpr[13] = r13;
    if (guest_range_mapped(&thread->cpu, thread->cpu.gpr[1], 0x20u))
        guest_set(&thread->cpu, thread->cpu.gpr[1], 0, 0x20u);
    if (thread->cpp_exception_init != 0u &&
        guest_range_mapped(&thread->cpu, thread->cpp_exception_globals,
                           GUEST_THREAD_CRT_SLOT_SIZE)) {
        guest_set(&thread->cpu, thread->cpp_exception_globals, 0,
                  GUEST_THREAD_CRT_SLOT_SIZE);
    }
    thread->initialized = true;

    if (g_thread_scheduler_log_count++ < 32u) {
        u32 queue = thread->argv + 32u;
        u32 vtable = safe_read32(&thread->cpu, thread->argv + 12u);
        fprintf(stderr,
                "coreinit: scheduling thread object=0x%08X entry=0x%08X "
                "argv=0x%08X sp=0x%08X mode=%u sentinel=0x%08X "
                "callback=0x%08X queue=[messages=0x%08X size=%u used=%u]\n",
                thread->object, thread->entry, thread->argv,
                thread->cpu.gpr[1], safe_read32(&thread->cpu,
                                                thread->argv + 116u),
                safe_read32(&thread->cpu, thread->argv + 120u),
                safe_read32(&thread->cpu, vtable + 132u),
                safe_read32(&thread->cpu, queue + 44u),
                safe_read32(&thread->cpu, queue + 48u),
                safe_read32(&thread->cpu, queue + 56u));
    }

    if (thread->cpp_exception_init != 0u && g_thread_cpp_init_log_count++ < 16u) {
        fprintf(stderr,
                "coreinit: running C++ thread init object=0x%08X "
                "callback=0x%08X globals=0x%08X\n",
                thread->object, thread->cpp_exception_init,
                thread->cpp_exception_globals);
    }
}

static void log_host_thread_progress(HostThread* thread) {
    CPUState* cpu = &thread->cpu;
    u32 handler = safe_read32(cpu, thread->argv + 148u);
    u32 vtable = safe_read32(cpu, handler);
    u32 callback = safe_read32(cpu, vtable + 12u);
    fprintf(stderr,
            "coreinit: thread progress object=0x%08X blocks=%llu "
            "pc=0x%08X lr=0x%08X sp=0x%08X handler=0x%08X "
            "callback=0x%08X r3=0x%08X r4=0x%08X r29=0x%08X "
            "r30=0x%08X r31=0x%08X cr=0x%08X\n",
            thread->object, (unsigned long long)thread->scheduled_blocks,
            cpu->pc, cpu->lr, cpu->gpr[1], handler, callback, cpu->gpr[3],
            cpu->gpr[4], cpu->gpr[29], cpu->gpr[30], cpu->gpr[31], cpu->cr);

    if (cpu->pc == 0x023D2ED8u && cpu->lr == 0x023D3BB0u &&
        g_resource_parse_trace_count++ < 4u) {
        u32 header = cpu->gpr[15];
        u32 record = cpu->gpr[28];
        u32 resource_slot = cpu->gpr[17];
        u32 source = safe_read32(cpu, resource_slot);
        u32 source_offset = safe_read32(cpu, source + 20u);
        u32 target = resource_slot >= 20u ? resource_slot - 20u : 0u;
        u32 parser_sp = cpu->gpr[1];
        u32 handoff_sp = safe_read32(cpu, parser_sp);
        u32 metadata = safe_read32(cpu, handoff_sp + 208u);
        u32 resource = safe_read32(cpu, handoff_sp + 220u);
        u32 resource_vtable = safe_read32(cpu, resource + 16u);
        u32 resource_get_data = safe_read32(cpu, resource_vtable + 60u);
        u32 resource_slot68 = safe_read32(cpu, resource_vtable + 68u);
        u32 resource_slot76 = safe_read32(cpu, resource_vtable + 76u);
        u32 resource_context = safe_read32(cpu, handoff_sp + 224u);
        u32 resource_owner = safe_read32(cpu, handoff_sp + 236u);
        fprintf(stderr,
                "coreinit: endian trace header=0x%08X record=0x%08X "
                "index=0x%08X count=0x%08X swap=%u "
                "header_words=[%08X,%08X,%08X,%08X] "
                "record_words=[%08X,%08X,%08X,%08X]\n",
                header, record, cpu->gpr[29], cpu->gpr[31],
                cpu->gpr[26], safe_read32(cpu, header),
                safe_read32(cpu, header + 4u), safe_read32(cpu, header + 8u),
                safe_read32(cpu, header + 12u), safe_read32(cpu, record),
                safe_read32(cpu, record + 4u), safe_read32(cpu, record + 8u),
                safe_read32(cpu, record + 12u));
        fprintf(stderr,
                "coreinit: resource trace slot=0x%08X target=0x%08X "
                "source=0x%08X canonical=0x%08X offset=0x%08X "
                "computed_header=0x%08X target_words=[%08X,%08X,%08X,%08X] "
                "source_words=[%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X]\n",
                resource_slot, target, source,
                wiiu_memory_canonical_address(source), source_offset,
                source + source_offset + 24u, safe_read32(cpu, target),
                safe_read32(cpu, target + 4u), safe_read32(cpu, target + 8u),
                safe_read32(cpu, target + 12u), safe_read32(cpu, source),
                safe_read32(cpu, source + 4u), safe_read32(cpu, source + 8u),
                safe_read32(cpu, source + 12u), safe_read32(cpu, source + 16u),
                source_offset, safe_read32(cpu, source + 24u),
                safe_read32(cpu, source + 28u));
        fprintf(stderr,
                "coreinit: resource owner trace handoff_sp=0x%08X "
                "resource=0x%08X vtable=0x%08X get_data=0x%08X "
                "slot68=0x%08X slot76=0x%08X context=0x%08X "
                "owner=0x%08X metadata=0x%08X "
                "resource_words=[%08X,%08X,%08X,%08X,%08X,%08X] "
                "metadata_words=[%08X,%08X,%08X,%08X] "
                "target_tail=[%08X,%08X,%08X,%08X]\n",
                handoff_sp, resource, resource_vtable, resource_get_data,
                resource_slot68, resource_slot76, resource_context,
                resource_owner, metadata, safe_read32(cpu, resource),
                safe_read32(cpu, resource + 4u),
                safe_read32(cpu, resource + 8u),
                safe_read32(cpu, resource + 12u), resource_vtable,
                safe_read32(cpu, resource + 20u), safe_read32(cpu, metadata),
                safe_read32(cpu, metadata + 4u), safe_read32(cpu, metadata + 8u),
                safe_read32(cpu, metadata + 12u), safe_read32(cpu, target + 48u),
                safe_read32(cpu, target + 52u), safe_read32(cpu, target + 56u),
                safe_read32(cpu, target + 60u));
    }

    u32 sp = cpu->gpr[1];
    for (u32 depth = 0; depth < 8u; depth++) {
        if (!guest_range_mapped(cpu, sp, 8u))
            break;
        u32 caller_sp = safe_read32(cpu, sp);
        if (caller_sp <= sp || !guest_range_mapped(cpu, caller_sp, 8u))
            break;
        fprintf(stderr, "  guest stack[%u] sp=0x%08X return=0x%08X\n",
                depth, sp, safe_read32(cpu, caller_sp + 4u));
        sp = caller_sp;
    }
}

void wiiu_imports_dump_threads(CPUState* main_cpu) {
    const char* enabled = getenv("BOTW_TRACE_THREADS");
    if (!enabled || !enabled[0] || strcmp(enabled, "0") == 0) return;
    for (u32 index = 0; index <= g_host_thread_count; ++index) {
        HostThread* thread = index ? &g_host_threads[index-1u] : NULL;
        if (thread && (!thread->initialized || thread->completed)) continue;
        CPUState* cpu = thread ? &thread->cpu : main_cpu;
        if (!cpu) continue;
        fprintf(stderr, "thread snapshot object=%08X pc=%08X lr=%08X blocked=%u queue=%08X event=%08X wait_pc=%08X",
                thread ? thread->object : WIIU_GUEST_THREAD, cpu->pc, cpu->lr,
                thread ? thread->blocked : 0u, thread ? thread->wait_queue : 0u,
                thread ? thread->wait_event : 0u, thread ? thread->wait_pc : 0u);
        for (u32 r = 3; r < 32u; ++r) fprintf(stderr, " r%u=%08X", r, cpu->gpr[r]);
        if (thread) fprintf(stderr, " semaphore=%08X sleeping=%u wake_tick=%llu",
                thread->wait_semaphore, thread->sleeping,
                (unsigned long long)thread->wake_tick);
        fputc('\n', stderr);
        if (!thread && (cpu->pc == 0x02E3D614u || cpu->pc == 0x030DF674u)) {
            u32 first = cpu->pc == 0x030DF674u ? 24u : 26u;
            for (u32 r = first; r <= 28u; r += 2u) {
                u32 owner = cpu->gpr[r];
                if (!guest_range_mapped(cpu, owner, 128u)) continue;
                fprintf(stderr, "dependency state r%u=%08X:", r, owner);
                for (u32 offset = 0u; offset < 128u; offset += 4u)
                    fprintf(stderr, " %08X", safe_read32(cpu, owner+offset));
                fputc('\n', stderr);
            }
        }
        u32 sp = cpu->gpr[1];
        for (u32 depth = 0; depth < 12u && guest_range_mapped(cpu, sp, 8u); ++depth) {
            fprintf(stderr, "thread stack object=%08X sp=%08X saved_lr=%08X\n",
                    thread ? thread->object : WIIU_GUEST_THREAD, sp, safe_read32(cpu, sp+4u));
            u32 parent = safe_read32(cpu, sp);
            if (parent <= sp) break;
            sp = parent;
        }
    }
}

void wiiu_imports_run_resumed_threads(CPUState* main_cpu,
                                      u32 blocks_per_thread) {
    if (!main_cpu || blocks_per_thread == 0)
        return;

    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        if (!thread->resumed || thread->completed)
            continue;
        if (thread->sleeping &&
            (s64)(thread->cpu.timebase - thread->wake_tick) >= 0) {
            thread->sleeping = false;
            thread->blocked = false;
            thread->cpu.pc = thread->wait_pc;
        }
        if (thread->blocked)
            continue;
        if (!thread->initialized)
            initialize_host_thread_context(thread, main_cpu);

        u64 previous_blocks = thread->scheduled_blocks;
        g_running_host_thread = thread;
        int still_running =
            run_timed_guest_dispatches(&thread->cpu, blocks_per_thread);
        thread->scheduled_blocks += blocks_per_thread;
        g_running_host_thread = NULL;

        if ((previous_blocks >> 24u) != (thread->scheduled_blocks >> 24u))
            log_host_thread_progress(thread);

        if (!still_running && !thread->completed && !thread->blocked) {
            thread->completed = true;
            thread->resumed = false;
            fprintf(stderr,
                    "warn: guest thread stopped object=0x%08X pc=0x%08X "
                    "lr=0x%08X exception=0x%08X\n",
                    thread->object, thread->cpu.pc, thread->cpu.lr,
                    thread->cpu.exception);
        }
    }
}

void wiiu_imports_advance_timebase(u64 ticks) {
    if (ticks == 0u)
        return;

    for (u32 i = 0; i < g_host_thread_count; i++) {
        HostThread* thread = &g_host_threads[i];
        if (thread->initialized)
            thread->cpu.timebase += ticks;
    }
    if (g_ax_callback.active)
        g_ax_callback.cpu.timebase += ticks;
    if (g_alarm_callback.active)
        g_alarm_callback.cpu.timebase += ticks;
}

static void begin_alarm_callback(CPUState* main_cpu,
                                 const HostAlarm* alarm) {
    CPUState* cpu = &g_alarm_callback.cpu;

    *cpu = *main_cpu;
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    memset(cpu->fpr, 0, sizeof(cpu->fpr));
    memset(cpu->ps1, 0, sizeof(cpu->ps1));
    cpu->pc = alarm->handler & ~3u;
    cpu->lr = HOST_ALARM_RETURN_ADDRESS;
    cpu->ctr = 0u;
    cpu->cr = 0u;
    cpu->xer = 0u;
    cpu->fpscr = 0u;
    cpu->exception = 0u;
    cpu->program_exception = 0u;
    cpu->reserve_valid = false;
    cpu->external_read_count = 0u;
    cpu->external_write_count = 0u;
    cpu->gpr[1] = (HOST_ALARM_STACK_TOP - 0x20u) & ~0xFu;
    cpu->gpr[2] = main_cpu->gpr[2];
    cpu->gpr[3] = alarm->alarm;
    cpu->gpr[13] = main_cpu->gpr[13];
    if (guest_range_mapped(cpu, cpu->gpr[1], 0x20u))
        guest_set(cpu, cpu->gpr[1], 0, 0x20u);

    g_alarm_callback.active = true;
    g_alarm_callback.alarm = alarm->alarm;
    g_alarm_callback.handler = alarm->handler;
    if (g_alarm_log_count++ < 32u) {
        fprintf(stderr,
                "coreinit: running alarm callback alarm=0x%08X handler=0x%08X\n",
                alarm->alarm, alarm->handler);
    }
}

static bool run_alarm_callback_slice(u32 block_limit) {
    int still_running =
        run_timed_guest_dispatches(&g_alarm_callback.cpu, block_limit);
    if (still_running)
        return false;

    if (g_alarm_callback.cpu.pc != HOST_ALARM_RETURN_ADDRESS) {
        fprintf(stderr,
                "coreinit: alarm callback 0x%08X stopped at pc=0x%08X "
                "lr=0x%08X exception=0x%08X\n",
                g_alarm_callback.handler, g_alarm_callback.cpu.pc,
                g_alarm_callback.cpu.lr, g_alarm_callback.cpu.exception);
    }
    g_alarm_callback.active = false;
    return true;
}

void wiiu_imports_run_alarms(CPUState* main_cpu, u32 callback_blocks) {
    if (!main_cpu || callback_blocks == 0u)
        return;

    for (u32 dispatches = 0u; dispatches < 4u; dispatches++) {
        if (g_alarm_callback.active) {
            if (!run_alarm_callback_slice(callback_blocks))
                return;
            continue;
        }

        HostAlarm* alarm = take_due_alarm(main_cpu);
        if (!alarm)
            return;
        if (alarm->handler == 0u)
            continue;
        begin_alarm_callback(main_cpu, alarm);
    }
}

static void ax_begin_callback(CPUState* main_cpu, u32 address, u32 argument,
                              HostAXCallbackPhase phase) {
    CPUState* cpu = &g_ax_callback.cpu;
    *cpu = *main_cpu;
    memset(cpu->gpr, 0, sizeof(cpu->gpr));
    memset(cpu->fpr, 0, sizeof(cpu->fpr));
    memset(cpu->ps1, 0, sizeof(cpu->ps1));
    cpu->pc = address;
    cpu->lr = HOST_AUDIO_RETURN_ADDRESS;
    cpu->ctr = 0u;
    cpu->cr = 0u;
    cpu->xer = 0u;
    cpu->fpscr = 0u;
    cpu->exception = 0u;
    cpu->program_exception = 0u;
    cpu->reserve_valid = false;
    cpu->external_read_count = 0u;
    cpu->external_write_count = 0u;
    cpu->gpr[1] = (AX_CALLBACK_STACK_TOP - 0x20u) & ~0xFu;
    cpu->gpr[2] = main_cpu->gpr[2];
    cpu->gpr[3] = argument;
    cpu->gpr[13] = main_cpu->gpr[13];
    if (guest_range_mapped(cpu, cpu->gpr[1], 0x20u))
        guest_set(cpu, cpu->gpr[1], 0, 0x20u);
    g_ax_callback.active = true;
    g_ax_callback.phase = phase;
    g_ax_callback.address = address;
}

static bool ax_run_callback_slice(u32 block_limit) {
    int still_running =
        run_timed_guest_dispatches(&g_ax_callback.cpu, block_limit);
    if (still_running)
        return false;

    if (g_ax_callback.cpu.pc != HOST_AUDIO_RETURN_ADDRESS) {
        fprintf(stderr,
                "audio: callback 0x%08X stopped at pc=0x%08X lr=0x%08X "
                "exception=0x%08X\n",
                g_ax_callback.address, g_ax_callback.cpu.pc,
                g_ax_callback.cpu.lr, g_ax_callback.cpu.exception);
    }
    g_ax_callback.active = false;
    return true;
}

void wiiu_imports_run_audio(CPUState* main_cpu, u32 callback_blocks) {
    if (!main_cpu || !g_ax_initialized || callback_blocks == 0u)
        return;

    for (u32 transition = 0; transition < 3u; transition++) {
        if (!g_ax_callback.active &&
            g_ax_callback.phase == AX_CALLBACK_IDLE) {
            /* AX processes 96 samples at 32 kHz (one frame per 3 ms), not
               one frame per scheduler turn. Resume in-flight callbacks
               without this gate; only starting another frame is paced.
               After a slow guest callback, do not flood the output queue
               with a burst of already-late frames. */
            if (g_ax_frame_clock_started &&
                (s64)(main_cpu->timebase - g_ax_next_frame_tick) < 0)
                return;
            const u64 frame_ticks =
                (62156250ull * AX_MIX_FRAMES + 31999ull) / 32000ull;
            /* Keep small scheduler jitter from permanently lowering the
               sample rate. Rebase only after a long stall, so catch-up is
               bounded below the 64 ms native prebuffer instead of a
               multi-second burst. Dropping time after only 12 ms discarded
               samples during ordinary resource-loading scheduler stalls. */
            if (!g_ax_frame_clock_started ||
                main_cpu->timebase - g_ax_next_frame_tick >= frame_ticks * 20u)
                g_ax_next_frame_tick = main_cpu->timebase + frame_ticks;
            else
                g_ax_next_frame_tick += frame_ticks;
            g_ax_frame_clock_started = true;
            u32 callback = g_ax_app_frame_callback != 0u
                               ? g_ax_app_frame_callback
                               : g_ax_frame_callback;
            if (callback != 0u) {
                ax_begin_callback(main_cpu, callback, 0u, AX_CALLBACK_APP);
            } else {
                ax_mix_voices(main_cpu, g_ax_mixed_samples, AX_MIX_FRAMES);
                ax_store_final_mix(main_cpu, g_ax_mixed_samples);
                if (g_ax_final_mix_callbacks[0] != 0u) {
                    ax_begin_callback(main_cpu, g_ax_final_mix_callbacks[0],
                                      AX_FINAL_MIX_PARAM,
                                      AX_CALLBACK_FINAL_MIX);
                } else {
                    wiiu_audio_submit_stereo(g_ax_mixed_samples,
                                              AX_MIX_FRAMES);
                    g_ax_audio_frame_count++;
                    return;
                }
            }
        }

        HostAXCallbackPhase completed_phase = g_ax_callback.phase;
        if (g_ax_callback.active &&
            !ax_run_callback_slice(callback_blocks)) {
            return;
        }

        if (completed_phase == AX_CALLBACK_APP) {
            ax_mix_voices(main_cpu, g_ax_mixed_samples, AX_MIX_FRAMES);
            ax_store_final_mix(main_cpu, g_ax_mixed_samples);
            if (g_ax_final_mix_callbacks[0] != 0u) {
                ax_begin_callback(main_cpu, g_ax_final_mix_callbacks[0],
                                  AX_FINAL_MIX_PARAM,
                                  AX_CALLBACK_FINAL_MIX);
                continue;
            }
            wiiu_audio_submit_stereo(g_ax_mixed_samples, AX_MIX_FRAMES);
            g_ax_audio_frame_count++;
            g_ax_callback.phase = AX_CALLBACK_IDLE;
            return;
        }

        if (completed_phase == AX_CALLBACK_FINAL_MIX) {
            ax_read_final_mix(main_cpu, g_ax_mixed_samples);
            wiiu_audio_submit_stereo(g_ax_mixed_samples, AX_MIX_FRAMES);
            g_ax_audio_frame_count++;
            g_ax_callback.phase = AX_CALLBACK_IDLE;
            return;
        }
    }
}
