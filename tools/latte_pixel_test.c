#include "wiiu_latte_pixel.h"
#include "wiiu_window.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void word(uint8_t* p,unsigned at,uint32_t w){for(unsigned c=0;c<4;++c)p[at+c]=(uint8_t)(w>>(8*c));}
static void pair(uint8_t* p,unsigned at,uint32_t a,uint32_t b){word(p,at*8,a);word(p,at*8+4,b);}
static uint32_t bits(float f){uint32_t b;memcpy(&b,&f,4);return b;}
static int captured(const char* dir,unsigned id,bool audit) {
    char name[2048];uint8_t bytes[16384],desc[256];
    snprintf(name,sizeof(name),"%s/botw_guide_variant_%u_ps_program.bin",dir,id);
    FILE* f=fopen(name,"rb");CHECK(f);size_t size=fread(bytes,1,sizeof(bytes),f);fclose(f);
    snprintf(name,sizeof(name),"%s/botw_guide_variant_%u_ps_descriptor.bin",dir,id);
    f=fopen(name,"rb");CHECK(f);CHECK(fread(desc,1,sizeof(desc),f)>=0x14);fclose(f);
    unsigned inputs=(unsigned)desc[16]<<24|(unsigned)desc[17]<<16|(unsigned)desc[18]<<8|desc[19];
    WiiULattePixelProgram* p=malloc(sizeof(*p));CHECK(p);
    unsigned control=(unsigned)desc[8]<<24|(unsigned)desc[9]<<16|(unsigned)desc[10]<<8|desc[11];
    const char* target_env=getenv("BOTW_REPLAY_TARGET_MASK");
    const char* channel_env=getenv("BOTW_REPLAY_CHANNEL_MASK");
    bool translated=channel_env?
        wiiu_latte_translate_pixel_channels(bytes,size,inputs,(control&256)?(control>>10)&31:UINT32_MAX,0,0,0,(uint32_t)strtoul(channel_env,NULL,0),p):
        wiiu_latte_translate_pixel_targets(bytes,size,inputs,(control&256)?(control>>10)&31:UINT32_MAX,0,0,0,
            target_env?(uint8_t)strtoul(target_env,NULL,0):255,p);
    if(audit) {
        if(translated && getenv("BOTW_REPLAY_HLSL"))puts(p->hlsl);
        unsigned at=p->instruction_offset;
        printf("PS %u %s inputs=%u bytes=%zu offset=%X instruction=",id,translated?"accepted":"rejected",inputs,size,at);
        for(unsigned i=0;i<8 && at+i<size;++i)printf("%02X",bytes[at+i]);
        puts("");free(p);return translated?0:1;
    }
    CHECK(translated);
    WiiUWindowSceneMaterial material={0};material.program=p;
    const char* uniform_log=getenv("BOTW_REPLAY_UNIFORM_LOG");
    if(uniform_log) {
        uint32_t registers[1024]={0};uint8_t valid[1024]={0};char line[4096];
        FILE* log=fopen(uniform_log,"rb");CHECK(log);
        while(fgets(line,sizeof(line),log)) {
            unsigned variant,at,a,b,c,d;
            if(sscanf(line,"gx2: guide variant_%u_ps c[%u]=%x,%x,%x,%x",&variant,&at,&a,&b,&c,&d)==6 && variant==id && at<=1020) {
                registers[at]=a;registers[at+1]=b;registers[at+2]=c;registers[at+3]=d;
                memset(valid+at,1,4);
            }
        }
        fclose(log);
        for(unsigned i=0;i<p->constant_count;++i)if(p->constants[i].bank==16) {
            unsigned v=p->constants[i].vector;CHECK(v<256);
            for(unsigned c=0;c<4;++c)material.constants[WIIU_PIXEL_TEXTURES+i][c]=registers[v*4+c];
        }
        const unsigned base=WIIU_PIXEL_TEXTURES+WIIU_PIXEL_CONSTANTS+1;
        for(unsigned v=0;v<256;++v)for(unsigned c=0;c<4;++c) {
            material.constants[base+v][c]=registers[v*4+c];
            if(valid[v*4+c])material.constants[base+256+v][0]|=1u<<c;
        }
    }
    uint8_t* blocks[16]={0};
    for(unsigned i=0;i<16;++i)if(p->block_mask&(1u<<i)) {
        snprintf(name,sizeof(name),"%s/botw_variant_%u_ps_block_%u.bin",dir,id,i);
        f=fopen(name,"rb");CHECK(f);blocks[i]=malloc(65536);CHECK(blocks[i]);
        size_t bytes_read=fread(blocks[i],1,65536,f);fclose(f);CHECK(bytes_read && !(bytes_read%16));
        material.uniform_blocks[i]=blocks[i];material.uniform_block_sizes[i]=(uint32_t)bytes_read;
    }
    const uint8_t tex[4]={96,64,128,255};
    for(unsigned i=0;i<WIIU_PIXEL_TEXTURES;++i) {
        material.textures[i]=(WiiUWindowEffectTexture){tex,1,1,77};
        for(unsigned c=0;c<4;++c)material.constants[i][c]=c;
    }
    WiiUWindowPostVertex v[3]={0};float xy[3][2]={{-1,1},{3,1},{-1,-3}};
    for(unsigned i=0;i<3;++i) {
        v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[3]=1;
        for(unsigned j=0;j<5;++j)for(unsigned c=0;c<4;++c)v[i].parameters[j][c]=.5f;
    }
    float viewport[4]={0,0,8,8};WiiUWindowGpuBlendControl blend={true,1,0,0,true,1,0,0};
    CHECK(wiiu_window_gpu_clear(99,8,8,0,0,0,255));
    for(unsigned i=0;i<8;++i)if(p->target_mask&(1u<<i)) {
        material.targets[i]=99+i;material.blends[i]=blend;
        CHECK(wiiu_window_gpu_clear(99+i,8,8,0,0,0,255));
    }
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    uint8_t pixels[256];CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    const char* expected_rgb=getenv("BOTW_REPLAY_EXPECT_RGB");
    if(expected_rgb) {
        unsigned r,g,b;CHECK(sscanf(expected_rgb,"%u,%u,%u",&r,&g,&b)==3 && r<=255 && g<=255 && b<=255);
        CHECK(abs((int)pixels[2]-(int)r)<=1 && abs((int)pixels[1]-(int)g)<=1 && abs((int)pixels[0]-(int)b)<=1);
    }
    if(uniform_log)for(unsigned i=0;i<8;++i)if(p->target_mask&(1u<<i)) {
        CHECK(wiiu_window_gpu_readback(99+i,pixels,8,8,32));
        printf("captured target %u BGRA=%u,%u,%u,%u\n",i,pixels[0],pixels[1],pixels[2],pixels[3]);
    }
    /* Variant 191 exports R0.xyyx = (1,0,0,1). */
    if(id==191)CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==255 && pixels[3]==255);
    printf("captured PS %u translated and GPU executed (inputs=%u textures=%04X constants=%u)\n",id,inputs,p->texture_mask,p->constant_count);
    for(unsigned i=0;i<16;++i)free(blocks[i]);
    free(p);return 0;
}
int main(int argc,char** argv) {
    if(argc==4 && !strcmp(argv[1],"--audit"))return captured(argv[2],(unsigned)strtoul(argv[3],NULL,10),true);
    wiiu_window_show("Latte pixel regression");
    if(argc==3)return captured(argv[1],(unsigned)strtoul(argv[2],NULL,10),false);
    uint8_t code[256]={0};WiiULattePixelProgram* p=malloc(sizeof(*p));CHECK(p);
    /* Texture 4, sampler 3, input R0.xy. R1 receives a complete sample. */
    pair(code,0,16,0x80800000);pair(code,1,0x8000,0x94200688);
    pair(code,16,0x00000410,0xF00D1001);pair(code,17,0x68818000,0);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    CHECK(p->input_mask[0]==3 && p->texture_mask==16 && p->sampler_mask==8 && !p->constant_count);
    WiiUWindowSceneMaterial material={0};material.program=p;
    const uint8_t tex[4]={32,64,128,255};material.textures[4]=(WiiUWindowEffectTexture){tex,1,1,201};
    for(unsigned c=0;c<4;++c)material.constants[4][c]=c;
    WiiUWindowPostVertex v[3]={0};float xy[3][2]={{-2,2},{0,2},{-2,0}};
    for(unsigned i=0;i<3;++i) {
        v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[3]=2;
        v[i].parameters[0][0]=v[i].parameters[0][1]=.5;
    }
    float viewport[4]={0,0,8,8};WiiUWindowGpuBlendControl blend={true,1,0,0,true,1,0,0};uint8_t pixels[256];
    CHECK(wiiu_window_gpu_clear(99,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    CHECK(!memcmp(pixels+(8+1)*4,tex,4));CHECK(pixels[(5*8+5)*4+2]==0);
    /* Texture slots are independent of the sixteen sampler slots. Check the
       old boundary and the high mask bit through actual GPU binding/readback. */
    const unsigned texture_slots[]={15,16,17,31};
    for(unsigned slot_index=0;slot_index<4;++slot_index) {
        unsigned slot=texture_slots[slot_index];
        pair(code,16,0x10|(slot<<8),0xF00D1001);
        CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
        CHECK(p->texture_mask==(1u<<slot) && p->sampler_mask==8);
        material.textures[slot]=(WiiUWindowEffectTexture){tex,1,1,201};
        for(unsigned c=0;c<4;++c)material.constants[slot][c]=c;
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        CHECK(!memcmp(pixels+(8+1)*4,tex,4));
    }
    pair(code,16,0x2010,0xF00D1001);
    CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    pair(code,16,0x410,0xF00D1001);pair(code,17,0x68880000,0);
    CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    pair(code,17,0x68818000,0);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    /* Array sampling selects layers, never blends between them. Keep the
       sampler separate from the high texture slot, and test single-layer SRVs. */
    const uint8_t layers[8]={0,0,255,255,0,255,0,255};
    pair(code,16,0x1f10,0xB00D1001);
    CHECK(wiiu_latte_translate_pixel_layout(code,sizeof(code),1,UINT32_MAX,1u<<31,p));
    material.textures[31]=(WiiUWindowEffectTexture){layers,1,1,301};
    material.textures[31].array_layers=2;
    const float layer_indices[]={0,1,.49f,.51f,-1,3};
    for(unsigned pass=0;pass<6;++pass) {
        for(unsigned i=0;i<3;++i)v[i].parameters[0][2]=layer_indices[pass];
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        unsigned layer=(pass==1 || pass==3 || pass==5)?1:0;
        CHECK(!memcmp(pixels+(8+1)*4,layers+layer*4,4));
    }
    material.textures[31].array_layers=1;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4+2]==255);
    material.textures[31].array_layers=0;
    CHECK(!wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    /* Unlike arrays, a volume filters between depth slices using normalized Z. */
    const float volume_texels[8]={1,0,0,1,0,1,0,1};
    pair(code,16,0x1f10,0xF00D1001);
    CHECK(wiiu_latte_translate_pixel_resources(code,sizeof(code),1,UINT32_MAX,0,0,1u<<31,p));
    material.textures[31]=(WiiUWindowEffectTexture){(const uint8_t*)volume_texels,1,1,302};
    material.textures[31].rgba_float=true;material.textures[31].volume_depth=2;
    material.textures[3].sampler_valid=true;
    material.textures[3].sampler_word0=146|(1u<<9)|(1u<<12);
    const float depths[5]={-.25f,.25f,.5f,.75f,1.25f};
    for(unsigned pass=0;pass<5;++pass) {
        for(unsigned i=0;i<3;++i)v[i].parameters[0][2]=depths[pass];
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        unsigned red=pixels[(8+1)*4+2],green=pixels[(8+1)*4+1];
        CHECK(pass<2?(red==255 && green==0):pass>2?(red==0 && green==255):
              (red>=127 && red<=128 && green>=127 && green<=128));
    }
    /* Wrap along W independently of clamped U/V. */
    material.textures[3].sampler_word0=18|(1u<<9)|(1u<<12);
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4+2]==255);
    material.textures[31].volume_depth=0;
    CHECK(!wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(!wiiu_latte_translate_pixel_resources(code,sizeof(code),1,UINT32_MAX,1u<<31,0,1u<<31,p));
    material.textures[3].sampler_valid=false;
    pair(code,16,0x410,0xF00D1001);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    /* Alpha-test comparisons discard the entire fragment before blending. */
    for(unsigned function=0;function<8;++function) {
        material.constants[WIIU_PIXEL_TEXTURES+WIIU_PIXEL_CONSTANTS][0]=8|function;
        material.constants[WIIU_PIXEL_TEXTURES+WIIU_PIXEL_CONSTANTS][1]=bits(1);
        CHECK(wiiu_window_gpu_clear(99,8,8,0,0,0,255));
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        bool pass=function==2 || function==3 || function==6 || function==7;
        CHECK(pixels[(8+1)*4+2]==(pass?128:0));
    }
    material.constants[WIIU_PIXEL_TEXTURES+WIIU_PIXEL_CONSTANTS][0]=0;
    CHECK(wiiu_window_gpu_upload_bgra(100,1,1,tex,4));
    material.source_surfaces[4]=100;material.textures[4].pixels=NULL;
    material.constants[4][0]=2;material.constants[4][2]=0;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    CHECK(pixels[(8+1)*4]==128 && pixels[(8+1)*4+2]==32);
    material.source_surfaces[4]=99;
    for(unsigned pass=0;pass<2;++pass) {
        CHECK(wiiu_window_gpu_clear(99,8,8,pass?192:64,32,16,255));
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        CHECK(pixels[(8+1)*4]==(pass?192:64) && pixels[(8+1)*4+2]==16);
    }
    /* FETCH4 returns four distinct texels, in Latte order, from the mapped
       red component. It must not swizzle the gathered positions as colors. */
    word(code,128,0x0000040f);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    const uint8_t gather_tex[16]={0,11,21,255,0,31,41,255,0,51,61,255,0,71,81,255};
    material.source_surfaces[4]=0;
    material.textures[4]=(WiiUWindowEffectTexture){gather_tex,2,2,202};
    for(unsigned channel=0;channel<2;++channel) {
        material.constants[4][0]=channel;
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        const uint8_t* pixel=pixels+(8+1)*4;
        CHECK(pixel[2]==(channel?31:41) && pixel[1]==(channel?51:61));
        CHECK(pixel[0]==(channel?71:81) && pixel[3]==(channel?11:21));
    }
    word(code,128,0x00000410);
    /* Reject malformed clauses, unsupported CF and undefined lanes. */
    word(code,4,0x85000000);CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    word(code,4,0x80800000);CHECK(!wiiu_latte_translate_pixel(code,136,1,p));
    CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),0,p));
    word(code,128,0x00000418);CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    /* Simultaneous VLIW writes: swap R0.xy, then export all four channels. */
    memset(code,0,sizeof(code));pair(code,0,8,0xA0040000);pair(code,1,0,0x94200688);
    pair(code,8,0x001F0400,0x00000C90);pair(code,9,0x801F0000,0x20000C90);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));CHECK(p->input_mask[0]==15);
    memset(&material,0,sizeof(material));material.program=p;
    for(unsigned i=0;i<3;++i){v[i].parameters[0][0]=.25f;v[i].parameters[0][1]=.5f;v[i].parameters[0][2]=.75f;v[i].parameters[0][3]=1;}
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    CHECK(abs(pixels[(8+1)*4]-191)<=1 && abs(pixels[(8+1)*4+1]-64)<=1 && abs(pixels[(8+1)*4+2]-128)<=1);
    /* KCACHE bank 2, vector 16; source floats are read at draw time. */
    pair(code,0,8|(2u<<22)|(1u<<30),0xA0000000|(1u<<2));
    pair(code,8,0x801F0080,0x00000C90);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    CHECK(p->constant_count==1 && p->constants[0].bank==2 && p->constants[0].vector==16);
    for(unsigned pass=0;pass<2;++pass) {
        material.constants[WIIU_PIXEL_TEXTURES][0]=bits(pass?.75f:.25f);
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        CHECK(pixels[(8+1)*4+2]==(pass?191:64));
    }
    /* Sixteen interpolators, and a real two-target burst with independent masks. */
    memset(code,0,sizeof(code));pair(code,0,15u<<15,0x94200688);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),16,p));CHECK(p->input_mask[15]==15);
    for(unsigned i=0;i<3;++i)for(unsigned c=0;c<4;++c)v[i].parameters[15][c]=.25f;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(abs(pixels[(8+1)*4]-64)<=1);
    pair(code,0,0,0x94220688);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),2,p));CHECK(p->target_mask==3);
    CHECK(wiiu_window_gpu_clear(101,8,8,0,0,0,255));
    material.targets[0]=99;material.targets[1]=101;material.blends[0]=material.blends[1]=blend;
    material.blends[1].write_mask_valid=true;material.blends[1].write_mask=7;
    for(unsigned i=0;i<3;++i)for(unsigned c=0;c<4;++c)v[i].parameters[1][c]=.5f;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(101,pixels,8,8,32));
    CHECK(abs(pixels[(8+1)*4]-128)<=1 && pixels[(8+1)*4+3]==255);
    /* DOT4 reads the four lanes as one VLIW operation, not four products. */
    memset(&material,0,sizeof(material));material.program=p;
    pair(code,0,8,0xA00C0000);pair(code,1,2u<<15,0x94200000);
    for(unsigned c=0;c<4;++c)pair(code,8+c,(c==3?0x80000000u:0)|(c<<10)|(1u<<13)|(c<<23),
        (2u<<21)|(c<<29)|(0x50<<7)|16);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),2,p));
    for(unsigned i=0;i<3;++i)for(unsigned c=0;c<4;++c){v[i].parameters[0][c]=.25f;v[i].parameters[1][c]=.5f;}
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(abs(pixels[(8+1)*4]-128)<=1);
    /* Integer add, integer-to-float, then float multiply: preserve register bits. */
    pair(code,0,8,0xA00C0000);pair(code,1,1u<<15,0x94200000);
    pair(code,8,0x80800000u|256u|(256u<<13),(1u<<21)|(0x34<<7)|16);
    pair(code,9,0x801F0001u,(1u<<21)|(0x6d<<7)|16);
    pair(code,10,0x80000001u|(253u<<13),(1u<<21)|(1<<7)|16);pair(code,11,bits(.01f),0);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),0,p));
    CHECK(p->constant_count==1 && p->constants[0].bank==16);
    material.constants[WIIU_PIXEL_TEXTURES][0]=17;material.constants[WIIU_PIXEL_TEXTURES][1]=23;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4]==102);
    /* Float conversion results are integer bit patterns, not denormal floats. */
    memset(code,0,sizeof(code));
    pair(code,0,8,0xA00C0000);pair(code,1,1u<<15,0x94200000);
    pair(code,8,0x80000000u,(1u<<21)|(0x6b<<7)|16);
    pair(code,9,0x80000001u,(1u<<21)|(0x6c<<7)|16);
    pair(code,10,0x80000001u|(253u<<13),(1u<<21)|(1<<7)|16);pair(code,11,bits(.1f),0);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=3.5f;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    fprintf(stderr,"float-to-int roundtrip B=%u\n",pixels[(8+1)*4]);
    CHECK(abs(pixels[(8+1)*4]-77)<=1);
    /* Mixed vector/scalar groups retain the integer payload until conversion.
       This is the addressing pattern used to build color lookup volumes. */
    memset(code,0,sizeof(code));
    pair(code,0,8,0xA0300000);pair(code,1,1u<<15,0x94200000);
    pair(code,8,0u|(256u<<13),(127u<<21)|(2u<<29)|(1u<<7)|16);
    pair(code,9,0x80000400u|(256u<<13),(127u<<21)|(3u<<29)|(1u<<7)|16);
    pair(code,10,254u|(2u<<10),(0x14u<<7));
    pair(code,11,0x800000FEu|(3u<<10),(1u<<29)|(0x14u<<7));
    pair(code,12,257u|(2u<<10),(3u<<21)|(0x19u<<7)|16);
    pair(code,13,0x800000FEu,(127u<<21)|(2u<<29)|(0x6bu<<7)|16);
    for(unsigned c=0;c<4;++c)
        pair(code,14+c,257u|(c<<10),((80u+c)<<21)|(c<<29)|(0x19u<<7)|16);
    pair(code,18,0x8000007Fu|(2u<<10),(4u<<21)|(1u<<29)|(0x6cu<<7)|16);
    pair(code,19,0x80000404u|(253u<<13),(1u<<21)|(1u<<7)|16);
    pair(code,20,bits(.1f),0);
    bool mixed_ok=wiiu_latte_translate_pixel(code,sizeof(code),1,p);
    if(!mixed_ok)fprintf(stderr,"mixed conversion rejected at %X\n",p->instruction_offset);
    CHECK(mixed_ok);
    for(unsigned i=0;i<p->constant_count;++i) {
        CHECK(p->constants[i].bank==16);
        for(unsigned c=0;c<4;++c)material.constants[WIIU_PIXEL_TEXTURES+i][c]=bits(p->constants[i].vector? .25f:7.0f);
    }
    for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=v[i].parameters[0][1]=.5f;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(abs(pixels[(8+1)*4]-77)<=1);
    /* Forward structured branch: both arms execute on GPU for opposite inputs. */
    memset(code,0,sizeof(code));
    pair(code,0,8,0xA4000000);pair(code,1,3,0x85000000);
    pair(code,2,9,0xA0000000);pair(code,3,5,0x86800001);
    pair(code,4,10,0xA8000000);pair(code,5,0,0x94200000);
    pair(code,8,0x80000000u|(248u<<13),(0x20<<7)|12);
    pair(code,9,0x801F00F9,0x00000C90);pair(code,10,0x801F00FC,0x00000C90);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=(float)pass;
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        fprintf(stderr,"branch pass=%u value=%u\n",pass,pixels[(8+1)*4]);
        CHECK(abs(pixels[(8+1)*4]-(pass?128:255))<=1);
    }
    word(code,24,4);CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p)); /* malformed ELSE join */
    /* Single-arm branch with a pop on the skipped JUMP and taken ALU arm. */
    memset(code,0,sizeof(code));pair(code,0,8,0xA0000000);pair(code,1,9,0xA4000000);
    pair(code,2,4,0x85000001);pair(code,3,10,0xA8000000);pair(code,4,1u<<15,0x94200000);
    pair(code,8,0x801F00FC,0x00200C90);pair(code,9,0x80000000u|(248u<<13),(0x20<<7)|12);
    pair(code,10,0x801F00F9,0x00200C90);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    for(unsigned pass=0;pass<2;++pass) {
        for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=(float)pass;
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        CHECK(abs(pixels[(8+1)*4]-(pass?128:255))<=1);
    }
    word(code,16,3);CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    word(code,16,4);CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    /* Indexed PS uniform VFETCH, with a separate bank and a draw-current upload. */
    memset(code,0,sizeof(code));pair(code,0,8,0xA0000000);pair(code,1,16,0x80800000);
    pair(code,2,1u<<15,0x94200688);pair(code,8,0x801F0000,(0x79u<<7)|16);
    word(code,128,0x3c008b40);word(code,132,0x002d1001);word(code,136,0x00080000);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));CHECK(p->block_mask==(1u<<11));
    uint32_t block[8]={bits(.25f),bits(.5f),bits(.75f),bits(1),bits(1),bits(.75f),bits(.5f),bits(.25f)};
    material.uniform_blocks[11]=(const uint8_t*)block;material.uniform_block_sizes[11]=sizeof(block);
    for(unsigned pass=0;pass<3;++pass) {
        for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=(float)(pass==2?0:pass);
        if(pass==2)block[0]=bits(.75f);
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
        CHECK(abs(pixels[(8+1)*4+2]-(pass==0?64:pass==1?255:191))<=1);
    }
    material.uniform_block_sizes[11]=17;
    CHECK(!wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    /* A high texture binding must coexist with structured uniform buffers.
       The former t16..t31 uniform range would overwrite texture slot 27. */
    uint8_t combined[160]={0};
    pair(combined,0,16,0x80800400);pair(combined,1,8,0xA0000000);
    pair(combined,2,2u<<15,0x94200000);
    pair(combined,8,0x80000001u|(2u<<13),(2u<<21)|16);
    pair(combined,16,0x1b10,0xF00D1001);pair(combined,17,0x68818000,0);
    pair(combined,18,0x3c008b40,0x002d1002);pair(combined,19,0x00080000,0);
    CHECK(wiiu_latte_translate_pixel(combined,sizeof(combined),1,p));
    CHECK(p->texture_mask==(1u<<27) && p->block_mask==(1u<<11));
    const uint8_t dark[4]={0,0,64,255};
    material.textures[27]=(WiiUWindowEffectTexture){dark,1,1,999};
    for(unsigned c=0;c<4;++c)material.constants[27][c]=c;
    material.uniform_block_sizes[11]=sizeof(block);block[0]=bits(.25f);
    for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=0;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    CHECK(abs(pixels[(8+1)*4+2]-128)<=1);
    /* Bias and signed whole-texel offsets compile and sample the actual image. */
    memset(&material,0,sizeof(material));material.program=p;
    memset(code,0,sizeof(code));pair(code,0,16,0x80800000);pair(code,1,1u<<15,0x94200688);
    pair(code,16,0x00000412,0xF00D1001);pair(code,17,0x68818002,0);
    CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    const uint8_t row[8]={0,0,255,255,0,255,0,255};
    material.textures[4]=(WiiUWindowEffectTexture){row,2,1,351};
    for(unsigned c=0;c<4;++c)material.constants[4][c]=c;
    for(unsigned i=0;i<3;++i){v[i].parameters[0][0]=.25f;v[i].parameters[0][1]=.5f;v[i].parameters[0][3]=0;}
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4+1]==255);
    word(code,136,0x68818003);CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p)); /* half texel */
    word(code,136,0x6881801e);CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=.75f;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4+2]==255);
    /* Raw guest floating-point textures must retain HDR/negative channels
       through upload and native sampling, not pass through BGRA8. */
    const float hdr_row[8]={16,-2,.125f,.5f,8,4,2,1};
    material.textures[4]=(WiiUWindowEffectTexture){(const uint8_t*)hdr_row,2,1,352};
    material.textures[4].rgba_float=true;
    CHECK(wiiu_window_gpu_configure(99,8,8,0x823));
    float clear_hdr[4]={0},hdr_pixels[8*8*4];
    CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));
    CHECK(hdr_pixels[(8+1)*4]==16 && hdr_pixels[(8+1)*4+1]==-2);
    CHECK(hdr_pixels[(8+1)*4+2]==.125f && hdr_pixels[(8+1)*4+3]==.5f);
    /* SPI position input is generated by rasterization, not a VS varying. */
    memset(code,0,sizeof(code));pair(code,0,0,0x94200688);
    CHECK(wiiu_latte_translate_pixel_position(code,8,1,0,p));
    CHECK(!p->input_mask[0]);
    CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));
    CHECK(hdr_pixels[(8+1)*4]==1.5f && hdr_pixels[(8+1)*4+1]==1.5f);
    CHECK(!wiiu_latte_translate_pixel_position(code,8,1,1,p));
    /* Same projected triangle, unequal clip W: linear interpolation must not
       accidentally reuse the perspective shader from the native cache. */
    WiiUWindowPostVertex interp[3]={0};
    const float clip_w[3]={2,4,2};
    for(unsigned i=0;i<3;++i) {
        interp[i].position[0]=xy[i][0]*clip_w[i]/2;
        interp[i].position[1]=xy[i][1]*clip_w[i]/2;
        interp[i].position[3]=clip_w[i];
        interp[i].parameters[0][0]=i==1?1:0;
        interp[i].parameters[0][3]=1;
    }
    uint32_t perspective_key=0;
    for(unsigned linear=0;linear<2;++linear) {
        CHECK(wiiu_latte_translate_pixel_interpolation(code,8,1,UINT32_MAX,0,linear,p));
        if(!linear)perspective_key=p->key;else CHECK(p->key!=perspective_key);
        CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,interp,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));
        CHECK(fabsf(hdr_pixels[(8+1)*4]-(linear?.375f:3.0f/13.0f))<.0001f);
    }
    CHECK(!wiiu_latte_translate_pixel_interpolation(code,8,1,UINT32_MAX,0,2,p));
    /* Menu shaders index the uniform-register bank through MOVA address lanes. */
    const unsigned register_base=WIIU_PIXEL_TEXTURES+WIIU_PIXEL_CONSTANTS+1;
    material.constants[register_base+1][0]=bits(.25f);
    material.constants[register_base+256+1][0]=1;
    for(unsigned integer=0;integer<2;++integer)for(unsigned lane=0;lane<4;++lane) {
        memset(code,0,sizeof(code));pair(code,0,8,0xA0040000);
        pair(code,1,1u<<15,0x94200B20);
        pair(code,8,integer?0x80000100u:0x80000000u,((integer?0x18:0x16)<<7)|(lane<<29));
        material.constants[WIIU_PIXEL_TEXTURES][0]=UINT32_MAX;
        pair(code,9,0x80000302u|(lane<<26),(1u<<21)|(0x19<<7)|16);
        CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p) && p->relative_uniforms);
        for(unsigned i=0;i<3;++i) {
            uint32_t value=integer?UINT32_MAX:bits(-.25f);
            memcpy(&v[i].parameters[0][0],&value,4);
        }
        CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
        CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));
        if(hdr_pixels[(8+1)*4]!=.25f)fprintf(stderr,"MOVA integer=%u lane=%u value=%g\n",integer,lane,hdr_pixels[(8+1)*4]);
        CHECK(hdr_pixels[(8+1)*4]==.25f);
    }
    material.constants[register_base+256+1][0]=0;
    CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));CHECK(hdr_pixels[(8+1)*4]==0);
    /* Undefined MOV results may flow only to unattached outputs, never pixels. */
    memset(code,0,sizeof(code));pair(code,0,8,0xA0000000);
    pair(code,1,0,0x94000B20);pair(code,2,(1u<<15)|1,0x94200B20);
    pair(code,8,0x80000002u,(1u<<21)|(0x19<<7)|16);
    CHECK(wiiu_latte_translate_pixel_targets(code,sizeof(code),1,UINT32_MAX,0,0,0,1,p));
    CHECK(p->target_mask==1);
    for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=.75f;
    CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));CHECK(hdr_pixels[(8+1)*4]==.75f);
    CHECK(!wiiu_latte_translate_pixel_targets(code,sizeof(code),1,UINT32_MAX,0,0,0,3,p));
    CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    pair(code,1,1u<<15,0x94000B20);
    CHECK(!wiiu_latte_translate_pixel_targets(code,sizeof(code),1,UINT32_MAX,0,0,0,1,p));
    /* RGB-only attachments do not consume undefined alpha. Defined output
       lanes remain strict; enabling alpha must reject this same program. */
    memset(code,0,sizeof(code));pair(code,0,8,0xA0000000);
    pair(code,1,0,0x94200688);
    pair(code,8,0x80000C02u,0x60000C90u); /* MOV R2.w -> R0.w */
    CHECK(wiiu_latte_translate_pixel_channels(code,sizeof(code),1,UINT32_MAX,0,0,0,7,p));
    CHECK(!wiiu_latte_translate_pixel_channels(code,sizeof(code),1,UINT32_MAX,0,0,0,15,p));
    /* CUBE is a vector reduction: T,S,signed twice-major-axis,face.
       Verify raw (not normalized) output, all faces, ties and modifiers. */
    const float directions[][3]={{4,1,2},{-4,1,2},{1,4,2},{1,-4,2},
        {1,2,4},{1,2,-4},{4,4,1},{4,4,4},{0,0,0}};
    const float expected[][4]={{-1,2,8,0},{-1,-2,-8,1},{2,1,8,2},
        {-2,1,-8,3},{-2,1,8,4},{-2,-1,-8,5},{1,4,8,2},{-4,4,8,4},{0,0,0,4}};
    const unsigned src0[4]={2,2,0,1},src1[4]={1,0,2,2};
    memset(code,0,sizeof(code));pair(code,0,8,0xA00C0000);pair(code,1,2u<<15,0x94200688);
    for(unsigned modified=0;modified<2;++modified) {
        for(unsigned c=0;c<4;++c)pair(code,8+c,(c==3?0x80000000u:0)|
            (src0[c]<<10)|(src1[c]<<23),(2u<<21)|(c<<29)|(0x52<<7)|16|
            (modified?0x80000020u:0));
        CHECK(wiiu_latte_translate_pixel(code,sizeof(code),1,p));
        CHECK(p->input_mask[0]==7);
        for(unsigned face=0;face<9;++face) {
            for(unsigned vertex=0;vertex<3;++vertex)
                for(unsigned c=0;c<3;++c)v[vertex].parameters[0][c]=directions[face][c];
            CHECK(wiiu_window_gpu_clear_float(99,8,8,clear_hdr));
            CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
            CHECK(wiiu_window_gpu_readback_float(99,hdr_pixels,8,8));
            for(unsigned c=0;c<4;++c) {
                float want=expected[face][c];
                if(modified && c!=3)want=fminf(1,fmaxf(0,want*2));
                CHECK(fabsf(hdr_pixels[(8+1)*4+c]-want)<.0001f);
            }
        }
    }
    pair(code,0,8,0xA0080000);word(code,10*8,0x80000000u|(src0[2]<<10)|(src1[2]<<23));
    CHECK(!wiiu_latte_translate_pixel(code,sizeof(code),1,p));
    puts("CUBE: six faces, signed major axis, ties, zero and face-safe modifiers verified");
    free(p);puts("Latte PS arithmetic, branches, 16 inputs, MRT, resources, constants and GPU pixels verified");return 0;
}
