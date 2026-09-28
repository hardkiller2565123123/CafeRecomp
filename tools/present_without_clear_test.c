#include "wiiu_gx2.h"
#include "wiiu_memory.h"
#include "wiiu_window.h"
#include "wiiu_latte_vertex.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void write_float(CPUState* cpu, u32 address, float value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    mem_write32(cpu, address, bits);
}

static int test_r8_glyph_quad(CPUState* cpu, u32 color, u32 texture,
                             u32 index_type, s32 base_vertex, bool ex2,
                             bool recorded, u32 instances, u32 minimum_coverage) {
    const u32 vertices = color + 512, indices = color + 640;
    mem_write32(cpu, color + 4, 16);
    mem_write32(cpu, color + 8, 16);
    mem_write32(cpu, color + 0x20, 1024);
    mem_write32(cpu, color + 0x3C, 16);
    cpu->gpr[3] = color; cpu->gpr[4] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetColorBuffer");
    cpu->fpr[1] = cpu->fpr[2] = cpu->fpr[3] = 0;
    cpu->fpr[4] = 1;
    cpu->gpr[3] = color;
    wiiu_gx2_handle_import(cpu, "GX2ClearColor");

    mem_write32(cpu, texture + 0x14, 1); /* R8 coverage atlas. */
    mem_write32(cpu, texture + 0x20, 4);
    mem_write32(cpu, texture + 0x84, 0x05050500); /* white RGB, R alpha */
    mem_write32(cpu, texture + 1024, 0xFFFFFFFF);
    cpu->gpr[3] = texture; cpu->gpr[4] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetPixelTexture");
    const float positions[4][2] = {{-4,4},{4,4},{-4,-4},{4,-4}};
    const u16 order[4] = {0,2,3,1};
    for (u32 i = 0; i < 4; ++i) {
        write_float(cpu, vertices + i*28, positions[i][0]);
        write_float(cpu, vertices + i*28 + 4, positions[i][1]);
        write_float(cpu, vertices + i*28 + 8, 0);
        mem_write32(cpu, vertices + i*28 + 12, 0xFFFFFFFF);
        write_float(cpu, vertices + i*28 + 16, (float)(i & 1));
        write_float(cpu, vertices + i*28 + 20, (float)(i >> 1));
        u32 index = order[i] + (base_vertex < 0 ? (u32)-base_vertex : 0u);
        if (index_type == 4) mem_write16(cpu, indices + i*2, (u16)index);
        else if (index_type == 9) mem_write32(cpu, indices + i*4, index);
        else for (u32 j = 0; j < (index_type == 0 ? 2u : 4u); ++j)
            mem_write8(cpu, indices + i*(index_type == 0 ? 2u : 4u) + j,
                       (u8)(index >> (j*8)));
    }
    u32 padding = base_vertex > 0 ? (u32)base_vertex * 28u : 0u;
    cpu->gpr[3] = 0; cpu->gpr[4] = 112 + padding;
    cpu->gpr[5] = 28; cpu->gpr[6] = vertices - padding;
    wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    if (recorded) {
        cpu->gpr[3] = color + 7000; cpu->gpr[4] = 256;
        wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
    }
    cpu->gpr[3] = 19; cpu->gpr[4] = 4; cpu->gpr[5] = index_type;
    cpu->gpr[6] = indices; cpu->gpr[7] = (u32)base_vertex;
    cpu->gpr[8] = instances; cpu->gpr[9] = 0;
    wiiu_gx2_handle_import(cpu, ex2 ? "GX2DrawIndexedEx2" : "GX2DrawIndexedEx");
    if (recorded) {
        cpu->gpr[3] = color + 7000;
        wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
        cpu->gpr[4] = cpu->gpr[3]; cpu->gpr[3] = color + 7000;
        wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
    }
    u8 pixels[16*16*4];
    if (!wiiu_gx2_readback_color_buffer(color, pixels, 16, 16, 64)) return 1;
    /* The glyph occupies the center, never the whole render target. */
    if (pixels[0] != 0 || (instances ? pixels[(8*16+8)*4] < minimum_coverage :
                                                    pixels[(8*16+8)*4] != 0)) {
        fprintf(stderr, "R8 glyph was not confined to its quad: corner=%u center=%u\n",
                pixels[0], pixels[(8*16+8)*4]);
        return 1;
    }
    /* An unsupported triangle must never expand the still-bound glyph atlas
       into a fullscreen substitute for the game's shader. */
    cpu->gpr[3] = 4; cpu->gpr[4] = 3; cpu->gpr[5] = index_type;
    cpu->gpr[6] = indices; cpu->gpr[7] = (u32)base_vertex;
    cpu->gpr[8] = 1; cpu->gpr[9] = 0;
    wiiu_gx2_handle_import(cpu, "GX2DrawIndexedEx2");
    u8 after[sizeof(pixels)];
    if (!wiiu_gx2_readback_color_buffer(color, after, 16, 16, 64) ||
        memcmp(pixels, after, sizeof(pixels)) != 0) {
        fprintf(stderr, "Unsupported triangle blitted the font atlas\n");
        return 1;
    }
    return 0;
}

static int test_context_restore(CPUState* cpu, u32 color, u32 texture) {
    const u32 first = color + 0x10000, second = color + 0x1B000;
    cpu->gpr[3] = first; cpu->gpr[4] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetupContextStateEx");
    if (test_r8_glyph_quad(cpu, color, texture, 4, 0, true, false, 1,250)) return 1;
    cpu->gpr[3] = second; cpu->gpr[4] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetupContextStateEx");
    cpu->fpr[1] = cpu->fpr[2] = 0; cpu->fpr[3] = cpu->fpr[4] = 1;
    cpu->fpr[5] = 0; cpu->fpr[6] = 1;
    wiiu_gx2_handle_import(cpu, "GX2SetViewport");
    cpu->gpr[3] = cpu->gpr[4] = 0; cpu->gpr[5] = cpu->gpr[6] = 1;
    wiiu_gx2_handle_import(cpu, "GX2SetScissor");
    cpu->gpr[3] = first;
    wiiu_gx2_handle_import(cpu, "GX2SetContextState");

    /* Recording a context switch cannot change the current live state. */
    cpu->gpr[3] = color + 7000; cpu->gpr[4] = 256;
    wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
    cpu->gpr[3] = second;
    wiiu_gx2_handle_import(cpu, "GX2SetContextState");
    cpu->gpr[3] = color + 7000;
    wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
    u32 list_size = cpu->gpr[3];

    for (u32 step = 0; step < 3; ++step) {
        if (step == 1) {
            cpu->gpr[3] = color + 7000; cpu->gpr[4] = list_size;
            wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
        } else if (step == 2) {
            cpu->gpr[3] = first; cpu->gpr[4] = color + 7100;
            cpu->gpr[5] = color + 7104;
            wiiu_gx2_handle_import(cpu, "GX2GetContextStateDisplayList");
            cpu->gpr[3] = mem_read32(cpu, color + 7100);
            cpu->gpr[4] = mem_read32(cpu, color + 7104);
            wiiu_gx2_handle_import(cpu, "GX2DirectCallDisplayList");
        }
        cpu->gpr[3] = color;
        cpu->fpr[1] = cpu->fpr[2] = cpu->fpr[3] = 0; cpu->fpr[4] = 1;
        wiiu_gx2_handle_import(cpu, "GX2ClearColor");
        cpu->gpr[3] = 19; cpu->gpr[4] = 4; cpu->gpr[5] = 4;
        cpu->gpr[6] = color + 640; cpu->gpr[7] = 0;
        cpu->gpr[8] = 1; cpu->gpr[9] = 0;
        wiiu_gx2_handle_import(cpu, "GX2DrawIndexedEx2");
        u8 pixels[16*16*4];
        if (!wiiu_gx2_readback_color_buffer(color, pixels, 16, 16, 64) || pixels[0] ||
            (step == 1 ? pixels[(8*16+8)*4] != 0 : pixels[(8*16+8)*4] < 250)) {
            fprintf(stderr, "Context restoration failed at step %u\n", step);
            return 1;
        }
    }
    cpu->gpr[3] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetContextState");
    return 0;
}

static int check_bound_glyph(CPUState* cpu, u32 color, bool visible) {
    cpu->gpr[3] = color;
    cpu->fpr[1] = cpu->fpr[2] = cpu->fpr[3] = 0; cpu->fpr[4] = 1;
    wiiu_gx2_handle_import(cpu, "GX2ClearColor");
    cpu->gpr[3] = 19; cpu->gpr[4] = 4; cpu->gpr[5] = 4;
    cpu->gpr[6] = color + 640; cpu->gpr[7] = 0;
    cpu->gpr[8] = 1; cpu->gpr[9] = 0;
    wiiu_gx2_handle_import(cpu, "GX2DrawIndexedEx2");
    u8 pixels[16*16*4];
    return !wiiu_gx2_readback_color_buffer(color, pixels, 16, 16, 64) ||
        pixels[0] || (visible ? pixels[(8*16+8)*4] < 250 :
                               pixels[(8*16+8)*4] != 0);
}

static int test_retained_display_lists(CPUState* cpu, u32 color, u32 texture) {
    if (test_r8_glyph_quad(cpu, color, texture, 4, 0, true, false, 1,250)) return 1;
    const u32 oldest = color + 0x5000;
    for (u32 i = 0; i < 300; ++i) {
        cpu->gpr[3] = oldest + i*32; cpu->gpr[4] = 32;
        wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
        cpu->gpr[3] = 0; cpu->gpr[4] = i == 0 ? 112 : 0;
        cpu->gpr[5] = 28; cpu->gpr[6] = i == 0 ? color + 512 : 0;
        wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
        cpu->gpr[3] = oldest + i*32;
        wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
    }
    cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = cpu->gpr[6] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    cpu->gpr[3] = oldest; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
    if (check_bound_glyph(cpu, color, true)) {
        fprintf(stderr, "A still-live display list was evicted after 300 recordings\n");
        return 1;
    }
    /* Copy submits immediately outside recording, and embeds a snapshot when
       recording: overwriting its source later must not change the copy. */
    cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = cpu->gpr[6] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    cpu->gpr[3] = oldest; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2CopyDisplayList");
    if (check_bound_glyph(cpu, color, true)) {
        fprintf(stderr, "Immediate display-list copy did not submit commands\n");
        return 1;
    }
    const u32 copied = color + 0x8000;
    cpu->gpr[3] = copied; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
    cpu->gpr[3] = oldest; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2CopyDisplayList");
    cpu->gpr[3] = copied;
    wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
    cpu->gpr[3] = oldest; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
    cpu->gpr[3] = cpu->gpr[4] = cpu->gpr[5] = cpu->gpr[6] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    cpu->gpr[3] = oldest;
    wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
    cpu->gpr[3] = oldest; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
    if (check_bound_glyph(cpu, color, false)) return 1;
    cpu->gpr[3] = copied; cpu->gpr[4] = 32;
    wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
    if (check_bound_glyph(cpu, color, true)) {
        fprintf(stderr, "Recorded copy changed after its source was overwritten\n");
        return 1;
    }
    /* Large command streams must retain commands after the former 256 limit. */
    cpu->gpr[3] = copied; cpu->gpr[4] = 0x2000;
    wiiu_gx2_handle_import(cpu, "GX2BeginDisplayList");
    for (u32 i = 0; i < 300; ++i) {
        cpu->gpr[3] = 0; cpu->gpr[4] = i == 299 ? 112 : 0;
        cpu->gpr[5] = 28; cpu->gpr[6] = i == 299 ? color + 512 : 0;
        wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    }
    cpu->gpr[3] = copied;
    wiiu_gx2_handle_import(cpu, "GX2EndDisplayList");
    u32 large_size = cpu->gpr[3];
    cpu->gpr[3] = copied; cpu->gpr[4] = large_size;
    wiiu_gx2_handle_import(cpu, "GX2CallDisplayList");
    if (check_bound_glyph(cpu, color, true)) {
        fprintf(stderr, "Large display list truncated after 256 commands\n");
        return 1;
    }
    return 0;
}

static int test_layout_shader(CPUState* cpu, u32 base, const char* path, bool bc4) {
    FILE* file = fopen(path, "rb");
    if (!file) return 1;
    u8 bytes[4096];
    size_t size = fread(bytes, 1, sizeof(bytes), file);
    fclose(file);
    if (!size || size == sizeof(bytes)) return 1;
    const u32 shader = base + 768, program = base + 2048;
    mem_write32(cpu, shader + 0xD0, (u32)size);
    mem_write32(cpu, shader + 0xD4, program);
    for (u32 i = 0; i < size; ++i) mem_write8(cpu, program + i, bytes[i]);
    u32 clause = ((u32)bytes[8] | ((u32)bytes[9]<<8) |
                  ((u32)bytes[10]<<16) | ((u32)bytes[11]<<24)) & 0x3FFFFFu;
    u32 shift = clause == 96 ? 4 : 0; /* Captured early/later shader variants. */
    float values[64] = {0};
    float* c = values + shift;
    c[12] = c[17] = c[22] = 1;
    c[23] = -1000;
    c[28] = 1000.0f/640.0f; c[33] = 1000.0f/360.0f;
    c[38] = -1; c[42] = -1;
    c[56] = 1280; c[57] = 720;
    c[58] = -640; c[59] = 360;
    u32 uniforms[64]; u8 valid[64];
    memcpy(uniforms, values, sizeof(values)); memset(valid, 1, sizeof(valid));
    const float uv[3][2] = {{0,0},{1,1},{0.5f,0.5f}};
    const float expected[3][2] = {{-1,1},{1,-1},{0,0}};
    float clip[4];
    for (u32 i = 0; i < 3; ++i) {
        if (!wiiu_latte_layout_position(cpu, shader, uniforms, valid, 64, uv[i], clip) ||
            fabsf(clip[3] - 1000) > 0.001f ||
            fabsf(clip[0]/clip[3] - expected[i][0]) > 0.0001f ||
            fabsf(clip[1]/clip[3] - expected[i][1]) > 0.0001f) {
            fprintf(stderr, "Captured layout shader projection failed at corner %u\n", i);
            return 1;
        }
    }
    /* Exercise the whole layout draw as well: both triangles must cover the
       center pane, not the whole target. This catches both corner order and
       accidental fullscreen-fallback regressions. */
    c[56] = 640; c[57] = 360; c[58] = -320; c[59] = 180;
    memcpy(uniforms, values, sizeof(values));
    cpu->gpr[3] = base;
    cpu->fpr[1] = cpu->fpr[2] = cpu->fpr[3] = 0; cpu->fpr[4] = 1;
    wiiu_gx2_handle_import(cpu, "GX2ClearColor");
    u32 texture = base + 256, vertices = base + 6000, indices = base + 6040;
    mem_write32(cpu, texture + 0x14, bc4 ? 0x34 : 0x1A);
    mem_write32(cpu, texture + 0x20, bc4 ? 8 : 16);
    mem_write32(cpu, texture + 0x84, bc4 ? 0x00000005 : 0x00010203);
    for (u32 i = 0; i < 4; ++i) mem_write32(cpu, texture + 1024 + i*4, 0xFFFFFFFF);
    cpu->gpr[3] = texture; cpu->gpr[4] = 0;
    wiiu_gx2_handle_import(cpu, "GX2SetPixelTexture");
    cpu->gpr[3] = shader;
    wiiu_gx2_handle_import(cpu, "GX2SetVertexShader");
    for (u32 i = 0; i < 64; ++i) mem_write32(cpu, base + 6500 + i*4, uniforms[i]);
    cpu->gpr[3] = 0; cpu->gpr[4] = 64; cpu->gpr[5] = base + 6500;
    wiiu_gx2_handle_import(cpu, "GX2SetVertexUniformReg");
    const u16 order[4] = {1,0,2,3};
    for (u32 i = 0; i < 4; ++i) {
        write_float(cpu, vertices + i*8, (float)(i & 1));
        write_float(cpu, vertices + i*8 + 4, (float)(i >> 1));
        mem_write16(cpu, indices + i*2, order[i]);
    }
    cpu->gpr[3] = 0; cpu->gpr[4] = 32; cpu->gpr[5] = 8; cpu->gpr[6] = vertices;
    wiiu_gx2_handle_import(cpu, "GX2SetAttribBuffer");
    cpu->gpr[3] = 19; cpu->gpr[4] = 4; cpu->gpr[5] = 4;
    cpu->gpr[6] = indices; cpu->gpr[7] = 0; cpu->gpr[8] = 1;
    wiiu_gx2_handle_import(cpu, "GX2DrawIndexedEx");
    u8 pixels[16*16*4];
    if (!wiiu_gx2_readback_color_buffer(base, pixels, 16, 16, 64)) return 1;
    for (u32 i = 0; i < 16*16; ++i) {
        u32 x = i % 16, y = i / 16;
        bool inside = x >= 4 && x < 12 && y >= 4 && y < 12;
        if ((inside && pixels[i*4] < 250) || (!inside && pixels[i*4] != 0)) {
            fprintf(stderr, "Layout quad coverage missing at pixel %u\n", i);
            return 1;
        }
    }
    valid[23 + shift] = 0;
    if (wiiu_latte_layout_position(cpu, shader, uniforms, valid, 64, uv[0], clip)) return 1;
    valid[23 + shift] = 1;
    mem_write8(cpu, program + clause*8, bytes[clause*8] ^ 1);
    if (wiiu_latte_layout_position(cpu, shader, uniforms, valid, 64, uv[0], clip)) return 1;
    fprintf(stderr, "Captured layout shader: %s corner projection and rejection tests passed\n",bc4?"BC4":"RGBA");
    return 0;
}

static void write_words_le(CPUState* cpu, u32 at, const u32* words, u32 count) {
    for(u32 i=0;i<count;++i)
        for(u32 b=0;b<4u;++b) mem_write8(cpu,at+i*4u+b,(u8)(words[i]>>(b*8u)));
}

static int test_constant_triangle(CPUState* cpu, u32 color) {
    const u32 vs=color+0x10000, ps=vs+512, vp=vs+1024, pp=vs+1536;
    const u32 vertices=vs+2048, indices=vs+2200, uniforms=vs+2300;
    const u32 vs_cf[]={0,0x09800000,0x20,0xA0100000,0xA03C,0x94000688,0x4000,0x94200FFF};
    const u32 vs_alu[]={0x001F0001,0x00200CB0,0x001F0401,0x20200CB0,
        0x001F0100,0x40200C90,0x801F00FD,0x60200C90,0x3F800000,0};
    const u32 ps_cf[]={0x20,0xA00C0000,0,0x94200688};
    const u32 ps_alu[]={0x001F0100,0x00000C90,0x001F0500,0x20000C90,
        0x001F0900,0x40000C90,0x801F0D00,0x60000C90};
    wiiu_gx2_reset(); /* In particular: no texture is bound. */
    mem_write32(cpu,vs+0xD0,296); mem_write32(cpu,vs+0xD4,vp);
    mem_write32(cpu,ps+0xA4,288); mem_write32(cpu,ps+0xA8,pp);
    write_words_le(cpu,vp,vs_cf,8); write_words_le(cpu,vp+256,vs_alu,10);
    write_words_le(cpu,pp,ps_cf,4); write_words_le(cpu,pp+256,ps_alu,8);
    cpu->gpr[3]=vs; wiiu_gx2_handle_import(cpu,"GX2SetVertexShader");
    cpu->gpr[3]=ps; wiiu_gx2_handle_import(cpu,"GX2SetPixelShader");
    cpu->gpr[3]=color; cpu->gpr[4]=0; wiiu_gx2_handle_import(cpu,"GX2SetColorBuffer");
    const float xy[3][2]={{-.5f,.5f},{1.5f,.5f},{-.5f,-1.5f}};
    for(u32 i=0;i<3u;++i) {
        write_float(cpu,vertices+i*32,xy[i][0]); write_float(cpu,vertices+i*32+4,xy[i][1]);
        mem_write16(cpu,indices+i*2,(u16)i);
    }
    cpu->gpr[3]=0; cpu->gpr[4]=96; cpu->gpr[5]=32; cpu->gpr[6]=vertices;
    wiiu_gx2_handle_import(cpu,"GX2SetAttribBuffer");
    write_float(cpu,uniforms,1); write_float(cpu,uniforms+4,0);
    write_float(cpu,uniforms+8,0); write_float(cpu,uniforms+12,.5f);
    cpu->gpr[3]=0; cpu->gpr[4]=4; cpu->gpr[5]=uniforms;
    wiiu_gx2_handle_import(cpu,"GX2SetPixelUniformReg");
    /* Direct/indexed, recorded, scissored, shader mismatch and non-covering
       geometry. None may depend on a stale font-atlas binding. */
    for(u32 mode=0;mode<12u;++mode) {
        cpu->gpr[3]=color;
        cpu->fpr[1]=cpu->fpr[2]=cpu->fpr[3]=0; cpu->fpr[4]=1;
        wiiu_gx2_handle_import(cpu,"GX2ClearColor");
        cpu->gpr[3]=mode==3?4:0; cpu->gpr[4]=mode==3?4:0;
        cpu->gpr[5]=mode==3?8:16; cpu->gpr[6]=mode==3?8:16;
        wiiu_gx2_handle_import(cpu,"GX2SetScissor");
        if(mode==4) mem_write8(cpu,pp+256,1); /* Unsupported source register. */
        if(mode==5) {
            mem_write8(cpu,pp+256,0);
            write_float(cpu,vertices+32,0); /* Triangle no longer covers viewport. */
        }
        if(mode>=6u) write_float(cpu,vertices+32,1.5f);
        if(mode==2 || mode>=7) {
            cpu->gpr[3]=vs+2600;cpu->gpr[4]=256;
            wiiu_gx2_handle_import(cpu,"GX2BeginDisplayList");
        }
        if(mode>=6u) {
            mem_write32(cpu,vs+2500,0);
            mem_write32(cpu,vs+2504,mode==6u?0x20010001u:0x25010504u);
            cpu->gpr[3]=vs+2500;
            wiiu_gx2_handle_import(cpu,"GX2SetBlendControlReg");
        }
        if(mode>=8u) {
            mem_write32(cpu,vs+2520,mode==8u?0xCC0000u:mode==9u?0xCC0110u:0xCC0100u);
            cpu->gpr[3]=vs+2520;wiiu_gx2_handle_import(cpu,"GX2SetColorControlReg");
            mem_write32(cpu,vs+2524,mode==10u?2u:15u);
            cpu->gpr[3]=vs+2524;wiiu_gx2_handle_import(cpu,"GX2SetTargetChannelMasksReg");
            if(mode==11u) {
                mem_write32(cpu,vs+2504,0x2001000Du); /* CONSTANT_COLOR, ZERO */
                cpu->gpr[3]=vs+2500;wiiu_gx2_handle_import(cpu,"GX2SetBlendControlReg");
                for(u32 c=0;c<4u;++c) write_float(cpu,vs+2530+c*4u,.25f);
                cpu->gpr[3]=vs+2530;wiiu_gx2_handle_import(cpu,"GX2SetBlendConstantColorReg");
            }
        }
        cpu->gpr[3]=4; cpu->gpr[4]=3;
        if(mode==0) {
            cpu->gpr[5]=0;cpu->gpr[6]=1;
            wiiu_gx2_handle_import(cpu,"GX2DrawEx");
        } else {
            cpu->gpr[5]=4;cpu->gpr[6]=indices;cpu->gpr[7]=0;cpu->gpr[8]=1;
            wiiu_gx2_handle_import(cpu,"GX2DrawIndexedEx");
        }
        if(mode==2 || mode>=7) {
            cpu->gpr[3]=vs+2600;wiiu_gx2_handle_import(cpu,"GX2EndDisplayList");
            if(mode==7) mem_write32(cpu,vs+2504,0); /* Replay owns the original bits. */
            if(mode>=8u) for(u32 b=2520;b<2546u;++b) mem_write8(cpu,vs+b,0);
            cpu->gpr[4]=cpu->gpr[3];cpu->gpr[3]=vs+2600;
            wiiu_gx2_handle_import(cpu,"GX2CallDisplayList");
        }
        u8 pixels[16*16*4];
        if(!wiiu_gx2_readback_color_buffer(color,pixels,16,16,64)) return 1;
        for(u32 i=0;i<256u;++i) {
            u32 x=i%16u,y=i/16u;
            bool inside=(mode<4u || mode>=6u) && (mode!=3u || (x>=4u&&x<12u&&y>=4u&&y<12u));
            u32 red=pixels[i*4u+2u];
            if(mode>=8u) {
                u32 expected=mode==8u?255u:mode==11u?64u:0u;
                if(red!=expected || pixels[i*4u] || pixels[i*4u+1u]) {
                    fprintf(stderr,"Color state mode=%u red=%u expected=%u\n",mode,red,expected);return 1;
                }
                continue;
            }
            if(pixels[i*4u] || pixels[i*4u+1u] ||
                (inside ? (mode==6u ? red!=255u : (red<126u||red>130u)) : red!=0u)) {
                fprintf(stderr,"Constant triangle mode=%u pixel=%u red=%u\n",mode,i,red);
                return 1;
            }
        }
    }
    fprintf(stderr,"Textureless constant triangle: blending, scissor, display-list and rejection tests passed\n");
    return 0;
}

static int test_captured_font(CPUState* cpu,u32 base,const char* path) {
    u8 bytes[544];
    FILE* file=fopen(path,"rb");
    if(!file) return 1;
    size_t size=fread(bytes,1,sizeof(bytes),file); fclose(file);
    if(size!=sizeof(bytes)) return 1;
    wiiu_gx2_reset();
    u32 descriptor=base+0x18000,program=descriptor+256,uniforms=program+1024;
    for(u32 i=0;i<size;++i) mem_write8(cpu,program+i,bytes[i]);
    mem_write32(cpu,descriptor+0xA4,(u32)size);
    mem_write32(cpu,descriptor+0xA8,program);
    cpu->gpr[3]=descriptor; wiiu_gx2_handle_import(cpu,"GX2SetPixelShader");
    for(u32 i=0;i<16u;++i) write_float(cpu,uniforms+i*4u,(i/4u)%2u?1.0f:0.0f);
    cpu->gpr[3]=0;cpu->gpr[4]=16;cpu->gpr[5]=uniforms;
    wiiu_gx2_handle_import(cpu,"GX2SetPixelUniformReg");
    if(test_r8_glyph_quad(cpu,base,base+256,4,0,false,false,1,126)) return 1;
    u8 pixels[16*16*4];
    if(!wiiu_gx2_readback_color_buffer(base,pixels,16,16,64) || pixels[(8*16+8)*4]>130u) {
        fprintf(stderr,"Captured font program fell back to raw atlas coverage\n"); return 1;
    }
    fprintf(stderr,"Captured font shader: guest binding, material and coverage integration passed\n");
    return 0;
}

static int test_material_pixels(u32 surface) {
    WiiUWindowGpuVertex quad[4]={{0,0,0,0,255,255,255,255},
        {16,0,1,0,255,255,255,255},{16,16,1,1,255,255,255,255},
        {0,16,0,1,255,255,255,255}};
    const u8 texture[4]={255,255,255,128};
    WiiUWindowGpuMaterial material={{.25f,0,0,.5f},{0,.5f,0,0},0,{0}};
    for(u32 mode=0;mode<3u;++mode) {
        if(!wiiu_window_gpu_clear(surface,16,16,0,0,0,255)) return 1;
        material.font_coverage=mode?1.0f:0.0f;
        u8 sampled[4]; memcpy(sampled,texture,4);
        if(mode==2) sampled[3]=255;
        if(!wiiu_window_gpu_draw_material(surface,16,16,sampled,1,1,100+mode,
            quad,1,&material,NULL,false,0,0,16,16)) return 1;
        u8 pixels[16*16*4];
        if(!wiiu_window_gpu_readback(surface,pixels,16,16,64)) return 1;
        u32 r=pixels[2],g=pixels[1],b=pixels[0];
        bool ok= mode==0 ? (r>=31&&r<=33&&g>=63&&g<=65&&b==0) :
                 mode==1 ? (r==0&&g==0&&b==0) :
                           (r>=127&&r<=129&&g==r&&b==r);
        if(!ok) { fprintf(stderr,"Material mode=%u rgb=%u,%u,%u\n",mode,r,g,b);return 1; }
    }
    fprintf(stderr,"Material shader: tint, transparency and font coverage tests passed\n");
    return 0;
}

static int test_texture_refresh(CPUState* cpu, u32 base) {
    wiiu_gx2_reset();
    u32 color=base,texture=base+256,data=base+1024;
    for(u32 d=color;d<=texture;d+=256u) {
        mem_write32(cpu,d+4,2);mem_write32(cpu,d+8,2);mem_write32(cpu,d+12,1);
        mem_write32(cpu,d+0x14,0x1A);mem_write32(cpu,d+0x20,16);
        mem_write32(cpu,d+0x24,d==texture?data:data+64);
        mem_write32(cpu,d+0x30,16);mem_write32(cpu,d+0x3C,2);
        mem_write32(cpu,d+0x84,0x00010203);
    }
    cpu->gpr[3]=color;cpu->gpr[4]=0;wiiu_gx2_handle_import(cpu,"GX2SetColorBuffer");
    cpu->gpr[3]=texture;cpu->gpr[4]=0;wiiu_gx2_handle_import(cpu,"GX2SetPixelTexture");
    for(u32 pass=0;pass<3;++pass) {
        if(pass!=1) {
            for(u32 i=0;i<4;++i) mem_write32(cpu,data+i*4,pass==0?0xFF0000FFu:0x0000FFFFu);
            cpu->gpr[3]=0x40;cpu->gpr[4]=data;cpu->gpr[5]=16;
            wiiu_gx2_handle_import(cpu,"GX2Invalidate");
        }
        cpu->gpr[3]=19;cpu->gpr[4]=4;cpu->gpr[5]=0;cpu->gpr[6]=1;
        wiiu_gx2_handle_import(cpu,"GX2DrawEx");
        u8 pixels[16];
        if(!wiiu_gx2_readback_color_buffer(color,pixels,2,2,8)) return 1;
        if(pixels[2]!=(pass==2?0:255) || pixels[0]!=(pass==2?255:0)) {
            fprintf(stderr,"Texture cache refresh failed pass=%u\n",pass);return 1;
        }
    }
    fprintf(stderr,"Texture cache: repeated draw and same-frame invalidation passed\n");
    return 0;
}

static int test_completion_status(CPUState* cpu, u32 base) {
    wiiu_gx2_reset();
    cpu->timebase=0x123456789ABCull;
    cpu->gpr[3]=base;
    wiiu_gx2_handle_import(cpu,"GX2SampleTopGPUCycle");
    if(mem_read32(cpu,base)!=0x1234u || mem_read32(cpu,base+4)!=0x56789ABCu) return 1;
    cpu->gpr[3]=base;
    wiiu_gx2_handle_import(cpu,"GX2SampleBottomGPUCycle");
    if(mem_read32(cpu,base)!=0xFFFFFFFFu || mem_read32(cpu,base+4)!=0xFFFFFFFFu) return 1;
    wiiu_gx2_handle_import(cpu,"GX2SwapScanBuffers");
    cpu->gpr[3]=base;cpu->gpr[4]=base+4;cpu->gpr[5]=base+8;cpu->gpr[6]=base+16;
    wiiu_gx2_handle_import(cpu,"GX2GetSwapStatus");
    if(mem_read32(cpu,base)!=1u || mem_read32(cpu,base+4)!=1u ||
       mem_read32(cpu,base+8)!=0x1234u || mem_read32(cpu,base+12)!=0x56789ABCu ||
       mem_read32(cpu,base+16)!=0x1234u || mem_read32(cpu,base+20)!=0x56789ABCu) return 1;
    if(!wiiu_window_gpu_clear(base,16,16,1,2,3,255)) return 1;
    wiiu_gx2_handle_import(cpu,"GX2DrawDone");
    if(cpu->gpr[3]!=1u) return 1;
    fprintf(stderr,"Completion status: swap counts, 64-bit timestamps and real GPU completion passed\n");
    return 0;
}

static int test_surface_copy(CPUState* cpu, u32 base) {
    wiiu_gx2_reset();
    const u32 src=base+0x20000,dst=src+256,list=src+1024;
    for(u32 d=src;d<=dst;d+=256) {
        mem_write32(cpu,d+4,16);mem_write32(cpu,d+8,16);
        mem_write32(cpu,d+12,1);mem_write32(cpu,d+0x14,0x1A);
        mem_write32(cpu,d+0x20,1024);mem_write32(cpu,d+0x24,d+4096);
        mem_write32(cpu,d+0x30,16);mem_write32(cpu,d+0x3C,16);
    }
    /* Deliberately restrictive draw state must not affect the copy. */
    cpu->gpr[3]=cpu->gpr[4]=0;cpu->gpr[5]=cpu->gpr[6]=1;
    wiiu_gx2_handle_import(cpu,"GX2SetScissor");
    cpu->gpr[3]=list;cpu->gpr[4]=256;
    wiiu_gx2_handle_import(cpu,"GX2BeginDisplayList");
    cpu->gpr[3]=src;cpu->gpr[4]=cpu->gpr[5]=0;
    cpu->gpr[6]=dst;cpu->gpr[7]=cpu->gpr[8]=0;
    wiiu_gx2_handle_import(cpu,"GX2CopySurface");
    cpu->gpr[3]=list;wiiu_gx2_handle_import(cpu,"GX2EndDisplayList");
    u32 length=cpu->gpr[3];
    /* Descriptor scratch reuse cannot redirect an already recorded copy. */
    mem_write32(cpu,dst+0x24,0);
    for(u32 pass=0;pass<2;++pass) {
        cpu->gpr[3]=src;cpu->fpr[1]=pass?0:1;cpu->fpr[2]=pass?1:0;
        cpu->fpr[3]=0;cpu->fpr[4]=0.5;
        wiiu_gx2_handle_import(cpu,"GX2ClearColor");
        cpu->gpr[3]=list;cpu->gpr[4]=length;
        wiiu_gx2_handle_import(cpu,"GX2CallDisplayList");
        /* Changing source after copy must not change destination. */
        cpu->gpr[3]=src;cpu->fpr[1]=cpu->fpr[2]=0;cpu->fpr[3]=1;
        wiiu_gx2_handle_import(cpu,"GX2ClearColor");
        u8 pixels[16*16*4];
        if(!wiiu_gx2_readback_color_buffer(dst,pixels,16,16,64)) return 1;
        for(u32 i=0;i<256;++i) {
            u8* p=pixels+i*4;
            if(p[0] || p[1]!=(pass?255:0) || p[2]!=(pass?0:255) || p[3]!=128) {
                fprintf(stderr,"Surface copy failed pass=%u pixel=%u rgba=%u/%u/%u/%u\n",
                    pass,i,p[2],p[1],p[0],p[3]); return 1;
            }
        }
    }
    fprintf(stderr,"Surface copy: replay-time pixels, descriptor snapshot, alpha and scissor independence passed\n");
    wiiu_gx2_reset();
    mem_write32(cpu,src+0x24,src+0x3000);
    mem_write32(cpu,dst+0x24,src+0x5000);
    mem_write32(cpu,dst+0x30,2); /* Linear -> micro-tiled -> linear, without color conversion. */
    for(u32 i=0;i<1024u;++i) mem_write8(cpu,src+0x3000+i,(u8)(i*37u+i/16u));
    cpu->gpr[3]=src;cpu->gpr[4]=cpu->gpr[5]=0;
    cpu->gpr[6]=dst;cpu->gpr[7]=cpu->gpr[8]=0;
    wiiu_gx2_handle_import(cpu,"GX2CopySurface");
    mem_write32(cpu,src+0x24,src+0x7000);
    cpu->gpr[3]=dst;cpu->gpr[4]=cpu->gpr[5]=0;
    cpu->gpr[6]=src;cpu->gpr[7]=cpu->gpr[8]=0;
    wiiu_gx2_handle_import(cpu,"GX2CopySurface");
    for(u32 i=0;i<1024u;++i) if(mem_read8(cpu,src+0x7000+i)!=(u8)(i*37u+i/16u)) {
        fprintf(stderr,"Raw surface tiling round-trip failed at %u\n",i);return 1;
    }
    fprintf(stderr,"Raw surface copy: lossless tiled/linear storage round-trip passed\n");
    return 0;
}

static int test_color_descriptor_reuse(CPUState* cpu,u32 base) {
    const u32 image_a=base+0x1000,image_b=base+0x2000,image_c=base+0x3000;
    const u32 list=base+0x4000;
    for(u32 recorded=0;recorded<2;++recorded) {
        wiiu_gx2_reset();
        mem_write32(cpu,base+4,2);mem_write32(cpu,base+8,2);
        mem_write32(cpu,base+12,1);mem_write32(cpu,base+0x14,0x1A);
        mem_write32(cpu,base+0x20,16);mem_write32(cpu,base+0x30,16);
        mem_write32(cpu,base+0x3C,2);
        if(recorded) {
            cpu->gpr[3]=list;cpu->gpr[4]=1024;
            wiiu_gx2_handle_import(cpu,"GX2BeginDisplayList");
        }
        for(u32 pass=0;pass<2;++pass) {
            mem_write32(cpu,base+0x24,pass?image_b:image_a);
            cpu->gpr[3]=base;cpu->gpr[4]=0;
            wiiu_gx2_handle_import(cpu,"GX2SetColorBuffer");
            cpu->fpr[1]=pass?0:1;cpu->fpr[2]=pass?1:0;
            cpu->fpr[3]=0;cpu->fpr[4]=1;cpu->gpr[3]=base;
            wiiu_gx2_handle_import(cpu,"GX2ClearColor");
        }
        mem_write32(cpu,base+0x24,image_c);
        if(recorded) {
            cpu->gpr[3]=list;wiiu_gx2_handle_import(cpu,"GX2EndDisplayList");
            cpu->gpr[4]=cpu->gpr[3];cpu->gpr[3]=list;
            wiiu_gx2_handle_import(cpu,"GX2CallDisplayList");
        }
        /* Rebinding the scratch descriptor must preserve each image, and
           replay must not read its current (unrelated) image C. */
        for(u32 pass=0;pass<2;++pass) {
            mem_write32(cpu,base+0x24,pass?image_b:image_a);
            cpu->gpr[3]=base;cpu->gpr[4]=0;
            wiiu_gx2_handle_import(cpu,"GX2SetColorBuffer");
            u8 pixels[16];
            if(!wiiu_gx2_readback_color_buffer(base,pixels,2,2,8)) {
                fprintf(stderr,"Reused color descriptor lost image: recorded=%u image=%u\n",recorded,pass);
                return 1;
            }
            for(u32 i=0;i<4;++i) if(pixels[i*4] ||
                pixels[i*4+1]!=(pass?255:0) || pixels[i*4+2]!=(pass?0:255)) {
                fprintf(stderr,"Reused color descriptor corrupted pixels: recorded=%u image=%u\n",recorded,pass);
                return 1;
            }
        }
    }
    fprintf(stderr,"Color descriptor reuse: independent images and recorded snapshots passed\n");
    return 0;
}

static int test_sampler_state(CPUState* cpu,u32 at) {
    u32 words[3];
#define VERIFY(x) do{if(!(x)){fprintf(stderr,"sampler check line %d\n",__LINE__);return 1;}}while(0)
    cpu->gpr[3]=at;cpu->gpr[4]=2;cpu->gpr[5]=1;
    wiiu_gx2_handle_import(cpu,"GX2InitSampler");
    u32 initial=146|(1<<9)|(1<<12)|(1<<15)|(1<<17)|(1<<25);
    VERIFY(mem_read32(cpu,at)==initial && mem_read32(cpu,at+4)==0xFFC00);
    VERIFY(mem_read32(cpu,at+8)==0x80000000u);
    cpu->gpr[3]=at;cpu->gpr[4]=0;cpu->gpr[5]=1;cpu->gpr[6]=2;
    wiiu_gx2_handle_import(cpu,"GX2InitSamplerClamping");
    u32 captured=(initial&~511u)|136u;
    VERIFY(mem_read32(cpu,at)==captured);
    cpu->gpr[3]=at;cpu->fpr[1]=-1;cpu->fpr[2]=32;cpu->fpr[3]=-.25;
    wiiu_gx2_handle_import(cpu,"GX2InitSamplerLOD");
    VERIFY(mem_read32(cpu,at+4)==(0xFFC00u|0xFF000000u));
    cpu->gpr[3]=at+256;cpu->gpr[4]=4096;
    wiiu_gx2_handle_import(cpu,"GX2BeginDisplayList");
    cpu->gpr[3]=at;cpu->gpr[4]=31;
    wiiu_gx2_handle_import(cpu,"GX2SetPixelSampler");
    cpu->gpr[3]=at+256;
    wiiu_gx2_handle_import(cpu,"GX2EndDisplayList");
    u32 bytes=cpu->gpr[3];
    mem_write32(cpu,at,0); /* Replay must use register values, not this pointer. */
    cpu->gpr[3]=at+256;cpu->gpr[4]=bytes;
    wiiu_gx2_handle_import(cpu,"GX2CallDisplayList");
    VERIFY(wiiu_gx2_get_pixel_sampler(31,words) && words[0]==captured);
    VERIFY(!wiiu_gx2_get_pixel_sampler(32,words));
    cpu->gpr[3]=at+0x2000;cpu->gpr[4]=0;
    wiiu_gx2_handle_import(cpu,"GX2SetupContextStateEx");
    cpu->gpr[3]=at;cpu->gpr[4]=0;
    wiiu_gx2_handle_import(cpu,"GX2SetPixelSampler");
    cpu->gpr[3]=at+0xD000;cpu->gpr[4]=0;
    wiiu_gx2_handle_import(cpu,"GX2SetupContextStateEx");
    VERIFY(!wiiu_gx2_get_pixel_sampler(0,words));
    cpu->gpr[3]=at+0x2000;
    wiiu_gx2_handle_import(cpu,"GX2SetContextState");
    VERIFY(wiiu_gx2_get_pixel_sampler(0,words) && words[0]==0);
    wiiu_gx2_reset();
    VERIFY(!wiiu_gx2_get_pixel_sampler(0,words));
#undef VERIFY
    return 0;
}

int main(int argc, char** argv) {
    WiiUMemory memory;
    CPUState cpu = {0};
    const u32 color = WIIU_GUEST_HEAP_BASE, texture = color + 256;
    wiiu_memory_init(&memory);
    if (!wiiu_memory_add_segment(&memory, "present_test", color, 0x30000, NULL, true)) return 1;
    wiiu_memory_bind_cpu(&memory, &cpu);
    wiiu_gx2_reset();
    if(test_sampler_state(&cpu,color))return 1;
    for (u32 descriptor = color; descriptor <= texture; descriptor += 256) {
        mem_write32(&cpu, descriptor + 4, 2);
        mem_write32(&cpu, descriptor + 8, 2);
        mem_write32(&cpu, descriptor + 12, 1);
        mem_write32(&cpu, descriptor + 0x14, 0x1A);
        mem_write32(&cpu, descriptor + 0x20, 16);
        mem_write32(&cpu, descriptor + 0x24, descriptor + 1024);
        mem_write32(&cpu, descriptor + 0x30, 16);
        mem_write32(&cpu, descriptor + 0x3C, 2);
        mem_write32(&cpu, descriptor + 0x84, 0x00010203);
    }
    for (u32 i = 0; i < 4; ++i) mem_write32(&cpu, texture + 1024 + i*4, 0x804020FF);
    cpu.gpr[3] = color; cpu.gpr[4] = 0;
    wiiu_gx2_handle_import(&cpu, "GX2SetColorBuffer");
    cpu.gpr[3] = color; cpu.gpr[4] = 1;
    wiiu_gx2_handle_import(&cpu, "GX2CopyColorBufferToScanBuffer");
    wiiu_gx2_handle_import(&cpu, "GX2SwapScanBuffers");
    int result = wiiu_gx2_has_active_frame(); /* Untouched storage isn't a frame. */
    cpu.gpr[3] = texture; cpu.gpr[4] = 0;
    wiiu_gx2_handle_import(&cpu, "GX2SetPixelTexture");
    cpu.gpr[3] = 19; cpu.gpr[4] = 4; cpu.gpr[5] = 0; cpu.gpr[6] = 1;
    wiiu_gx2_handle_import(&cpu, "GX2DrawEx");
    /* BOTW's UI draws without issuing GX2ClearColor. It must still present. */
    wiiu_gx2_handle_import(&cpu, "GX2SwapScanBuffers");
    if (!wiiu_gx2_has_active_frame()) {
        fprintf(stderr, "Drawn scan-out rejected because no explicit clear occurred\n");
        result = 1;
    }
    const u32 types[4] = {0,1,4,9};
    const s32 bases[3] = {0,4,-4};
    for (u32 type = 0; type < 4; ++type)
        for (u32 base = 0; base < 3; ++base)
            for (u32 ex2 = 0; ex2 < 2; ++ex2)
                for (u32 recorded = 0; recorded < 2; ++recorded)
                    result |= test_r8_glyph_quad(&cpu, color, texture,
                        types[type], bases[base], ex2 != 0, recorded != 0, 1,250);
    result |= test_r8_glyph_quad(&cpu, color, texture, 4, 0, true, false, 0,250);
    result |= test_r8_glyph_quad(&cpu, color, texture, 4, 0, true, true, 0,250);
    result |= test_context_restore(&cpu, color, texture);
    result |= test_retained_display_lists(&cpu, color, texture);
    if (argc > 1) {
        result |= test_layout_shader(&cpu, color, argv[1],false);
        result |= test_layout_shader(&cpu, color, argv[1],true);
    }
    result |= test_constant_triangle(&cpu, color);
    result |= test_material_pixels(color);
    result |= test_surface_copy(&cpu,color);
    if(argc>2) result |= test_captured_font(&cpu,color,argv[2]);
    result |= test_completion_status(&cpu,color+0x28000);
    result |= test_texture_refresh(&cpu,color+0x28000);
    result |= test_color_descriptor_reuse(&cpu,color+0x20000);
    if (memory.unmapped_read_count || memory.unmapped_write_count) result = 1;
    wiiu_gx2_reset();
    wiiu_memory_free(&memory);
    return result;
}
