#include "wiiu_gx2_software.h"

#include <stddef.h>
#include <math.h>
#include <string.h>

#include "wiiu_gx2_cemu_layout.h"
#include "wiiu_memory.h"

enum {
    GX2_BANKS = 4,
    GX2_PIPES = 2,
    GX2_PIPE_INTERLEAVE_BITS = 8,
};

static u32 min_u32(u32 a, u32 b) {
    return a < b ? a : b;
}

static const u8* texture_data(CPUState* cpu,
                              const WiiUGX2TextureView* texture) {
    WiiUMemory* memory = (WiiUMemory*)cpu->external_user_data;
    WiiUMemorySegment* segment =
        memory ? wiiu_memory_find(memory, texture->image,
                                  texture->image_size)
               : NULL;
    if (!segment)
        return NULL;
    return segment->data +
           (wiiu_memory_canonical_address(texture->image) - segment->base);
}

static bool tile_mode_thick(u32 tile_mode) {
    return tile_mode == 3u || tile_mode == 7u || tile_mode == 11u ||
           tile_mode == 13u || tile_mode == 15u;
}

static bool tile_mode_bank_swapped(u32 tile_mode) {
    return tile_mode == 8u || tile_mode == 9u || tile_mode == 10u ||
           tile_mode == 11u || tile_mode == 14u || tile_mode == 15u;
}

static u32 pixel_index_in_micro_tile(u32 x, u32 y, u32 z, u32 bpp,
                                     u32 tile_mode) {
    u32 bits[9] = {0};
    switch (bpp) {
    case 8:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = (x >> 2) & 1u;
        bits[3] = (y >> 1) & 1u;
        bits[4] = y & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 16:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = (x >> 2) & 1u;
        bits[3] = y & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 64:
        bits[0] = x & 1u;
        bits[1] = y & 1u;
        bits[2] = (x >> 1) & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    case 128:
        bits[0] = y & 1u;
        bits[1] = x & 1u;
        bits[2] = (x >> 1) & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    default:
        bits[0] = x & 1u;
        bits[1] = (x >> 1) & 1u;
        bits[2] = y & 1u;
        bits[3] = (x >> 2) & 1u;
        bits[4] = (y >> 1) & 1u;
        bits[5] = (y >> 2) & 1u;
        break;
    }
    if (tile_mode_thick(tile_mode)) {
        bits[6] = z & 1u;
        bits[7] = (z >> 1) & 1u;
    }
    u32 index = 0;
    for (u32 i = 0; i < 9u; i++)
        index |= bits[i] << i;
    return index;
}

static u32 micro_tiled_offset(u32 x, u32 y, u32 slice, u32 bpp,
                              u32 pitch, u32 height, u32 tile_mode) {
    u32 thickness = tile_mode_thick(tile_mode) ? 4u : 1u;
    u32 tile_bytes = thickness * ((bpp * 64u + 7u) >> 3);
    u64 tile_offset =
        (u64)tile_bytes * ((x >> 3) + (u64)(pitch >> 3) * (y >> 3));
    u64 slice_bytes = ((u64)height * pitch * thickness * bpp + 7u) >> 3;
    u64 slice_offset = slice_bytes * (slice / thickness);
    u32 pixel = pixel_index_in_micro_tile(x, y, slice, bpp, tile_mode);
    u64 result = tile_offset + slice_offset + ((u64)bpp * pixel >> 3);
    return result <= 0xFFFFFFFFu ? (u32)result : 0xFFFFFFFFu;
}

static u32 surface_rotation(u32 tile_mode) {
    if (tile_mode >= 4u && tile_mode <= 11u)
        return 2u;
    if (tile_mode >= 12u && tile_mode <= 15u)
        return 1u;
    return 0u;
}

static u32 bank_swap_width(u32 tile_mode, u32 bpp, u32 pitch) {
    if (!tile_mode_bank_swapped(tile_mode) || bpp == 0)
        return 0;
    u32 bytes_per_sample = 8u * bpp;
    u32 swap_tiles = (128u / bpp) > 1u ? 128u / bpp : 1u;
    u32 swap_width = swap_tiles * 8u * GX2_BANKS;
    u32 height_bytes = 2u * bpp;
    u32 swap_max = GX2_PIPES * GX2_BANKS * 2048u / height_bytes;
    u32 swap_min = 256u * 8u * GX2_BANKS / bytes_per_sample;
    u32 value = swap_max >= swap_width
                    ? (swap_width > swap_min ? swap_width : swap_min)
                    : swap_max;
    while (value >= 2u * pitch)
        value >>= 1;
    return value;
}

static u32 macro_tiled_offset(u32 x, u32 y, u32 slice, u32 bpp,
                              u32 pitch, u32 height, u32 tile_mode,
                              u32 swizzle) {
    static const u32 bank_swap_order[4] = {0u, 1u, 3u, 2u};
    u32 thickness = tile_mode_thick(tile_mode) ? 4u : 1u;
    u32 tile_bits = bpp * thickness * 64u;
    u32 tile_bytes = tile_bits >> 3;
    u32 pixel = pixel_index_in_micro_tile(x, y, slice, bpp, tile_mode);
    u32 element_offset = bpp * pixel >> 3;

    u32 pipe = ((y >> 3) ^ (x >> 3)) & 1u;
    u32 bank = (y >> 4) & 3u;
    bank = ((bank >> 1) | (bank << 1)) & 3u;
    bank = (bank ^ (x >> 3)) & 3u;
    u32 bank_pipe = pipe + GX2_PIPES * bank;
    u32 pipe_swizzle = (swizzle >> 8) & 1u;
    u32 bank_swizzle = (swizzle >> 9) & 3u;
    u32 slice_in = tile_mode_thick(tile_mode) ? slice >> 2 : slice;
    bank_pipe ^= pipe_swizzle + GX2_PIPES * bank_swizzle +
                 slice_in * surface_rotation(tile_mode);
    bank_pipe %= GX2_PIPES * GX2_BANKS;
    pipe = bank_pipe % GX2_PIPES;
    bank = bank_pipe / GX2_PIPES;

    u64 slice_bytes = ((u64)height * pitch * thickness * bpp + 7u) >> 3;
    u64 slice_offset = slice_bytes * (slice / thickness);
    u32 macro_pitch = 8u * GX2_BANKS;
    u32 macro_height = 8u * GX2_PIPES;
    if (tile_mode == 5u || tile_mode == 9u) {
        macro_pitch >>= 1;
        macro_height <<= 1;
    } else if (tile_mode == 6u || tile_mode == 10u) {
        macro_pitch >>= 2;
        macro_height <<= 2;
    }
    u32 macro_bytes =
        (thickness * bpp * macro_height * macro_pitch + 7u) >> 3;
    u64 macro_offset =
        ((u64)(x / macro_pitch) +
         (u64)(pitch / macro_pitch) * (y / macro_height)) *
        macro_bytes;
    if (tile_mode_bank_swapped(tile_mode)) {
        u32 width = bank_swap_width(tile_mode, bpp, pitch);
        if (width != 0) {
            u32 swap = macro_pitch * (x / macro_pitch) / width;
            bank ^= bank_swap_order[swap & 3u];
        }
    }

    u32 pipe_offset = pipe << GX2_PIPE_INTERLEAVE_BITS;
    u32 bank_offset = bank << (1u + GX2_PIPE_INTERLEAVE_BITS);
    u64 macro_slice = (macro_offset + slice_offset) >> 3;
    macro_slice += element_offset;
    u64 high = macro_slice & ~255ull;
    u64 low = macro_slice & 255ull;
    u64 result = (high << 3) | low | pipe_offset | bank_offset;
    return result <= 0xFFFFFFFFu ? (u32)result : 0xFFFFFFFFu;
}

static u32 texture_offset(u32 x, u32 y, u32 slice, u32 bpp, u32 pitch,
                          u32 height, u32 tile_mode, u32 swizzle) {
    u32 offset = 0u;
    return wiiu_cemu_surface_offset(x, y, slice, bpp, pitch, height,
                                    tile_mode, swizzle, &offset)
               ? offset
               : 0xFFFFFFFFu;
}

static u16 read_le16(const u8* data) {
    return (u16)data[0] | (u16)((u16)data[1] << 8);
}

static u32 read_le32(const u8* data) {
    return (u32)data[0] | ((u32)data[1] << 8) | ((u32)data[2] << 16) |
           ((u32)data[3] << 24);
}

static void decode_r5g6b5(u16 value, u8 color[4]) {
    /* Latte R5_G6_B5 stores red in bits 0..4 and blue in bits 11..15. */
    color[2] = (u8)((value & 31u) * 255u / 31u);
    color[1] = (u8)(((value >> 5) & 63u) * 255u / 63u);
    color[0] = (u8)(((value >> 11) & 31u) * 255u / 31u);
    color[3] = 255u;
}

static u8 select_texture_component(const u8 rgba[4], u32 selector) {
    if (selector < 4u)
        return rgba[selector];
    if (selector == 4u)
        return 0u;
    if (selector == 5u)
        return 255u;
    return 0u;
}

static void apply_texture_component_map(const WiiUGX2TextureView* texture,
                                        u8 color[4]) {
    /* GX2Texture::compSel is encoded as X/Y/Z/W selectors in byte lanes. */
    if (texture->comp_map == 0u)
        return;

    u8 rgba[4] = {color[2], color[1], color[0], color[3]};
    u8 mapped[4] = {
        select_texture_component(rgba, (texture->comp_map >> 24u) & 7u),
        select_texture_component(rgba, (texture->comp_map >> 16u) & 7u),
        select_texture_component(rgba, (texture->comp_map >> 8u) & 7u),
        select_texture_component(rgba, texture->comp_map & 7u),
    };
    color[0] = mapped[2];
    color[1] = mapped[1];
    color[2] = mapped[0];
    color[3] = mapped[3];
}

static void decode_bc1_color(const u8* block, u32 pixel, bool force_four,
                             u8 output[4]) {
    u16 c0 = read_le16(block);
    u16 c1 = read_le16(block + 2);
    u8 colors[4][4];
    decode_r5g6b5(c0, colors[0]);
    decode_r5g6b5(c1, colors[1]);
    if (c0 > c1 || force_four) {
        for (u32 channel = 0; channel < 3u; channel++) {
            colors[2][channel] =
                (u8)((2u * colors[0][channel] + colors[1][channel]) / 3u);
            colors[3][channel] =
                (u8)((colors[0][channel] + 2u * colors[1][channel]) / 3u);
        }
        colors[2][3] = colors[3][3] = 255u;
    } else {
        for (u32 channel = 0; channel < 3u; channel++)
            colors[2][channel] =
                (u8)((colors[0][channel] + colors[1][channel]) / 2u);
        colors[2][3] = 255u;
        colors[3][0] = colors[3][1] = colors[3][2] = colors[3][3] = 0u;
    }
    u32 selectors = read_le32(block + 4);
    u32 index = (selectors >> (pixel * 2u)) & 3u;
    for (u32 channel = 0; channel < 4u; channel++)
        output[channel] = colors[index][channel];
}

static u8 decode_bc_alpha(const u8* block, u32 pixel) {
    u8 values[8];
    values[0] = block[0];
    values[1] = block[1];
    if (values[0] > values[1]) {
        for (u32 i = 1; i <= 6u; i++)
            values[i + 1u] =
                (u8)(((7u - i) * values[0] + i * values[1]) / 7u);
    } else {
        for (u32 i = 1; i <= 4u; i++)
            values[i + 1u] =
                (u8)(((5u - i) * values[0] + i * values[1]) / 5u);
        values[6] = 0u;
        values[7] = 255u;
    }
    u64 selectors = 0;
    for (u32 i = 0; i < 6u; i++)
        selectors |= (u64)block[2u + i] << (8u * i);
    return values[(selectors >> (pixel * 3u)) & 7u];
}

static bool sample_texture(const u8* data,
                           const WiiUGX2TextureView* texture, u32 x, u32 y,
                           u8 output[4]) {
    u32 hw_format = texture->format & 0x3Fu;
    bool compressed = hw_format >= 0x31u && hw_format <= 0x35u;
    u32 source_x = compressed ? x >> 2 : x;
    u32 source_y = compressed ? y >> 2 : y;
    u32 bpp;
    if (compressed)
        bpp = (hw_format == 0x31u || hw_format == 0x34u) ? 64u : 128u;
    else if (hw_format == 0x01u)
        bpp = 8u;
    else if (hw_format == 0x05u || hw_format == 0x07u ||
             hw_format == 0x08u)
        bpp = 16u;
    else
        bpp = 32u;

    u32 pitch = 0;
    u32 surface_height = 0;
    if (!wiiu_cemu_normalize_texture_layout(
            texture->width, texture->height, texture->format,
            texture->pitch, texture->image_size, &pitch, &surface_height)) {
        return false;
    }
    u32 offset = texture_offset(source_x, source_y, texture->first_slice,
                                bpp, pitch, surface_height,
                                texture->tile_mode, texture->swizzle);
    u32 bytes = bpp >> 3;
    if (offset == 0xFFFFFFFFu || offset > texture->image_size ||
        bytes > texture->image_size - offset) {
        return false;
    }
    const u8* source = data + offset;
    u32 block_pixel = (x & 3u) + (y & 3u) * 4u;
    switch (hw_format) {
    case 0x01:
        output[0] = output[1] = output[2] = source[0];
        output[3] = 255u;
        break;
    case 0x07:
        output[2] = source[0];
        output[1] = source[1];
        output[0] = 0u;
        output[3] = 255u;
        break;
    case 0x08:
        decode_r5g6b5(read_le16(source), output);
        break;
    case 0x1A:
        output[2] = source[0];
        output[1] = source[1];
        output[0] = source[2];
        output[3] = source[3];
        break;
    case 0x31:
        decode_bc1_color(source, block_pixel, false, output);
        break;
    case 0x32: {
        decode_bc1_color(source + 8, block_pixel, true, output);
        u64 alpha = 0;
        for (u32 i = 0; i < 8u; i++)
            alpha |= (u64)source[i] << (i * 8u);
        output[3] = (u8)(((alpha >> (block_pixel * 4u)) & 15u) * 17u);
        break;
    }
    case 0x33:
        decode_bc1_color(source + 8, block_pixel, true, output);
        output[3] = decode_bc_alpha(source, block_pixel);
        break;
    case 0x34:
        output[0] = output[1] = output[2] =
            decode_bc_alpha(source, block_pixel);
        output[3] = 255u;
        break;
    case 0x35:
        output[2] = decode_bc_alpha(source, block_pixel);
        output[1] = decode_bc_alpha(source + 8, block_pixel);
        output[0] = 0u;
        output[3] = 255u;
        break;
    default:
        return false;
    }
    apply_texture_component_map(texture, output);
    return true;
}

static float half_float(u16 h) {
    unsigned exponent=(h>>10)&31,mantissa=h&1023;
    float value=exponent==31?(mantissa?NAN:INFINITY):
        exponent?ldexpf(1.0f+mantissa/1024.0f,(int)exponent-15):ldexpf((float)mantissa,-24);
    return h&0x8000?-value:value;
}
static bool float_texel(const u8* data,const WiiUGX2TextureView* t,u32 x,u32 y,float out[4]) {
    unsigned channels=0,bytes=0;
    switch(t->format) {
    case 0x806:channels=1;bytes=2;break;
    case 0x810:channels=2;bytes=2;break;
    case 0x820:channels=4;bytes=2;break;
    case 0x80e:channels=1;bytes=4;break;
    case 0x81e:channels=2;bytes=4;break;
    case 0x823:channels=4;bytes=4;break;
    default: {
        if(t->format!=1 && t->format!=7 && t->format!=8 && t->format!=0x1a &&
           (t->format<0x31 || t->format>0x35))return false;
        u8 bgra[4];if(!sample_texture(data,t,x,y,bgra))return false;
        out[0]=bgra[2]/255.0f;out[1]=bgra[1]/255.0f;out[2]=bgra[0]/255.0f;out[3]=bgra[3]/255.0f;
        return true;
    }
    }
    u32 pitch,height;
    if(!wiiu_cemu_normalize_texture_layout(t->width,t->height,t->format,t->pitch,t->image_size,&pitch,&height))return false;
    u32 offset=texture_offset(x,y,t->first_slice,channels*bytes*8,pitch,height,t->tile_mode,t->swizzle);
    if(offset>t->image_size || channels*bytes>t->image_size-offset)return false;
    float rgba[4]={0,0,0,1};
    for(unsigned c=0;c<channels;++c) {
        if(bytes==2)rgba[c]=half_float(read_le16(data+offset+c*2));
        else {u32 bits=read_le32(data+offset+c*4);memcpy(&rgba[c],&bits,4);}
    }
    for(unsigned c=0;c<4;++c) {
        unsigned sel=(t->comp_map>>((3-c)*8))&255;
        if(sel>5)return false;
        out[c]=sel<4?rgba[sel]:sel==5?1:0;
    }
    return true;
}
static int sample_address(int coordinate,int extent,unsigned mode) {
    if(mode==0){coordinate%=extent;return coordinate<0?coordinate+extent:coordinate;}
    if(mode==1){int period=extent*2;coordinate%=period;if(coordinate<0)coordinate+=period;return coordinate<extent?coordinate:period-1-coordinate;}
    if(mode==2)return coordinate<0?0:coordinate>=extent?extent-1:coordinate;
    if(mode==3){if(coordinate<0)coordinate=-1-coordinate;return coordinate>=extent?extent-1:coordinate;}
    return coordinate<0 || coordinate>=extent?-1:coordinate; /* border */
}
static bool sample_float_data(const u8* data,unsigned native_layout,const WiiUGX2TextureView* t,
    u32 word,float u,float v,float out[4]) {
    if(!data || !t || !out || !t->width || !t->height || t->width>65536 || t->height>65536 ||
       t->depth!=1 || t->first_slice || !isfinite(u) || !isfinite(v) || fabsf(u)>16384 || fabsf(v)>16384)return false;
    unsigned mx=word&7,my=(word>>3)&7,mag=(word>>9)&7,min=(word>>12)&7,border=(word>>22)&3;
    if((mx>3 && mx!=6) || (my>3 && my!=6) || border==3 ||
       (mag!=0 && mag!=1 && mag!=4 && mag!=5) || (min!=0 && min!=1 && min!=4 && min!=5))return false;
    /* At explicit level zero magnification selects the base filter. */
    bool linear=(mag&1)!=0;
    float px=u*t->width-(linear?.5f:0),py=v*t->height-(linear?.5f:0);
    int ix=(int)floorf(px),iy=(int)floorf(py);float fx=px-floorf(px),fy=py-floorf(py);
    float value[4]={0};
    for(unsigned y=0;y<(linear?2u:1u);++y)for(unsigned x=0;x<(linear?2u:1u);++x) {
        int sx=sample_address(ix+(int)x,(int)t->width,mx),sy=sample_address(iy+(int)y,(int)t->height,my);
        float texel[4]={border==2?1:0,border==2?1:0,border==2?1:0,border?1:0};
        if(sx>=0 && sy>=0) {
            if(!native_layout) {if(!float_texel(data,t,(u32)sx,(u32)sy,texel))return false;}
            else {
                float rgba[4];
                if(native_layout==2)memcpy(rgba,data+((size_t)sy*t->width+sx)*16,16);
                else {
                    const u8* p=data+((size_t)sy*t->width+sx)*4;
                    rgba[0]=p[2]/255.0f;rgba[1]=p[1]/255.0f;rgba[2]=p[0]/255.0f;rgba[3]=p[3]/255.0f;
                    if(t->format==1)rgba[1]=0;
                    if(t->format==1 || t->format==7){rgba[2]=0;rgba[3]=1;}
                }
                for(unsigned c=0;c<4;++c) {
                    unsigned sel=(t->comp_map>>((3-c)*8))&255;
                    if(sel>5)return false;
                    texel[c]=sel<4?rgba[sel]:sel==5?1:0;
                }
            }
        }
        float weight=linear?(x?fx:1-fx)*(y?fy:1-fy):1;
        for(unsigned c=0;c<4;++c)value[c]+=texel[c]*weight;
    }
    memcpy(out,value,sizeof(value));return true;
}
bool wiiu_gx2_software_sample_float(CPUState* cpu,const WiiUGX2TextureView* t,
    u32 word,float u,float v,float out[4]) {
    if(!cpu || !t)return false;
    return sample_float_data(texture_data(cpu,t),false,t,word,u,v,out);
}
bool wiiu_gx2_software_sample_bgra(const u8* pixels,const WiiUGX2TextureView* t,
    u32 word,float u,float v,float out[4]) {
    /* A BGRA readback cannot represent float, integer or sRGB texture data. */
    if(!t || (t->format!=1 && t->format!=7 && t->format!=0x1a))return false;
    return sample_float_data(pixels,true,t,word,u,v,out);
}
bool wiiu_gx2_software_sample_rgba_float(const float* pixels,const WiiUGX2TextureView* t,
    u32 word,float u,float v,float out[4]) {
    return sample_float_data((const u8*)pixels,2,t,word,u,v,out);
}

bool wiiu_gx2_software_blit(CPUState* cpu,
                            const WiiUGX2TextureView* texture,
                            u8* destination, u32 destination_width,
                            u32 destination_height, u32 destination_stride,
                            u32 x, u32 y, u32 width, u32 height,
                            bool alpha_blend) {
    if (!cpu || !texture || !destination || texture->image_size == 0 ||
        texture->width == 0 || texture->height == 0 || width == 0 ||
        height == 0 || destination_stride < destination_width * 4u) {
        return false;
    }
    const u8* data = texture_data(cpu, texture);
    if (!data || x >= destination_width || y >= destination_height)
        return false;
    width = min_u32(width, destination_width - x);
    height = min_u32(height, destination_height - y);
    bool wrote = false;
    for (u32 destination_y = 0; destination_y < height; destination_y++) {
        u32 source_y =
            (u32)((u64)destination_y * texture->height / height);
        u8* row = destination + (size_t)(y + destination_y) *
                                    destination_stride +
                  (size_t)x * 4u;
        for (u32 destination_x = 0; destination_x < width; destination_x++) {
            u32 source_x =
                (u32)((u64)destination_x * texture->width / width);
            u8 color[4];
            if (!sample_texture(data, texture, source_x, source_y, color))
                continue;
            u8* pixel = row + (size_t)destination_x * 4u;
            if (alpha_blend && color[3] < 255u) {
                u32 alpha = color[3];
                for (u32 channel = 0; channel < 3u; channel++) {
                    pixel[channel] = (u8)((color[channel] * alpha +
                                           pixel[channel] * (255u - alpha)) /
                                          255u);
                }
                pixel[3] = 255u;
            } else {
                pixel[0] = color[0];
                pixel[1] = color[1];
                pixel[2] = color[2];
                pixel[3] = color[3];
            }
            wrote = true;
        }
    }
    return wrote;
}
