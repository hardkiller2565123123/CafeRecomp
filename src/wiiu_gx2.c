#include "wiiu_gx2.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "wiiu_gx2_software.h"
#include "wiiu_gx2_cemu_layout.h"
#include "wiiu_latte_fetch.h"
#include "wiiu_latte_vertex.h"
#include "wiiu_latte_execute.h"
#include "wiiu_memory.h"
#include "wiiu_window.h"

typedef struct {
    CPUState* owner;
    bool active;
    u32 address;
    u32 capacity;
    u32 used;
    u32 command_count;
    u32 command_capacity;
} GX2DisplayListState;

typedef struct {
    bool valid;
    bool cleared;
    u32 descriptor;
    u32 image;
    u32 width;
    u32 height;
    u32 pitch;
    u32 format;
    u32 tile_mode;
    u8 red;
    u8 green;
    u8 blue;
    u8 alpha;
    u64 clear_serial;
    u8* pixels;
    size_t pixel_capacity;
    u32 pixel_width;
    u32 pixel_height;
    bool drawn;
    bool gpu_composited;
    bool cpu_pixels_stale;
    u64 draw_serial;
    float* vertex_float_pixels;
    u64 vertex_float_clear_serial,vertex_float_draw_serial;
} GX2HostColorSurface;
static void trace_completed_draw(const char* path,bool wrote,GX2HostColorSurface* target);

typedef struct {
    bool valid;
    u32 descriptor;
    u32 image;
    u32 image_size;
    u32 width;
    u32 height;
    u32 depth;
    u32 format;
    u32 tile_mode;
    u32 swizzle;
    u32 pitch;
    u32 comp_map;
    u32 first_mip;
    u32 first_slice;
    u32 dimension,aa;
} GX2HostTexture;

typedef struct {
    bool valid;
    bool dirty;
    GX2HostTexture texture;
    u8* pixels;
    size_t pixel_capacity;
    u64 serial;
    u64 use_serial;
    u64 data_signature;
    u64 last_signature_frame;
} GX2DecodedTexture;

typedef struct {
    bool valid;
    u32 size;
    u32 stride;
    u32 data;
} GX2HostAttribBuffer;

typedef struct {
    u32 location;
    u32 buffer;
    u32 offset;
    u32 format;
    u32 type;
    u32 divisor;
    u32 mask;
    u32 endian_swap;
    bool format_is_fetch;
    u8 number_format;
    bool signed_values;
} GX2HostAttribStream;

typedef struct {
    bool valid;
    u32 descriptor;
    u32 program;
    u32 attribute_count;
    GX2HostAttribStream attributes[16];
} GX2HostFetchShader;

typedef struct {
    bool valid;
    u32 size;
    u32 data;
} GX2HostUniformBlock;

typedef enum {
    GX2_RECORDED_NONE,
    GX2_RECORDED_SET_PIXEL_SAMPLER,
    GX2_RECORDED_SET_VERTEX_SAMPLER,
    GX2_RECORDED_SET_COLOR_BUFFER,
    GX2_RECORDED_CLEAR_COLOR,
    GX2_RECORDED_SET_PIXEL_TEXTURE,
    GX2_RECORDED_SET_VERTEX_TEXTURE,
    GX2_RECORDED_SET_ATTRIB_BUFFER,
    GX2_RECORDED_SET_FETCH_SHADER,
    GX2_RECORDED_SET_VERTEX_SHADER,
    GX2_RECORDED_SET_PIXEL_SHADER,
    GX2_RECORDED_SET_VERTEX_UNIFORM_REG,
    GX2_RECORDED_SET_PIXEL_UNIFORM_REG,
    GX2_RECORDED_SET_VERTEX_UNIFORM_BLOCK,
    GX2_RECORDED_SET_PIXEL_UNIFORM_BLOCK,
    GX2_RECORDED_SET_BLEND_CONTROL,
    GX2_RECORDED_SET_COLOR_CONTROL,
    GX2_RECORDED_SET_CHANNEL_MASK,
    GX2_RECORDED_SET_BLEND_CONSTANT,
    GX2_RECORDED_SET_VIEWPORT,
    GX2_RECORDED_SET_SCISSOR,
    GX2_RECORDED_COPY_COLOR_TO_SCAN,
    GX2_RECORDED_COPY_SURFACE,
    GX2_RECORDED_DRAW,
    GX2_RECORDED_DRAW_INDEXED,
    GX2_RECORDED_CALL_DISPLAY_LIST,
    GX2_RECORDED_SET_CONTEXT,
    GX2_RECORDED_SET_CLIP_CONTROL,
} GX2RecordedKind;

typedef struct {
    GX2RecordedKind kind;
    u32 args[8];
    f64 fargs[6];
    u32 uniform_word_count;
    u32 uniform_words[64];
    GX2HostTexture texture;
    GX2HostTexture copy_destination;
} GX2RecordedCommand;

enum {
    GX2_HOST_DISPLAY_LIST_BUCKETS = 4096,
};

typedef struct GX2HostDisplayList {
    bool valid;
    u32 address;
    u32 size;
    u32 command_count;
    u32 command_capacity;
    u64 serial;
    GX2RecordedCommand* commands;
    struct GX2HostDisplayList* next;
} GX2HostDisplayList;

enum {
    GX2_HOST_SURFACE_LIMIT = 256,
    GX2_RENDER_TARGET_LIMIT = 8,
    GX2_DISPLAY_LIST_CONTEXT_LIMIT = 64,
    GX2_TEXTURE_UNIT_LIMIT = 32,
    GX2_ATTRIB_BUFFER_LIMIT = 16,
    GX2_FETCH_SHADER_LIMIT = 1024,
    GX2_UNIFORM_BLOCK_LIMIT = 16,
    GX2_UNIFORM_REGISTER_LIMIT = 1024,
    GX2_DECODED_TEXTURE_LIMIT = 12,
};

static GX2DisplayListState
    g_display_lists[GX2_DISPLAY_LIST_CONTEXT_LIMIT];
static GX2RecordedCommand* g_recorded_commands[GX2_DISPLAY_LIST_CONTEXT_LIMIT];
static GX2HostDisplayList* g_host_display_lists[GX2_HOST_DISPLAY_LIST_BUCKETS];
static GX2HostColorSurface g_color_surfaces[GX2_HOST_SURFACE_LIMIT];
static u32 g_current_color_buffers[GX2_RENDER_TARGET_LIMIT];
static WiiUWindowGpuBlendControl g_blend_controls[GX2_RENDER_TARGET_LIMIT];
static GX2HostTexture g_pixel_textures[GX2_TEXTURE_UNIT_LIMIT];
typedef struct { bool valid; u32 words[3]; } GX2HostSampler;
static GX2HostSampler g_pixel_samplers[GX2_TEXTURE_UNIT_LIMIT];
static GX2HostSampler g_vertex_samplers[GX2_TEXTURE_UNIT_LIMIT];
static GX2HostTexture g_vertex_textures[GX2_TEXTURE_UNIT_LIMIT];
static GX2DecodedTexture g_decoded_textures[GX2_DECODED_TEXTURE_LIMIT];
static GX2HostAttribBuffer g_attrib_buffers[GX2_ATTRIB_BUFFER_LIMIT];
static GX2HostFetchShader g_fetch_shaders[GX2_FETCH_SHADER_LIMIT];
static GX2HostUniformBlock g_vertex_uniform_blocks[GX2_UNIFORM_BLOCK_LIMIT];
static GX2HostUniformBlock g_pixel_uniform_blocks[GX2_UNIFORM_BLOCK_LIMIT];
static u32 g_vertex_uniform_registers[GX2_UNIFORM_REGISTER_LIMIT];
static u32 g_pixel_uniform_registers[GX2_UNIFORM_REGISTER_LIMIT];
static u8 g_vertex_uniform_register_valid[GX2_UNIFORM_REGISTER_LIMIT];
static u8 g_pixel_uniform_register_valid[GX2_UNIFORM_REGISTER_LIMIT];
static WiiULatteVertexTransform g_ui_vertex_transform;
static u32 g_current_fetch_shader;
static u32 g_current_vertex_shader;
static u32 g_current_pixel_shader;
static bool g_viewport_valid;
static f64 g_viewport[6];
static bool g_scissor_valid;
static u32 g_scissor[4];
static u32 g_tv_scan_color_buffer;
static u32 g_drc_scan_color_buffer;
static u32 g_tv_width;
static u32 g_tv_height;
static u32 g_swap_interval;
static u32 g_surface_log_count;
static u32 g_display_list_log_count;
static u32 g_clear_log_count;
static u32 g_swap_log_count;
static u32 g_draw_log_count;
static u32 g_draw_state_log_count;
static u32 g_display_call_log_count;
static u32 g_software_draw_log_count;
static u32 g_software_ui_draw_log_count;
static u64 g_fast_ui_quad_count;
static u64 g_fallback_ui_quad_count;
static u64 g_gpu_ui_quad_count;
static u64 g_gpu_surface_blit_count;
static u64 g_gpu_texture_blit_count;
static clock_t g_texture_draw_ticks;
static clock_t g_ui_draw_ticks;
static clock_t g_present_ticks;
static u64 g_texture_draw_calls;
static u64 g_ui_draw_calls;
static u32 g_late_draw_trace_count;
static u32 g_texture_refresh_log_count;
static u32 g_texture_cache_log_count;
static u32 g_display_list_detail_log_count;
static u32 g_shader_bind_log_count;
static u32 g_blend_control_log_count;
static u32 g_largest_frame_command_count;
static bool g_first_render_dumped;
static bool g_has_active_frame;
static bool g_menu_candidate_dumped;
static bool g_ui_buffers_dumped;
static bool g_ui_uniform_state_dumped;
static bool g_ui_latte_transform_logged;
static u32 g_latte_fetch_used_log_count;
static u32 g_latte_fetch_program_log_count;
static u32 g_guide_texture_capture_count;
static u64 g_guide_texture_capture_signature;
static u64 g_guide_texture_capture_frame;
static bool g_guide_bc3_state_dumped;
static bool g_guide_bc3_texture_dumped;
static u64 g_clear_serial;
static u64 g_draw_serial;
static u64 g_display_list_serial;
static u64 g_frame_count;
static u64 g_last_swap_tick;
static u64 g_decoded_texture_serial;
/* Cafe default is full Z [-W,+W], rasterization and Z clipping enabled. */
static u32 g_clip_control;

/* Host-side shadow of the state this renderer implements. Resource storage
   and scan-out selection are shared, not private copies of a context. */
typedef struct {
    u32 current_color_buffers[GX2_RENDER_TARGET_LIMIT];
    WiiUWindowGpuBlendControl blend_controls[GX2_RENDER_TARGET_LIMIT];
    GX2HostTexture pixel_textures[GX2_TEXTURE_UNIT_LIMIT];
    GX2HostSampler pixel_samplers[GX2_TEXTURE_UNIT_LIMIT];
    GX2HostSampler vertex_samplers[GX2_TEXTURE_UNIT_LIMIT];
    GX2HostTexture vertex_textures[GX2_TEXTURE_UNIT_LIMIT];
    GX2HostAttribBuffer attrib_buffers[GX2_ATTRIB_BUFFER_LIMIT];
    GX2HostUniformBlock vertex_uniform_blocks[GX2_UNIFORM_BLOCK_LIMIT];
    GX2HostUniformBlock pixel_uniform_blocks[GX2_UNIFORM_BLOCK_LIMIT];
    u32 vertex_uniform_registers[GX2_UNIFORM_REGISTER_LIMIT];
    u32 pixel_uniform_registers[GX2_UNIFORM_REGISTER_LIMIT];
    u8 vertex_uniform_register_valid[GX2_UNIFORM_REGISTER_LIMIT];
    u8 pixel_uniform_register_valid[GX2_UNIFORM_REGISTER_LIMIT];
    u32 current_fetch_shader, current_vertex_shader, current_pixel_shader;
    bool viewport_valid, scissor_valid;
    f64 viewport[6];
    u32 scissor[4];
    u32 clip_control;
} GX2Bindings;
typedef struct { u32 address; GX2Bindings bindings; } GX2ContextShadow;
static GX2ContextShadow g_context_shadows[64];
static u32 g_current_context;

static void transfer_bindings(GX2Bindings* state, bool save) {
#define TRANSFER(field) do { \
    if (save) memcpy(&state->field, &g_##field, sizeof(state->field)); \
    else memcpy(&g_##field, &state->field, sizeof(state->field)); \
} while (0)
    TRANSFER(current_color_buffers); TRANSFER(blend_controls);
    TRANSFER(pixel_textures); TRANSFER(vertex_textures); TRANSFER(attrib_buffers);
    TRANSFER(pixel_samplers);
    TRANSFER(vertex_samplers);
    TRANSFER(vertex_uniform_blocks); TRANSFER(pixel_uniform_blocks);
    TRANSFER(vertex_uniform_registers); TRANSFER(pixel_uniform_registers);
    TRANSFER(vertex_uniform_register_valid); TRANSFER(pixel_uniform_register_valid);
    TRANSFER(current_fetch_shader); TRANSFER(current_vertex_shader);
    TRANSFER(current_pixel_shader); TRANSFER(viewport_valid); TRANSFER(viewport);
    TRANSFER(scissor_valid); TRANSFER(scissor);
    TRANSFER(clip_control);
#undef TRANSFER
}

static GX2ContextShadow* find_context_shadow(u32 address, bool create) {
    if (!address) return NULL;
    GX2ContextShadow* empty = NULL;
    for (u32 i = 0; i < 64; ++i) {
        if (g_context_shadows[i].address == address) return &g_context_shadows[i];
        if (!g_context_shadows[i].address && !empty) empty = &g_context_shadows[i];
    }
    if (create && empty) { empty->address = address; return empty; }
    return NULL;
}

static void switch_context_shadow(u32 address, bool initialize) {
    GX2ContextShadow* previous = find_context_shadow(g_current_context, false);
    if (previous) transfer_bindings(&previous->bindings, true);
    if (!address) { g_current_context = 0; return; } /* Disable shadowing. */
    GX2ContextShadow* next = find_context_shadow(address, true);
    if (!next) { fprintf(stderr, "gx2: context shadow capacity exhausted\n"); return; }
    if (initialize) memset(&next->bindings, 0, sizeof(next->bindings));
    transfer_bindings(&next->bindings, false);
    g_current_context = address;
}

static const char* recorded_kind_name(GX2RecordedKind kind) {
    switch (kind) {
    case GX2_RECORDED_SET_COLOR_BUFFER:
        return "color-buffer";
    case GX2_RECORDED_CLEAR_COLOR:
        return "clear-color";
    case GX2_RECORDED_SET_PIXEL_TEXTURE:
        return "pixel-texture";
    case GX2_RECORDED_SET_PIXEL_SAMPLER: return "pixel-sampler";
    case GX2_RECORDED_SET_VERTEX_SAMPLER: return "vertex-sampler";
    case GX2_RECORDED_SET_VERTEX_TEXTURE:
        return "vertex-texture";
    case GX2_RECORDED_SET_ATTRIB_BUFFER:
        return "attrib-buffer";
    case GX2_RECORDED_SET_FETCH_SHADER:
        return "fetch-shader";
    case GX2_RECORDED_SET_VERTEX_SHADER:
        return "vertex-shader";
    case GX2_RECORDED_SET_PIXEL_SHADER:
        return "pixel-shader";
    case GX2_RECORDED_SET_VERTEX_UNIFORM_REG:
        return "vertex-uniform-reg";
    case GX2_RECORDED_SET_PIXEL_UNIFORM_REG:
        return "pixel-uniform-reg";
    case GX2_RECORDED_SET_VERTEX_UNIFORM_BLOCK:
        return "vertex-uniform";
    case GX2_RECORDED_SET_PIXEL_UNIFORM_BLOCK:
        return "pixel-uniform";
    case GX2_RECORDED_SET_BLEND_CONTROL:
        return "blend-control";
    case GX2_RECORDED_SET_COLOR_CONTROL: return "color-control";
    case GX2_RECORDED_SET_CHANNEL_MASK: return "channel-mask";
    case GX2_RECORDED_SET_BLEND_CONSTANT: return "blend-constant";
    case GX2_RECORDED_SET_CLIP_CONTROL: return "clip-control";
    case GX2_RECORDED_SET_VIEWPORT:
        return "viewport";
    case GX2_RECORDED_SET_SCISSOR:
        return "scissor";
    case GX2_RECORDED_COPY_COLOR_TO_SCAN:
        return "copy-to-scan";
    case GX2_RECORDED_COPY_SURFACE:
        return "copy-surface";
    case GX2_RECORDED_DRAW:
        return "draw";
    case GX2_RECORDED_DRAW_INDEXED:
        return "draw-indexed";
    case GX2_RECORDED_CALL_DISPLAY_LIST:
        return "call-list";
    case GX2_RECORDED_SET_CONTEXT:
        return "set-context";
    default:
        return "unknown";
    }
}

static void set_blend_control_from_args(const u32 args[8]) {
    if (!args || args[0] >= GX2_RENDER_TARGET_LIMIT)
        return;
    WiiUWindowGpuBlendControl* control = &g_blend_controls[args[0]];
    control->valid = true;
    control->color_source_factor = args[1];
    control->color_destination_factor = args[2];
    control->color_operation = args[3];
    control->separate_alpha = args[4] != 0u;
    control->alpha_source_factor = args[5];
    control->alpha_destination_factor = args[6];
    control->alpha_operation = args[7];
    if (g_blend_control_log_count++ < 32u) {
        fprintf(stderr,
                "gx2: blend target=%u color=(%u,%u,%u) alpha=%u (%u,%u,%u)\n",
                args[0], args[1], args[2], args[3], args[4], args[5],
                args[6], args[7]);
    }
}

static void set_color_state(GX2RecordedKind kind, const u32* args) {
    for(u32 i=0;i<GX2_RENDER_TARGET_LIMIT;++i) {
        WiiUWindowGpuBlendControl* control=&g_blend_controls[i];
        if(!control->valid) {
            control->valid=true;
            control->color_source_factor=4; control->color_destination_factor=5;
            control->separate_alpha=true;control->alpha_source_factor=1;
            control->alpha_destination_factor=5;
        }
        if(kind==GX2_RECORDED_SET_COLOR_CONTROL) {
            control->color_control_valid=true;
            control->blend_enabled=(args[0]&(1u<<(8u+i)))!=0;
            control->color_enabled=((args[0]>>4u)&7u)!=1u;
        } else if(kind==GX2_RECORDED_SET_CHANNEL_MASK) {
            control->write_mask_valid=true;
            control->write_mask=(u8)((args[0]>>(4u*i))&15u);
        } else {
            control->constant_valid=true;
            memcpy(control->constant,args,sizeof(control->constant));
        }
    }
}

static GX2DisplayListState* get_display_list_state(CPUState* cpu,
                                                   bool create) {
    GX2DisplayListState* free_slot = NULL;
    for (u32 i = 0; i < GX2_DISPLAY_LIST_CONTEXT_LIMIT; i++) {
        GX2DisplayListState* state = &g_display_lists[i];
        if (state->owner == cpu)
            return state;
        if (!state->owner && !free_slot)
            free_slot = state;
    }
    if (!create || !free_slot)
        return NULL;
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->owner = cpu;
    return free_slot;
}

static bool display_list_is_recording(CPUState* cpu) {
    GX2DisplayListState* state = get_display_list_state(cpu, false);
    return state && state->active;
}

static GX2RecordedCommand* record_command(CPUState* cpu,
                                          GX2RecordedKind kind) {
    GX2DisplayListState* state = get_display_list_state(cpu, false);
    if (!state || !state->active) {
        return NULL;
    }
    u32 state_index = (u32)(state - g_display_lists);
    if (state->command_count == state->command_capacity) {
        u32 capacity = state->command_capacity ? state->command_capacity * 2u : 32u;
        if (capacity <= state->command_capacity ||
            (size_t)capacity > SIZE_MAX / sizeof(GX2RecordedCommand))
            return NULL;
        GX2RecordedCommand* commands = realloc(g_recorded_commands[state_index],
            (size_t)capacity * sizeof(*commands));
        if (!commands) {
            fprintf(stderr, "gx2: cannot grow recording 0x%08X to %u commands\n",
                    state->address, capacity);
            return NULL;
        }
        g_recorded_commands[state_index] = commands;
        state->command_capacity = capacity;
    }
    GX2RecordedCommand* command =
        &g_recorded_commands[state_index][state->command_count++];
    memset(command, 0, sizeof(*command));
    command->kind = kind;
    return command;
}

static GX2RecordedCommand* record_gpr_command(CPUState* cpu,
                                              GX2RecordedKind kind,
                                              u32 count) {
    GX2RecordedCommand* command = record_command(cpu, kind);
    if (!command)
        return NULL;
    if (count > 8u)
        count = 8u;
    for (u32 i = 0; i < count; i++)
        command->args[i] = cpu->gpr[i + 3u];
    return command;
}

static GX2RecordedCommand* record_fpr_command(CPUState* cpu,
                                              GX2RecordedKind kind,
                                              u32 count) {
    GX2RecordedCommand* command = record_command(cpu, kind);
    if (!command)
        return NULL;
    if (count > 6u)
        count = 6u;
    for (u32 i = 0; i < count; i++)
        command->fargs[i] = cpu->fpr[i + 1u];
    return command;
}

static GX2HostDisplayList* find_host_display_list(u32 address, bool create) {
    const u32 bucket = ((address >> 5) ^ (address >> 17)) &
                       (GX2_HOST_DISPLAY_LIST_BUCKETS - 1u);
    for (GX2HostDisplayList* list = g_host_display_lists[bucket]; list;
         list = list->next) {
        if (list->address == address)
            return list;
    }
    if (!create)
        return NULL;
    // Guest lists remain callable until overwritten. Eviction loses commands:
    // the guest buffer does not contain a replayable copy of the host records.
    GX2HostDisplayList* list = calloc(1, sizeof(*list));
    if (!list) {
        fprintf(stderr, "gx2: cannot allocate display list 0x%08X\n", address);
        return NULL;
    }
    list->valid = true;
    list->address = address;
    list->next = g_host_display_lists[bucket];
    g_host_display_lists[bucket] = list;
    return list;
}

static void commit_display_list(GX2DisplayListState* state, u32 size) {
    if (!state)
        return;
    GX2HostDisplayList* list = find_host_display_list(state->address, true);
    if (!list)
        return;
    u32 state_index = (u32)(state - g_display_lists);
    list->size = size;
    u32 count = state->command_count;
    list->command_count = 0;
    if (count > list->command_capacity) {
        GX2RecordedCommand* commands = realloc(
            list->commands, (size_t)count * sizeof(*commands));
        if (!commands) {
            fprintf(stderr, "gx2: cannot allocate %u commands for 0x%08X\n",
                    count, list->address);
            return;
        }
        list->commands = commands;
        list->command_capacity = count;
    }
    if (count)
        memcpy(list->commands, g_recorded_commands[state_index],
               (size_t)count * sizeof(list->commands[0]));
    list->command_count = count;
    list->serial = ++g_display_list_serial;
    if (list->command_count > g_largest_frame_command_count)
        g_largest_frame_command_count = list->command_count;
    if (list->command_count >= 80u &&
        g_display_list_detail_log_count++ < 2u) {
        fprintf(stderr,
                "gx2: detailed display list address=0x%08X size=0x%X "
                "commands=%u\n",
                list->address, list->size, list->command_count);
        for (u32 i = 0; i < list->command_count; i++) {
            const GX2RecordedCommand* command = &list->commands[i];
            fprintf(stderr,
                    "gx2: command[%u] %s args=%08X,%08X,%08X,%08X,"
                    "%08X,%08X\n",
                    i, recorded_kind_name(command->kind), command->args[0],
                    command->args[1], command->args[2], command->args[3],
                    command->args[4], command->args[5]);
            if (command->kind == GX2_RECORDED_SET_PIXEL_TEXTURE ||
                command->kind == GX2_RECORDED_SET_VERTEX_TEXTURE) {
                fprintf(stderr,
                        "gx2: command[%u] texture image=0x%08X %ux%u "
                        "pitch=%u size=0x%X fmt=0x%X tile=%u "
                        "swizzle=0x%X comp=0x%X\n",
                        i, command->texture.image, command->texture.width,
                        command->texture.height, command->texture.pitch,
                        command->texture.image_size, command->texture.format,
                        command->texture.tile_mode, command->texture.swizzle,
                        command->texture.comp_map);
            }
        }
    }
}

static u32 align_up(u32 value, u32 alignment) {
    if (alignment <= 1u)
        return value;
    u64 aligned = ((u64)value + alignment - 1u) & ~(u64)(alignment - 1u);
    return aligned <= 0xFFFFFFFFu ? (u32)aligned : 0xFFFFFFFFu;
}

static bool guest_range_valid(CPUState* cpu, u32 address, u32 size) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    return address != 0 && size != 0 && memory &&
           wiiu_memory_find(memory, address, size) != NULL;
}

static const u8* guest_bytes(CPUState* cpu, u32 address, u32 size) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* segment =
        memory ? wiiu_memory_find(memory, address, size) : NULL;
    if (!segment)
        return NULL;
    return segment->data +
           (wiiu_memory_canonical_address(address) - segment->base);
}

static void apply_uniform_register_words(u32* registers, u8* valid,
                                         u32 offset, const u32* words,
                                         u32 word_count) {
    if (!registers || !valid || !words || offset >= GX2_UNIFORM_REGISTER_LIMIT)
        return;
    if (word_count > GX2_UNIFORM_REGISTER_LIMIT - offset)
        word_count = GX2_UNIFORM_REGISTER_LIMIT - offset;
    for (u32 i = 0; i < word_count; i++) {
        registers[offset + i] = words[i];
        valid[offset + i] = 1u;
    }
}

static void apply_uniform_registers(CPUState* cpu, bool vertex, u32 offset,
                                    u32 word_count, u32 data) {
    if (!cpu || data == 0u || offset >= GX2_UNIFORM_REGISTER_LIMIT)
        return;
    if (word_count > GX2_UNIFORM_REGISTER_LIMIT - offset)
        word_count = GX2_UNIFORM_REGISTER_LIMIT - offset;
    if (word_count == 0u ||
        !guest_range_valid(cpu, data, word_count * sizeof(u32))) {
        return;
    }
    u32 words[64];
    while (word_count != 0u) {
        u32 chunk = word_count < 64u ? word_count : 64u;
        for (u32 i = 0; i < chunk; i++)
            words[i] = mem_read32(cpu, data + i * sizeof(u32));
        if (vertex) {
            apply_uniform_register_words(g_vertex_uniform_registers,
                                         g_vertex_uniform_register_valid,
                                         offset, words, chunk);
        } else {
            apply_uniform_register_words(g_pixel_uniform_registers,
                                         g_pixel_uniform_register_valid,
                                         offset, words, chunk);
        }
        offset += chunk;
        data += chunk * sizeof(u32);
        word_count -= chunk;
    }
}

static GX2RecordedCommand* record_uniform_register_command(
    CPUState* cpu, GX2RecordedKind kind) {
    GX2RecordedCommand* command = record_gpr_command(cpu, kind, 3u);
    if (!command || !cpu)
        return command;

    u32 offset = cpu->gpr[3];
    u32 word_count = cpu->gpr[4];
    u32 data = cpu->gpr[5];
    if (offset >= GX2_UNIFORM_REGISTER_LIMIT || data == 0u)
        return command;
    if (word_count > GX2_UNIFORM_REGISTER_LIMIT - offset)
        word_count = GX2_UNIFORM_REGISTER_LIMIT - offset;
    if (word_count > sizeof(command->uniform_words) /
                         sizeof(command->uniform_words[0])) {
        word_count = sizeof(command->uniform_words) /
                     sizeof(command->uniform_words[0]);
    }
    if (!guest_range_valid(cpu, data, word_count * sizeof(u32)))
        return command;

    for (u32 i = 0; i < word_count; i++)
        command->uniform_words[i] = mem_read32(cpu, data + i * sizeof(u32));
    command->uniform_word_count = word_count;
    return command;
}

static void guest_zero(CPUState* cpu, u32 address, u32 size) {
    if (!guest_range_valid(cpu, address, size))
        return;
    for (u32 i = 0; i < size; i++)
        mem_write8(cpu, address + i, 0);
}

static void write_output(CPUState* cpu, u32 address, u32 value) {
    if (guest_range_valid(cpu, address, 4))
        mem_write32(cpu, address, value);
}

static bool is_name(const char* name, const char* expected) {
    return name && strcmp(name, expected) == 0;
}

static bool starts_with(const char* name, const char* prefix) {
    return name && prefix && strncmp(name, prefix, strlen(prefix)) == 0;
}

static u8 color_to_u8(f64 value) {
    if (!(value > 0.0))
        return 0u;
    if (value >= 1.0)
        return 255u;
    return (u8)(value * 255.0 + 0.5);
}

/* Bindings hold image addresses, not pointers to reusable GX2 descriptors. */
static GX2HostColorSurface* find_color_surface(u32 image, bool create) {
    image=wiiu_memory_canonical_address(image);
    if(!image) return NULL;
    GX2HostColorSurface* free_slot = NULL;
    GX2HostColorSurface* oldest = NULL;
    for (u32 i = 0; i < GX2_HOST_SURFACE_LIMIT; i++) {
        GX2HostColorSurface* surface = &g_color_surfaces[i];
        if (surface->valid && surface->image == image)
            return surface;
        if (!surface->valid && !free_slot)
            free_slot = surface;
        if (surface->valid && (!oldest ||
                               surface->clear_serial < oldest->clear_serial))
            oldest = surface;
    }
    if (!create)
        return NULL;
    GX2HostColorSurface* surface = free_slot ? free_slot : oldest;
    if (!surface)
        return NULL;
    if(surface->valid) wiiu_window_gpu_invalidate(surface->image);
    free(surface->pixels);
    free(surface->vertex_float_pixels);
    memset(surface, 0, sizeof(*surface));
    surface->valid = true;
    surface->image = image;
    return surface;
}

static bool ensure_color_surface_pixels(GX2HostColorSurface* surface) {
    if (!surface || surface->width == 0 || surface->height == 0 ||
        surface->width > 8192u || surface->height > 8192u) {
        return false;
    }
    if ((size_t)surface->width > SIZE_MAX / 4u / surface->height)
        return false;
    size_t required = (size_t)surface->width * surface->height * 4u;
    bool dimensions_changed = surface->pixel_width != surface->width ||
                              surface->pixel_height != surface->height;
    if (required > surface->pixel_capacity) {
        u8* replacement = (u8*)realloc(surface->pixels, required);
        if (!replacement)
            return false;
        surface->pixels = replacement;
        surface->pixel_capacity = required;
        dimensions_changed = true;
    }
    if (dimensions_changed) {
        size_t pixels = (size_t)surface->width * surface->height;
        for (size_t i = 0; i < pixels; i++) {
            surface->pixels[i * 4u + 0u] = surface->blue;
            surface->pixels[i * 4u + 1u] = surface->green;
            surface->pixels[i * 4u + 2u] = surface->red;
            surface->pixels[i * 4u + 3u] = surface->alpha;
        }
        surface->pixel_width = surface->width;
        surface->pixel_height = surface->height;
    }
    return true;
}

static GX2HostColorSurface* find_color_surface_by_image(u32 image) {
    image=wiiu_memory_canonical_address(image);
    if (image == 0)
        return NULL;
    for (u32 i = 0; i < GX2_HOST_SURFACE_LIMIT; i++) {
        GX2HostColorSurface* surface = &g_color_surfaces[i];
        if (surface->valid && surface->image == image &&
            (surface->pixels || surface->gpu_composited))
            return surface;
    }
    return NULL;
}

static bool read_copy_surface(CPUState* cpu,u32 descriptor,GX2HostTexture* out);

/* Debug readback keeps descriptor aliases separate from resource identity.
   Core draw/context/scanout state references the captured image directly. */
typedef struct { u32 descriptor,image; u64 serial; } GX2ColorAlias;
static GX2ColorAlias g_color_aliases[256];
static u64 g_color_binding_serial;

static GX2HostColorSurface* bind_color_snapshot(const GX2HostTexture* desc) {
    if(!desc || !desc->valid) return NULL;
    GX2HostColorSurface* surface=find_color_surface(desc->image,true);
    if(!surface) return NULL;
    if(surface->width != desc->width || surface->height != desc->height ||
       surface->format != desc->format || surface->pitch != desc->pitch ||
       surface->tile_mode != desc->tile_mode) {
        wiiu_window_gpu_invalidate(surface->image);
        free(surface->pixels);
        free(surface->vertex_float_pixels);
        memset(surface,0,sizeof(*surface));
        surface->valid=true;
    }
    surface->image=wiiu_memory_canonical_address(desc->image);
    surface->descriptor=desc->descriptor;
    surface->width=desc->width;surface->height=desc->height;
    surface->format=desc->format;surface->tile_mode=desc->tile_mode;surface->pitch=desc->pitch;
    GX2ColorAlias* alias=&g_color_aliases[0];
    for(u32 i=0;i<256;++i) {
        if(g_color_aliases[i].descriptor==desc->descriptor) {alias=&g_color_aliases[i];break;}
        if(g_color_aliases[i].serial<alias->serial) alias=&g_color_aliases[i];
    }
    *alias=(GX2ColorAlias){desc->descriptor,surface->image,++g_color_binding_serial};
    return surface;
}

static GX2HostColorSurface* update_color_surface(CPUState* cpu,u32 descriptor) {
    GX2HostTexture snapshot;
    return read_copy_surface(cpu,descriptor,&snapshot)?bind_color_snapshot(&snapshot):NULL;
}

bool wiiu_gx2_readback_color_buffer(u32 descriptor,u8* pixels,u32 width,u32 height,u32 stride) {
    for(u32 i=0;i<256;++i) if(g_color_aliases[i].descriptor==descriptor) {
        GX2HostColorSurface* surface=find_color_surface(g_color_aliases[i].image,false);
        return surface && wiiu_window_gpu_readback(surface->image,pixels,width,height,stride);
    }
    return false;
}

static bool update_texture(CPUState* cpu, u32 descriptor,
                           GX2HostTexture* texture) {
    if (!texture)
        return false;
    memset(texture, 0, sizeof(*texture));
    if (!guest_range_valid(cpu, descriptor, 0x9Cu))
        return false;
    texture->valid = true;
    texture->descriptor = descriptor;
    texture->dimension=mem_read32(cpu,descriptor);
    texture->aa=mem_read32(cpu,descriptor+0x18);
    texture->width = mem_read32(cpu, descriptor + 0x04u);
    texture->height = mem_read32(cpu, descriptor + 0x08u);
    texture->depth = mem_read32(cpu, descriptor + 0x0Cu);
    texture->format = mem_read32(cpu, descriptor + 0x14u);
    texture->image_size = mem_read32(cpu, descriptor + 0x20u);
    texture->image = mem_read32(cpu, descriptor + 0x24u);
    texture->tile_mode = mem_read32(cpu, descriptor + 0x30u);
    texture->swizzle = mem_read32(cpu, descriptor + 0x34u);
    texture->pitch = mem_read32(cpu, descriptor + 0x3Cu);
    texture->first_mip = mem_read32(cpu, descriptor + 0x74u);
    texture->first_slice = mem_read32(cpu, descriptor + 0x7Cu);
    texture->comp_map = mem_read32(cpu, descriptor + 0x84u);
    if (texture->width == 0 || texture->height == 0 ||
        texture->width > 16384u || texture->height > 16384u ||
        texture->image == 0) {
        texture->valid = false;
    }
    return texture->valid;
}

static GX2HostFetchShader* find_fetch_shader(u32 descriptor, bool create) {
    GX2HostFetchShader* free_slot = NULL;
    for (u32 i = 0; i < GX2_FETCH_SHADER_LIMIT; i++) {
        GX2HostFetchShader* shader = &g_fetch_shaders[i];
        if (shader->valid && shader->descriptor == descriptor)
            return shader;
        if (!shader->valid && !free_slot)
            free_slot = shader;
    }
    if (!create)
        return NULL;
    GX2HostFetchShader* shader =
        free_slot ? free_slot : &g_fetch_shaders[descriptor % GX2_FETCH_SHADER_LIMIT];
    memset(shader, 0, sizeof(*shader));
    shader->valid = true;
    shader->descriptor = descriptor;
    return shader;
}

static void remember_fetch_shader(CPUState* cpu, u32 descriptor, u32 program,
                                  u32 count, u32 streams) {
    GX2HostFetchShader* shader = find_fetch_shader(descriptor, true);
    if (!shader)
        return;
    shader->program = program;
    shader->attribute_count = count > 16u ? 16u : count;
    for (u32 i = 0; i < shader->attribute_count; i++) {
        u32 stream = streams + i * 0x20u;
        if (!guest_range_valid(cpu, stream, 0x20u)) {
            shader->attribute_count = i;
            break;
        }
        GX2HostAttribStream* out = &shader->attributes[i];
        out->location = mem_read32(cpu, stream + 0x00u);
        out->buffer = mem_read32(cpu, stream + 0x04u);
        out->offset = mem_read32(cpu, stream + 0x08u);
        out->format = mem_read32(cpu, stream + 0x0Cu);
        out->type = mem_read32(cpu, stream + 0x10u);
        out->divisor = mem_read32(cpu, stream + 0x14u);
        out->mask = mem_read32(cpu, stream + 0x18u);
        out->endian_swap = mem_read32(cpu, stream + 0x1Cu);
        out->format_is_fetch = false;
        out->number_format = 0u;
        out->signed_values = false;
    }
}

static void copy_attrib_stream_to_latte(const GX2HostAttribStream* source,
                                        WiiULatteFetchStream* destination) {
    if (!source || !destination)
        return;
    memset(destination, 0, sizeof(*destination));
    destination->location = source->location;
    destination->buffer = source->buffer;
    destination->offset = source->offset;
    destination->format = source->format;
    destination->index_type = source->type;
    destination->divisor = source->divisor;
    destination->dest_sel = source->mask;
    destination->endian_swap = source->endian_swap;
    destination->format_is_fetch = source->format_is_fetch;
    destination->number_format = source->number_format;
    destination->signed_values = source->signed_values;
}

static bool remember_fetch_shader_from_program(CPUState* cpu, u32 descriptor) {
    GX2HostFetchShader* existing = find_fetch_shader(descriptor, false);
    if (existing && existing->attribute_count != 0u)
        return true;
    if (!cpu || descriptor == 0u ||
        !guest_range_valid(cpu, descriptor, 0x20u)) {
        return false;
    }

    u32 program = mem_read32(cpu, descriptor + 0x0Cu);
    u32 program_size = mem_read32(cpu, descriptor + 0x08u);
    u32 divisor_count = mem_read32(cpu, descriptor + 0x14u);
    u32 divisors[2] = {mem_read32(cpu, descriptor + 0x18u),
                       mem_read32(cpu, descriptor + 0x1Cu)};
    WiiULatteFetchStream parsed[WIIU_LATTE_FETCH_MAX_STREAMS];
    u32 parsed_count = 0u;
    if (!wiiu_latte_fetch_parse_program(
            cpu, program, program_size, divisors, divisor_count, parsed,
            WIIU_LATTE_FETCH_MAX_STREAMS, &parsed_count)) {
        return false;
    }

    GX2HostFetchShader* shader = find_fetch_shader(descriptor, true);
    if (!shader)
        return false;
    shader->program = program;
    shader->attribute_count = parsed_count;
    for (u32 i = 0u; i < parsed_count; i++) {
        const WiiULatteFetchStream* source = &parsed[i];
        GX2HostAttribStream* destination = &shader->attributes[i];
        memset(destination, 0, sizeof(*destination));
        destination->location = source->location;
        destination->buffer = source->buffer;
        destination->offset = source->offset;
        destination->format = source->format;
        destination->type = source->index_type;
        destination->divisor = source->divisor;
        destination->mask = source->dest_sel;
        destination->endian_swap = source->endian_swap;
        destination->format_is_fetch = source->format_is_fetch;
        destination->number_format = source->number_format;
        destination->signed_values = source->signed_values;
    }
    if (g_latte_fetch_program_log_count++ < 16u) {
        fprintf(stderr,
                "gx2: decoded native Latte fetch descriptor=0x%08X "
                "program=0x%08X streams=%u\n",
                descriptor, program, parsed_count);
    }
    return true;
}

static bool fetch_current_vertex(CPUState* cpu, u32 vertex_index,
                                 u32 instance_index,
                                 WiiULatteFetchedVertex* result,
                                 GX2HostFetchShader** fetch_out) {
    if (!cpu || !result)
        return false;
    GX2HostFetchShader* fetch =
        find_fetch_shader(g_current_fetch_shader, false);
    if (!fetch || fetch->attribute_count == 0u)
        return false;

    WiiULatteFetchStream streams[WIIU_LATTE_FETCH_MAX_STREAMS];
    memset(streams, 0, sizeof(streams));
    WiiULatteFetchBuffer buffers[GX2_ATTRIB_BUFFER_LIMIT];
    for (u32 i = 0u; i < GX2_ATTRIB_BUFFER_LIMIT; i++) {
        buffers[i].valid = g_attrib_buffers[i].valid;
        buffers[i].size = g_attrib_buffers[i].size;
        buffers[i].stride = g_attrib_buffers[i].stride;
        buffers[i].data = g_attrib_buffers[i].data;
    }
    for (u32 i = 0u; i < fetch->attribute_count; i++) {
        const GX2HostAttribStream* source = &fetch->attributes[i];
        WiiULatteFetchStream* destination = &streams[i];
        copy_attrib_stream_to_latte(source, destination);
    }
    if (fetch_out)
        *fetch_out = fetch;
    return wiiu_latte_fetch_vertex(cpu, streams, fetch->attribute_count,
                                   buffers, GX2_ATTRIB_BUFFER_LIMIT,
                                   vertex_index, instance_index, result);
}

static void log_guest_prefix(CPUState* cpu, u32 address, u32 size) {
    u32 shown = size < 32u ? size : 32u;
    const u8* bytes = shown ? guest_bytes(cpu, address, shown) : NULL;
    if (!bytes) {
        fprintf(stderr, " <unmapped>");
        return;
    }
    for (u32 i = 0; i < shown; i++)
        fprintf(stderr, "%s%02X", i ? " " : " ", bytes[i]);
}

static FILE* open_capture_file(const char* name) {
    const char* directory = getenv("BOTW_CAPTURE_DIR");
    if (!directory || !directory[0] || !name)
        return NULL;
    char path[2048];
    const char* suffix = strncmp(name, "sm3dw_", 6u) == 0 ? name + 6u : name;
    int count = snprintf(path, sizeof(path), "%s/botw_%s", directory, suffix);
    if (count < 0 || (size_t)count >= sizeof(path))
        return NULL;
    return fopen(path, "wb");
}

static bool dump_guest_buffer(CPUState* cpu, u32 address, u32 size,
                              const char* path) {
    if (!cpu || address == 0u || size == 0u || !path || !getenv("BOTW_CAPTURE_DIR"))
        return false;
    u8* bytes = (u8*)malloc(size);
    if (!bytes)
        return false;
    for (u32 i = 0; i < size; i++)
        bytes[i] = mem_read8(cpu, address + i);
    FILE* file = open_capture_file(path);
    bool wrote = file && fwrite(bytes, 1u, size, file) == size;
    if (file)
        fclose(file);
    free(bytes);
    return wrote;
}

static bool dump_host_buffer(const void* data, size_t size,
                             const char* path) {
    if (!data || size == 0u || !path)
        return false;
    FILE* file = open_capture_file(path);
    bool wrote = file && fwrite(data, 1u, size, file) == size;
    if (file)
        fclose(file);
    return wrote;
}

static void log_draw_state(CPUState* cpu) {
    if (g_draw_state_log_count++ >= 12u)
        return;
    GX2DisplayListState* list = get_display_list_state(cpu, false);
    fprintf(stderr,
            "gx2: draw state rt=0x%08X fetch=0x%08X vs=0x%08X "
            "ps=0x%08X recording=%u\n",
            g_current_color_buffers[0], g_current_fetch_shader,
            g_current_vertex_shader, g_current_pixel_shader,
            list && list->active ? 1u : 0u);
    if (g_viewport_valid) {
        fprintf(stderr,
                "gx2: viewport %.3f %.3f %.3f %.3f z=%.3f..%.3f\n",
                g_viewport[0], g_viewport[1], g_viewport[2], g_viewport[3],
                g_viewport[4], g_viewport[5]);
    }
    if (g_scissor_valid) {
        fprintf(stderr, "gx2: scissor %u %u %u %u\n", g_scissor[0],
                g_scissor[1], g_scissor[2], g_scissor[3]);
    }

    GX2HostFetchShader* fetch = find_fetch_shader(g_current_fetch_shader, false);
    if (fetch) {
        fprintf(stderr, "gx2: fetch program=0x%08X attributes=%u\n",
                fetch->program, fetch->attribute_count);
        for (u32 i = 0; i < fetch->attribute_count; i++) {
            GX2HostAttribStream* stream = &fetch->attributes[i];
            fprintf(stderr,
                    "gx2: stream[%u] loc=%u buffer=%u offset=0x%X "
                    "fmt=0x%X type=%u divisor=%u mask=0x%X endian=%u\n",
                    i, stream->location, stream->buffer, stream->offset,
                    stream->format, stream->type, stream->divisor,
                    stream->mask, stream->endian_swap);
        }
    }
    for (u32 i = 0; i < GX2_ATTRIB_BUFFER_LIMIT; i++) {
        GX2HostAttribBuffer* buffer = &g_attrib_buffers[i];
        if (!buffer->valid)
            continue;
        fprintf(stderr,
                "gx2: attrib[%u] data=0x%08X size=0x%X stride=%u bytes=",
                i, buffer->data, buffer->size, buffer->stride);
        log_guest_prefix(cpu, buffer->data, buffer->size);
        fputc('\n', stderr);
    }
    for (u32 i = 0; i < GX2_TEXTURE_UNIT_LIMIT; i++) {
        GX2HostTexture* texture = &g_pixel_textures[i];
        if (!texture->valid)
            continue;
        fprintf(stderr,
                "gx2: pixel texture[%u] desc=0x%08X image=0x%08X "
                "%ux%ux%u pitch=%u size=0x%X fmt=0x%X tile=%u "
                "swizzle=0x%X comp=0x%X mip=%u slice=%u bytes=",
                i, texture->descriptor, texture->image, texture->width,
                texture->height, texture->depth, texture->pitch,
                texture->image_size, texture->format, texture->tile_mode,
                texture->swizzle, texture->comp_map, texture->first_mip,
                texture->first_slice);
        log_guest_prefix(cpu, texture->image, texture->image_size);
        fputc('\n', stderr);
    }
}

static void clear_color_surface_values(GX2HostColorSurface* surface,
                                       f64 red, f64 green, f64 blue,
                                       f64 alpha) {
    if (!surface)
        return;
    surface->red = color_to_u8(red);
    surface->green = color_to_u8(green);
    surface->blue = color_to_u8(blue);
    surface->alpha = color_to_u8(alpha);
    surface->cleared = true;
    surface->drawn = false;
    surface->clear_serial = ++g_clear_serial;
    /* Do not also clear a full CPU mirror for every GPU render target.
       Scene frames clear several large attachments; download only if a
       later software operation actually needs those pixels. */
    const bool hdr=surface->format==0x806 || surface->format==0x810 || surface->format==0x820 ||
        surface->format==0x80e || surface->format==0x81e || surface->format==0x823 || surface->format==0x816;
    const float clear[4]={hdr?(float)red:surface->red/255.0f,
        hdr?(float)green:surface->green/255.0f,hdr?(float)blue:surface->blue/255.0f,
        hdr?(float)alpha:surface->alpha/255.0f};
    surface->gpu_composited = wiiu_window_gpu_configure(surface->image,
        surface->width,surface->height,surface->format) &&
        wiiu_window_gpu_clear_float(surface->image,surface->width,surface->height,clear);
    if (!surface->gpu_composited && ensure_color_surface_pixels(surface)) {
        size_t pixels = (size_t)surface->width * surface->height;
        for (size_t i = 0; i < pixels; i++) {
            surface->pixels[i * 4u + 0u] = surface->blue;
            surface->pixels[i * 4u + 1u] = surface->green;
            surface->pixels[i * 4u + 2u] = surface->red;
            surface->pixels[i * 4u + 3u] = surface->alpha;
        }
    }
    surface->cpu_pixels_stale = surface->gpu_composited;
    trace_completed_draw("clear",true,surface);
    if (g_clear_log_count++ < 32u) {
        fprintf(stderr,
                "gx2: clear color=0x%08X image=0x%08X %ux%u pitch=%u "
                "fmt=0x%X tile=%u rgba=(%u,%u,%u,%u)\n",
                surface->descriptor, surface->image, surface->width, surface->height,
                surface->pitch, surface->format, surface->tile_mode,
                surface->red, surface->green, surface->blue, surface->alpha);
    }
}

static void clear_color_surface(CPUState* cpu, u32 descriptor) {
    clear_color_surface_values(update_color_surface(cpu,descriptor), cpu->fpr[1], cpu->fpr[2],
                               cpu->fpr[3], cpu->fpr[4]);
}

/* Copy an accelerated surface back only when a later draw needs the CPU path. */
static bool synchronize_gpu_surface_for_software(GX2HostColorSurface* surface) {
    if (!surface || !surface->gpu_composited)
        return surface != NULL;
    if (surface->cpu_pixels_stale) {
        if (!ensure_color_surface_pixels(surface) ||
            !wiiu_window_gpu_readback(surface->image, surface->pixels,
                                      surface->width, surface->height,
                                      surface->width * 4u)) {
            return false;
        }
        surface->cpu_pixels_stale = false;
    }
    surface->gpu_composited = false;
    wiiu_window_gpu_invalidate(surface->image);
    return true;
}

static void upload_software_surface_to_gpu(GX2HostColorSurface* surface) {
    if (!surface || !surface->pixels || surface->width == 0u ||
        surface->height == 0u) {
        return;
    }
    surface->gpu_composited = wiiu_window_gpu_configure(surface->image,
        surface->width,surface->height,surface->format) && wiiu_window_gpu_upload_bgra(
        surface->image, surface->width, surface->height, surface->pixels,
        surface->width * 4u);
    surface->cpu_pixels_stale = false;
}

static bool software_blit_pixels(GX2HostColorSurface* destination,
                                 const u8* source_pixels, u32 source_width,
                                 u32 source_height, u32 x, u32 y, u32 width,
                                 u32 height, bool alpha_blend) {
    if (!destination || !source_pixels || source_width == 0u ||
        source_height == 0u || !destination->pixels || width == 0 ||
        height == 0 || x >= destination->width || y >= destination->height) {
        return false;
    }
    if (width > destination->width - x)
        width = destination->width - x;
    if (height > destination->height - y)
        height = destination->height - y;

    if (!alpha_blend && x == 0u && y == 0u &&
        width == destination->width && height == destination->height &&
        width == source_width && height == source_height) {
        memcpy(destination->pixels, source_pixels,
               (size_t)width * height * 4u);
        return true;
    }

    for (u32 dy = 0; dy < height; dy++) {
        u32 sy = (u32)((u64)dy * source_height / height);
        u8* destination_row =
            destination->pixels +
            ((size_t)(y + dy) * destination->width + x) * 4u;
        const u8* source_row =
            source_pixels + (size_t)sy * source_width * 4u;
        for (u32 dx = 0; dx < width; dx++) {
            u32 sx = (u32)((u64)dx * source_width / width);
            const u8* source = source_row + (size_t)sx * 4u;
            u8* target = destination_row + (size_t)dx * 4u;
            if (!alpha_blend || source[3] == 255u) {
                memcpy(target, source, 4u);
            } else if (source[3] != 0u) {
                u32 alpha = source[3];
                u32 inverse = 255u - alpha;
                target[0] = (u8)((source[0] * alpha + target[0] * inverse +
                                  127u) /
                                 255u);
                target[1] = (u8)((source[1] * alpha + target[1] * inverse +
                                  127u) /
                                 255u);
                target[2] = (u8)((source[2] * alpha + target[2] * inverse +
                                  127u) /
                                 255u);
                target[3] = 255u;
            }
        }
    }
    return true;
}

static bool software_blit_host_surface(GX2HostColorSurface* destination,
                                       GX2HostColorSurface* source, u32 x,
                                       u32 y, u32 width, u32 height) {
    if (!source || destination == source)
        return false;
    return software_blit_pixels(destination, source->pixels, source->width,
                                source->height, x, y, width, height, false);
}

static bool decoded_texture_matches(const GX2DecodedTexture* decoded,
                                    const GX2HostTexture* texture) {
    const GX2HostTexture* cached = &decoded->texture;
    return decoded->valid && cached->image == texture->image &&
           cached->image_size == texture->image_size &&
           cached->width == texture->width &&
           cached->height == texture->height &&
           cached->depth == texture->depth &&
           cached->format == texture->format &&
           cached->tile_mode == texture->tile_mode &&
           cached->swizzle == texture->swizzle &&
           cached->pitch == texture->pitch &&
           cached->comp_map == texture->comp_map &&
           cached->first_mip == texture->first_mip &&
           cached->first_slice == texture->first_slice;
}

/*
 * Cemu's texture cache checks source memory when a texture is reused.  A
 * compact, once-per-frame fingerprint catches asynchronous guest writes
 * without decoding a large image on every draw.
 */
static u64 texture_content_signature(CPUState* cpu,
                                     const GX2HostTexture* texture) {
    const u8* bytes = guest_bytes(cpu, texture->image, texture->image_size);
    if (!bytes)
        return 0u;

    const u32 stride = 2048u;
    u64 signature = 1469598103934665603ull;
    for (u32 offset = 0u;;) {
        u32 remaining = texture->image_size - offset;
        u32 count = remaining < 4u ? remaining : 4u;
        for (u32 i = 0u; i < count; i++) {
            signature ^= bytes[offset + i];
            signature *= 1099511628211ull;
        }
        if (remaining <= stride)
            break;
        offset += stride;
    }
    signature ^= texture->image_size;
    signature *= 1099511628211ull;
    return signature;
}

static GX2DecodedTexture* decode_texture_cached(CPUState* cpu,
                                                const GX2HostTexture* texture) {
    GX2DecodedTexture* replacement = NULL;
    for (u32 i = 0; i < GX2_DECODED_TEXTURE_LIMIT; i++) {
        GX2DecodedTexture* decoded = &g_decoded_textures[i];
        if (decoded_texture_matches(decoded, texture)) {
            if (decoded->dirty || decoded->last_signature_frame != g_frame_count) {
                u64 signature = texture_content_signature(cpu, texture);
                decoded->last_signature_frame = g_frame_count;
                decoded->dirty = false;
                if (decoded->data_signature != signature) {
                    if (g_texture_refresh_log_count++ < 32u) {
                        fprintf(stderr,
                                "gx2: refreshing changed texture image=0x%08X "
                                "%ux%u\n",
                                texture->image, texture->width,
                                texture->height);
                    }
                    decoded->valid = false;
                    replacement = decoded;
                    break;
                }
            }
            /* The native cache uses serial as a content version. Updating
               it on every lookup uploaded an unchanged atlas every draw. */
            decoded->use_serial = ++g_decoded_texture_serial;
            return decoded;
        }
        if (!replacement || !decoded->valid ||
            (replacement->valid && decoded->use_serial < replacement->use_serial)) {
            replacement = decoded;
        }
    }
    if (!replacement || texture->width == 0u || texture->height == 0u ||
        texture->height > SIZE_MAX / texture->width / 4u) {
        return NULL;
    }

    size_t required = (size_t)texture->width * texture->height * 4u;
    if (required > replacement->pixel_capacity) {
        u8* pixels = (u8*)realloc(replacement->pixels, required);
        if (!pixels)
            return NULL;
        replacement->pixels = pixels;
        replacement->pixel_capacity = required;
    }
    memset(replacement->pixels, 0, required);

    WiiUGX2TextureView view;
    view.image = texture->image;
    view.image_size = texture->image_size;
    view.width = texture->width;
    view.height = texture->height;
    view.depth = texture->depth;
    view.format = texture->format;
    view.tile_mode = texture->tile_mode;
    view.swizzle = texture->swizzle;
    view.pitch = texture->pitch;
    view.comp_map = texture->comp_map;
    view.first_slice = texture->first_slice;

    /*
     * The guide source is reused for more than one boot scene. Preserve a
     * few spaced content revisions so the offline Cemu-layout check can tell
     * a bad address calculation from an incomplete guest-side surface write.
     */
    if (texture->width == 1280u && texture->height == 720u &&
        (texture->format & 0x3Fu) == 0x08u &&
        texture->image_size >= 1280u * 720u * 2u &&
        g_guide_texture_capture_count < 6u) {
        u64 signature = texture_content_signature(cpu, texture);
        bool first = g_guide_texture_capture_count == 0u;
        bool changed = signature != g_guide_texture_capture_signature;
        bool spaced = g_frame_count >= g_guide_texture_capture_frame + 120u;
        if ((first || (changed && spaced))) {
            char path[64];
            snprintf(path, sizeof(path), "sm3dw_guide_rgb565_%u.bin",
                     g_guide_texture_capture_count);
            if (dump_guest_buffer(cpu, texture->image, texture->image_size,
                                  path)) {
                fprintf(stderr,
                        "gx2: captured guide RGB565 revision=%u frame=%llu "
                        "image=0x%08X size=0x%X\n",
                        g_guide_texture_capture_count,
                        (unsigned long long)g_frame_count, texture->image,
                        texture->image_size);
                g_guide_texture_capture_count++;
                g_guide_texture_capture_signature = signature;
                g_guide_texture_capture_frame = g_frame_count;
            }
        }
    }
    if (!wiiu_gx2_software_blit(cpu, &view, replacement->pixels,
                                texture->width, texture->height,
                                texture->width * 4u, 0u, 0u, texture->width,
                                texture->height, false)) {
        replacement->valid = false;
        return NULL;
    }

    replacement->valid = true;
    replacement->dirty = false;
    replacement->texture = *texture;
    replacement->serial = ++g_decoded_texture_serial;
    replacement->use_serial = replacement->serial;
    replacement->data_signature = texture_content_signature(cpu, texture);
    replacement->last_signature_frame = g_frame_count;
    if (g_texture_cache_log_count++ < 32u) {
        fprintf(stderr,
                "gx2: cached texture image=0x%08X %ux%u format=0x%X "
                "tile=%u\n",
                texture->image, texture->width, texture->height,
                texture->format, texture->tile_mode);
    }
    return replacement;
}

static void invalidate_decoded_textures(u32 address, u32 size) {
    u64 end = (u64)address + size;
    for (u32 i = 0; i < GX2_DECODED_TEXTURE_LIMIT; i++) {
        GX2DecodedTexture* decoded = &g_decoded_textures[i];
        if (!decoded->valid)
            continue;
        u64 texture_end =
            (u64)decoded->texture.image + decoded->texture.image_size;
        if ((u64)address < texture_end && end > decoded->texture.image)
            decoded->dirty = true;
    }
}

static f32 f32_from_bits(u32 bits) {
    f32 value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static bool is_guide_bc3_texture(const GX2HostTexture* texture) {
    return texture && (texture->format & 0x3Fu) == 0x33u &&
           texture->height == 180u && texture->width >= 250u &&
           texture->width <= 320u;
}

static void dump_guide_shader(CPUState* cpu, const char* label,
                              u32 descriptor, u32 descriptor_size,
                              u32 size_offset, u32 pointer_offset) {
    if (!cpu || !label || descriptor == 0u ||
        !guest_range_valid(cpu, descriptor, descriptor_size)) {
        return;
    }
    u32 size = mem_read32(cpu, descriptor + size_offset);
    u32 program = mem_read32(cpu, descriptor + pointer_offset);
    char descriptor_path[80];
    char program_path[80];
    snprintf(descriptor_path, sizeof(descriptor_path),
             "sm3dw_guide_%s_descriptor.bin", label);
    snprintf(program_path, sizeof(program_path), "sm3dw_guide_%s_program.bin",
             label);
    fprintf(stderr,
            "gx2: guide %s descriptor=0x%08X program=0x%08X size=0x%X\n",
            label, descriptor, program, size);
    dump_guest_buffer(cpu, descriptor, descriptor_size, descriptor_path);
    if (program != 0u && size != 0u && size <= 0x100000u &&
        guest_range_valid(cpu, program, size)) {
        dump_guest_buffer(cpu, program, size, program_path);
    }
}

static void dump_guide_uniform_registers(const char* label,
                                         const u32* registers,
                                         const u8* valid) {
    u32 shown = 0u;
    for (u32 base = 0u; base < GX2_UNIFORM_REGISTER_LIMIT && shown < 96u;
         base += 4u) {
        bool any = valid[base] || valid[base + 1u] || valid[base + 2u] ||
                   valid[base + 3u];
        if (!any)
            continue;
        fprintf(stderr,
                "gx2: guide %s c[%u]=%08X,%08X,%08X,%08X f=(%.6g,%.6g,%.6g,%.6g)\n",
                label, base, registers[base], registers[base + 1u],
                registers[base + 2u], registers[base + 3u],
                (double)f32_from_bits(registers[base]),
                (double)f32_from_bits(registers[base + 1u]),
                (double)f32_from_bits(registers[base + 2u]),
                (double)f32_from_bits(registers[base + 3u]));
        shown++;
    }
}

static void dump_guide_bc3_state(CPUState* cpu,
                                 const GX2HostTexture* texture) {
    if (g_guide_bc3_state_dumped || !is_guide_bc3_texture(texture))
        return;
    g_guide_bc3_state_dumped = true;
    fprintf(stderr,
            "gx2: guide BC3 state frame=%llu image=0x%08X %ux%u "
            "fetch=0x%08X vs=0x%08X ps=0x%08X\n",
            (unsigned long long)g_frame_count, texture->image, texture->width,
            texture->height, g_current_fetch_shader, g_current_vertex_shader,
            g_current_pixel_shader);
    fprintf(stderr,
            "gx2: guide BC3 layout descriptor=0x%08X size=0x%X pitch=%u "
            "tile=%u swizzle=0x%X comp=0x%X slice=%u\n",
            texture->descriptor, texture->image_size, texture->pitch,
            texture->tile_mode, texture->swizzle, texture->comp_map,
            texture->first_slice);

    dump_guide_shader(cpu, "fetch", g_current_fetch_shader, 0x20u, 0x08u,
                      0x0Cu);
    dump_guide_shader(cpu, "vs", g_current_vertex_shader, 0x134u, 0xD0u,
                      0xD4u);
    dump_guide_shader(cpu, "ps", g_current_pixel_shader, 0xECu, 0xA4u,
                      0xA8u);
    for (u32 i = 0u; i < GX2_UNIFORM_BLOCK_LIMIT; i++) {
        GX2HostUniformBlock* block = &g_vertex_uniform_blocks[i];
        if (!block->valid || block->data == 0u || block->size == 0u)
            continue;
        u32 size = block->size > 0x10000u ? 0x10000u : block->size;
        char path[80];
        snprintf(path, sizeof(path), "sm3dw_guide_vs_block_%u.bin", i);
        fprintf(stderr, "gx2: guide vs block[%u] data=0x%08X size=0x%X\n",
                i, block->data, block->size);
        if (guest_range_valid(cpu, block->data, size))
            dump_guest_buffer(cpu, block->data, size, path);
    }
    dump_guide_uniform_registers("vs", g_vertex_uniform_registers,
                                 g_vertex_uniform_register_valid);
    dump_guide_uniform_registers("ps", g_pixel_uniform_registers,
                                 g_pixel_uniform_register_valid);
    for (u32 i = 0u; i < GX2_ATTRIB_BUFFER_LIMIT; i++) {
        const GX2HostAttribBuffer* buffer = &g_attrib_buffers[i];
        if (!buffer->valid)
            continue;
        fprintf(stderr,
                "gx2: guide attrib[%u] data=0x%08X size=0x%X stride=%u bytes=",
                i, buffer->data, buffer->size, buffer->stride);
        log_guest_prefix(cpu, buffer->data, buffer->size);
        fputc('\n', stderr);
    }
    dump_guest_buffer(cpu, texture->image, texture->image_size,
                      "sm3dw_guide_bc3_raw.bin");
}

typedef struct {
    f32 x;
    f32 y;
    f32 u;
    f32 v;
} GX2HostSpriteVertex;

static bool read_sprite_vertex(CPUState* cpu,
                               const GX2HostAttribBuffer* buffer,
                               u32 index, GX2HostSpriteVertex* out) {
    if (!cpu || !buffer || !out || !buffer->valid || buffer->stride != 8u)
        return false;

    u64 offset = (u64)index * buffer->stride;
    if (offset > buffer->size || buffer->size - (u32)offset < 8u)
        return false;
    u32 address = buffer->data + (u32)offset;
    if (!guest_range_valid(cpu, address, 8u))
        return false;

    out->u = f32_from_bits(mem_read32(cpu, address));
    out->v = f32_from_bits(mem_read32(cpu, address + 4u));
    return isfinite(out->u) && isfinite(out->v) && fabsf(out->u) <= 8.0f &&
           fabsf(out->v) <= 8.0f;
}

static bool sprite_uniforms_valid(u32 first, u32 count) {
    if (first > GX2_UNIFORM_REGISTER_LIMIT ||
        count > GX2_UNIFORM_REGISTER_LIMIT - first) {
        return false;
    }
    for (u32 i = 0; i < count; i++) {
        if (!g_vertex_uniform_register_valid[first + i])
            return false;
    }
    return true;
}

static bool project_layout_sprite_vertex(CPUState* cpu, GX2HostSpriteVertex* vertex,
                                         f32 viewport_x, f32 viewport_y,
                                         f32 viewport_width,
                                         f32 viewport_height) {
    if (vertex) {
        const float uv[2] = {vertex->u, vertex->v};
        float clip[4];
        if (wiiu_latte_layout_position(cpu, g_current_vertex_shader,
                g_vertex_uniform_registers, g_vertex_uniform_register_valid,
                GX2_UNIFORM_REGISTER_LIMIT, uv, clip)) {
            if (fabsf(clip[3]) < 0.000001f) return false;
            float x = clip[0] / clip[3], y = clip[1] / clip[3];
            if (!isfinite(x) || !isfinite(y) || fabsf(x) > 16 || fabsf(y) > 16)
                return false;
            vertex->x = viewport_x + (x + 1)*viewport_width*0.5f;
            vertex->y = viewport_y + (1 - y)*viewport_height*0.5f;
            return true;
        }
    }
    if (!vertex || !sprite_uniforms_valid(16u, 8u) ||
        !sprite_uniforms_valid(28u, 12u) ||
        !sprite_uniforms_valid(60u, 4u)) {
        return false;
    }

    const f32 width = f32_from_bits(g_vertex_uniform_registers[60u]);
    const f32 height = f32_from_bits(g_vertex_uniform_registers[61u]);
    const f32 origin_x = f32_from_bits(g_vertex_uniform_registers[62u]);
    const f32 origin_y = f32_from_bits(g_vertex_uniform_registers[63u]);
    if (!isfinite(width) || !isfinite(height) || !isfinite(origin_x) ||
        !isfinite(origin_y) || width <= 0.0f || height <= 0.0f ||
        width > 8192.0f || height > 8192.0f) {
        return false;
    }

    const f32 local_x = origin_x + vertex->u * width;
    const f32 local_y = origin_y + vertex->v * height;
    const f32 transform_x =
        f32_from_bits(g_vertex_uniform_registers[16u]) * local_x +
        f32_from_bits(g_vertex_uniform_registers[17u]) * local_y +
        f32_from_bits(g_vertex_uniform_registers[19u]);
    const f32 transform_y =
        f32_from_bits(g_vertex_uniform_registers[20u]) * local_x +
        f32_from_bits(g_vertex_uniform_registers[21u]) * local_y +
        f32_from_bits(g_vertex_uniform_registers[23u]);
    const f32 clip_x =
        f32_from_bits(g_vertex_uniform_registers[28u]) * transform_x +
        f32_from_bits(g_vertex_uniform_registers[29u]) * transform_y +
        f32_from_bits(g_vertex_uniform_registers[31u]);
    const f32 clip_y =
        f32_from_bits(g_vertex_uniform_registers[36u]) * transform_x +
        f32_from_bits(g_vertex_uniform_registers[37u]) * transform_y +
        f32_from_bits(g_vertex_uniform_registers[39u]);
    if (!isfinite(clip_x) || !isfinite(clip_y) || fabsf(clip_x) > 4.0f ||
        fabsf(clip_y) > 4.0f) {
        return false;
    }

    vertex->x = viewport_x + (clip_x + 1.0f) * viewport_width * 0.5f;
    vertex->y = viewport_y + (1.0f - clip_y) * viewport_height * 0.5f;
    return true;
}

static f32 sprite_edge(const GX2HostSpriteVertex* a,
                       const GX2HostSpriteVertex* b, f32 x, f32 y) {
    return (x - a->x) * (b->y - a->y) -
           (y - a->y) * (b->x - a->x);
}

static bool rasterize_sprite_triangle(GX2HostColorSurface* target,
                                      const GX2DecodedTexture* texture,
                                      const GX2HostSpriteVertex* a,
                                      const GX2HostSpriteVertex* b,
                                      const GX2HostSpriteVertex* c) {
    const f32 area = sprite_edge(a, b, c->x, c->y);
    if (fabsf(area) < 0.0001f)
        return false;

    f32 min_x = a->x < b->x ? a->x : b->x;
    f32 max_x = a->x > b->x ? a->x : b->x;
    f32 min_y = a->y < b->y ? a->y : b->y;
    f32 max_y = a->y > b->y ? a->y : b->y;
    if (c->x < min_x)
        min_x = c->x;
    if (c->x > max_x)
        max_x = c->x;
    if (c->y < min_y)
        min_y = c->y;
    if (c->y > max_y)
        max_y = c->y;

    s32 left = (s32)floorf(min_x);
    s32 right = (s32)ceilf(max_x) - 1;
    s32 top = (s32)floorf(min_y);
    s32 bottom = (s32)ceilf(max_y) - 1;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= (s32)target->width)
        right = (s32)target->width - 1;
    if (bottom >= (s32)target->height)
        bottom = (s32)target->height - 1;
    if (g_scissor_valid) {
        s32 scissor_right = (s32)(g_scissor[0] + g_scissor[2]) - 1;
        s32 scissor_bottom = (s32)(g_scissor[1] + g_scissor[3]) - 1;
        if (left < (s32)g_scissor[0])
            left = (s32)g_scissor[0];
        if (top < (s32)g_scissor[1])
            top = (s32)g_scissor[1];
        if (right > scissor_right)
            right = scissor_right;
        if (bottom > scissor_bottom)
            bottom = scissor_bottom;
    }
    if (left > right || top > bottom)
        return false;

    bool wrote = false;
    for (s32 y = top; y <= bottom; y++) {
        for (s32 x = left; x <= right; x++) {
            const f32 px = (f32)x + 0.5f;
            const f32 py = (f32)y + 0.5f;
            const f32 weight_a = sprite_edge(b, c, px, py) / area;
            const f32 weight_b = sprite_edge(c, a, px, py) / area;
            const f32 weight_c = 1.0f - weight_a - weight_b;
            if (weight_a < 0.0f || weight_b < 0.0f || weight_c < 0.0f)
                continue;

            f32 u = weight_a * a->u + weight_b * b->u + weight_c * c->u;
            f32 v = weight_a * a->v + weight_b * b->v + weight_c * c->v;
            if (u < 0.0f)
                u = 0.0f;
            if (u > 1.0f)
                u = 1.0f;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            const u32 texture_x =
                (u32)(u * (texture->texture.width - 1u) + 0.5f);
            const u32 texture_y =
                (u32)(v * (texture->texture.height - 1u) + 0.5f);
            const u8* source = texture->pixels +
                               ((size_t)texture_y * texture->texture.width +
                                texture_x) *
                                   4u;
            if (source[3] == 0u)
                continue;

            u8* destination = target->pixels +
                              ((size_t)y * target->width + (u32)x) * 4u;
            if (source[3] == 255u) {
                memcpy(destination, source, 4u);
            } else {
                const u32 alpha = source[3];
                const u32 inverse = 255u - alpha;
                destination[0] =
                    (u8)((source[0] * alpha + destination[0] * inverse +
                          127u) /
                         255u);
                destination[1] =
                    (u8)((source[1] * alpha + destination[1] * inverse +
                          127u) /
                         255u);
                destination[2] =
                    (u8)((source[2] * alpha + destination[2] * inverse +
                          127u) /
                         255u);
                destination[3] = 255u;
            }
            wrote = true;
        }
    }
    return wrote;
}

/*
 * Layout's simple sprite shader supplies UVs in a compact vertex buffer and
 * builds screen-space geometry from its transform uniforms.  Reproduce that
 * fixed function path here instead of treating every texture draw as a full
 * screen blit.
 */
static u32 read_known_pixel_material(CPUState* cpu, WiiUWindowGpuMaterial* material) {
    if (!guest_range_valid(cpu,g_current_pixel_shader,0xACu)) return 0;
    u32 size=mem_read32(cpu,g_current_pixel_shader+0xA4u);
    u32 program=mem_read32(cpu,g_current_pixel_shader+0xA8u);
    if ((size!=416u && size!=544u) || !guest_range_valid(cpu,program,size)) return 0;
    /* Fingerprint all executable clauses and padding, excluding the compiler's
       final 16-byte identifier. Do not guess material semantics from size. */
    u32 hash=2166136261u;
    for(u32 i=0;i<size-16u;++i) hash=(hash^mem_read8(cpu,program+i))*16777619u;
    bool font=size==544u && hash==0xB94F9237u;
    bool layout=size==416u && (hash==0x88BF245Eu || hash==0xEFDB56E2u);
    if(!font && !layout) return 0;
    memset(material,0,sizeof(*material));
    for(u32 i=0;i<(font?16u:8u);++i)
        if(!g_pixel_uniform_register_valid[i] ||
           !isfinite(f32_from_bits(g_pixel_uniform_registers[i]))) return 0;
    for(u32 i=0;i<4u;++i) {
        material->bias[i]=f32_from_bits(g_pixel_uniform_registers[i]);
        material->scale[i]=f32_from_bits(g_pixel_uniform_registers[i+4u]);
    }
    material->font_coverage=font?1.0f:0.0f;
    return font?2u:1u;
}

static u32 surface_bytes_per_pixel(u32 format);
static void invalidate_decoded_textures(u32 address, u32 size);

static bool copy_raw_surface(CPUState* cpu, const GX2HostTexture* src,
                             const GX2HostTexture* dst, u32 src_slice, u32 dst_slice) {
    if(src_slice>=src->depth || dst_slice>=dst->depth) return false;
    const u8* source=guest_bytes(cpu,src->image,src->image_size);
    u8* target=(u8*)guest_bytes(cpu,dst->image,dst->image_size);
    if(!source || !target) return false;
    u32 hw=src->format&63u;
    bool compressed=hw>=0x31u && hw<=0x35u;
    u32 bytes=compressed ? (hw==0x31u || hw==0x34u ? 8u:16u) : surface_bytes_per_pixel(hw);
    u32 width=compressed?(dst->width+3u)/4u:dst->width;
    u32 height=compressed?(dst->height+3u)/4u:dst->height;
    u32 sp,sh,dp,dh;
    if(!wiiu_cemu_normalize_texture_layout(src->width,src->height,src->format,
        src->pitch,src->image_size,&sp,&sh) ||
       !wiiu_cemu_normalize_texture_layout(dst->width,dst->height,dst->format,
        dst->pitch,dst->image_size,&dp,&dh)) return false;
    size_t count=(size_t)width*height;
    if(count>SIZE_MAX/bytes || count>SIZE_MAX/sizeof(u32)) return false;
    u8* staged=(u8*)malloc(count*bytes);
    u32* offsets=(u32*)malloc(count*sizeof(u32));
    if(!staged || !offsets) {free(staged);free(offsets);return false;}
    bool valid=true;
    for(u32 y=0;y<height && valid;++y) for(u32 x=0;x<width;++x) {
        u32 so,doff;
        if(!wiiu_cemu_surface_offset(x,y,src_slice,bytes*8u,sp,sh,src->tile_mode,src->swizzle,&so) ||
           !wiiu_cemu_surface_offset(x,y,dst_slice,bytes*8u,dp,dh,dst->tile_mode,dst->swizzle,&doff) ||
           so>src->image_size || bytes>src->image_size-so ||
           doff>dst->image_size || bytes>dst->image_size-doff) {valid=false;break;}
        size_t index=(size_t)y*width+x;
        memcpy(staged+index*bytes,source+so,bytes);offsets[index]=doff;
    }
    if(valid) {
        for(size_t i=0;i<count;++i) memcpy(target+offsets[i],staged+i*bytes,bytes);
        invalidate_decoded_textures(dst->image,dst->image_size);
        GX2HostColorSurface* cached=find_color_surface_by_image(dst->image);
        if(cached) {
            wiiu_window_gpu_invalidate(cached->image);
            free(cached->pixels);memset(cached,0,sizeof(*cached));
        }
    }
    free(offsets);free(staged);
    return valid;
}

static bool read_copy_surface(CPUState* cpu, u32 descriptor, GX2HostTexture* out) {
    memset(out, 0, sizeof(*out));
    if (!guest_range_valid(cpu, descriptor, 0x74u)) return false;
    out->descriptor = descriptor;
    out->width = mem_read32(cpu, descriptor + 4u);
    out->height = mem_read32(cpu, descriptor + 8u);
    out->depth = mem_read32(cpu, descriptor + 12u);
    out->format = mem_read32(cpu, descriptor + 0x14u);
    out->image_size = mem_read32(cpu, descriptor + 0x20u);
    out->image = mem_read32(cpu, descriptor + 0x24u);
    out->tile_mode = mem_read32(cpu, descriptor + 0x30u);
    out->swizzle = mem_read32(cpu, descriptor + 0x34u);
    out->pitch = mem_read32(cpu, descriptor + 0x3Cu);
    out->comp_map = 0x00010203u;
    out->valid = out->image && out->width && out->height &&
        out->width <= 8192u && out->height <= 8192u;
    return out->valid;
}

/* Copy storage, not a draw: blend, viewport, scissor and shader state must
   have no effect. Display lists capture descriptors but read pixels on replay. */
static bool copy_surface(CPUState* cpu, const GX2RecordedCommand* command) {
    const GX2HostTexture* src = &command->texture;
    const GX2HostTexture* dst = &command->copy_destination;
    static u32 copy_log_count;
    if (copy_log_count++ < 24u) {
        fprintf(stderr,"gx2: copy surface %08X %ux%u fmt=%X -> %08X %ux%u fmt=%X mip=%u/%u slice=%u/%u\n",
            src->image,src->width,src->height,src->format,
            dst->image,dst->width,dst->height,dst->format,
            command->args[1],command->args[4],command->args[2],command->args[5]);
    }
    if (!src->valid || !dst->valid || command->args[1] ||
        command->args[4] || src->format != dst->format ||
        src->width < dst->width || src->height < dst->height) return false;
    if (src->image == dst->image && command->args[2]==command->args[5]) return true;
    GX2HostColorSurface* source = find_color_surface_by_image(src->image);
    if (!source) return copy_raw_surface(cpu,src,dst,command->args[2],command->args[5]);
    if(command->args[2] || command->args[5] || source->width!=src->width ||
       source->height!=src->height) return false;
    GX2HostColorSurface* target = bind_color_snapshot(dst);
    if (!target || target == source) return false;
    if (!target->gpu_composited) {
        if (!ensure_color_surface_pixels(target)) return false;
        upload_software_surface_to_gpu(target);
    }
    if (source && !source->gpu_composited) upload_software_surface_to_gpu(source);
    float u = (float)dst->width / src->width, v = (float)dst->height / src->height;
    WiiUWindowGpuVertex quad[4] = {
        {0,0,0,0,255,255,255,255},{(float)dst->width,0,u,0,255,255,255,255},
        {(float)dst->width,(float)dst->height,u,v,255,255,255,255},
        {0,(float)dst->height,0,v,255,255,255,255}};
    WiiUWindowGpuBlendControl replace = {true,1,0,0,true,1,0,0};
    bool copied = wiiu_window_gpu_draw_surface(target->image,
        dst->width,dst->height,source->image,quad,&replace,false,0,0,0,0);
    if (!copied) {
        if (!ensure_color_surface_pixels(target) ||
            (source && !synchronize_gpu_surface_for_software(source))) return false;
        const u8* pixels = source->pixels;
        if (!pixels) return false;
        for (u32 y = 0; y < dst->height; ++y)
            memcpy(target->pixels + (size_t)y*dst->width*4u,
                pixels + (size_t)y*src->width*4u,(size_t)dst->width*4u);
        target->cpu_pixels_stale = false;
        upload_software_surface_to_gpu(target);
    } else target->cpu_pixels_stale = true;
    target->drawn = true;
    target->draw_serial = ++g_draw_serial;
    trace_completed_draw("copy",true,target);
    return true;
}

static bool rasterize_layout_sprite(CPUState* cpu, GX2HostColorSurface* target,
                                    const GX2HostTexture* texture,
                                    GX2HostSpriteVertex* vertices) {
    if (!cpu || !target || !texture || !vertices)
        return false;
    static u32 layout_capture_count;
    if (getenv("BOTW_CAPTURE_DIR") && layout_capture_count < 2u) {
        const char* tag = layout_capture_count++ ? "layout_second" : "layout_first";
        fprintf(stderr, "gx2: %s texture=%08X format=%X\n", tag,
                texture->image, texture->format);
        dump_guide_uniform_registers(tag, g_vertex_uniform_registers,
                                     g_vertex_uniform_register_valid);
        dump_guide_shader(cpu, tag, g_current_vertex_shader, 0x134u,
                          0xD0u, 0xD4u);
    }
    f32 viewport_x = 0.0f;
    f32 viewport_y = 0.0f;
    f32 viewport_width = (f32)target->width;
    f32 viewport_height = (f32)target->height;
    if (g_viewport_valid && g_viewport[2] > 0.0 && g_viewport[3] > 0.0) {
        viewport_x = (f32)g_viewport[0];
        viewport_y = (f32)g_viewport[1];
        viewport_width = (f32)g_viewport[2];
        viewport_height = (f32)g_viewport[3];
    }

    for (u32 i = 0; i < 4u; i++) {
        if (!project_layout_sprite_vertex(cpu, &vertices[i], viewport_x, viewport_y,
                                          viewport_width, viewport_height)) {
            return false;
        }
    }

    GX2HostColorSurface* source = find_color_surface_by_image(texture->image);
    if (source == target) return false;
    GX2DecodedTexture* decoded = source ? NULL : decode_texture_cached(cpu, texture);
    if (!source && (!decoded || !decoded->pixels || decoded->texture.width == 0u ||
        decoded->texture.height == 0u)) {
        return false;
    }
    /* Keep layout composition on the GPU. Previously every sprite forced a
       render-target readback, two CPU triangle walks, and a full re-upload. */
    if (!target->gpu_composited) {
        if (!ensure_color_surface_pixels(target)) return false;
        upload_software_surface_to_gpu(target);
    }
    if (target->gpu_composited) {
        WiiUWindowGpuVertex quad[4];
        for (u32 i = 0; i < 4; ++i) {
            /* CPU layout uses TL,TR,BL,BR; native quads use perimeter order. */
            const u32 index = i < 2u ? i : 5u - i;
            quad[i].x = vertices[index].x; quad[i].y = vertices[index].y;
            quad[i].u = vertices[index].u; quad[i].v = vertices[index].v;
            quad[i].red = quad[i].green = quad[i].blue = quad[i].alpha = 255;
        }
        WiiUWindowGpuMaterial material;
        u32 material_kind=read_known_pixel_material(cpu,&material);
        if (source && !source->gpu_composited) upload_software_surface_to_gpu(source);
        bool drawn = source ? (material_kind==1u ? wiiu_window_gpu_draw_surface_material(
                target->image,target->width,target->height,source->image,
                quad,&material,&g_blend_controls[0],g_scissor_valid,
                g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]) :
            wiiu_window_gpu_draw_surface(target->image,target->width,target->height,
                source->image,quad,&g_blend_controls[0],g_scissor_valid,
                g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3])) :
            material_kind==1u ? wiiu_window_gpu_draw_material(
                target->image,target->width,target->height,decoded->pixels,
                decoded->texture.width,decoded->texture.height,decoded->serial,
                quad,1u,&material,&g_blend_controls[0],g_scissor_valid,
                g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]) :
            wiiu_window_gpu_draw_bgra_color(target->image,
                target->width, target->height, decoded->pixels,
                decoded->texture.width, decoded->texture.height, decoded->serial,
                quad, &g_blend_controls[0], g_scissor_valid,
                g_scissor[0], g_scissor[1], g_scissor[2], g_scissor[3]);
        if (drawn) {
            target->cpu_pixels_stale = true;
            return true;
        }
    }
    if (!synchronize_gpu_surface_for_software(target) ||
        !ensure_color_surface_pixels(target)) return false;
    GX2DecodedTexture surface_pixels = {0};
    if (source) {
        if (!synchronize_gpu_surface_for_software(source)) return false;
        surface_pixels.texture = *texture;
        surface_pixels.pixels = source->pixels;
        decoded = &surface_pixels;
    }
    bool wrote = rasterize_sprite_triangle(target, decoded, &vertices[0],
                                           &vertices[1], &vertices[2]);
    wrote |= rasterize_sprite_triangle(target, decoded, &vertices[2],
                                       &vertices[1], &vertices[3]);
    return wrote;
}

static bool draw_layout_sprite(CPUState* cpu, GX2HostColorSurface* target,
                               const GX2HostTexture* texture, u32 primitive,
                               u32 vertex_count, u32 first_vertex) {
    if (!cpu || !target || !texture || primitive != 19u ||
        vertex_count != 4u || !g_attrib_buffers[0].valid ||
        g_attrib_buffers[0].stride != 8u) {
        return false;
    }

    GX2HostSpriteVertex vertices[4];
    for (u32 i = 0; i < 4u; i++) {
        if (!read_sprite_vertex(cpu, &g_attrib_buffers[0], first_vertex + i,
                                &vertices[i])) {
            return false;
        }
    }
    return rasterize_layout_sprite(cpu, target, texture, vertices);
}

/* GX2 baseVertex offsets decoded vertex indices, not the index-buffer address.
   Index types 0/1 are little endian and 4/9 are big endian (16/32 bit). */
static u32 gx2_index_size(u32 type) {
    return type == 0u || type == 4u ? 2u :
           type == 1u || type == 9u ? 4u : 0u;
}

static bool read_draw_index(CPUState* cpu, u32 data, u32 element,
                            u32 type, s32 base_vertex, u32* result) {
    u32 size = gx2_index_size(type);
    u64 address = (u64)data + (u64)element * size;
    if (!size || address > 0xFFFFFFFFu ||
        !guest_range_valid(cpu, (u32)address, size)) return false;
    u32 raw = size == 2u ? mem_read16(cpu, (u32)address) :
                          mem_read32(cpu, (u32)address);
    if (type == 0u) raw = ((raw & 255u) << 8u) | (raw >> 8u);
    if (type == 1u) raw = ((raw & 255u) << 24u) |
        ((raw & 0xFF00u) << 8u) | ((raw >> 8u) & 0xFF00u) | (raw >> 24u);
    s64 vertex = (s64)raw + base_vertex;
    if (vertex < 0 || vertex > 0xFFFFFFFFLL) return false;
    *result = (u32)vertex;
    return true;
}

static bool draw_layout_sprite_indexed(
    CPUState* cpu, GX2HostColorSurface* target, const GX2HostTexture* texture,
    u32 primitive, u32 index_count, u32 index_type, u32 index_data,
    s32 index_offset) {
    if (!cpu || !target || !texture || primitive != 19u || index_count != 4u ||
        !gx2_index_size(index_type) ||
        !g_attrib_buffers[0].valid || g_attrib_buffers[0].stride != 8u) {
        return false;
    }

    GX2HostSpriteVertex vertices[4];
    for (u32 i = 0; i < 4u; i++) {
        u32 index;
        if (!read_draw_index(cpu, index_data, i, index_type, index_offset, &index) ||
            !read_sprite_vertex(cpu, &g_attrib_buffers[0], index,
                                &vertices[i])) {
            return false;
        }
    }

    /* Indexed layout quads arrive as a strip; normalize them to UV corners. */
    GX2HostSpriteVertex ordered[4];
    bool occupied[4] = {false, false, false, false};
    f32 min_u = vertices[0].u;
    f32 max_u = vertices[0].u;
    f32 min_v = vertices[0].v;
    f32 max_v = vertices[0].v;
    for (u32 i = 1u; i < 4u; i++) {
        if (vertices[i].u < min_u)
            min_u = vertices[i].u;
        if (vertices[i].u > max_u)
            max_u = vertices[i].u;
        if (vertices[i].v < min_v)
            min_v = vertices[i].v;
        if (vertices[i].v > max_v)
            max_v = vertices[i].v;
    }
    if (max_u - min_u < 0.0001f || max_v - min_v < 0.0001f)
        return false;
    for (u32 i = 0u; i < 4u; i++) {
        bool left = fabsf(vertices[i].u - min_u) <= 0.01f;
        bool right = fabsf(vertices[i].u - max_u) <= 0.01f;
        bool top = fabsf(vertices[i].v - min_v) <= 0.01f;
        bool bottom = fabsf(vertices[i].v - max_v) <= 0.01f;
        if ((left == right) || (top == bottom))
            return false;
        u32 slot = top ? (left ? 0u : 1u) : (left ? 2u : 3u);
        if (occupied[slot])
            return false;
        ordered[slot] = vertices[i];
        occupied[slot] = true;
    }
    if (!occupied[0] || !occupied[1] || !occupied[2] || !occupied[3])
        return false;
    return rasterize_layout_sprite(cpu, target, texture, ordered);
}

static bool draw_host_surface_on_gpu(GX2HostColorSurface* target,
                                     GX2HostColorSurface* source, u32 x,
                                     u32 y, u32 width, u32 height) {
    if (!target || !source || source == target || width == 0u || height == 0u)
        return false;

    // The software renderer already owns a current CPU copy of these GX2
    // surfaces. Upload it once, then keep the cross-surface compose entirely
    // on the native GPU instead of reading either surface back every frame.
    if (!target->gpu_composited && target->pixels)
        upload_software_surface_to_gpu(target);
    if (!source->gpu_composited && source->pixels)
        upload_software_surface_to_gpu(source);
    if (!target->gpu_composited || !source->gpu_composited)
        return false;

    WiiUWindowGpuVertex vertices[4] = {
        { (f32)x, (f32)y, 0.0f, 0.0f, 255u, 255u, 255u, 255u },
        { (f32)(x + width), (f32)y, 1.0f, 0.0f, 255u, 255u, 255u, 255u },
        { (f32)(x + width), (f32)(y + height), 1.0f, 1.0f,
          255u, 255u, 255u, 255u },
        { (f32)x, (f32)(y + height), 0.0f, 1.0f,
          255u, 255u, 255u, 255u },
    };
    if (!wiiu_window_gpu_draw_surface(
            target->image, target->width, target->height,
            source->image, vertices, &g_blend_controls[0],
            g_scissor_valid, g_scissor[0],
            g_scissor[1], g_scissor[2], g_scissor[3])) {
        return false;
    }

    target->cpu_pixels_stale = true;
    g_gpu_surface_blit_count++;
    if (g_gpu_surface_blit_count <= 8u) {
        fprintf(stderr,
                "gx2: native GPU surface draw target=0x%08X source=0x%08X "
                "rect=%u,%u %ux%u\n",
                target->image, source->image, x, y, width, height);
    }
    return true;
}

static bool draw_decoded_texture_on_gpu(CPUState* cpu,
                                        GX2HostColorSurface* target,
                                        const GX2HostTexture* texture, u32 x,
                                        u32 y, u32 width, u32 height) {
    if (!cpu || !target || !texture || width == 0u || height == 0u)
        return false;

    if (!target->gpu_composited && target->pixels)
        upload_software_surface_to_gpu(target);
    if (!target->gpu_composited)
        return false;

    GX2DecodedTexture* decoded = decode_texture_cached(cpu, texture);
    if (!decoded || !decoded->pixels || decoded->texture.width == 0u ||
        decoded->texture.height == 0u) {
        return false;
    }

    WiiUWindowGpuVertex vertices[4] = {
        { (f32)x, (f32)y, 0.0f, 0.0f, 255u, 255u, 255u, 255u },
        { (f32)(x + width), (f32)y, 1.0f, 0.0f, 255u, 255u, 255u,
          255u },
        { (f32)(x + width), (f32)(y + height), 1.0f, 1.0f, 255u, 255u,
          255u, 255u },
        { (f32)x, (f32)(y + height), 0.0f, 1.0f, 255u, 255u, 255u,
          255u },
    };
    if (!wiiu_window_gpu_draw_bgra_color(
            target->image, target->width, target->height,
            decoded->pixels, decoded->texture.width, decoded->texture.height,
            decoded->serial, vertices, &g_blend_controls[0],
            g_scissor_valid, g_scissor[0],
            g_scissor[1], g_scissor[2], g_scissor[3])) {
        return false;
    }

    target->cpu_pixels_stale = true;
    g_gpu_texture_blit_count++;
    if (g_gpu_texture_blit_count <= 8u) {
        fprintf(stderr,
                "gx2: native GPU texture draw target=0x%08X image=0x%08X "
                "%ux%u rect=%u,%u %ux%u\n",
                target->image, texture->image, texture->width,
                texture->height, x, y, width, height);
    }
    return true;
}

static bool software_draw_texture(CPUState* cpu, u32 primitive,
                                  u32 vertex_count, u32 first_vertex) {
    GX2HostColorSurface* target =
        find_color_surface(g_current_color_buffers[0], false);
    GX2HostTexture* texture = &g_pixel_textures[0];
    if (!target || !target->width || !target->height || !texture->valid) {
        return false;
    }
    u32 hw_format = texture->format & 0x3Fu;
    if ((hw_format == 0x34u || hw_format == 0x35u) &&
        !(primitive == 19u && vertex_count == 4u &&
          g_attrib_buffers[0].valid && g_attrib_buffers[0].stride == 8u))
        return false;

    u32 x = 0;
    u32 y = 0;
    u32 width = target->width;
    u32 height = target->height;
    if (g_viewport_valid && g_viewport[2] > 0.0 && g_viewport[3] > 0.0) {
        x = g_viewport[0] > 0.0 ? (u32)g_viewport[0] : 0u;
        y = g_viewport[1] > 0.0 ? (u32)g_viewport[1] : 0u;
        width = (u32)g_viewport[2];
        height = (u32)g_viewport[3];
    }
    if (g_scissor_valid) {
        u32 left = x > g_scissor[0] ? x : g_scissor[0];
        u32 top = y > g_scissor[1] ? y : g_scissor[1];
        u64 right_a = (u64)x + width;
        u64 right_b = (u64)g_scissor[0] + g_scissor[2];
        u64 bottom_a = (u64)y + height;
        u64 bottom_b = (u64)g_scissor[1] + g_scissor[3];
        u32 right = (u32)(right_a < right_b ? right_a : right_b);
        u32 bottom = (u32)(bottom_a < bottom_b ? bottom_a : bottom_b);
        if (right <= left || bottom <= top)
            return false;
        x = left;
        y = top;
        width = right - left;
        height = bottom - top;
    }

    dump_guide_bc3_state(cpu, texture);
    GX2HostColorSurface* source = find_color_surface_by_image(texture->image);
    bool layout_candidate = primitive == 19u && vertex_count == 4u &&
                            g_attrib_buffers[0].valid &&
                            g_attrib_buffers[0].stride == 8u;
    /* Real shaders describe geometry/materials, not an implicit texture
       rectangle. The recognized copy/effect/constant paths ran before this
       fallback. Never replace an unimplemented scene pass with a fullscreen
       blit of whatever texture happened to remain bound. */
    bool shader_bound = g_current_vertex_shader || g_current_pixel_shader;
    if (shader_bound && !layout_candidate) return false;
    if (!layout_candidate &&
        draw_host_surface_on_gpu(target, source, x, y, width, height)) {
        target->drawn = true;
        target->draw_serial = ++g_draw_serial;
        return true;
    }
    if (!layout_candidate &&
        draw_decoded_texture_on_gpu(cpu, target, texture, x, y, width,
                                    height)) {
        target->drawn = true;
        target->draw_serial = ++g_draw_serial;
        return true;
    }

    if (draw_layout_sprite(cpu, target, texture, primitive, vertex_count,
                           first_vertex)) {
        target->drawn = true;
        target->draw_serial = ++g_draw_serial;
        if (!target->gpu_composited) upload_software_surface_to_gpu(target);
        return true;
    }
    if (shader_bound) return false;
    if (!synchronize_gpu_surface_for_software(target) ||
        !ensure_color_surface_pixels(target)) return false;
    if (source && source != target &&
        !synchronize_gpu_surface_for_software(source)) {
        source = NULL;
    }
    if (g_frame_count >= 800u && g_late_draw_trace_count++ < 160u) {
        fprintf(stderr,
                "gx2: late draw frame=%llu target=0x%08X image=0x%08X "
                "%ux%u fmt=0x%X tile=%u swizzle=0x%X source=%s/0x%08X "
                "drawn=%u\n",
                (unsigned long long)g_frame_count, target->image,
                texture->image, texture->width, texture->height,
                texture->format, texture->tile_mode, texture->swizzle,
                source && source != target ? "host" : "raw",
                source ? source->image : 0u,
                source && source->drawn ? 1u : 0u);
    }
    bool wrote = software_blit_host_surface(target, source, x, y, width,
                                            height);
    if (!wrote) {
        GX2DecodedTexture* decoded = decode_texture_cached(cpu, texture);
        if (decoded) {
            if (!g_guide_bc3_texture_dumped && is_guide_bc3_texture(texture) &&
                dump_host_buffer(decoded->pixels,
                                 (size_t)decoded->texture.width *
                                     decoded->texture.height * 4u,
                                 "sm3dw_guide_bc3.bgra")) {
                g_guide_bc3_texture_dumped = true;
            }
            bool alpha_blend =
                hw_format == 0x1Au ||
                (hw_format >= 0x31u && hw_format <= 0x33u);
            wrote = software_blit_pixels(
                target, decoded->pixels, texture->width, texture->height, x,
                y, width, height, alpha_blend);
        }
    }
    if (!wrote)
        return false;
    target->drawn = true;
    target->draw_serial = ++g_draw_serial;
    upload_software_surface_to_gpu(target);
    if (g_software_draw_log_count++ < 64u) {
        fprintf(stderr,
                "gx2: software draw target=0x%08X texture=0x%08X "
                "%ux%u fmt=0x%X tile=%u rect=%u,%u %ux%u\n",
                target->image, texture->image, texture->width,
                texture->height, texture->format, texture->tile_mode, x, y,
                width, height);
    }
    return true;
}

static bool software_draw_indexed_texture(CPUState* cpu, u32 primitive,
                                          u32 index_count, u32 index_type,
                                          u32 index_data, s32 index_offset) {
    GX2HostColorSurface* target =
        find_color_surface(g_current_color_buffers[0], false);
    GX2HostTexture* texture = &g_pixel_textures[0];
    if (!target || !texture->valid)
        return false;
    bool layout_candidate = primitive == 19u && index_count == 4u &&
                            gx2_index_size(index_type) != 0u &&
                            g_attrib_buffers[0].valid &&
                            g_attrib_buffers[0].stride == 8u;
    /* A bound texture is not evidence that the pixel shader samples it.
       In particular the title's three-index draw leaves the font atlas bound.
       Blitting it here draws the entire character sheet across the screen. */
    if (!layout_candidate) {
        static u32 unsupported_indexed_logs;
        if (unsupported_indexed_logs++ < 4u) {
            fprintf(stderr, "gx2: unsupported indexed shader primitive=%u count=%u "
                    "vs=%08X ps=%08X fetch=%08X stride=%u\n", primitive,
                    index_count, g_current_vertex_shader, g_current_pixel_shader,
                    g_current_fetch_shader, g_attrib_buffers[0].stride);
            dump_guide_shader(cpu, "unsupported_vs", g_current_vertex_shader,
                              0x134u, 0xD0u, 0xD4u);
            dump_guide_shader(cpu, "unsupported_ps", g_current_pixel_shader,
                              0xECu, 0xA4u, 0xA8u);
        }
        return false;
    }
    if (!draw_layout_sprite_indexed(cpu, target, texture, primitive,
                                    index_count, index_type, index_data,
                                    index_offset)) {
        return false;
    }
    target->drawn = true;
    target->draw_serial = ++g_draw_serial;
    if (!target->gpu_composited) upload_software_surface_to_gpu(target);
    return true;
}

typedef struct {
    f32 x;
    f32 y;
    f32 z;
    f32 u;
    f32 v;
    u8 red;
    u8 green;
    u8 blue;
    u8 alpha;
} GX2HostUiVertex;

static bool read_ui_vertex(CPUState* cpu, const GX2HostAttribBuffer* buffer,
                           u32 index, GX2HostUiVertex* out) {
    if (!cpu || !buffer || !out || !buffer->valid ||
        buffer->stride != 28u) {
        return false;
    }

    u64 offset = (u64)index * buffer->stride;
    if (offset > buffer->size || buffer->size - (u32)offset < 24u)
        return false;
    u32 address = buffer->data + (u32)offset;
    if (!guest_range_valid(cpu, address, 24u))
        return false;

    out->x = f32_from_bits(mem_read32(cpu, address + 0u));
    out->y = f32_from_bits(mem_read32(cpu, address + 4u));
    out->z = f32_from_bits(mem_read32(cpu, address + 8u));
    out->red = mem_read8(cpu, address + 12u);
    out->green = mem_read8(cpu, address + 13u);
    out->blue = mem_read8(cpu, address + 14u);
    out->alpha = mem_read8(cpu, address + 15u);
    out->u = f32_from_bits(mem_read32(cpu, address + 16u));
    out->v = f32_from_bits(mem_read32(cpu, address + 20u));
    return isfinite(out->x) && isfinite(out->y) && isfinite(out->u) &&
           isfinite(out->v) && fabsf(out->x) <= 16384.0f &&
           fabsf(out->y) <= 16384.0f;
}

static u8 fetched_color_to_u8(float value) {
    if (!isfinite(value) || value <= 0.0f)
        return 0u;
    if (value <= 1.0f)
        value *= 255.0f;
    return value >= 255.0f ? 255u : (u8)(value + 0.5f);
}

static bool read_ui_vertex_from_fetch(
    CPUState* cpu, u32 index, const WiiULatteVertexTransform* transform,
    GX2HostUiVertex* out) {
    if (!cpu || !out)
        return false;

    WiiULatteFetchedVertex fetched;
    GX2HostFetchShader* fetch = NULL;
    if (!fetch_current_vertex(cpu, index, 0u, &fetched, &fetch) || !fetch)
        return false;

    bool have_position = false;
    bool have_uv = false;
    bool have_color = false;
    for (u32 i = 0u; i < fetch->attribute_count; i++) {
        const GX2HostAttribStream* stream = &fetch->attributes[i];
        WiiULatteFetchStream latte_stream;
        WiiULatteFetchFormatInfo format;
        float values[4];
        copy_attrib_stream_to_latte(stream, &latte_stream);
        if (!wiiu_latte_fetch_stream_format_info(&latte_stream, &format) ||
            !wiiu_latte_fetch_get_gpr(&fetched, stream->location, values)) {
            continue;
        }

        bool is_position =
            !have_position && format.floating && format.component_count >= 3u &&
            (!transform || stream->location == transform->input_gpr);
        if (is_position) {
            out->x = values[0];
            out->y = values[1];
            out->z = values[2];
            have_position = true;
            continue;
        }
        if (!have_uv && format.floating && format.component_count >= 2u) {
            out->u = values[0];
            out->v = values[1];
            have_uv = true;
            continue;
        }
        if (!have_color && format.component_count == 4u && !format.floating) {
            out->red = fetched_color_to_u8(values[0]);
            out->green = fetched_color_to_u8(values[1]);
            out->blue = fetched_color_to_u8(values[2]);
            out->alpha = fetched_color_to_u8(values[3]);
            have_color = true;
        }
    }

    if (!have_position || !have_uv || !have_color || !isfinite(out->x) ||
        !isfinite(out->y) || !isfinite(out->u) || !isfinite(out->v) ||
        fabsf(out->x) > 16384.0f || fabsf(out->y) > 16384.0f) {
        return false;
    }
    if (g_latte_fetch_used_log_count++ < 4u) {
        fprintf(stderr,
                "gx2: native Latte fetch vertex=%u streams=%u gpr=%u "
                "failed=%u pos=(%.3f,%.3f,%.3f) uv=(%.3f,%.3f)\n",
                index, fetch->attribute_count,
                transform ? (unsigned)transform->input_gpr : 0u,
                fetched.failed_count, out->x, out->y, out->z, out->u, out->v);
    }
    return true;
}

static void map_ui_vertex_to_surface(
    GX2HostUiVertex* vertex, const WiiULatteVertexTransform* transform,
    f32 viewport_x, f32 viewport_y, f32 viewport_width,
    f32 viewport_height) {
    if (transform) {
        const float input[4] = {vertex->x, vertex->y, vertex->z, 1.0f};
        float clip[4];
        if (wiiu_latte_vertex_transform_apply(
                transform, g_vertex_uniform_registers,
                g_vertex_uniform_register_valid, GX2_UNIFORM_REGISTER_LIMIT,
                input, clip) &&
            fabsf(clip[3]) > 0.000001f) {
            f32 inverse_w = 1.0f / clip[3];
            f32 clip_x = clip[0] * inverse_w;
            f32 clip_y = clip[1] * inverse_w;
            if (isfinite(clip_x) && isfinite(clip_y) &&
                fabsf(clip_x) <= 16.0f && fabsf(clip_y) <= 16.0f) {
                vertex->x =
                    viewport_x + (clip_x + 1.0f) * viewport_width * 0.5f;
                vertex->y =
                    viewport_y + (1.0f - clip_y) * viewport_height * 0.5f;
                return;
            }
        }
    }

    if (fabsf(vertex->x) <= 2.0f && fabsf(vertex->y) <= 2.0f) {
        vertex->x = viewport_x + (vertex->x + 1.0f) * viewport_width * 0.5f;
        vertex->y = viewport_y + (1.0f - vertex->y) * viewport_height * 0.5f;
        return;
    }

    vertex->x = viewport_x + viewport_width * 0.5f + vertex->x;
    vertex->y = viewport_y + viewport_height * 0.5f - vertex->y;
}

static u8 clamp_color(f32 value) {
    if (value <= 0.0f)
        return 0u;
    if (value >= 255.0f)
        return 255u;
    return (u8)(value + 0.5f);
}

static f32 ui_edge(const GX2HostUiVertex* a, const GX2HostUiVertex* b,
                   f32 x, f32 y) {
    return (x - a->x) * (b->y - a->y) -
           (y - a->y) * (b->x - a->x);
}

static bool rasterize_ui_triangle(GX2HostColorSurface* target,
                                  const GX2DecodedTexture* texture,
                                  const GX2HostUiVertex* a,
                                  const GX2HostUiVertex* b,
                                  const GX2HostUiVertex* c) {
    f32 area = ui_edge(a, b, c->x, c->y);
    if (fabsf(area) < 0.0001f)
        return false;

    f32 min_x_f = a->x < b->x ? a->x : b->x;
    f32 max_x_f = a->x > b->x ? a->x : b->x;
    f32 min_y_f = a->y < b->y ? a->y : b->y;
    f32 max_y_f = a->y > b->y ? a->y : b->y;
    if (c->x < min_x_f)
        min_x_f = c->x;
    if (c->x > max_x_f)
        max_x_f = c->x;
    if (c->y < min_y_f)
        min_y_f = c->y;
    if (c->y > max_y_f)
        max_y_f = c->y;

    s32 left = (s32)floorf(min_x_f);
    s32 right = (s32)ceilf(max_x_f) - 1;
    s32 top = (s32)floorf(min_y_f);
    s32 bottom = (s32)ceilf(max_y_f) - 1;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= (s32)target->width)
        right = (s32)target->width - 1;
    if (bottom >= (s32)target->height)
        bottom = (s32)target->height - 1;
    if (g_scissor_valid) {
        s32 scissor_right = (s32)(g_scissor[0] + g_scissor[2]) - 1;
        s32 scissor_bottom = (s32)(g_scissor[1] + g_scissor[3]) - 1;
        if (left < (s32)g_scissor[0])
            left = (s32)g_scissor[0];
        if (top < (s32)g_scissor[1])
            top = (s32)g_scissor[1];
        if (right > scissor_right)
            right = scissor_right;
        if (bottom > scissor_bottom)
            bottom = scissor_bottom;
    }
    if (left > right || top > bottom)
        return false;

    bool wrote = false;
    for (s32 y = top; y <= bottom; y++) {
        for (s32 x = left; x <= right; x++) {
            f32 px = (f32)x + 0.5f;
            f32 py = (f32)y + 0.5f;
            f32 weight_a = ui_edge(b, c, px, py) / area;
            f32 weight_b = ui_edge(c, a, px, py) / area;
            f32 weight_c = 1.0f - weight_a - weight_b;
            if (weight_a < 0.0f || weight_b < 0.0f || weight_c < 0.0f)
                continue;

            f32 u = weight_a * a->u + weight_b * b->u + weight_c * c->u;
            f32 v = weight_a * a->v + weight_b * b->v + weight_c * c->v;
            if (u < 0.0f)
                u = 0.0f;
            if (u > 1.0f)
                u = 1.0f;
            if (v < 0.0f)
                v = 0.0f;
            if (v > 1.0f)
                v = 1.0f;
            u32 texture_x = (u32)(u * (texture->texture.width - 1u) + 0.5f);
            u32 texture_y = (u32)(v * (texture->texture.height - 1u) + 0.5f);
            const u8* sample = texture->pixels +
                               ((size_t)texture_y * texture->texture.width +
                                texture_x) *
                                   4u;
            u32 coverage = sample[3];
            u32 tint_alpha = clamp_color(weight_a * a->alpha +
                                         weight_b * b->alpha +
                                         weight_c * c->alpha);
            u32 alpha = (coverage * tint_alpha + 127u) / 255u;
            if (alpha == 0u)
                continue;

            u8 red = clamp_color(weight_a * a->red + weight_b * b->red +
                                 weight_c * c->red);
            u8 green = clamp_color(weight_a * a->green +
                                   weight_b * b->green +
                                   weight_c * c->green);
            u8 blue = clamp_color(weight_a * a->blue + weight_b * b->blue +
                                  weight_c * c->blue);
            u8* destination = target->pixels +
                              ((size_t)y * target->width + (u32)x) * 4u;
            u32 inverse = 255u - alpha;
            destination[0] =
                (u8)((blue * alpha + destination[0] * inverse + 127u) / 255u);
            destination[1] = (u8)((green * alpha + destination[1] * inverse +
                                   127u) /
                                  255u);
            destination[2] =
                (u8)((red * alpha + destination[2] * inverse + 127u) / 255u);
            destination[3] = 255u;
            wrote = true;
        }
    }
    return wrote;
}

static bool order_ui_axis_aligned_quad(const GX2HostUiVertex* vertices,
                                       GX2HostUiVertex* ordered) {
    const f32 epsilon = 0.01f;
    if (!vertices || !ordered)
        return false;

    f32 min_x = vertices[0].x;
    f32 max_x = vertices[0].x;
    f32 min_y = vertices[0].y;
    f32 max_y = vertices[0].y;
    for (u32 i = 1u; i < 4u; i++) {
        if (vertices[i].x < min_x)
            min_x = vertices[i].x;
        if (vertices[i].x > max_x)
            max_x = vertices[i].x;
        if (vertices[i].y < min_y)
            min_y = vertices[i].y;
        if (vertices[i].y > max_y)
            max_y = vertices[i].y;
    }
    if (max_x - min_x < epsilon || max_y - min_y < epsilon)
        return false;

    bool occupied[4] = {false, false, false, false};
    for (u32 i = 0u; i < 4u; i++) {
        bool left = fabsf(vertices[i].x - min_x) <= epsilon;
        bool right = fabsf(vertices[i].x - max_x) <= epsilon;
        bool top = fabsf(vertices[i].y - min_y) <= epsilon;
        bool bottom = fabsf(vertices[i].y - max_y) <= epsilon;
        if ((left == right) || (top == bottom))
            return false;

        /* TL, TR, BR, BL matches the fast rasterizer's edge layout. */
        u32 slot = top ? (left ? 0u : 1u) : (left ? 3u : 2u);
        if (occupied[slot])
            return false;
        ordered[slot] = vertices[i];
        occupied[slot] = true;
    }
    return occupied[0] && occupied[1] && occupied[2] && occupied[3];
}

/* Layout panes are axis-aligned quads. Avoid the general triangle path here. */
static bool rasterize_ui_axis_aligned_quad(
    GX2HostColorSurface* target, const GX2DecodedTexture* texture,
    const GX2HostUiVertex* a, const GX2HostUiVertex* b,
    const GX2HostUiVertex* c, const GX2HostUiVertex* d) {
    const f32 epsilon = 0.01f;
    if (fabsf(a->x - d->x) > epsilon || fabsf(b->x - c->x) > epsilon ||
        fabsf(a->y - b->y) > epsilon || fabsf(c->y - d->y) > epsilon ||
        fabsf(a->u - d->u) > epsilon || fabsf(b->u - c->u) > epsilon ||
        fabsf(a->v - b->v) > epsilon || fabsf(c->v - d->v) > epsilon ||
        a->red != b->red || a->red != c->red || a->red != d->red ||
        a->green != b->green || a->green != c->green ||
        a->green != d->green || a->blue != b->blue ||
        a->blue != c->blue || a->blue != d->blue || a->alpha != b->alpha ||
        a->alpha != c->alpha || a->alpha != d->alpha) {
        return false;
    }

    f32 x_span = b->x - a->x;
    f32 y_span = c->y - a->y;
    if (fabsf(x_span) < epsilon || fabsf(y_span) < epsilon)
        return false;

    f32 min_x = a->x < b->x ? a->x : b->x;
    f32 max_x = a->x > b->x ? a->x : b->x;
    f32 min_y = a->y < c->y ? a->y : c->y;
    f32 max_y = a->y > c->y ? a->y : c->y;
    s32 left = (s32)floorf(min_x);
    s32 right = (s32)ceilf(max_x) - 1;
    s32 top = (s32)floorf(min_y);
    s32 bottom = (s32)ceilf(max_y) - 1;
    if (left < 0)
        left = 0;
    if (top < 0)
        top = 0;
    if (right >= (s32)target->width)
        right = (s32)target->width - 1;
    if (bottom >= (s32)target->height)
        bottom = (s32)target->height - 1;
    if (g_scissor_valid) {
        s32 scissor_right = (s32)(g_scissor[0] + g_scissor[2]) - 1;
        s32 scissor_bottom = (s32)(g_scissor[1] + g_scissor[3]) - 1;
        if (left < (s32)g_scissor[0])
            left = (s32)g_scissor[0];
        if (top < (s32)g_scissor[1])
            top = (s32)g_scissor[1];
        if (right > scissor_right)
            right = scissor_right;
        if (bottom > scissor_bottom)
            bottom = scissor_bottom;
    }
    if (left > right || top > bottom)
        return false;

    const f32 u_step = (b->u - a->u) / x_span;
    const f32 v_step = (c->v - a->v) / y_span;
    const f32 u_start = a->u + ((f32)left + 0.5f - a->x) * u_step;
    f32 v = a->v + ((f32)top + 0.5f - a->y) * v_step;
    const u32 tint_alpha = a->alpha;

    for (s32 y = top; y <= bottom; y++, v += v_step) {
        f32 clamped_v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        u32 texture_y =
            (u32)(clamped_v * (texture->texture.height - 1u) + 0.5f);
        const u8* source_row =
            texture->pixels + (size_t)texture_y * texture->texture.width * 4u;
        u8* destination = target->pixels +
                          ((size_t)y * target->width + (u32)left) * 4u;
        f32 u = u_start;
        for (s32 x = left; x <= right; x++, u += u_step, destination += 4u) {
            f32 clamped_u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            u32 texture_x =
                (u32)(clamped_u * (texture->texture.width - 1u) + 0.5f);
            const u8* sample = source_row + (size_t)texture_x * 4u;
            u32 alpha = (sample[3] * tint_alpha + 127u) / 255u;
            if (alpha == 0u)
                continue;
            if (alpha == 255u) {
                destination[0] = a->blue;
                destination[1] = a->green;
                destination[2] = a->red;
            } else {
                u32 inverse = 255u - alpha;
                destination[0] =
                    (u8)((a->blue * alpha + destination[0] * inverse + 127u) /
                         255u);
                destination[1] =
                    (u8)((a->green * alpha + destination[1] * inverse + 127u) /
                         255u);
                destination[2] =
                    (u8)((a->red * alpha + destination[2] * inverse + 127u) /
                         255u);
            }
            destination[3] = 255u;
        }
    }
    return true;
}

static bool draw_indexed_ui_gpu_batch(
    CPUState* cpu, GX2HostColorSurface* target,
    const GX2HostAttribBuffer* vertices,
    const WiiULatteVertexTransform* vertex_transform, u32 index_address,
    u32 index_count, u32 index_type, s32 base_vertex,
    const GX2DecodedTexture* texture, f32 viewport_x,
    f32 viewport_y, f32 viewport_width, f32 viewport_height) {
    if (!cpu || !target || !target->gpu_composited || !vertices || !texture ||
        !texture->pixels || index_count < 4u) {
        return false;
    }
    u32 maximum_quads = index_count / 4u;
    WiiUWindowGpuMaterial material;
    bool font_material=read_known_pixel_material(cpu,&material)==2u;
    WiiUWindowGpuVertex* gpu_vertices = malloc(
        (size_t)maximum_quads * 4u * sizeof(*gpu_vertices));
    if (!gpu_vertices)
        return false;

    u32 quad_count = 0u;
    u32 axis_aligned_count = 0u;
    for (u32 i = 0u; i + 3u < index_count; i += 4u) {
        GX2HostUiVertex quad[4];
        bool valid = true;
        for (u32 vertex = 0u; vertex < 4u; vertex++) {
            u32 index;
            if (!read_draw_index(cpu, index_address, i + vertex, index_type,
                                  base_vertex, &index)) { valid = false; break; }
            if (!read_ui_vertex_from_fetch(cpu, index, vertex_transform,
                                           &quad[vertex]) &&
                !read_ui_vertex(cpu, vertices, index, &quad[vertex])) {
                valid = false;
                break;
            }
            map_ui_vertex_to_surface(&quad[vertex], vertex_transform,
                                     viewport_x, viewport_y, viewport_width,
                                     viewport_height);
            if (font_material) {
                /* The font VS uses the sign of texcoord.z to select the
                   alternate material and negate alpha for its shadow pass. */
                u32 at=vertices->data+index*vertices->stride;
                float layer=guest_range_valid(cpu,at+24u,4u)?
                    f32_from_bits(mem_read32(cpu,at+24u)):0.0f;
                u32 base=layer<0.0f?8u:0u;
                u8* channels[4]={&quad[vertex].red,&quad[vertex].green,
                    &quad[vertex].blue,&quad[vertex].alpha};
                for(u32 c=0;c<4u;++c) {
                    float factor=f32_from_bits(g_pixel_uniform_registers[base+4u+c]);
                    if(c<3u) factor+=f32_from_bits(g_pixel_uniform_registers[base+c]);
                    *channels[c]=clamp_color(*channels[c]*factor);
                }
            }
        }
        if (!valid)
            continue;

        GX2HostUiVertex ordered[4];
        bool axis_aligned = order_ui_axis_aligned_quad(quad, ordered);
        const GX2HostUiVertex* draw_vertices =
            axis_aligned ? ordered : quad;
        for (u32 vertex = 0u; vertex < 4u; vertex++) {
            WiiUWindowGpuVertex* output =
                &gpu_vertices[quad_count * 4u + vertex];
            output->x = draw_vertices[vertex].x;
            output->y = draw_vertices[vertex].y;
            output->u = draw_vertices[vertex].u;
            output->v = draw_vertices[vertex].v;
            output->red = draw_vertices[vertex].red;
            output->green = draw_vertices[vertex].green;
            output->blue = draw_vertices[vertex].blue;
            output->alpha = draw_vertices[vertex].alpha;
        }
        if (axis_aligned)
            axis_aligned_count++;
        quad_count++;
    }

    bool drawn = quad_count != 0u && (font_material ? wiiu_window_gpu_draw_material(
        target->image,target->width,target->height,texture->pixels,
        texture->texture.width,texture->texture.height,texture->serial,
        gpu_vertices,quad_count,&material,&g_blend_controls[0],g_scissor_valid,
        g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]) : wiiu_window_gpu_draw_bgra_quads(
        target->image, target->width, target->height, texture->pixels,
        texture->texture.width, texture->texture.height, texture->serial,
        gpu_vertices, quad_count, &g_blend_controls[0], g_scissor_valid,
        g_scissor[0], g_scissor[1], g_scissor[2], g_scissor[3]));
    free(gpu_vertices);
    if (!drawn)
        return false;
    g_fast_ui_quad_count += axis_aligned_count;
    g_fallback_ui_quad_count += quad_count - axis_aligned_count;
    g_gpu_ui_quad_count += quad_count;
    target->cpu_pixels_stale = true;
    if (g_software_ui_draw_log_count++ < 32u) {
        fprintf(stderr,
                "gx2: batched native UI draw target=0x%08X texture=0x%08X "
                "quads=%u viewport=%.0fx%.0f\n",
                target->image, texture->texture.image, quad_count,
                viewport_width, viewport_height);
    }
    return true;
}

static bool software_draw_indexed_ui(CPUState* cpu, u32 primitive,
                                     u32 index_count, u32 index_type,
                                     u32 index_data, s32 index_offset) {
    if (!cpu || primitive != 19u || !gx2_index_size(index_type) ||
        index_count < 4u || index_count > 8192u)
        return false;

    GX2HostColorSurface* target =
        find_color_surface(g_current_color_buffers[0], false);
    GX2HostAttribBuffer* vertices = &g_attrib_buffers[0];
    GX2HostTexture* bound_texture = &g_pixel_textures[0];
    if (!target || !vertices->valid || vertices->stride != 28u ||
        !bound_texture->valid) {
        return false;
    }
    /* BOTW uses both BC4 and R8 glyph atlases with this vertex layout.
       Rejecting R8 sent the entire font atlas through the fullscreen blit. */
    u32 atlas_format = bound_texture->format & 0x3Fu;
    if (atlas_format != 0x34u && atlas_format != 0x01u)
        return false;

    if (!g_ui_uniform_state_dumped) {
        fprintf(stderr, "gx2: UI vertex uniform state frame=%llu\n",
                (unsigned long long)g_frame_count);
        dump_guide_uniform_registers("ui", g_vertex_uniform_registers,
                                     g_vertex_uniform_register_valid);
        dump_guide_shader(cpu, "ui_vs", g_current_vertex_shader, 0x134u,
                          0xD0u, 0xD4u);
        dump_guide_shader(cpu, "ui_ps", g_current_pixel_shader, 0xECu,
                          0xA4u, 0xA8u);
        g_ui_uniform_state_dumped = true;
    }

    u64 index_size = (u64)index_count * gx2_index_size(index_type);
    if (index_size > 0xFFFFFFFFu ||
        !guest_range_valid(cpu, index_data, (u32)index_size)) {
        return false;
    }
    GX2DecodedTexture* texture = decode_texture_cached(cpu, bound_texture);
    if (!texture || !texture->pixels || texture->texture.width == 0u ||
        texture->texture.height == 0u) {
        return false;
    }

    f32 viewport_x = 0.0f;
    f32 viewport_y = 0.0f;
    f32 viewport_width = (f32)target->width;
    f32 viewport_height = (f32)target->height;
    if (g_viewport_valid && g_viewport[2] > 0.0 && g_viewport[3] > 0.0) {
        viewport_x = (f32)g_viewport[0];
        viewport_y = (f32)g_viewport[1];
        viewport_width = (f32)g_viewport[2];
        viewport_height = (f32)g_viewport[3];
    }

    const WiiULatteVertexTransform* vertex_transform = NULL;
    if (wiiu_latte_vertex_transform_decode(cpu, g_current_vertex_shader,
                                            &g_ui_vertex_transform)) {
        vertex_transform = &g_ui_vertex_transform;
        if (!g_ui_latte_transform_logged) {
            fprintf(stderr,
                    "gx2: native Latte UI transform vs=0x%08X gpr=%u "
                    "uniforms=%u,%u,%u,%u\n",
                    g_current_vertex_shader,
                    (unsigned)g_ui_vertex_transform.input_gpr,
                    (unsigned)g_ui_vertex_transform.uniform_vectors[0],
                    (unsigned)g_ui_vertex_transform.uniform_vectors[1],
                    (unsigned)g_ui_vertex_transform.uniform_vectors[2],
                    (unsigned)g_ui_vertex_transform.uniform_vectors[3]);
            g_ui_latte_transform_logged = true;
        }
    }

    u32 index_address = index_data;
    if (target->gpu_composited) {
        if (draw_indexed_ui_gpu_batch(
                cpu, target, vertices, vertex_transform, index_address,
                index_count, index_type, index_offset, texture,
                viewport_x, viewport_y, viewport_width,
                viewport_height)) {
            target->drawn = true;
            target->draw_serial = ++g_draw_serial;
            return true;
        }
        if (!synchronize_gpu_surface_for_software(target))
            return false;
        target->gpu_composited = false;
    }

    bool wrote = false;
    for (u32 i = 0; i + 3u < index_count; i += 4u) {
        GX2HostUiVertex quad[4];
        bool valid = true;
        for (u32 vertex = 0; vertex < 4u; vertex++) {
            u32 index;
            if (!read_draw_index(cpu, index_address, i + vertex, index_type,
                                  index_offset, &index)) { valid = false; break; }
            if (!read_ui_vertex_from_fetch(cpu, index, vertex_transform,
                                           &quad[vertex]) &&
                !read_ui_vertex(cpu, vertices, index, &quad[vertex])) {
                valid = false;
                break;
            }
            map_ui_vertex_to_surface(&quad[vertex], vertex_transform,
                                     viewport_x, viewport_y, viewport_width,
                                     viewport_height);
        }
        if (!valid)
            continue;
        GX2HostUiVertex ordered[4];
        bool axis_aligned = order_ui_axis_aligned_quad(quad, ordered);
        const GX2HostUiVertex* draw_vertices =
            axis_aligned ? ordered : quad;
        bool gpu_drawn = false;
        if (target->gpu_composited) {
            WiiUWindowGpuVertex gpu_vertices[4];
            for (u32 vertex = 0u; vertex < 4u; vertex++) {
                gpu_vertices[vertex].x = draw_vertices[vertex].x;
                gpu_vertices[vertex].y = draw_vertices[vertex].y;
                gpu_vertices[vertex].u = draw_vertices[vertex].u;
                gpu_vertices[vertex].v = draw_vertices[vertex].v;
                gpu_vertices[vertex].red = draw_vertices[vertex].red;
                gpu_vertices[vertex].green = draw_vertices[vertex].green;
                gpu_vertices[vertex].blue = draw_vertices[vertex].blue;
                gpu_vertices[vertex].alpha = draw_vertices[vertex].alpha;
            }
            gpu_drawn = wiiu_window_gpu_draw_bgra(
                target->image, target->width, target->height,
                texture->pixels, texture->texture.width, texture->texture.height,
                texture->serial, gpu_vertices, &g_blend_controls[0],
                g_scissor_valid, g_scissor[0],
                g_scissor[1], g_scissor[2], g_scissor[3]);
            if (!gpu_drawn && !synchronize_gpu_surface_for_software(target))
                return false;
        }
        if (gpu_drawn) {
            if (axis_aligned)
                g_fast_ui_quad_count++;
            else
                g_fallback_ui_quad_count++;
            g_gpu_ui_quad_count++;
            target->cpu_pixels_stale = true;
            wrote = true;
            continue;
        }
        if (!ensure_color_surface_pixels(target))
            return false;
        bool fast = axis_aligned &&
                    rasterize_ui_axis_aligned_quad(target, texture,
                                                    &ordered[0], &ordered[1],
                                                    &ordered[2], &ordered[3]);
        if (!fast) {
            fast = rasterize_ui_axis_aligned_quad(target, texture, &quad[0],
                                                   &quad[1], &quad[2],
                                                   &quad[3]);
        }
        if (fast) {
            g_fast_ui_quad_count++;
            wrote = true;
        } else {
            g_fallback_ui_quad_count++;
            wrote |= rasterize_ui_triangle(target, texture, &quad[0],
                                           &quad[1], &quad[2]);
            wrote |= rasterize_ui_triangle(target, texture, &quad[0],
                                           &quad[2], &quad[3]);
        }
    }
    if (!wrote)
        return false;

    target->drawn = true;
    target->draw_serial = ++g_draw_serial;
    if (!target->gpu_composited)
        upload_software_surface_to_gpu(target);
    if (g_software_ui_draw_log_count++ < 32u) {
        fprintf(stderr,
                "gx2: Cemu-layout UI draw target=0x%08X texture=0x%08X "
                "indices=%u viewport=%.0fx%.0f\n",
                target->image, bound_texture->image, index_count,
                viewport_width, viewport_height);
    }
    return true;
}

/* Capture each draw-state variant once, rather than only the first title
   draws. Later menus and geometry otherwise disappear behind exhausted log
   limits. This is disabled for ordinary launches. */
static void capture_draw_variant(CPUState* cpu, u32 primitive, u32 count,
                                  u32 index_data, u32 index_type) {
    const char* directory = getenv("BOTW_CAPTURE_DIR");
    if (!directory || !*directory) return;
    typedef struct { u32 vs, ps, rt, stride, primitive, count, checkpoint; } Variant;
    static Variant seen[256];
    static u32 seen_count;
    Variant key = {g_current_vertex_shader, g_current_pixel_shader,
        g_current_color_buffers[0], g_attrib_buffers[0].stride, primitive, count,
        g_frame_count >= 600u ? 3u : g_frame_count >= 240u ? 2u :
        g_frame_count >= 185u ? 1u : 0u};
    for (u32 i = 0; i < seen_count; ++i) {
        Variant previous = seen[i];
        /* Particle counts change every frame. After bootstrap capture shader
           pairs, not every count, leaving room for actual scene programs. */
        if (key.checkpoint == 3u) previous.count = key.count;
        if (memcmp(&previous, &key, sizeof(key)) == 0) return;
    }
    if (seen_count == sizeof(seen)/sizeof(seen[0])) return;
    u32 id = seen_count; seen[seen_count++] = key;
    char label[48], path[80];
    fprintf(stderr, "gx2: variant=%u frame=%llu primitive=%u count=%u rt=%08X "
            "vs=%08X ps=%08X stride=%u texture=%08X fmt=%X comp=%X\n",
            id, (unsigned long long)g_frame_count, primitive, count, key.rt,
            key.vs, key.ps, key.stride, g_pixel_textures[0].image,
            g_pixel_textures[0].format, g_pixel_textures[0].comp_map);
    fprintf(stderr,"gx2: variant-state=%u blend=%u/%u color=%u mask=%u/%X viewport=%.0f,%.0f %.0fx%.0f scissor=%u,%u %ux%u\n",
        id,g_blend_controls[0].color_control_valid,g_blend_controls[0].blend_enabled,
        g_blend_controls[0].color_enabled,g_blend_controls[0].write_mask_valid,
        g_blend_controls[0].write_mask,g_viewport[0],g_viewport[1],g_viewport[2],g_viewport[3],
        g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]);
    snprintf(label, sizeof(label), "variant_%u_vs", id);
    dump_guide_shader(cpu, label, key.vs, 0x134u, 0xD0u, 0xD4u);
    dump_guide_uniform_registers(label, g_vertex_uniform_registers,
                                  g_vertex_uniform_register_valid);
    snprintf(label, sizeof(label), "variant_%u_ps", id);
    dump_guide_shader(cpu, label, key.ps, 0xECu, 0xA4u, 0xA8u);
    dump_guide_uniform_registers(label, g_pixel_uniform_registers,
                                  g_pixel_uniform_register_valid);
    snprintf(label, sizeof(label), "variant_%u_fetch", id);
    dump_guide_shader(cpu, label, g_current_fetch_shader, 0x20u, 8u, 12u);
    snprintf(path, sizeof(path), "sm3dw_variant_%u_vertices.bin", id);
    if (g_attrib_buffers[0].valid)
        dump_guest_buffer(cpu, g_attrib_buffers[0].data,
            g_attrib_buffers[0].size < 65536u ? g_attrib_buffers[0].size : 65536u, path);
    /* Effect/scene shaders use multiple vertex streams and uniform blocks,
       not the title layout's register constants. Preserve their actual draw
       inputs so later black frames can be reproduced offline. */
    for (u32 slot = 1; slot < GX2_ATTRIB_BUFFER_LIMIT; ++slot) {
        const GX2HostAttribBuffer* buffer = &g_attrib_buffers[slot];
        if (!buffer->valid || !buffer->size) continue;
        fprintf(stderr, "gx2: variant=%u attrib=%u data=%08X size=%u stride=%u\n",
                id, slot, buffer->data, buffer->size, buffer->stride);
        snprintf(path, sizeof(path), "sm3dw_variant_%u_attrib_%u.bin", id, slot);
        dump_guest_buffer(cpu, buffer->data, buffer->size < 65536u ? buffer->size : 65536u, path);
    }
    for(u32 slot=0;slot<3;++slot)if(g_pixel_samplers[slot].valid)
        fprintf(stderr,"gx2: variant=%u sampler=%u words=%08X,%08X,%08X\n",id,slot,
            g_pixel_samplers[slot].words[0],g_pixel_samplers[slot].words[1],g_pixel_samplers[slot].words[2]);
    for(u32 slot=0;slot<GX2_TEXTURE_UNIT_LIMIT;++slot) {
        const GX2HostTexture* t=&g_vertex_textures[slot];
        if(t->valid)fprintf(stderr,"gx2: variant=%u vertex-texture=%u image=%08X size=%u %ux%u format=%X tile=%u mip=%u dimension=%u\n",
            id,slot,t->image,t->image_size,t->width,t->height,t->format,t->tile_mode,t->first_mip,t->dimension);
        if(g_vertex_samplers[slot].valid)fprintf(stderr,"gx2: variant=%u vertex-sampler=%u words=%08X,%08X,%08X\n",id,slot,
            g_vertex_samplers[slot].words[0],g_vertex_samplers[slot].words[1],g_vertex_samplers[slot].words[2]);
    }
    for (u32 stage = 0; stage < 2u; ++stage) {
        const GX2HostUniformBlock* blocks = stage ? g_pixel_uniform_blocks : g_vertex_uniform_blocks;
        for (u32 slot = 0; slot < GX2_UNIFORM_BLOCK_LIMIT; ++slot) {
            const GX2HostUniformBlock* block = &blocks[slot];
            if (!block->valid || !block->size) continue;
            fprintf(stderr, "gx2: variant=%u %s-block=%u data=%08X size=%u\n",
                    id, stage ? "ps" : "vs", slot, block->data, block->size);
            snprintf(path, sizeof(path), "sm3dw_variant_%u_%s_block_%u.bin",
                    id, stage ? "ps" : "vs", slot);
            dump_guest_buffer(cpu, block->data, block->size < 65536u ? block->size : 65536u, path);
        }
    }
    if (index_data && count <= 16384u && gx2_index_size(index_type)) {
        snprintf(path, sizeof(path), "sm3dw_variant_%u_indices.bin", id);
        dump_guest_buffer(cpu, index_data, count*gx2_index_size(index_type), path);
    }
}

/* This pass has no texture instruction. Check its complete executable clauses
   before using the constant-color fast path; an arbitrary triangle must never
   be expanded into a fullscreen blit. Shader words are little-endian. */
static bool match_shader_words(CPUState* cpu, u32 program, u32 offset,
                               const u32* words, u32 count) {
    if ((u64)program + offset + (u64)count * 4u > UINT32_MAX ||
        !guest_range_valid(cpu, program + offset, count * 4u)) return false;
    for (u32 i = 0; i < count; ++i) {
        u32 at = program + offset + i*4u;
        u32 word = (u32)mem_read8(cpu, at) | ((u32)mem_read8(cpu, at+1u) << 8u) |
                   ((u32)mem_read8(cpu, at+2u) << 16u) | ((u32)mem_read8(cpu, at+3u) << 24u);
        if (word != words[i]) return false;
    }
    return true;
}

static bool draw_constant_fullscreen_triangle(CPUState* cpu, u32 primitive,
    u32 count, u32 first_vertex, u32 index_type, u32 index_data, s32 index_offset) {
    if (primitive != 4u || count != 3u || !g_attrib_buffers[0].valid ||
        g_attrib_buffers[0].stride != 32u ||
        !guest_range_valid(cpu, g_current_vertex_shader, 0xD8u) ||
        !guest_range_valid(cpu, g_current_pixel_shader, 0xACu)) return false;
    u32 vs = mem_read32(cpu, g_current_vertex_shader+0xD4u);
    u32 ps = mem_read32(cpu, g_current_pixel_shader+0xA8u);
    static const u32 vs_cf[] = {0,0x09800000,0x20,0xA0100000,
        0xA03C,0x94000688,0x4000,0x94200FFF};
    static const u32 vs_alu[] = {0x001F0001,0x00200CB0,0x001F0401,0x20200CB0,
        0x001F0100,0x40200C90,0x801F00FD,0x60200C90,0x3F800000,0};
    static const u32 ps_cf[] = {0x20,0xA00C0000,0,0x94200688};
    static const u32 ps_alu[] = {0x001F0100,0x00000C90,0x001F0500,0x20000C90,
        0x001F0900,0x40000C90,0x801F0D00,0x60000C90};
    if (mem_read32(cpu,g_current_vertex_shader+0xD0u) < 296u ||
        mem_read32(cpu,g_current_pixel_shader+0xA4u) < 288u ||
        !match_shader_words(cpu,vs,0,vs_cf,8) ||
        !match_shader_words(cpu,vs,256,vs_alu,10) ||
        !match_shader_words(cpu,ps,0,ps_cf,4) ||
        !match_shader_words(cpu,ps,256,ps_alu,8)) return false;
    float xy[3][2];
    for (u32 i=0; i<3u; ++i) {
        u32 index = first_vertex+i;
        if (index_data && !read_draw_index(cpu,index_data,i,index_type,index_offset,&index))
            return false;
        u64 offset=(u64)index*32u;
        GX2HostAttribBuffer* buffer=&g_attrib_buffers[0];
        if (offset+8u > buffer->size || (u64)buffer->data+offset+8u > UINT32_MAX ||
            !guest_range_valid(cpu,buffer->data+(u32)offset,8u)) return false;
        for(u32 c=0;c<2u;++c) {
            xy[i][c]=2.0f*f32_from_bits(mem_read32(cpu,buffer->data+(u32)offset+c*4u));
            if (!isfinite(xy[i][c])) return false;
        }
    }
    /* Prove that the triangle covers all four clip-space corners. */
    float area=(xy[1][0]-xy[0][0])*(xy[2][1]-xy[0][1])-
               (xy[1][1]-xy[0][1])*(xy[2][0]-xy[0][0]);
    if (!isfinite(area) || fabsf(area)<0.000001f) return false;
    for(u32 corner=0;corner<4u;++corner) {
        float x=(corner&1u)?1.0f:-1.0f, y=(corner&2u)?1.0f:-1.0f;
        for(u32 edge=0;edge<3u;++edge) {
            u32 next=(edge+1u)%3u;
            float side=(xy[next][0]-xy[edge][0])*(y-xy[edge][1])-
                       (xy[next][1]-xy[edge][1])*(x-xy[edge][0]);
            if (side/area < -0.000001f) return false;
        }
    }
    u8 color[4];
    for(u32 c=0;c<4u;++c) {
        float v=f32_from_bits(g_pixel_uniform_registers[c]);
        if (!g_pixel_uniform_register_valid[c] || !isfinite(v)) return false;
        color[c]=color_to_u8(v);
    }
    GX2HostColorSurface* target=find_color_surface(g_current_color_buffers[0],false);
    if (!target || !target->width || !target->height) return false;
    if (!target->gpu_composited) {
        if (!ensure_color_surface_pixels(target)) return false;
        upload_software_surface_to_gpu(target);
    }
    float x=0,y=0,w=(float)target->width,h=(float)target->height;
    if(g_viewport_valid) {
        x=(float)g_viewport[0]; y=(float)g_viewport[1];
        w=(float)g_viewport[2]; h=(float)g_viewport[3];
    }
    if(!isfinite(x)||!isfinite(y)||!isfinite(w)||!isfinite(h)||w<=0||h<=0) return false;
    WiiUWindowGpuVertex quad[4];
    for(u32 i=0;i<4u;++i) {
        quad[i]=(WiiUWindowGpuVertex){x+((i==1u||i==2u)?w:0),y+(i>=2u?h:0),
            0,0,color[0],color[1],color[2],color[3]};
    }
    static const u8 white[4]={255,255,255,255};
    if(!wiiu_window_gpu_draw_bgra_color(target->image,target->width,target->height,
        white,1,1,1,quad,&g_blend_controls[0],g_scissor_valid,
        g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3])) return false;
    target->cpu_pixels_stale=true;
    target->drawn=true;
    target->draw_serial=++g_draw_serial;
    return true;
}

static struct {
    u32 hash,size,inputs;
    u64 used;
    u8* bytes;
    WiiULattePixelProgram* program;
} g_scene_programs[256];
static u64 g_scene_program_serial;

static bool scene_rejected(const char* stage,u32 slot) {
    /* One bounded diagnostic per shader/stage, only for capture runs. */
    static struct {u32 vs,ps,slot;const char* stage;} seen[256];
    static u32 used;
    const char* capture=getenv("BOTW_CAPTURE_DIR");
    if(!capture || !*capture || used==256)return false;
    for(u32 i=0;i<used;++i)if(seen[i].vs==g_current_vertex_shader &&
        seen[i].ps==g_current_pixel_shader && seen[i].slot==slot && !strcmp(seen[i].stage,stage))return false;
    seen[used].vs=g_current_vertex_shader;seen[used].ps=g_current_pixel_shader;
    seen[used].slot=slot;seen[used++].stage=stage;
    fprintf(stderr,"gx2: scene rejected stage=%s slot=%u vs=%08X ps=%08X frame=%llu\n",
        stage,slot,g_current_vertex_shader,g_current_pixel_shader,(unsigned long long)g_frame_count);
    return false;
}

static const WiiULattePixelProgram* scene_program(const u8* bytes,u32 size,u32 inputs,u32 hash) {
    u32 slot=256;
    for(u32 i=0;i<256;++i) {
        if(g_scene_programs[i].bytes && g_scene_programs[i].hash==hash &&
           g_scene_programs[i].size==size && g_scene_programs[i].inputs==inputs &&
           !memcmp(g_scene_programs[i].bytes,bytes,size)) {
            g_scene_programs[i].used=++g_scene_program_serial;return g_scene_programs[i].program;
        }
        if(!g_scene_programs[i].bytes && slot==256)slot=i;
    }
    if(slot==256) {
        slot=0;for(u32 i=1;i<256;++i)if(g_scene_programs[i].used<g_scene_programs[slot].used)slot=i;
        free(g_scene_programs[slot].bytes);free(g_scene_programs[slot].program);
        memset(&g_scene_programs[slot],0,sizeof(g_scene_programs[slot]));
    }
    u8* copy=malloc(size);WiiULattePixelProgram* program=malloc(sizeof(*program));
    if(!copy || !program){free(copy);free(program);return NULL;}
    memcpy(copy,bytes,size);
    if(!wiiu_latte_translate_pixel(bytes,size,inputs,program)){free(program);program=NULL;}
    else fprintf(stderr,"gx2: translated scene PS hash=%08X bytes=%u inputs=%u textures=%04X targets=%02X\n",
        hash,size,inputs,program->texture_mask,program->target_mask);
    g_scene_programs[slot].bytes=copy;g_scene_programs[slot].program=program;
    g_scene_programs[slot].hash=hash;g_scene_programs[slot].size=size;g_scene_programs[slot].inputs=inputs;
    g_scene_programs[slot].used=++g_scene_program_serial;
    return program;
}

static bool vertex_texture_info(void* user,unsigned texture,u32 lod,u32 result[4]) {
    CPUState* cpu=user;
    if(!cpu || texture>=GX2_TEXTURE_UNIT_LIMIT)return false;
    const GX2HostTexture* t=&g_vertex_textures[texture];
    /* The implemented sampling path exposes a single base-level 2D view. */
    if(!t->valid || t->dimension!=1 || t->aa || t->first_mip || lod)return false;
    result[0]=t->width;result[1]=t->height;result[2]=1;result[3]=1;
    return true;
}

bool wiiu_gx2_sample_vertex_texture(void* user,unsigned texture,unsigned sampler,
    float u,float v,float lod,float result[4]) {
    CPUState* cpu=user;
    if(!cpu || texture>=GX2_TEXTURE_UNIT_LIMIT || sampler>=GX2_TEXTURE_UNIT_LIMIT || !isfinite(lod))return false;
    const GX2HostTexture* t=&g_vertex_textures[texture];
    const GX2HostSampler* s=&g_vertex_samplers[sampler];
    if(!t->valid || !s->valid || t->dimension!=1 || t->aa || t->first_mip)return false;
    float minimum=(s->words[1]&1023)/64.0f,maximum=((s->words[1]>>10)&1023)/64.0f;
    if(minimum>maximum)return false;
    lod=fminf(maximum,fmaxf(minimum,lod));
    if(lod!=0)return false; /* Other mip levels require their own layout/bytes. */
    WiiUGX2TextureView view={t->image,t->image_size,t->width,t->height,t->depth,
        t->format,t->tile_mode,t->swizzle,t->pitch,t->comp_map,t->first_slice};
    GX2HostColorSurface* source=find_color_surface_by_image(t->image);
    if(source) {
        bool hdr=t->format==0x806 || t->format==0x810 || t->format==0x820 ||
            t->format==0x80e || t->format==0x81e || t->format==0x823 || t->format==0x816;
        if(hdr) {
            if(source->format!=t->format || source->width!=t->width || source->height!=t->height ||
               !source->gpu_composited || !t->width || !t->height || t->width>8192 || t->height>8192)return false;
            bool refresh=!source->vertex_float_pixels || source->vertex_float_clear_serial!=source->clear_serial ||
                source->vertex_float_draw_serial!=source->draw_serial;
            if(!source->vertex_float_pixels)source->vertex_float_pixels=malloc((size_t)t->width*t->height*16);
            if(!source->vertex_float_pixels)return false;
            if(refresh) {
                if(!wiiu_window_gpu_readback_float(source->image,source->vertex_float_pixels,t->width,t->height)) {
                    free(source->vertex_float_pixels);source->vertex_float_pixels=NULL;return false;
                }
                source->vertex_float_clear_serial=source->clear_serial;
                source->vertex_float_draw_serial=source->draw_serial;
            }
            return wiiu_gx2_software_sample_rgba_float(source->vertex_float_pixels,&view,s->words[0],u,v,result);
        }
        if((t->format!=1 && t->format!=7 && t->format!=0x1a) ||
           source->width!=t->width || source->height!=t->height)return false;
        /* Read once per surface update, not once per vertex. Keep GPU ownership:
           vertex sampling is read-only and must not evict the render target. */
        if(source->cpu_pixels_stale) {
            if(!ensure_color_surface_pixels(source) ||
               !wiiu_window_gpu_readback(source->image,source->pixels,
                   source->width,source->height,source->width*4))return false;
            source->cpu_pixels_stale=false;
        }
        return wiiu_gx2_software_sample_bgra(source->pixels,&view,s->words[0],u,v,result);
    }
    return wiiu_gx2_software_sample_float(cpu,&view,s->words[0],u,v,result);
}

static bool draw_scene_material(CPUState* cpu,const WiiULattePixelProgram* program,
    GX2HostColorSurface* target,const WiiUWindowPostVertex* vertices,u32 count) {
    WiiUWindowSceneMaterial material={0};material.program=program;
    for(u32 i=0;i<16;++i)if(program->block_mask&(1u<<i)) {
        const GX2HostUniformBlock* block=&g_pixel_uniform_blocks[i];
        if(!block->valid || !block->size || block->size%16 || block->size>1048576)return false;
        material.uniform_blocks[i]=guest_bytes(cpu,block->data,block->size);
        material.uniform_block_sizes[i]=block->size;
        if(!material.uniform_blocks[i])return false;
    }
    GX2HostColorSurface* targets[8]={0};
    for(u32 i=0;i<8;++i)if(program->target_mask&(1u<<i)) {
        targets[i]=find_color_surface(g_current_color_buffers[i],false);
        if(!targets[i] || targets[i]->width!=target->width || targets[i]->height!=target->height)return scene_rejected("MRT-attachment",i);
        for(u32 j=0;j<i;++j)if(targets[i]==targets[j])return false;
        material.targets[i]=targets[i]->image;material.blends[i]=g_blend_controls[i];
        /* D3D has one blend constant shared by all attachments. */
        if(g_blend_controls[i].constant_valid && (!g_blend_controls[0].constant_valid ||
            memcmp(g_blend_controls[i].constant,g_blend_controls[0].constant,16)))return false;
    }
    for(u32 i=0;i<program->constant_count;++i) {
        u32 bank=program->constants[i].bank,vector=program->constants[i].vector;
        if(bank==16) {
            if(vector>=GX2_UNIFORM_REGISTER_LIMIT/4)return false;
            for(u32 c=0;c<4;++c) {
                if(!g_pixel_uniform_register_valid[vector*4+c])return false;
                material.constants[16+i][c]=g_pixel_uniform_registers[vector*4+c];
            }
        } else {
            GX2HostUniformBlock* block=&g_pixel_uniform_blocks[bank];
            if(!block->valid || (u64)vector*16+16>block->size)return false;
            const u8* data=guest_bytes(cpu,block->data,block->size);
            if(!data)return false;
            for(u32 c=0;c<4;++c) {
                const u8* p=data+vector*16+c*4;
                material.constants[16+i][c]=p[0]|(u32)p[1]<<8|(u32)p[2]<<16|(u32)p[3]<<24;
            }
        }
    }
    u32 decoded_count=0;
    for(u32 i=0;i<16;++i)if((program->texture_mask&(1u<<i)) &&
        !find_color_surface_by_image(g_pixel_textures[i].image))++decoded_count;
    /* Decoded cache pointers must remain alive until the GPU upload. */
    if(decoded_count>GX2_DECODED_TEXTURE_LIMIT)return false;
    for(u32 i=0;i<16;++i) {
        material.textures[i].sampler_valid=g_pixel_samplers[i].valid;
        material.textures[i].sampler_word0=g_pixel_samplers[i].words[0];
        if(!(program->texture_mask&(1u<<i)))continue;
        GX2HostTexture* texture=&g_pixel_textures[i];
        if(!texture->valid || texture->depth!=1 || texture->first_slice || texture->first_mip ||
           texture->dimension!=1 || texture->aa)return scene_rejected("pixel-texture-view",i);
        GX2HostColorSurface* source=find_color_surface_by_image(texture->image);
        u32 map=0x00010203;
        if(source) {
            for(u32 j=0;j<8;++j)if(source==targets[j])return false;
            if(!source->gpu_composited)upload_software_surface_to_gpu(source);
            material.source_surfaces[i]=source->image;map=texture->comp_map;
        } else {
            GX2DecodedTexture* decoded=decode_texture_cached(cpu,texture);
            if(!decoded || !decoded->pixels)return scene_rejected("pixel-texture-format",i);
            material.textures[i].pixels=decoded->pixels;material.textures[i].serial=decoded->serial;
            material.textures[i].width=texture->width;material.textures[i].height=texture->height;
        }
        for(u32 c=0;c<4;++c) {
            u32 component=(map>>((3-c)*8))&255;
            if(component>5)return false;
            material.constants[i][c]=component;
        }
    }
    float viewport[4]={0,0,(float)target->width,(float)target->height};
    if(g_viewport_valid)for(u32 c=0;c<4;++c)viewport[c]=(float)g_viewport[c];
    for(u32 c=0;c<4;++c)if(!isfinite(viewport[c]))return false;
    if(viewport[2]<=0 || viewport[3]<=0)return false;
    for(u32 i=0;i<8;++i)if(targets[i] && !targets[i]->gpu_composited) {
        if(!ensure_color_surface_pixels(targets[i]))return false;
        upload_software_surface_to_gpu(targets[i]);
    }
    bool drawn=wiiu_window_gpu_draw_scene(target->image,target->width,target->height,&material,
        vertices,count,viewport,&g_blend_controls[0],g_scissor_valid,
        g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]);
    if(drawn)for(u32 i=0;i<8;++i)if(targets[i]) {
        targets[i]->cpu_pixels_stale=true;targets[i]->drawn=true;targets[i]->draw_serial=++g_draw_serial;
    }
    return drawn;
}

/* -1 = another shader path; 0 = recognized pass failed; 1 = drawn. A failed
   recognized pass must not fall through to the old whole-texture blitter. */
static int draw_postprocess(CPUState* cpu,u32 primitive,u32 count,u32 first_vertex,
    u32 index_type,u32 index_data,s32 index_offset,u32 instances,u32 base_instance) {
    if((primitive!=4 && primitive!=19) || !count || count>6144 ||
       !guest_range_valid(cpu,g_current_pixel_shader,0xAC) ||
       !guest_range_valid(cpu,g_current_vertex_shader,0xD8)) return -1;
    u32 ps_size=mem_read32(cpu,g_current_pixel_shader+0xA4);
    if(!ps_size || ps_size>16384 || ps_size%8)return -1;
    const u8* ps=guest_bytes(cpu,mem_read32(cpu,g_current_pixel_shader+0xA8),ps_size);
    if(!ps) return -1;
    u32 hash=2166136261u;
    for(u32 i=0;i<ps_size;++i) hash=(hash^ps[i])*16777619u;
    /* Full captured program signatures: copy, four-tap reduction, and the
       two separable three-tap blur passes. Other PS programs fail closed. */
    u32 mode;
    const WiiULattePixelProgram* translated=NULL;
    if(ps_size==144 && hash==0xC9FFF2B6) mode=0;
    else if(ps_size==576 && hash==0xF202D5FB) mode=1;
    else if(ps_size==432 && hash==0x61B3DF28) mode=2;
    else if(ps_size==432 && hash==0x0B9201E0) mode=3;
    else if(ps_size==416 && hash==0x7191C128) mode=4; /* two-texture effect material */
    else if(ps_size==400 && hash==0xCE68D7DC) mode=5; /* squared texture color */
    else if(ps_size==400 && hash==0x21D10AB1) mode=6; /* linear texture color */
    else if(ps_size==560 && hash==0x5ACC23EA) mode=7; /* triple-texture multiply and alpha erosion */
    else if(ps_size==560 && hash==0x0161E31B) mode=8; /* triple-texture add and alpha erosion */
    else {
        WiiUWindowGpuMaterial ui_material;
        /* Layout shaders use indexed transform constants (MOVA). Execute their
           real VS now that address registers are supported; the approximation
           can expand a small/fading sprite across the entire scene. Font draws
           retain their dedicated coverage path. */
        if(read_known_pixel_material(cpu,&ui_material)==2u)return -1;
        u32 inputs=mem_read32(cpu,g_current_pixel_shader+0x10);
        if(inputs>WIIU_PIXEL_INPUTS || !(translated=scene_program(ps,ps_size,inputs,hash)))return -1;
        mode=9;
    }
    if(!instances || instances>256 || base_instance>UINT32_MAX-instances) return 0;
    if((primitive==4 && count%3) || (primitive==19 && (mode<4 || count%4))) return 0;
    u32 per_instance=primitive==19?count/4*6:count;
    if(per_instance>6144/instances) return 0;
    u32 output_count=per_instance*instances;
    if(g_clip_control&(1u<<22)) return 0; /* rasterization disabled */
    u32 input_count=translated?mem_read32(cpu,g_current_pixel_shader+0x10):mode>=7?5:mode>=4?4:mode==1?2:1,parameter[WIIU_PIXEL_INPUTS]={0};
    if(mem_read32(cpu,g_current_pixel_shader+0x10)!=input_count ||
       mem_read32(cpu,g_current_pixel_shader+8)!=((input_count?0x14000000u:0x04000000u)|input_count) ||
       mem_read32(cpu,g_current_pixel_shader+12)) return false;
    /* Link PS inputs by semantic IDs, not by assuming export order. */
    for(u32 i=0;i<input_count;++i) {
        if(translated && !translated->input_mask[i])continue;
        u32 control=mem_read32(cpu,g_current_pixel_shader+0x14+i*4);
        if((control&~255u)!=0x100) return false;
        bool found=false;
        for(u32 j=0;j<32;++j) {
            u32 ids=mem_read32(cpu,g_current_vertex_shader+0x10+(j/4)*4);
            if(((ids>>((j%4)*8))&255)==(control&255)) {
                parameter[i]=j; found=true; break;
            }
        }
        if(!found) return false;
    }
    u32 vs_size=mem_read32(cpu,g_current_vertex_shader+0xD0);
    if(vs_size>1048576) return false;
    const u8* vs=guest_bytes(cpu,mem_read32(cpu,g_current_vertex_shader+0xD4),vs_size);
    GX2HostTexture* texture=&g_pixel_textures[0];
    GX2HostColorSurface* target=find_color_surface(g_current_color_buffers[0],false);
    GX2HostColorSurface* source=find_color_surface_by_image(texture->image);
    /* The scene's final copy samples a non-RGBA8 render target, not an
       RGBA8 guest bitmap. A host surface already contains rendered pixels;
       its backing format must not send a recognized copy pass to rejection.
       Keep the raw-memory restriction until those numeric formats decode. */
    if(!vs || (!translated && !texture->valid) || (mode<4 && !source && texture->format!=0x1A) || !target ||
       !target->width || !target->height) return false;
    WiiULatteVertexInputs inputs={0};
    inputs.sample_texture=wiiu_gx2_sample_vertex_texture;
    inputs.texture_info=vertex_texture_info;
    inputs.texture_user=cpu;
    inputs.uniforms=g_vertex_uniform_registers;
    inputs.uniform_valid=g_vertex_uniform_register_valid;
    inputs.uniform_count=GX2_UNIFORM_REGISTER_LIMIT;
    for(u32 i=0;i<16;++i) if(g_vertex_uniform_blocks[i].valid) {
        inputs.blocks[i]=guest_bytes(cpu,g_vertex_uniform_blocks[i].data,g_vertex_uniform_blocks[i].size);
        inputs.block_sizes[i]=g_vertex_uniform_blocks[i].size;
    }
    WiiUWindowPostVertex* vertices=calloc(output_count,sizeof(*vertices));
    if(!vertices) return false;
    /* Indexed triangles and expanded quads reuse vertices. Shader execution
       is pure for these passes, but the instance is part of the input key.
       Keep this cache draw-local so changing buffers/uniforms cannot go stale. */
    typedef struct {u32 index,instance,vertex_plus_one;} ExecutedVertex;
    u32 cache_size=1;
    while(cache_size<output_count*2)cache_size*=2;
    ExecutedVertex* cache=calloc(cache_size,sizeof(*cache));
    if(!cache){free(vertices);return false;}
    bool drawn=false;
    for(u32 i=0;i<output_count;++i) {
        static const u32 quad_order[6]={0,1,2,0,2,3};
        u32 instance=base_instance+i/per_instance;
        u32 logical=i%per_instance;
        if(primitive==19) logical=logical/6*4+quad_order[logical%6];
        u32 index=first_vertex+logical;
        if(index<first_vertex || (index_data &&
           !read_draw_index(cpu,index_data,logical,index_type,index_offset,&index))) goto done;
        u32 cache_slot=(index*2654435761u+instance*2246822519u)&(cache_size-1);
        while(cache[cache_slot].vertex_plus_one &&
              (cache[cache_slot].index!=index || cache[cache_slot].instance!=instance))
            cache_slot=(cache_slot+1)&(cache_size-1);
        if(cache[cache_slot].vertex_plus_one) {
            vertices[i]=vertices[cache[cache_slot].vertex_plus_one-1];
            continue;
        }
        WiiULatteFetchedVertex fetched;
        if(!fetch_current_vertex(cpu,index,instance,&fetched,NULL) || fetched.failed_count) {
            scene_rejected("vertex-fetch",0);goto done;
        }
        memset(inputs.valid,0,sizeof(inputs.valid));
        /* Latte reserves R0 for integer vertex/instance IDs. Procedural scene
           passes read it even when the fetch shader provides no attributes. */
        inputs.gpr[0][0]=index;inputs.gpr[0][1]=inputs.gpr[0][2]=0;
        inputs.gpr[0][3]=instance;inputs.valid[0]=15;
        /* VTX_SEMANTIC locations are attribute IDs, not physical GPRs.
           SQ_VTX_SEMANTIC[j] maps the attribute to R(j+1); R0 is reserved. */
        for(u32 j=0;j<32;++j) {
            u32 semantic=mem_read32(cpu,g_current_vertex_shader+0x44+j*4);
            if(semantic<128 && fetched.valid[semantic]) {
                memcpy(inputs.gpr[j+1],fetched.gpr[semantic],16);
                inputs.valid[j+1]=15;
            }
        }
        WiiULatteVertexOutputs out;
        if(!wiiu_latte_execute_vertex(vs,vs_size,&inputs,&out)) {
            scene_rejected("vertex-program",0);goto done;
        }
        memcpy(vertices[i].position,out.position,sizeof(out.position));
        if(g_clip_control&((1u<<26)|(1u<<27))) {
            /* These color-only passes have no depth attachment. With Z
               clipping disabled the Z value cannot affect rasterization. */
            vertices[i].position[2]=0;
        } else if(!(g_clip_control&(1u<<19))) {
            vertices[i].position[2]=0.5f*(out.position[2]+out.position[3]);
        }
        for(u32 p=0;p<input_count;++p) {
            const u8 effect_masks[5]={15,mode==4?7:15,1,(mode==4 || mode>=7)?15:3,3};
            u8 required=translated?translated->input_mask[p]:mode>=4?effect_masks[p]:mode==0?3:mode==1?(p?10:5):15;
            if(!required)continue;
            if((out.parameter_mask[parameter[p]]&required)!=required) {
                scene_rejected("vertex-varying",p);goto done;
            }
            memcpy(vertices[i].parameters[p],out.parameters[parameter[p]],16);
        }
        cache[cache_slot]=(ExecutedVertex){index,instance,i+1};
    }
    if(translated) {
        drawn=draw_scene_material(cpu,translated,target,vertices,output_count);
        if(!drawn)scene_rejected("scene-material",0);
        goto mark_draw;
    }
    if(source==target) goto done;
    if(mode>=4 && source) goto done; /* effect material currently consumes decoded textures only */
    GX2DecodedTexture* decoded=source?NULL:decode_texture_cached(cpu,texture);
    if(!source && (!decoded || !decoded->pixels)) goto done;
    GX2DecodedTexture* extra[2]={0};
    u32 extra_count=mode>=7?2:mode==4?1:0;
    for(u32 i=0;i<extra_count;++i) {
        if(!g_pixel_textures[i+1].valid || find_color_surface_by_image(g_pixel_textures[i+1].image)) goto done;
        extra[i]=decode_texture_cached(cpu,&g_pixel_textures[i+1]);
        if(!extra[i] || !extra[i]->pixels) goto done;
    }
    float viewport[4]={0,0,(float)target->width,(float)target->height};
    if(g_viewport_valid) for(u32 i=0;i<4;++i) viewport[i]=(float)g_viewport[i];
    for(u32 i=0;i<4;++i) if(!isfinite(viewport[i])) goto done;
    if(viewport[2]<=0 || viewport[3]<=0) goto done;
    if(!target->gpu_composited) {
        if(!ensure_color_surface_pixels(target)) goto done;
        upload_software_surface_to_gpu(target);
    }
    if(source && !source->gpu_composited) upload_software_surface_to_gpu(source);
    if(mode>=4) {
        WiiUWindowEffectTexture textures[3]={{decoded->pixels,texture->width,texture->height,decoded->serial}};
        for(u32 i=0;i<extra_count;++i) textures[i+1]=(WiiUWindowEffectTexture){
            extra[i]->pixels,g_pixel_textures[i+1].width,g_pixel_textures[i+1].height,extra[i]->serial};
        for(u32 i=0;i<=extra_count;++i) {
            textures[i].sampler_valid=g_pixel_samplers[i].valid;
            textures[i].sampler_word0=g_pixel_samplers[i].words[0];
        }
        drawn=wiiu_window_gpu_draw_effect(target->image,target->width,target->height,
            textures,vertices,output_count,mode,viewport,&g_blend_controls[0],g_scissor_valid,
            g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]);
    } else drawn=wiiu_window_gpu_draw_postprocess(target->image,target->width,target->height,
        source?source->image:0,decoded?decoded->pixels:NULL,
        texture->width,texture->height,decoded?decoded->serial:0,
        vertices,output_count,mode,source?texture->comp_map:0x00010203,viewport,
        &g_blend_controls[0],g_scissor_valid,g_scissor[0],g_scissor[1],g_scissor[2],g_scissor[3]);
mark_draw:
    if(drawn) {
        if(!translated) {
            target->cpu_pixels_stale=true; target->drawn=true; target->draw_serial=++g_draw_serial;
        }
        static u32 logged_modes;
        if(!(logged_modes&(1u<<mode))) {
            logged_modes|=1u<<mode;
            fprintf(stderr,"gx2: native postprocess mode=%u vertices=%u vs=%08X ps=%08X frame=%llu\n",
                mode,count,g_current_vertex_shader,g_current_pixel_shader,(unsigned long long)g_frame_count);
        }
    }
done:
    free(cache);free(vertices); return drawn;
}

static bool dump_surface_ppm(const GX2HostColorSurface* surface,const char* path);
static void trace_completed_draw(const char* path,bool wrote,GX2HostColorSurface* target) {
    const char* requested=getenv("BOTW_TRACE_DRAW_FRAME");
    static bool yellow_reported;
    if(!wrote || !requested || !*requested)return;
    bool yellow=strcmp(requested,"yellow")==0;
    if(yellow?yellow_reported:strtoull(requested,NULL,10)!=g_frame_count)return;
    if(!target)target=find_color_surface(g_current_color_buffers[0],false);
    if(!target)return;
    if(yellow) {
        if(target->width!=1280 || target->height!=720 || !target->gpu_composited)return;
        size_t size=(size_t)target->width*target->height*4;
        u8* pixels=malloc(size);if(!pixels)return;
        bool match=wiiu_window_gpu_readback(target->image,pixels,target->width,target->height,target->width*4);
        if(match)match=pixels[2]>=250 && pixels[1]>=245 && pixels[0]>=185 && pixels[0]<=205;
        for(size_t i=4;match && i<size;i+=4)
            if(memcmp(pixels,pixels+i,3))match=false;
        free(pixels);if(!match)return;
        yellow_reported=true;
        GX2HostTexture* t=&g_pixel_textures[0];
        WiiUWindowGpuBlendControl* b=&g_blend_controls[0];
        fprintf(stderr,"gx2: yellow-source desc=%08X image=%08X size=%u %ux%u fmt=%X comp=%08X tile=%u pitch=%u blend=%u/%u factors=%u,%u,%u alpha=%u/%u,%u,%u\n",
            t->descriptor,t->image,t->image_size,t->width,t->height,t->format,t->comp_map,t->tile_mode,t->pitch,
            b->color_control_valid,b->blend_enabled,b->color_source_factor,b->color_destination_factor,b->color_operation,
            b->separate_alpha,b->alpha_source_factor,b->alpha_destination_factor,b->alpha_operation);
        dump_guide_uniform_registers("yellow_ps",g_pixel_uniform_registers,g_pixel_uniform_register_valid);
        for(u32 i=0;i<GX2_DECODED_TEXTURE_LIMIT;++i)if(decoded_texture_matches(&g_decoded_textures[i],t)) {
            u8* p=g_decoded_textures[i].pixels;
            fprintf(stderr,"gx2: yellow-decoded BGRA=%u,%u,%u,%u\n",p[0],p[1],p[2],p[3]);
        }
    }
    char name[128];snprintf(name,sizeof(name),"draw_%llu_%llu_%s.ppm",
        (unsigned long long)g_frame_count,(unsigned long long)target->draw_serial,path);
    if(dump_surface_ppm(target,name))fprintf(stderr,
        "gx2: draw-trace file=%s target=%08X vs=%08X ps=%08X source=%08X\n",
        name,target->image,g_current_vertex_shader,g_current_pixel_shader,g_pixel_textures[0].image);
}
static bool timed_software_draw_texture(CPUState* cpu, u32 primitive,
                                        u32 vertex_count, u32 first_vertex,u32 instances) {
    if(!instances) return false;
    capture_draw_variant(cpu, primitive, vertex_count, 0u, 0u);
    clock_t start = clock();
    bool wrote=draw_constant_fullscreen_triangle(cpu,primitive,vertex_count,first_vertex,0,0,0);
    if(!wrote) {
        int post=draw_postprocess(cpu,primitive,vertex_count,first_vertex,0,0,0,instances,0);
        wrote=post>=0?post!=0:software_draw_texture(cpu,primitive,vertex_count,first_vertex);
    }
    g_texture_draw_ticks += clock() - start;
    g_texture_draw_calls++;
    trace_completed_draw("nonindexed",wrote,NULL);
    return wrote;
}

static bool timed_software_draw_indexed_texture(
    CPUState* cpu, u32 primitive, u32 index_count, u32 index_type,
    u32 index_data, s32 index_offset,u32 instances,u32 base_instance) {
    clock_t start = clock();
    bool wrote=draw_constant_fullscreen_triangle(cpu,primitive,index_count,0,index_type,index_data,index_offset);
    if(!wrote) {
        int post=draw_postprocess(cpu,primitive,index_count,0,index_type,index_data,index_offset,instances,base_instance);
        wrote=post>=0?post!=0:software_draw_indexed_texture(cpu,primitive,index_count,index_type,index_data,index_offset);
    }
    g_texture_draw_ticks += clock() - start;
    g_texture_draw_calls++;
    trace_completed_draw("indexed",wrote,NULL);
    return wrote;
}

static bool timed_software_draw_indexed_ui(CPUState* cpu, u32 primitive,
                                           u32 index_count, u32 index_type,
                                           u32 index_data,
                                           s32 index_offset) {
    capture_draw_variant(cpu, primitive, index_count, index_data, index_type);
    clock_t start = clock();
    bool wrote = software_draw_indexed_ui(cpu, primitive, index_count,
                                          index_type, index_data, index_offset);
    g_ui_draw_ticks += clock() - start;
    g_ui_draw_calls++;
    trace_completed_draw("ui",wrote,NULL);
    return wrote;
}

static void execute_display_list(CPUState* cpu, u32 address, u32 size,
                                 u32 depth);

static void execute_recorded_command(CPUState* cpu,
                                     const GX2RecordedCommand* command,
                                     u32 depth) {
    if (g_frame_count==240u && getenv("BOTW_CAPTURE_DIR"))
        fprintf(stderr,"gx2: frame-command depth=%u %s %08X %08X %08X %08X\n",depth,
            recorded_kind_name(command->kind),command->args[0],command->args[1],command->args[2],command->args[3]);
    switch (command->kind) {
    case GX2_RECORDED_SET_COLOR_BUFFER: {
        u32 target = command->args[1];
        GX2HostColorSurface* surface=bind_color_snapshot(&command->texture);
        if (target < GX2_RENDER_TARGET_LIMIT)
            g_current_color_buffers[target] = surface?surface->image:0;
        break;
    }
    case GX2_RECORDED_CLEAR_COLOR:
        clear_color_surface_values(bind_color_snapshot(&command->texture), command->fargs[0],
                                   command->fargs[1], command->fargs[2],
                                   command->fargs[3]);
        break;
    case GX2_RECORDED_SET_PIXEL_TEXTURE:
        if (command->args[1] < GX2_TEXTURE_UNIT_LIMIT)
            g_pixel_textures[command->args[1]] = command->texture;
        break;
    case GX2_RECORDED_SET_VERTEX_SAMPLER:
    case GX2_RECORDED_SET_PIXEL_SAMPLER:
        if(command->args[0]<GX2_TEXTURE_UNIT_LIMIT) {
            GX2HostSampler* sampler=command->kind==GX2_RECORDED_SET_PIXEL_SAMPLER?
                &g_pixel_samplers[command->args[0]]:&g_vertex_samplers[command->args[0]];
            sampler->valid=true;
            memcpy(sampler->words,&command->args[1],sizeof(sampler->words));
        }
        break;
    case GX2_RECORDED_SET_VERTEX_TEXTURE:
        if (command->args[1] < GX2_TEXTURE_UNIT_LIMIT)
            g_vertex_textures[command->args[1]] = command->texture;
        break;
    case GX2_RECORDED_SET_ATTRIB_BUFFER:
        if (command->args[0] < GX2_ATTRIB_BUFFER_LIMIT) {
            GX2HostAttribBuffer* buffer =
                &g_attrib_buffers[command->args[0]];
            buffer->valid = command->args[3] != 0 && command->args[1] != 0;
            buffer->size = command->args[1];
            buffer->stride = command->args[2];
            buffer->data = command->args[3];
        }
        break;
    case GX2_RECORDED_SET_FETCH_SHADER:
        g_current_fetch_shader = command->args[0];
        (void)remember_fetch_shader_from_program(cpu, g_current_fetch_shader);
        break;
    case GX2_RECORDED_SET_VERTEX_SHADER:
        g_current_vertex_shader = command->args[0];
        break;
    case GX2_RECORDED_SET_PIXEL_SHADER:
        g_current_pixel_shader = command->args[0];
        break;
    case GX2_RECORDED_SET_VERTEX_UNIFORM_REG: {
        u32 captured = command->uniform_word_count;
        apply_uniform_register_words(g_vertex_uniform_registers,
                                     g_vertex_uniform_register_valid,
                                     command->args[0], command->uniform_words,
                                     captured);
        if (command->args[1] > captured) {
            apply_uniform_registers(
                cpu, true, command->args[0] + captured,
                command->args[1] - captured,
                command->args[2] + captured * sizeof(u32));
        }
        break;
    }
    case GX2_RECORDED_SET_PIXEL_UNIFORM_REG: {
        u32 captured = command->uniform_word_count;
        apply_uniform_register_words(g_pixel_uniform_registers,
                                     g_pixel_uniform_register_valid,
                                     command->args[0], command->uniform_words,
                                     captured);
        if (command->args[1] > captured) {
            apply_uniform_registers(
                cpu, false, command->args[0] + captured,
                command->args[1] - captured,
                command->args[2] + captured * sizeof(u32));
        }
        break;
    }
    case GX2_RECORDED_SET_VERTEX_UNIFORM_BLOCK:
        if (command->args[0] < GX2_UNIFORM_BLOCK_LIMIT) {
            GX2HostUniformBlock* block =
                &g_vertex_uniform_blocks[command->args[0]];
            block->valid = command->args[2] != 0;
            block->size = command->args[1];
            block->data = command->args[2];
        }
        break;
    case GX2_RECORDED_SET_PIXEL_UNIFORM_BLOCK:
        if (command->args[0] < GX2_UNIFORM_BLOCK_LIMIT) {
            GX2HostUniformBlock* block =
                &g_pixel_uniform_blocks[command->args[0]];
            block->valid = command->args[2] != 0;
            block->size = command->args[1];
            block->data = command->args[2];
        }
        break;
    case GX2_RECORDED_SET_BLEND_CONTROL:
        set_blend_control_from_args(command->args);
        break;
    case GX2_RECORDED_SET_COLOR_CONTROL:
    case GX2_RECORDED_SET_CHANNEL_MASK:
    case GX2_RECORDED_SET_BLEND_CONSTANT:
        set_color_state(command->kind,command->args);
        break;
    case GX2_RECORDED_SET_VIEWPORT:
        memcpy(g_viewport, command->fargs, sizeof(g_viewport));
        g_viewport_valid = true;
        break;
    case GX2_RECORDED_SET_SCISSOR:
        memcpy(g_scissor, command->args, sizeof(g_scissor));
        g_scissor_valid = true;
        break;
    case GX2_RECORDED_COPY_SURFACE:
        (void)copy_surface(cpu, command);
        break;
    case GX2_RECORDED_COPY_COLOR_TO_SCAN: {
        GX2HostColorSurface* surface=bind_color_snapshot(&command->texture);
        if ((command->args[1] & 0x3u) != 0)
            g_tv_scan_color_buffer = surface?surface->image:0;
        if ((command->args[1] & 0xCu) != 0)
            g_drc_scan_color_buffer = surface?surface->image:0;
        break;
    }
    case GX2_RECORDED_SET_CONTEXT:
        switch_context_shadow(command->args[0], false);
        break;
    case GX2_RECORDED_SET_CLIP_CONTROL:
        g_clip_control=(!command->args[0]?(1u<<22):0) |
            (!command->args[1]?((1u<<26)|(1u<<27)):0) |
            (command->args[2]?(1u<<19):0);
        break;
    case GX2_RECORDED_DRAW:
        if (g_draw_log_count++ < 96u) {
            fprintf(stderr,
                    "gx2: execute draw primitive=%u vertices=%u first=%u "
                    "instances=%u\n",
                    command->args[0], command->args[1], command->args[2],
                    command->args[3]);
        }
        timed_software_draw_texture(cpu, command->args[0], command->args[1],
                                    command->args[2],command->args[3]);
        log_draw_state(cpu);
        break;
    case GX2_RECORDED_DRAW_INDEXED:
        if (!command->args[1] || !command->args[5]) break;
        if (g_draw_log_count++ < 96u) {
            fprintf(stderr,
                    "gx2: execute indexed primitive=%u indices=%u type=%u "
                    "data=0x%08X baseVertex=%d instances=%u baseInstance=%u\n",
                    command->args[0], command->args[1], command->args[2],
                    command->args[3], (s32)command->args[4],
                    command->args[5], command->args[6]);
        }
        if (!g_ui_buffers_dumped && g_attrib_buffers[0].valid &&
            g_attrib_buffers[0].stride == 28u && command->args[1] >= 100u) {
            u32 index_size = (command->args[2] & 1u) ? 4u : 2u;
            GX2DecodedTexture* atlas =
                decode_texture_cached(cpu, &g_pixel_textures[0]);
            bool vertices = dump_guest_buffer(
                cpu, g_attrib_buffers[0].data, g_attrib_buffers[0].size,
                "sm3dw_ui_vertices.bin");
            bool indices = dump_guest_buffer(
                cpu, command->args[3], command->args[1] * index_size,
                "sm3dw_ui_indices.bin");
            bool texture = atlas && dump_host_buffer(
                atlas->pixels,
                (size_t)atlas->texture.width * atlas->texture.height * 4u,
                "sm3dw_ui_atlas.bgra");
            if (vertices && indices && texture) {
                fprintf(stderr,
                        "gx2: captured UI buffers vertices=0x%X stride=%u "
                        "indices=%u atlas=%ux%u format=0x%X\n",
                        g_attrib_buffers[0].size,
                        g_attrib_buffers[0].stride, command->args[1],
                        atlas->texture.width, atlas->texture.height,
                        atlas->texture.format);
                g_ui_buffers_dumped = true;
            }
        }
        if (!timed_software_draw_indexed_ui(cpu, command->args[0],
                                            command->args[1], command->args[2],
                                            command->args[3],
                                            (s32)command->args[4])) {
            timed_software_draw_indexed_texture(
                cpu, command->args[0], command->args[1], command->args[2],
                command->args[3], (s32)command->args[4],command->args[5],command->args[6]);
        }
        if (g_draw_state_log_count < 12u) {
            fprintf(stderr, "gx2: execute index bytes=");
            log_guest_prefix(
                cpu, command->args[3],
                command->args[1] * ((command->args[2] & 1u) ? 4u : 2u));
            fputc('\n', stderr);
        }
        log_draw_state(cpu);
        break;
    case GX2_RECORDED_CALL_DISPLAY_LIST:
        execute_display_list(cpu, command->args[0], command->args[1],
                             depth + 1u);
        break;
    default:
        break;
    }
}

static void execute_display_list(CPUState* cpu, u32 address, u32 size,
                                 u32 depth) {
    static u32 missing_log_count;
    static u32 execute_log_count;
    if (depth >= 8u)
        return;
    /* GX2GetContextStateDisplayList returns a register-restore list embedded
       in the guest context, not one built through GX2BeginDisplayList. */
    if (size) for (u32 i = 0; i < 64; ++i) {
        u32 context = g_context_shadows[i].address;
        if (context && (u64)context + 0x9E00u == address) {
            switch_context_shadow(context, false);
            return;
        }
    }
    GX2HostDisplayList* list = find_host_display_list(address, false);
    if (!list) {
        if (missing_log_count++ < 32u) {
            fprintf(stderr,
                    "gx2: no host display list address=0x%08X size=0x%X\n",
                    address, size);
        }
        return;
    }
    list->serial = ++g_display_list_serial;
    if (execute_log_count++ < 64u) {
        fprintf(stderr,
                "gx2: execute display list address=0x%08X size=0x%X "
                "recorded=0x%X commands=%u\n",
                address, size, list->size, list->command_count);
    }
    for (u32 i = 0; i < list->command_count; i++)
        execute_recorded_command(cpu, &list->commands[i], depth);
}

static GX2HostColorSurface* select_present_surface(void) {
    const u32 preferred[] = {g_tv_scan_color_buffer};
    for (u32 i = 0; i < sizeof(preferred) / sizeof(preferred[0]); i++) {
        GX2HostColorSurface* surface = find_color_surface(preferred[i], false);
        if (surface && (surface->cleared || surface->drawn) &&
            surface->width && surface->height)
            return surface;
    }

    GX2HostColorSurface* best = NULL;
    for (u32 i = 0; i < GX2_HOST_SURFACE_LIMIT; i++) {
        GX2HostColorSurface* surface = &g_color_surfaces[i];
        if (!surface->valid || (!surface->cleared && !surface->drawn) || surface->width == 0 ||
            surface->height == 0)
            continue;
        bool matches_tv = g_tv_width != 0 && g_tv_height != 0 &&
                          surface->width == g_tv_width &&
                          surface->height == g_tv_height;
        bool best_matches_tv = best && g_tv_width != 0 && g_tv_height != 0 &&
                               best->width == g_tv_width &&
                               best->height == g_tv_height;
        if (!best || (matches_tv && !best_matches_tv) ||
            (matches_tv == best_matches_tv &&
             surface->clear_serial > best->clear_serial)) {
            best = surface;
        }
    }
    return best;
}

static bool dump_surface_ppm(const GX2HostColorSurface* surface,
                             const char* path) {
    if (!surface || !path || surface->width == 0 ||
        surface->height == 0 || !getenv("BOTW_CAPTURE_DIR")) {
        return false;
    }
    if (surface->gpu_composited) {
        GX2HostColorSurface copy = *surface;
        size_t bytes = (size_t)surface->width * surface->height * 4u;
        copy.pixels = (u8*)malloc(bytes);
        if (!copy.pixels)
            return false;
        copy.gpu_composited = false;
        bool captured = wiiu_window_gpu_readback(surface->image, copy.pixels,
                surface->width, surface->height, surface->width * 4u) &&
                dump_surface_ppm(&copy, path);
        free(copy.pixels);
        return captured;
    }
    if (!surface->pixels)
        return false;
    FILE* file = open_capture_file(path);
    if (!file)
        return false;
    fprintf(file, "P6\n%u %u\n255\n", surface->width, surface->height);
    size_t row_size = (size_t)surface->width * 3u;
    u8* rgb_row = (u8*)malloc(row_size);
    if (!rgb_row) {
        fclose(file);
        return false;
    }
    for (u32 y = 0; y < surface->height; y++) {
        const u8* row =
            surface->pixels + (size_t)y * surface->width * 4u;
        for (u32 x = 0; x < surface->width; x++) {
            const u8* pixel = row + (size_t)x * 4u;
            rgb_row[x * 3u + 0u] = pixel[2];
            rgb_row[x * 3u + 1u] = pixel[1];
            rgb_row[x * 3u + 2u] = pixel[0];
        }
        if (fwrite(rgb_row, 1u, row_size, file) != row_size) {
            free(rgb_row);
            fclose(file);
            return false;
        }
    }
    free(rgb_row);
    fclose(file);
    fprintf(stderr, "gx2: wrote frame capture %s (%ux%u)\n", path,
            surface->width, surface->height);
    return true;
}

static void present_scan_buffer(void) {
    clock_t present_start = clock();
    /* End the previous input frame before this present can pump new events. */
    wiiu_window_advance_input_frame();
    GX2HostColorSurface* surface = select_present_surface();
    g_frame_count++;
    if (!surface) {
        g_present_ticks += clock() - present_start;
        return;
    }
    /*
     * The game begins with several clear-only scan-buffer presents.  Those
     * are still real frame boundaries, so they must switch the guest runner
     * from its bulk boot slice to its responsive frame slice.
     */
    g_has_active_frame = true;
    bool presented_gpu = false;
    if (surface->gpu_composited) {
        presented_gpu = wiiu_window_gpu_present(surface->image,
                                                 surface->width,
                                                 surface->height);
        if (!presented_gpu)
            (void)synchronize_gpu_surface_for_software(surface);
    }
    if (!presented_gpu && surface->pixels) {
        wiiu_window_present_bgra(surface->width, surface->height,
                                 surface->pixels, surface->width * 4u);
    } else if (!presented_gpu) {
        wiiu_window_present_solid(surface->width, surface->height,
                                  surface->red, surface->green,
                                  surface->blue, surface->alpha);
    }
    if (surface->drawn && !g_first_render_dumped &&
        dump_surface_ppm(surface, "sm3dw_first_render.ppm")) {
        g_first_render_dumped = true;
    }
    if (surface->drawn &&
        g_largest_frame_command_count >= 80u &&
        !g_menu_candidate_dumped &&
        dump_surface_ppm(surface, "sm3dw_menu_candidate.ppm")) {
        g_menu_candidate_dumped = true;
    }
    if (surface->drawn && (g_frame_count % 120u) == 0u) {
        dump_surface_ppm(surface, "sm3dw_latest.ppm");
        GX2HostColorSurface* drc=find_color_surface(g_drc_scan_color_buffer,false);
        if(drc && drc!=surface) dump_surface_ppm(drc,"sm3dw_drc_latest.ppm");
        if(g_frame_count==240u) {
            for(u32 i=0;i<GX2_HOST_SURFACE_LIMIT;++i) {
                GX2HostColorSurface* other=&g_color_surfaces[i];
                if(other->valid && other->drawn && other!=surface) {
                    char path[64];snprintf(path,sizeof(path),"sm3dw_offscreen_%08X.ppm",other->descriptor);
                    dump_surface_ppm(other,path);
                }
            }
        }
    }
    if (g_swap_log_count++ < 32u || (g_frame_count % 60u) == 0u) {
        fprintf(stderr,
                "gx2: present frame=%llu color=0x%08X image=0x%08X "
                "%ux%u rgba=(%u,%u,%u,%u)\n",
                (unsigned long long)g_frame_count, surface->descriptor,
                surface->image, surface->width, surface->height,
                surface->red, surface->green, surface->blue, surface->alpha);
    }
    if ((g_frame_count % 60u) == 0u &&
        (g_fast_ui_quad_count != 0u || g_fallback_ui_quad_count != 0u ||
         g_gpu_ui_quad_count != 0u)) {
        fprintf(stderr,
                "gx2: UI quads gpu=%llu fast=%llu fallback=%llu\n",
                (unsigned long long)g_gpu_ui_quad_count,
                (unsigned long long)g_fast_ui_quad_count,
                (unsigned long long)g_fallback_ui_quad_count);
    }
    g_present_ticks += clock() - present_start;
    if ((g_frame_count % 60u) == 0u) {
        double ticks_to_ms = 1000.0 / (double)CLOCKS_PER_SEC;
        fprintf(stderr,
                "gx2: perf texture=%.1fms/%llu ui=%.1fms/%llu "
                "present=%.1fms\n",
                (double)g_texture_draw_ticks * ticks_to_ms,
                (unsigned long long)g_texture_draw_calls,
                (double)g_ui_draw_ticks * ticks_to_ms,
                (unsigned long long)g_ui_draw_calls,
                (double)g_present_ticks * ticks_to_ms);
        g_texture_draw_ticks = 0;
        g_ui_draw_ticks = 0;
        g_present_ticks = 0;
        g_texture_draw_calls = 0;
        g_ui_draw_calls = 0;
    }
}

bool wiiu_gx2_has_active_frame(void) {
    return g_has_active_frame;
}

static u32 surface_bytes_per_pixel(u32 format) {
    switch (format & 0x3Fu) {
    case 0x01:
    case 0x02:
        return 1;
    case 0x05:
    case 0x06:
    case 0x07:
    case 0x08:
    case 0x0A:
    case 0x0B:
    case 0x0C:
        return 2;
    case 0x0D:
    case 0x0E:
    case 0x0F:
    case 0x10:
    case 0x11:
    case 0x16:
    case 0x19:
    case 0x1A:
    case 0x1B:
    case 0x1C:
        return 4;
    case 0x1D:
    case 0x1E:
    case 0x1F:
    case 0x20:
        return 8;
    case 0x22:
    case 0x23:
        return 16;
    default:
        return 4;
    }
}

static bool surface_is_compressed(u32 format) {
    u32 hw_format = format & 0x3Fu;
    return hw_format >= 0x31u && hw_format <= 0x35u;
}

static bool surface_is_macro_tiled(u32 tile_mode) {
    u32 mode = tile_mode & 0xFu;
    return mode >= 4u && mode <= 15u;
}

static bool surface_is_thick_tiled(u32 tile_mode) {
    u32 mode = tile_mode & 0xFu;
    return mode == 3u || mode == 7u || mode == 11u || mode == 13u ||
           mode == 15u;
}

static void surface_macro_dimensions(u32 tile_mode, u32* width_out,
                                     u32* height_out) {
    u32 mode = tile_mode & 0xFu;
    u32 width = 32u;
    u32 height = 16u;
    if (mode == 5u || mode == 9u) {
        width = 16u;
        height = 32u;
    } else if (mode == 6u || mode == 10u) {
        width = 8u;
        height = 64u;
    }
    *width_out = width;
    *height_out = height;
}

static u32 surface_level_size(u32 width, u32 height, u32 depth, u32 format,
                              u32 aa, u32 tile_mode, u32* pitch_out,
                              u32* alignment_out) {
    width = width ? width : 1u;
    height = height ? height : 1u;
    depth = depth ? depth : 1u;
    u32 samples = aa < 4u ? 1u << aa : 1u;
    bool compressed = surface_is_compressed(format);
    u32 bits = compressed
                   ? (((format & 0x3Fu) == 0x31u ||
                       (format & 0x3Fu) == 0x34u)
                          ? 64u
                          : 128u)
                   : surface_bytes_per_pixel(format) * 8u;
    u32 elements_wide = compressed ? (width + 3u) / 4u : width;
    u32 elements_high = compressed ? (height + 3u) / 4u : height;
    u32 stored_depth = depth;
    u32 mode = tile_mode & 0xFu;
    u32 pitch_alignment = 1u;
    u32 height_alignment = 1u;
    u32 alignment = 1u;

    if (surface_is_macro_tiled(tile_mode)) {
        u32 macro_width = 0u;
        u32 macro_height = 0u;
        u32 thickness = surface_is_thick_tiled(tile_mode) ? 4u : 1u;
        surface_macro_dimensions(tile_mode, &macro_width, &macro_height);
        u32 pixels_per_interleave = 256u / bits;
        u32 pitch_factor = pixels_per_interleave / (8u * thickness * samples);
        pitch_alignment = macro_width;
        if (pitch_factor != 0u)
            pitch_alignment = macro_width * pitch_factor;
        if (pitch_alignment < macro_width)
            pitch_alignment = macro_width;
        height_alignment = macro_height;
        u64 macro_bytes =
            (u64)samples * bits * thickness * macro_width * macro_height / 8u;
        alignment = macro_bytes > 256u
                        ? (macro_bytes > 0xFFFFFFFFu ? 0xFFFFFFFFu
                                                      : (u32)macro_bytes)
                        : 256u;
        if (thickness > 1u)
            stored_depth = align_up(stored_depth, thickness);
    } else if (mode == 2u || mode == 3u) {
        u32 thickness = mode == 3u ? 4u : 1u;
        pitch_alignment = 256u / bits / samples / thickness;
        if (pitch_alignment < 8u)
            pitch_alignment = 8u;
        height_alignment = 8u;
        alignment = 256u;
        if (thickness > 1u)
            stored_depth = align_up(stored_depth, thickness);
    } else if (mode == 1u) {
        pitch_alignment = 2048u / bits;
        if (pitch_alignment < 64u)
            pitch_alignment = 64u;
        alignment = 256u;
    }

    elements_wide = align_up(elements_wide, pitch_alignment);
    elements_high = align_up(elements_high, height_alignment);
    if (pitch_out)
        *pitch_out = elements_wide;
    if (alignment_out)
        *alignment_out = alignment;

    u64 size = (u64)elements_wide * elements_high * stored_depth * samples *
               bits / 8u;

    return size <= 0xFFFFFFFFu ? (u32)size : 0xFFFFFFFFu;
}

static u32 scan_image_size(u32 width, u32 height, u32 format) {
    u64 size = (u64)width * height * surface_bytes_per_pixel(format);
    if (format == 0x81u)
        size += size / 2u;
    if (size > 0xFFFFFFFFu)
        return 0xFFFFFFFFu;
    return align_up((u32)size, 0x800u);
}

static void calculate_surface(CPUState* cpu, u32 surface) {
    if (!guest_range_valid(cpu, surface, 0x74u))
        return;

    u32 dim = mem_read32(cpu, surface + 0x00u);
    u32 width = mem_read32(cpu, surface + 0x04u);
    u32 height = mem_read32(cpu, surface + 0x08u);
    u32 depth = mem_read32(cpu, surface + 0x0Cu);
    u32 levels = mem_read32(cpu, surface + 0x10u);
    u32 format = mem_read32(cpu, surface + 0x14u);
    u32 aa = mem_read32(cpu, surface + 0x18u);
    u32 tile_mode = mem_read32(cpu, surface + 0x30u);

    if (dim >= 50u || aa >= 0x100u || width >= 0x01000000u ||
        height >= 0x01000000u || depth >= 0x01000000u ||
        format >= 0x10000u || width == 0 || height == 0) {
        dim = 1u;
        width = 8u;
        height = 8u;
        depth = 1u;
        levels = 1u;
        format = 0x1Au;
        aa = 0;
        tile_mode = 4u;
        mem_write32(cpu, surface + 0x00u, dim);
        mem_write32(cpu, surface + 0x04u, width);
        mem_write32(cpu, surface + 0x08u, height);
        mem_write32(cpu, surface + 0x0Cu, depth);
        mem_write32(cpu, surface + 0x10u, levels);
        mem_write32(cpu, surface + 0x14u, format);
        mem_write32(cpu, surface + 0x18u, aa);
        mem_write32(cpu, surface + 0x30u, tile_mode);
    }

    if (levels == 0)
        levels = 1u;
    if (levels > 14u)
        levels = 14u;
    if (depth == 0)
        depth = 1u;
    if (tile_mode == 0u)
        tile_mode = dim == 0u && aa == 0u ? 1u : 4u;

    u32 alignment = 0u;
    u32 pitch = 0;
    u32 image_size = surface_level_size(width, height, depth, format, aa,
                                        tile_mode, &pitch, &alignment);

    u32 mip_size = 0;
    u32 mip_cursor = 0;
    for (u32 level = 1; level < levels; level++) {
        u32 mip_width = width >> level;
        u32 mip_height = height >> level;
        u32 mip_depth = dim == 2u ? depth >> level : depth;
        if (mip_width == 0)
            mip_width = 1u;
        if (mip_height == 0)
            mip_height = 1u;
        if (mip_depth == 0)
            mip_depth = 1u;
        u32 level_size = surface_level_size(mip_width, mip_height, mip_depth,
                                            format, aa, tile_mode, NULL, NULL);
        mip_cursor = align_up(mip_cursor, level == 1u ? alignment : 0x100u);
        if (level - 1u < 13u)
            mem_write32(cpu, surface + 0x40u + (level - 1u) * 4u,
                        mip_cursor);
        u64 next = (u64)mip_cursor + level_size;
        mip_cursor = next <= 0xFFFFFFFFu ? (u32)next : 0xFFFFFFFFu;
    }
    if (levels > 1u)
        mip_size = align_up(mip_cursor, 0x100u);

    mem_write32(cpu, surface + 0x10u, levels);
    mem_write32(cpu, surface + 0x20u, image_size);
    mem_write32(cpu, surface + 0x28u, mip_size);
    mem_write32(cpu, surface + 0x30u, tile_mode);
    mem_write32(cpu, surface + 0x38u, alignment);
    mem_write32(cpu, surface + 0x3Cu, pitch);

    if (g_surface_log_count++ < 24u) {
        fprintf(stderr,
                "gx2: surface %ux%ux%u fmt=0x%X levels=%u image=0x%X "
                "mips=0x%X align=0x%X pitch=%u\n",
                width, height, depth, format, levels, image_size, mip_size,
                alignment, pitch);
    }
}

static void display_list_append(CPUState* cpu, u32 bytes) {
    GX2DisplayListState* state = get_display_list_state(cpu, false);
    if (!state || !state->active || bytes == 0)
        return;
    u32 available = state->capacity > state->used
                        ? state->capacity - state->used
                        : 0;
    if (bytes > available)
        bytes = available;
    bytes &= ~3u;
    if (bytes == 0)
        return;
    guest_zero(cpu, state->address + state->used, bytes);
    state->used += bytes;
}

static void handle_display_list(CPUState* cpu, const char* name) {
    GX2DisplayListState* state = get_display_list_state(cpu, false);
    if (is_name(name, "GX2BeginDisplayList") ||
        is_name(name, "GX2BeginDisplayListEx")) {
        state = get_display_list_state(cpu, true);
        if (!state) {
            cpu->gpr[3] = 0;
            return;
        }
        state->active = true;
        state->address = cpu->gpr[3];
        state->capacity = cpu->gpr[4] & ~3u;
        state->used = 0;
        state->command_count = 0;
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2GetCurrentDisplayList")) {
        write_output(cpu, cpu->gpr[3],
                     state && state->active ? state->address : 0u);
        write_output(cpu, cpu->gpr[4],
                     state && state->active ? state->capacity : 0u);
        cpu->gpr[3] = state && state->active ? 1u : 0u;
        return;
    }

    if (is_name(name, "GX2GetDisplayListWriteStatus")) {
        cpu->gpr[3] = state && state->active ? 1u : 0u;
        return;
    }

    if (is_name(name, "GX2EndDisplayList")) {
        if (!state || !state->active ||
            (cpu->gpr[3] != 0 && cpu->gpr[3] != state->address)) {
            cpu->gpr[3] = 0;
            return;
        }
        u32 used = state->used;
        if (used != 0)
            used = align_up(used, 32u);
        if (used > state->capacity)
            used = state->capacity & ~3u;
        if (used > state->used)
            guest_zero(cpu, state->address + state->used,
                       used - state->used);
        commit_display_list(state, used);
        bool has_draw = false;
        u32 state_index = (u32)(state - g_display_lists);
        for (u32 i = 0; i < state->command_count; i++) {
            GX2RecordedKind kind = g_recorded_commands[state_index][i].kind;
            if (kind == GX2_RECORDED_DRAW ||
                kind == GX2_RECORDED_DRAW_INDEXED) {
                has_draw = true;
                break;
            }
        }
        u32 log_index = g_display_list_log_count++;
        if (log_index < 64u || (has_draw && (log_index % 1024u) == 0u)) {
            fprintf(stderr,
                    "gx2: display list address=0x%08X used=0x%X "
                    "capacity=0x%X commands=%u\n",
                    state->address, used, state->capacity,
                    state->command_count);
        }
        state->active = false;
        state->used = used;
        cpu->gpr[3] = used;
        return;
    }

    if (state && state->active &&
        (starts_with(name, "GX2Set") || starts_with(name, "GX2Init") ||
         starts_with(name, "GX2Invalidate"))) {
        display_list_append(cpu, 16u);
    }
}

void wiiu_gx2_reset(void) {
    for(u32 i=0;i<256;++i) {
        free(g_scene_programs[i].bytes);free(g_scene_programs[i].program);
    }
    memset(g_scene_programs,0,sizeof(g_scene_programs));
    memset(g_color_aliases,0,sizeof(g_color_aliases));
    g_color_binding_serial=0;
    memset(g_context_shadows, 0, sizeof(g_context_shadows));
    g_current_context = 0;
    for (u32 i = 0; i < GX2_HOST_SURFACE_LIMIT; i++) {
        if(g_color_surfaces[i].valid) wiiu_window_gpu_invalidate(g_color_surfaces[i].image);
        free(g_color_surfaces[i].pixels);
        free(g_color_surfaces[i].vertex_float_pixels);
    }
    for (u32 i = 0; i < GX2_DECODED_TEXTURE_LIMIT; i++)
        free(g_decoded_textures[i].pixels);
    for (u32 i = 0; i < GX2_DISPLAY_LIST_CONTEXT_LIMIT; i++)
        free(g_recorded_commands[i]);
    memset(g_display_lists, 0, sizeof(g_display_lists));
    memset(g_recorded_commands, 0, sizeof(g_recorded_commands));
    for (u32 i = 0; i < GX2_HOST_DISPLAY_LIST_BUCKETS; i++) {
        GX2HostDisplayList* list = g_host_display_lists[i];
        while (list) {
            GX2HostDisplayList* next = list->next;
            free(list->commands);
            free(list);
            list = next;
        }
    }
    memset(g_host_display_lists, 0, sizeof(g_host_display_lists));
    memset(g_color_surfaces, 0, sizeof(g_color_surfaces));
    memset(g_current_color_buffers, 0, sizeof(g_current_color_buffers));
    memset(g_blend_controls, 0, sizeof(g_blend_controls));
    memset(g_pixel_textures, 0, sizeof(g_pixel_textures));
    memset(g_pixel_samplers, 0, sizeof(g_pixel_samplers));
    memset(g_vertex_samplers, 0, sizeof(g_vertex_samplers));
    memset(g_vertex_textures, 0, sizeof(g_vertex_textures));
    memset(g_decoded_textures, 0, sizeof(g_decoded_textures));
    memset(g_attrib_buffers, 0, sizeof(g_attrib_buffers));
    memset(g_fetch_shaders, 0, sizeof(g_fetch_shaders));
    memset(g_vertex_uniform_blocks, 0, sizeof(g_vertex_uniform_blocks));
    memset(g_pixel_uniform_blocks, 0, sizeof(g_pixel_uniform_blocks));
    memset(g_vertex_uniform_registers, 0, sizeof(g_vertex_uniform_registers));
    memset(g_pixel_uniform_registers, 0, sizeof(g_pixel_uniform_registers));
    memset(g_vertex_uniform_register_valid, 0,
           sizeof(g_vertex_uniform_register_valid));
    memset(g_pixel_uniform_register_valid, 0,
           sizeof(g_pixel_uniform_register_valid));
    memset(&g_ui_vertex_transform, 0, sizeof(g_ui_vertex_transform));
    g_current_fetch_shader = 0;
    g_current_vertex_shader = 0;
    g_current_pixel_shader = 0;
    g_viewport_valid = false;
    memset(g_viewport, 0, sizeof(g_viewport));
    g_scissor_valid = false;
    memset(g_scissor, 0, sizeof(g_scissor));
    g_tv_scan_color_buffer = 0;
    g_drc_scan_color_buffer = 0;
    g_tv_width = 0;
    g_tv_height = 0;
    g_swap_interval = 1u;
    g_surface_log_count = 0;
    g_display_list_log_count = 0;
    g_clear_log_count = 0;
    g_swap_log_count = 0;
    g_draw_log_count = 0;
    g_draw_state_log_count = 0;
    g_display_call_log_count = 0;
    g_software_draw_log_count = 0;
    g_software_ui_draw_log_count = 0;
    g_fast_ui_quad_count = 0;
    g_fallback_ui_quad_count = 0;
    g_gpu_ui_quad_count = 0;
    g_texture_draw_ticks = 0;
    g_ui_draw_ticks = 0;
    g_present_ticks = 0;
    g_texture_draw_calls = 0;
    g_ui_draw_calls = 0;
    g_late_draw_trace_count = 0;
    g_texture_refresh_log_count = 0;
    g_texture_cache_log_count = 0;
    g_display_list_detail_log_count = 0;
    g_shader_bind_log_count = 0;
    g_largest_frame_command_count = 0;
    g_first_render_dumped = false;
    g_has_active_frame = false;
    g_menu_candidate_dumped = false;
    g_ui_buffers_dumped = false;
    g_ui_uniform_state_dumped = false;
    g_ui_latte_transform_logged = false;
    g_latte_fetch_used_log_count = 0;
    g_latte_fetch_program_log_count = 0;
    g_guide_texture_capture_count = 0;
    g_guide_texture_capture_signature = 0;
    g_guide_texture_capture_frame = 0;
    g_guide_bc3_state_dumped = false;
    g_guide_bc3_texture_dumped = false;
    g_clear_serial = 0;
    g_draw_serial = 0;
    g_display_list_serial = 0;
    g_frame_count = 0;
    g_last_swap_tick = 0;
    g_clip_control = 0;
    /* GPU texture entries can outlive this reset. Keep content versions
       monotonic so a recycled CPU allocation cannot match an old upload. */
}

bool wiiu_gx2_get_pixel_sampler(u32 slot,u32 words[3]) {
    if(slot>=GX2_TEXTURE_UNIT_LIMIT || !words || !g_pixel_samplers[slot].valid)return false;
    memcpy(words,g_pixel_samplers[slot].words,sizeof(g_pixel_samplers[slot].words));
    return true;
}

static bool initialize_sampler(CPUState* cpu,const char* name) {
    if(strncmp(name,"GX2InitSampler",13))return false;
    u32 at=cpu->gpr[3],a=cpu->gpr[4],b=cpu->gpr[5],c=cpu->gpr[6];
    if(!guest_range_valid(cpu,at,12))return true;
    u32 word=mem_read32(cpu,at);
    if(is_name(name,"GX2InitSampler")) {
        word=(a&7)*73u|((b&7)<<9)|((b&7)<<12)|(1u<<15)|(1u<<17)|(1u<<25);
        mem_write32(cpu,at+4,0x3FFu<<10);mem_write32(cpu,at+8,0x80000000u);
    } else if(is_name(name,"GX2InitSamplerClamping"))
        word=(word&~511u)|(a&7)|((b&7)<<3)|((c&7)<<6);
    else if(is_name(name,"GX2InitSamplerXYFilter")) {
        if(c) { if(a<=1)a+=4; if(b<=1)b+=4; }
        word=(word&~((63u<<9)|(7u<<19)))|((a&7)<<9)|((b&7)<<12)|((c&7)<<19);
    } else if(is_name(name,"GX2InitSamplerZMFilter"))
        word=(word&~(15u<<15))|((a&3)<<15)|((b&3)<<17);
    else if(is_name(name,"GX2InitSamplerBorderType"))
        word=(word&~(3u<<22))|((a&3)<<22);
    else if(is_name(name,"GX2InitSamplerDepthCompare"))
        word=(word&~(7u<<26))|((a&7)<<26);
    else if(is_name(name,"GX2InitSamplerLOD")) {
        if(!isfinite(cpu->fpr[1]) || !isfinite(cpu->fpr[2]) || !isfinite(cpu->fpr[3]))return true;
        u32 min=(u32)fmin(1023,fmax(0,floor(cpu->fpr[1]*64)));
        u32 max=(u32)fmin(1023,fmax(0,floor(cpu->fpr[2]*64)));
        s32 bias=(s32)fmin(2047,fmax(-2048,floor(cpu->fpr[3]*64)));
        mem_write32(cpu,at+4,min|(max<<10)|(((u32)bias&4095)<<20));
        return true;
    } else return false;
    mem_write32(cpu,at,word);return true;
}

void wiiu_gx2_handle_import(CPUState* cpu, const char* name) {
    if(initialize_sampler(cpu,name)){cpu->gpr[3]=0;return;}
    wiiu_window_show(name);

    if(is_name(name,"GX2GetSwapStatus")) {
        write_output(cpu,cpu->gpr[3],(u32)g_frame_count);
        write_output(cpu,cpu->gpr[4],(u32)g_frame_count);
        for(u32 reg=5;reg<=6;++reg) if(guest_range_valid(cpu,cpu->gpr[reg],8u)) {
            mem_write32(cpu,cpu->gpr[reg],(u32)(g_last_swap_tick>>32u));
            mem_write32(cpu,cpu->gpr[reg]+4u,(u32)g_last_swap_tick);
        }
        cpu->gpr[3]=0;
        return;
    }
    if(is_name(name,"GX2DrawDone")) {
        cpu->gpr[3]=wiiu_window_gpu_finish()?1u:0u;
        return;
    }
    if(is_name(name,"GX2SampleTopGPUCycle") || is_name(name,"GX2SampleBottomGPUCycle")) {
        /* Like the reference backend, mark bottom-cycle timing unsupported
           instead of exposing uninitialized memory as a valid GPU duration. */
        u64 value=is_name(name,"GX2SampleTopGPUCycle")?cpu->timebase:UINT64_MAX;
        if(guest_range_valid(cpu,cpu->gpr[3],8u)) {
            mem_write32(cpu,cpu->gpr[3],(u32)(value>>32u));
            mem_write32(cpu,cpu->gpr[3]+4u,(u32)value);
        }
        cpu->gpr[3]=0;return;
    }
    if(is_name(name,"GX2GPUTimeToCPUTime")) {
        cpu->gpr[3]=cpu->gpr[4]=0;return;
    }

    /* Cemu only treats the CPU-cache flag as a texture-data update. */
    if (is_name(name, "GX2Invalidate") && (cpu->gpr[3] & 0x40u) != 0u &&
        cpu->gpr[5] != 0u)
        invalidate_decoded_textures(cpu->gpr[4], cpu->gpr[5]);

    if (is_name(name, "GX2CalcTVSize") || is_name(name, "GX2CalcDRCSize")) {
        u32 mode = cpu->gpr[3];
        u32 format = cpu->gpr[4];
        u32 buffering = cpu->gpr[5] ? cpu->gpr[5] : 1u;
        u32 width = 0;
        u32 height = 0;
        if (is_name(name, "GX2CalcTVSize")) {
            static const u16 widths[] = {0, 640, 854, 1280, 1280, 1920};
            static const u16 heights[] = {0, 480, 480, 720, 720, 1080};
            if (mode < 6u) {
                width = widths[mode];
                height = heights[mode];
            }
            if (mode < 3u)
                buffering = 4u;
        } else if (mode > 0u) {
            width = 854u;
            height = 480u;
        }
        u32 image_size = scan_image_size(width, height, format);
        u64 total = (u64)image_size * buffering;
        write_output(cpu, cpu->gpr[6],
                     total <= 0xFFFFFFFFu ? (u32)total : 0xFFFFFFFFu);
        write_output(cpu, cpu->gpr[7], 0u);
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2CalcSurfaceSizeAndAlignment")) {
        calculate_surface(cpu, cpu->gpr[3]);
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2CalcDepthBufferHiZInfo") ||
        is_name(name, "GX2CalcColorBufferAuxInfo")) {
        write_output(cpu, cpu->gpr[4], 0x1000u);
        write_output(cpu, cpu->gpr[5], 0x100u);
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2CalcGeometryShaderInputRingBufferSize") ||
        is_name(name, "GX2CalcGeometryShaderOutputRingBufferSize")) {
        u64 size = (u64)cpu->gpr[3] * 4u * 0x1000u;
        cpu->gpr[3] = size <= 0xFFFFFFFFu ? (u32)size : 0xFFFFFFFFu;
        return;
    }

    if (is_name(name, "GX2CalcFetchShaderSizeEx")) {
        u32 attributes = cpu->gpr[3];
        u32 cf_size = align_up((((attributes + 15u) / 16u) + 1u) * 8u,
                               16u);
        u64 size = (u64)cf_size + (u64)attributes * 16u;
        cpu->gpr[3] = size <= 0xFFFFFFFFu ? (u32)size : 0xFFFFFFFFu;
        return;
    }

    if (is_name(name, "GX2InitFetchShaderEx")) {
        u32 descriptor = cpu->gpr[3];
        u32 program = cpu->gpr[4];
        u32 attributes = cpu->gpr[5];
        u32 type = cpu->gpr[7];
        u32 cf_size = align_up((((attributes + 15u) / 16u) + 1u) * 8u,
                               16u);
        u64 full_size = (u64)cf_size + (u64)attributes * 16u;
        u32 size = full_size <= 0xFFFFFFFFu ? (u32)full_size : 0xFFFFFFFFu;
        guest_zero(cpu, descriptor, 0x20u);
        remember_fetch_shader(cpu, descriptor, program, attributes,
                              cpu->gpr[6]);
        GX2HostFetchShader* fetch = find_fetch_shader(descriptor, false);
        WiiULatteFetchStream streams[WIIU_LATTE_FETCH_MAX_STREAMS];
        memset(streams, 0, sizeof(streams));
        u32 stream_count = fetch ? fetch->attribute_count : 0u;
        for (u32 i = 0u; i < stream_count; i++)
            copy_attrib_stream_to_latte(&fetch->attributes[i], &streams[i]);

        u32 divisors[2] = {0u, 0u};
        u32 divisor_count = 0u;
        u32 program_size = 0u;
        bool built = size != 0xFFFFFFFFu && stream_count != 0u &&
                     wiiu_latte_fetch_build_program(
                         cpu, program, size, streams, stream_count, divisors,
                         &divisor_count, &program_size);
        if (!built) {
            guest_zero(cpu, program, size);
            if (g_latte_fetch_program_log_count++ < 16u) {
                fprintf(stderr,
                        "gx2: could not build Latte fetch program "
                        "descriptor=0x%08X streams=%u size=0x%X\n",
                        descriptor, stream_count, size);
            }
        }
        if (guest_range_valid(cpu, descriptor, 0x20u)) {
            mem_write32(cpu, descriptor + 0x00u, type);
            mem_write32(cpu, descriptor + 0x04u, 2u);
            mem_write32(cpu, descriptor + 0x08u,
                        built ? program_size : size);
            mem_write32(cpu, descriptor + 0x0Cu, program);
            mem_write32(cpu, descriptor + 0x10u, attributes);
            mem_write32(cpu, descriptor + 0x14u,
                        built ? divisor_count : 0u);
            mem_write32(cpu, descriptor + 0x18u,
                        built ? divisors[0] : 0u);
            mem_write32(cpu, descriptor + 0x1Cu,
                        built ? divisors[1] : 0u);
        }
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2GetVertexShaderGPRs") ||
        is_name(name, "GX2GetPixelShaderGPRs")) {
        u32 resources = guest_range_valid(cpu, cpu->gpr[3], 4u)
                            ? mem_read32(cpu, cpu->gpr[3])
                            : 0u;
        cpu->gpr[3] = resources & 0xFFu;
        return;
    }

    if (is_name(name, "GX2GetVertexShaderStackEntries") ||
        is_name(name, "GX2GetPixelShaderStackEntries")) {
        u32 resources = guest_range_valid(cpu, cpu->gpr[3], 4u)
                            ? mem_read32(cpu, cpu->gpr[3])
                            : 0u;
        cpu->gpr[3] = (resources >> 8) & 0xFFu;
        return;
    }

    if (is_name(name, "GX2SetupContextStateEx")) {
        u32 context = cpu->gpr[3];
        guest_zero(cpu, context, 0xA100u);
        if (guest_range_valid(cpu, context, 0xA100u)) {
            mem_write32(cpu, context + 0x9800u, cpu->gpr[4] & 1u);
            mem_write32(cpu, context + 0x9804u, 0x300u);
            switch_context_shadow(context, true);
        }
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2SetContextState")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_CONTEXT, 1u);
        if (!display_list_is_recording(cpu))
            switch_context_shadow(cpu->gpr[3], false);
        handle_display_list(cpu, name);
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2GetContextStateDisplayList")) {
        write_output(cpu, cpu->gpr[4], cpu->gpr[3] + 0x9E00u);
        u32 size = guest_range_valid(cpu, cpu->gpr[3] + 0x9804u, 4u)
                       ? mem_read32(cpu, cpu->gpr[3] + 0x9804u)
                       : 0u;
        write_output(cpu, cpu->gpr[5], size);
        cpu->gpr[3] = 0;
        return;
    }

    if (is_name(name, "GX2GetSystemTVScanMode")) {
        cpu->gpr[3] = 3u;
        return;
    }
    if (is_name(name, "GX2GetSystemTVAspectRatio")) {
        cpu->gpr[3] = 1u;
        return;
    }
    if (is_name(name, "GX2TempGetGPUVersion")) {
        cpu->gpr[3] = 2u;
        return;
    }
    if (is_name(name, "GX2GetSwapInterval")) {
        cpu->gpr[3] = g_swap_interval;
        return;
    }
    if (is_name(name, "GX2SetSwapInterval")) {
        g_swap_interval = cpu->gpr[3];
        cpu->gpr[3] = 0;
        return;
    }
    if (is_name(name, "GX2GetDRCConnectStatus")) {
        cpu->gpr[3] = 1u;
        return;
    }

    if (is_name(name, "GX2SetTVBuffer")) {
        static const u16 widths[] = {0, 640, 854, 1280, 1280, 1920};
        static const u16 heights[] = {0, 480, 480, 720, 720, 1080};
        u32 mode = cpu->gpr[5];
        if (mode < sizeof(widths) / sizeof(widths[0])) {
            g_tv_width = widths[mode];
            g_tv_height = heights[mode];
        }
        fprintf(stderr,
                "gx2: TV scan buffer=0x%08X size=0x%X mode=%u fmt=0x%X "
                "buffers=%u %ux%u\n",
                cpu->gpr[3], cpu->gpr[4], mode, cpu->gpr[6], cpu->gpr[7],
                g_tv_width, g_tv_height);
    } else if (is_name(name, "GX2SetTVScale")) {
        if (cpu->gpr[3] != 0 && cpu->gpr[4] != 0) {
            g_tv_width = cpu->gpr[3];
            g_tv_height = cpu->gpr[4];
        }
    } else if (is_name(name, "GX2SetColorBuffer")) {
        GX2RecordedCommand* command=record_gpr_command(cpu, GX2_RECORDED_SET_COLOR_BUFFER, 2u);
        if(command) read_copy_surface(cpu,cpu->gpr[3],&command->texture);
        if (!display_list_is_recording(cpu)) {
            u32 target = cpu->gpr[4];
            GX2HostColorSurface* surface=update_color_surface(cpu, cpu->gpr[3]);
            if (target < GX2_RENDER_TARGET_LIMIT)
                g_current_color_buffers[target] = surface?surface->image:0;
        }
    } else if (is_name(name, "GX2SetBlendControlReg")) {
        if (guest_range_valid(cpu,cpu->gpr[3],8u)) {
            u32 index=mem_read32(cpu,cpu->gpr[3]);
            u32 reg=mem_read32(cpu,cpu->gpr[3]+4u);
            u32 args[8]={index,reg&31u,(reg>>8u)&31u,(reg>>5u)&7u,
                (reg>>29u)&1u,(reg>>16u)&31u,(reg>>24u)&31u,(reg>>21u)&7u};
            GX2RecordedCommand* command=record_command(cpu,GX2_RECORDED_SET_BLEND_CONTROL);
            if(command) memcpy(command->args,args,sizeof(args));
            if(!display_list_is_recording(cpu)) set_blend_control_from_args(args);
        }
    } else if (is_name(name, "GX2SetBlendControl")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_BLEND_CONTROL, 8u);
        if (!display_list_is_recording(cpu))
            set_blend_control_from_args(&cpu->gpr[3]);
    } else if (is_name(name,"GX2SetPixelSampler") || is_name(name,"GX2SetVertexSampler")) {
        bool vertex=is_name(name,"GX2SetVertexSampler");
        u32 slot=cpu->gpr[4];
        if(slot<GX2_TEXTURE_UNIT_LIMIT && guest_range_valid(cpu,cpu->gpr[3],12)) {
            GX2HostSampler sampler={true,{mem_read32(cpu,cpu->gpr[3]),
                mem_read32(cpu,cpu->gpr[3]+4),mem_read32(cpu,cpu->gpr[3]+8)}};
            GX2RecordedCommand* command=record_gpr_command(cpu,vertex?GX2_RECORDED_SET_VERTEX_SAMPLER:GX2_RECORDED_SET_PIXEL_SAMPLER,0);
            if(command) { command->args[0]=slot;memcpy(&command->args[1],sampler.words,sizeof(sampler.words)); }
            if(!display_list_is_recording(cpu)) {
                if(vertex)g_vertex_samplers[slot]=sampler;else g_pixel_samplers[slot]=sampler;
            }
        }
    } else if (is_name(name, "GX2SetPixelTexture")) {
        u32 unit = cpu->gpr[4];
        if (unit < GX2_TEXTURE_UNIT_LIMIT) {
            GX2HostTexture texture;
            update_texture(cpu, cpu->gpr[3], &texture);
            GX2RecordedCommand* command =
                record_gpr_command(cpu, GX2_RECORDED_SET_PIXEL_TEXTURE, 2u);
            if (command)
                command->texture = texture;
            if (!display_list_is_recording(cpu))
                g_pixel_textures[unit] = texture;
        }
    } else if (is_name(name, "GX2SetVertexTexture")) {
        u32 unit = cpu->gpr[4];
        if (unit < GX2_TEXTURE_UNIT_LIMIT) {
            GX2HostTexture texture;
            update_texture(cpu, cpu->gpr[3], &texture);
            GX2RecordedCommand* command =
                record_gpr_command(cpu, GX2_RECORDED_SET_VERTEX_TEXTURE, 2u);
            if (command)
                command->texture = texture;
            if (!display_list_is_recording(cpu))
                g_vertex_textures[unit] = texture;
        }
    } else if (is_name(name, "GX2SetAttribBuffer")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_ATTRIB_BUFFER, 4u);
        if (!display_list_is_recording(cpu)) {
            u32 index = cpu->gpr[3];
            if (index < GX2_ATTRIB_BUFFER_LIMIT) {
                GX2HostAttribBuffer* buffer = &g_attrib_buffers[index];
                buffer->valid = cpu->gpr[6] != 0 && cpu->gpr[4] != 0;
                buffer->size = cpu->gpr[4];
                buffer->stride = cpu->gpr[5];
                buffer->data = cpu->gpr[6];
            }
        }
    } else if (is_name(name, "GX2SetFetchShader")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_FETCH_SHADER, 1u);
        if (g_shader_bind_log_count++ < 64u) {
            fprintf(stderr,
                    "gx2: bind fetch shader=0x%08X lr=0x%08X recording=%u\n",
                    cpu->gpr[3], cpu->lr,
                    display_list_is_recording(cpu) ? 1u : 0u);
            if (cpu->lr == 0x04219034u) {
                fprintf(stderr,
                        "gx2: shader-bundle state caller=0x%08X outer=0x%08X top=0x%08X dst=0x%08X "
                        "count=%u flags=0x%08X work=0x%08X vs=0x%08X ps=0x%08X\n",
                        mem_read32(cpu, cpu->gpr[1] + 60u),
                        mem_read32(cpu, cpu->gpr[1] + 84u),
                        mem_read32(cpu, cpu->gpr[1] + 116u), cpu->gpr[27],
                        cpu->gpr[28], cpu->gpr[29], cpu->gpr[30],
                        cpu->gpr[20], cpu->gpr[21]);
            } else if (cpu->lr == 0x03C51D5Cu) {
                fprintf(stderr,
                        "gx2: material state caller=0x%08X object=0x%08X "
                        "manager=0x%08X vs=0x%08X ps=0x%08X\n",
                        mem_read32(cpu, cpu->gpr[1] + 28u),
                        cpu->gpr[31] - 52u, cpu->gpr[3], cpu->gpr[29],
                        cpu->gpr[30]);
            }
        }
        if (!display_list_is_recording(cpu)) {
            g_current_fetch_shader = cpu->gpr[3];
            (void)remember_fetch_shader_from_program(cpu,
                                                      g_current_fetch_shader);
        }
    } else if (is_name(name, "GX2SetVertexShader")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_VERTEX_SHADER, 1u);
        if (g_shader_bind_log_count++ < 64u) {
            fprintf(stderr,
                    "gx2: bind vertex shader=0x%08X lr=0x%08X recording=%u\n",
                    cpu->gpr[3], cpu->lr,
                    display_list_is_recording(cpu) ? 1u : 0u);
        }
        if (!display_list_is_recording(cpu))
            g_current_vertex_shader = cpu->gpr[3];
    } else if (is_name(name, "GX2SetPixelShader")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_PIXEL_SHADER, 1u);
        if (g_shader_bind_log_count++ < 64u) {
            fprintf(stderr,
                    "gx2: bind pixel shader=0x%08X lr=0x%08X recording=%u\n",
                    cpu->gpr[3], cpu->lr,
                    display_list_is_recording(cpu) ? 1u : 0u);
        }
        if (!display_list_is_recording(cpu))
            g_current_pixel_shader = cpu->gpr[3];
    } else if (is_name(name, "GX2SetVertexUniformReg")) {
        record_uniform_register_command(cpu,
                                        GX2_RECORDED_SET_VERTEX_UNIFORM_REG);
        if (!display_list_is_recording(cpu)) {
            apply_uniform_registers(cpu, true, cpu->gpr[3], cpu->gpr[4],
                                    cpu->gpr[5]);
        }
    } else if (is_name(name, "GX2SetPixelUniformReg")) {
        record_uniform_register_command(cpu,
                                        GX2_RECORDED_SET_PIXEL_UNIFORM_REG);
        if (!display_list_is_recording(cpu)) {
            apply_uniform_registers(cpu, false, cpu->gpr[3], cpu->gpr[4],
                                    cpu->gpr[5]);
        }
    } else if (is_name(name, "GX2SetVertexUniformBlock")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_VERTEX_UNIFORM_BLOCK, 3u);
        if (!display_list_is_recording(cpu)) {
            u32 location = cpu->gpr[3];
            if (location < GX2_UNIFORM_BLOCK_LIMIT) {
                g_vertex_uniform_blocks[location].valid = cpu->gpr[5] != 0;
                g_vertex_uniform_blocks[location].size = cpu->gpr[4];
                g_vertex_uniform_blocks[location].data = cpu->gpr[5];
            }
        }
    } else if (is_name(name, "GX2SetPixelUniformBlock")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_PIXEL_UNIFORM_BLOCK, 3u);
        if (!display_list_is_recording(cpu)) {
            u32 location = cpu->gpr[3];
            if (location < GX2_UNIFORM_BLOCK_LIMIT) {
                g_pixel_uniform_blocks[location].valid = cpu->gpr[5] != 0;
                g_pixel_uniform_blocks[location].size = cpu->gpr[4];
                g_pixel_uniform_blocks[location].data = cpu->gpr[5];
            }
        }
    } else if (is_name(name,"GX2SetColorControlReg") ||
               is_name(name,"GX2SetTargetChannelMasksReg") ||
               is_name(name,"GX2SetBlendConstantColorReg") ||
               is_name(name,"GX2SetColorControl") ||
               is_name(name,"GX2SetTargetChannelMasks") ||
               is_name(name,"GX2SetBlendConstantColor")) {
        GX2RecordedKind kind=strstr(name,"ColorControl") ? GX2_RECORDED_SET_COLOR_CONTROL :
            strstr(name,"ChannelMasks") ? GX2_RECORDED_SET_CHANNEL_MASK : GX2_RECORDED_SET_BLEND_CONSTANT;
        u32 args[4]={0};
        u32 count=kind==GX2_RECORDED_SET_BLEND_CONSTANT?4u:1u;
        bool reg=strstr(name,"Reg")!=NULL;
        if(reg && !guest_range_valid(cpu,cpu->gpr[3],count*4u)) return;
        if(reg) {
            for(u32 i=0;i<count;++i) args[i]=mem_read32(cpu,cpu->gpr[3]+i*4u);
        } else if(kind==GX2_RECORDED_SET_COLOR_CONTROL) {
            args[0]=((cpu->gpr[3]&255u)<<16u)|((cpu->gpr[4]&255u)<<8u)|
                (cpu->gpr[5]?2u:0u)|(cpu->gpr[6]?0u:16u);
        } else if(kind==GX2_RECORDED_SET_CHANNEL_MASK) {
            for(u32 i=0;i<8u;++i) args[0]|=(cpu->gpr[3+i]&15u)<<(4u*i);
        } else {
            for(u32 i=0;i<4u;++i) { float value=(float)cpu->fpr[1+i];memcpy(&args[i],&value,4); }
        }
        GX2RecordedCommand* command=record_command(cpu,kind);
        if(command) memcpy(command->args,args,count*4u);
        if(!display_list_is_recording(cpu)) set_color_state(kind,args);
    } else if (is_name(name,"GX2SetRasterizerClipControl") ||
               is_name(name,"GX2SetRasterizerClipControlHalfZ") ||
               is_name(name,"GX2SetRasterizerClipControlEx")) {
        bool extended=!is_name(name,"GX2SetRasterizerClipControl");
        record_gpr_command(cpu,GX2_RECORDED_SET_CLIP_CONTROL,extended?3u:2u);
        if(!display_list_is_recording(cpu))
            g_clip_control=(!cpu->gpr[3]?(1u<<22):0) |
                (!cpu->gpr[4]?((1u<<26)|(1u<<27)):0) |
                (extended && cpu->gpr[5]?(1u<<19):0);
    } else if (is_name(name, "GX2SetViewport")) {
        record_fpr_command(cpu, GX2_RECORDED_SET_VIEWPORT, 6u);
        if (!display_list_is_recording(cpu)) {
            for (u32 i = 0; i < 6u; i++)
                g_viewport[i] = cpu->fpr[i + 1u];
            g_viewport_valid = true;
        }
    } else if (is_name(name, "GX2SetScissor")) {
        record_gpr_command(cpu, GX2_RECORDED_SET_SCISSOR, 4u);
        if (!display_list_is_recording(cpu)) {
            for (u32 i = 0; i < 4u; i++)
                g_scissor[i] = cpu->gpr[i + 3u];
            g_scissor_valid = true;
        }
    } else if (is_name(name, "GX2ClearColor") ||
               is_name(name, "GX2ClearBuffersEx")) {
        GX2RecordedCommand* command =
            record_fpr_command(cpu, GX2_RECORDED_CLEAR_COLOR, 4u);
        if (command) {
            command->args[0] = cpu->gpr[3];
            read_copy_surface(cpu,cpu->gpr[3],&command->texture);
            display_list_append(cpu, 24u);
        }
        if (!display_list_is_recording(cpu))
            clear_color_surface(cpu, cpu->gpr[3]);
    } else if (is_name(name, "GX2CopySurface")) {
        GX2RecordedCommand local = {0};
        GX2RecordedCommand* command = record_gpr_command(cpu, GX2_RECORDED_COPY_SURFACE, 6u);
        if(command) display_list_append(cpu,28u);
        if (!command) {
            command = &local;
            for (u32 i=0; i<6u; ++i) command->args[i] = cpu->gpr[3u+i];
        }
        read_copy_surface(cpu, command->args[0], &command->texture);
        read_copy_surface(cpu, command->args[3], &command->copy_destination);
        if (!display_list_is_recording(cpu)) (void)copy_surface(cpu, command);
    } else if (is_name(name, "GX2CopyColorBufferToScanBuffer")) {
        GX2RecordedCommand* command =
            record_gpr_command(cpu, GX2_RECORDED_COPY_COLOR_TO_SCAN, 2u);
        if (command) {
            read_copy_surface(cpu,cpu->gpr[3],&command->texture);
            display_list_append(cpu, 16u);
        }
        if (!display_list_is_recording(cpu)) {
            GX2HostColorSurface* surface=update_color_surface(cpu,cpu->gpr[3]);
            if ((cpu->gpr[4] & 0x3u) != 0)
                g_tv_scan_color_buffer = surface?surface->image:0;
            if ((cpu->gpr[4] & 0xCu) != 0)
                g_drc_scan_color_buffer = surface?surface->image:0;
        }
    } else if (is_name(name, "GX2SwapScanBuffers")) {
        present_scan_buffer();
        g_last_swap_tick=cpu->timebase;
    } else if (is_name(name, "GX2CallDisplayList")) {
        if (g_display_call_log_count++ < 32u) {
            fprintf(stderr, "gx2: %s address=0x%08X size=0x%X\n", name,
                    cpu->gpr[3], cpu->gpr[4]);
        }
        GX2DisplayListState* state = get_display_list_state(cpu, false);
        if (state && state->active) {
            record_gpr_command(cpu, GX2_RECORDED_CALL_DISPLAY_LIST, 2u);
            display_list_append(cpu, 16u);
        } else {
            execute_display_list(cpu, cpu->gpr[3], cpu->gpr[4], 0u);
        }
    } else if (is_name(name, "GX2CopyDisplayList")) {
        GX2DisplayListState* state = get_display_list_state(cpu, false);
        if (state && state->active) {
            // Unlike CallDisplayList, Copy embeds the source commands now.
            // Keeping an indirect reference incorrectly observes later writes
            // to the source list, a common reuse pattern for command buffers.
            GX2HostDisplayList* source =
                find_host_display_list(cpu->gpr[3], false);
            if (source && cpu->gpr[4]) {
                for (u32 i = 0; i < source->command_count; ++i) {
                    GX2RecordedCommand* command =
                        record_command(cpu, source->commands[i].kind);
                    if (!command)
                        break;
                    *command = source->commands[i];
                }
                display_list_append(cpu, cpu->gpr[4]);
            } else if (cpu->gpr[4]) {
                fprintf(stderr, "gx2: cannot copy unrecorded list 0x%08X\n",
                        cpu->gpr[3]);
            }
        } else {
            execute_display_list(cpu, cpu->gpr[3], cpu->gpr[4], 0u);
        }
    } else if (is_name(name, "GX2DirectCallDisplayList")) {
        if (g_display_call_log_count++ < 32u) {
            fprintf(stderr, "gx2: %s address=0x%08X size=0x%X\n", name,
                    cpu->gpr[3], cpu->gpr[4]);
        }
        execute_display_list(cpu, cpu->gpr[3], cpu->gpr[4], 0u);
    } else if (is_name(name, "GX2DrawEx")) {
        GX2DisplayListState* state = get_display_list_state(cpu, false);
        if (state && state->active) {
            record_gpr_command(cpu, GX2_RECORDED_DRAW, 4u);
            display_list_append(cpu, 24u);
        } else if (g_draw_log_count++ < 64u) {
            fprintf(stderr,
                    "gx2: draw primitive=%u vertices=%u first=%u "
                    "instances=%u\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6]);
        }
        if (!state || !state->active)
            timed_software_draw_texture(cpu, cpu->gpr[3], cpu->gpr[4],
                                        cpu->gpr[5],cpu->gpr[6]);
        if (!state || !state->active)
            log_draw_state(cpu);
    } else if (is_name(name, "GX2DrawIndexedEx") ||
               is_name(name, "GX2DrawIndexedEx2")) {
        bool ex2 = is_name(name, "GX2DrawIndexedEx2");
        GX2DisplayListState* state = get_display_list_state(cpu, false);
        if (state && state->active) {
            record_gpr_command(cpu, GX2_RECORDED_DRAW_INDEXED, ex2 ? 7u : 6u);
            display_list_append(cpu, 24u);
        } else if (g_draw_log_count++ < 64u) {
            fprintf(stderr,
                    "gx2: draw indexed primitive=%u indices=%u type=%u "
                    "data=0x%08X baseVertex=%d instances=%u baseInstance=%u\n",
                    cpu->gpr[3], cpu->gpr[4], cpu->gpr[5], cpu->gpr[6],
                    (s32)cpu->gpr[7], cpu->gpr[8], ex2 ? cpu->gpr[9] : 0u);
        }
        if ((!state || !state->active) && g_draw_state_log_count < 12u) {
            fprintf(stderr, "gx2: index bytes=");
            log_guest_prefix(cpu, cpu->gpr[6],
                             cpu->gpr[4] *
                                 ((cpu->gpr[5] & 1u) ? 4u : 2u));
            fputc('\n', stderr);
        }
        if ((!state || !state->active) && cpu->gpr[4] && cpu->gpr[8]) {
            if (!timed_software_draw_indexed_ui(cpu, cpu->gpr[3],
                                                cpu->gpr[4], cpu->gpr[5],
                                                cpu->gpr[6],
                                                (s32)cpu->gpr[7])) {
                timed_software_draw_indexed_texture(
                    cpu, cpu->gpr[3], cpu->gpr[4], cpu->gpr[5],
                    cpu->gpr[6], (s32)cpu->gpr[7],cpu->gpr[8],ex2?cpu->gpr[9]:0);
            }
        }
        if (!state || !state->active)
            log_draw_state(cpu);
    }

    handle_display_list(cpu, name);
    if (!is_name(name, "GX2GetCurrentDisplayList") &&
        !is_name(name, "GX2EndDisplayList") &&
        !is_name(name, "GX2GetDisplayListWriteStatus")) {
        cpu->gpr[3] = 0;
    }
    if (is_name(name, "GX2WaitForVsync") ||
        is_name(name, "GX2SwapScanBuffers")) {
        wiiu_window_pump();
    }
}
