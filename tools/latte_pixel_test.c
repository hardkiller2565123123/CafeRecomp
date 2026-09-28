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
    bool translated=wiiu_latte_translate_pixel(bytes,size,inputs,p);
    if(audit) {
        unsigned at=p->instruction_offset;
        printf("PS %u %s inputs=%u bytes=%zu offset=%X instruction=",id,translated?"accepted":"rejected",inputs,size,at);
        for(unsigned i=0;i<8 && at+i<size;++i)printf("%02X",bytes[at+i]);
        puts("");free(p);return translated?0:1;
    }
    CHECK(translated);
    WiiUWindowSceneMaterial material={0};material.program=p;
    uint8_t* blocks[16]={0};
    for(unsigned i=0;i<16;++i)if(p->block_mask&(1u<<i)) {
        snprintf(name,sizeof(name),"%s/botw_variant_%u_ps_block_%u.bin",dir,id,i);
        f=fopen(name,"rb");CHECK(f);blocks[i]=malloc(65536);CHECK(blocks[i]);
        size_t bytes_read=fread(blocks[i],1,65536,f);fclose(f);CHECK(bytes_read && !(bytes_read%16));
        material.uniform_blocks[i]=blocks[i];material.uniform_block_sizes[i]=(uint32_t)bytes_read;
    }
    const uint8_t tex[4]={96,64,128,255};
    for(unsigned i=0;i<16;++i) {
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
    uint8_t code[144]={0};WiiULattePixelProgram* p=malloc(sizeof(*p));CHECK(p);
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
    CHECK(wiiu_window_gpu_upload_bgra(100,1,1,tex,4));
    material.source_surfaces[4]=100;material.textures[4].pixels=NULL;
    material.constants[4][0]=2;material.constants[4][2]=0;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));
    CHECK(pixels[(8+1)*4]==128 && pixels[(8+1)*4+2]==32);
    material.source_surfaces[4]=99;
    CHECK(!wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
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
        material.constants[16][0]=bits(pass?.75f:.25f);
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
    material.constants[16][0]=17;material.constants[16][1]=23;
    CHECK(wiiu_window_gpu_draw_scene(99,8,8,&material,v,3,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(99,pixels,8,8,32));CHECK(pixels[(8+1)*4]==102);
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
    free(p);puts("Latte PS arithmetic, branches, 16 inputs, MRT, resources, constants and GPU pixels verified");return 0;
}
