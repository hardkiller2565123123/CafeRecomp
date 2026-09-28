#include "wiiu_gx2.h"
#include "wiiu_memory.h"
#include "wiiu_window.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while(0)
static int load(CPUState* cpu,uint32_t address,const char* dir,unsigned variant,const char* suffix) {
    char path[2048];snprintf(path,sizeof(path),"%s/botw_guide_variant_%u_%s.bin",dir,variant,suffix);
    FILE* f=fopen(path,"rb");if(!f)return 0;
    unsigned i=0;int c;
    while((c=fgetc(f))!=EOF && i<16384)mem_write8(cpu,address+i++,(u8)c);
    fclose(f);return i;
}
static void write_float(CPUState* cpu,u32 a,float f) {u32 b;memcpy(&b,&f,4);mem_write32(cpu,a,b);}
static void draw_pass(CPUState* cpu,unsigned variant,u32 indices) {
    if(variant==36) {
        cpu->gpr[3]=4;cpu->gpr[4]=3;cpu->gpr[5]=0;cpu->gpr[6]=1;
        wiiu_gx2_handle_import(cpu,"GX2DrawEx");
    } else {
        cpu->gpr[3]=4;cpu->gpr[4]=6;cpu->gpr[5]=4;cpu->gpr[6]=indices;cpu->gpr[7]=0;cpu->gpr[8]=1;
        wiiu_gx2_handle_import(cpu,"GX2DrawIndexedEx");
    }
}
static int run(const char* directory,unsigned variant) {
    WiiUMemory memory;CPUState cpu={0};wiiu_memory_init(&memory);
    const u32 base=0x11000000,color=base,texture=base+256,vs=base+512,ps=base+1024,
        fs=base+1536,vp=base+0x2000,pp=base+0x4000,fp=base+0x6000,
        vertices=base+0x8000,indices=base+0x9000,uniforms=base+0xA000;
    CHECK(wiiu_memory_add_segment(&memory,"post capture",base,0x20000,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);wiiu_gx2_reset();
    cpu.gpr[3]=base+0xC000;cpu.gpr[4]=0;
    wiiu_gx2_handle_import(&cpu,"GX2SetupContextStateEx");
    CHECK(load(&cpu,vs,directory,variant,"vs_descriptor"));
    CHECK(load(&cpu,ps,directory,variant,"ps_descriptor"));
    CHECK(load(&cpu,fs,directory,variant,"fetch_descriptor"));
    CHECK(load(&cpu,vp,directory,variant,"vs_program"));
    CHECK(load(&cpu,pp,directory,variant,"ps_program"));
    CHECK(load(&cpu,fp,directory,variant,"fetch_program"));
    mem_write32(&cpu,vs+0xD4,vp);mem_write32(&cpu,ps+0xA8,pp);mem_write32(&cpu,fs+12,fp);
    for(u32 d=color;d<=texture;d+=256) {
        u32 w=d==color?8:4;
        mem_write32(&cpu,d+4,w);mem_write32(&cpu,d+8,w);mem_write32(&cpu,d+12,1);
        mem_write32(&cpu,d+0x14,0x1A);mem_write32(&cpu,d+0x20,w*w*4);
        mem_write32(&cpu,d+0x24,d+0x10000);mem_write32(&cpu,d+0x30,16);
        mem_write32(&cpu,d+0x3C,w);mem_write32(&cpu,d+0x84,0x00010203);
    }
    for(u32 i=0;i<16;++i)mem_write32(&cpu,texture+0x10000+i*4,0x804020FF);
    cpu.gpr[3]=color;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetColorBuffer");
    cpu.gpr[3]=color;cpu.fpr[1]=cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
    wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
    cpu.gpr[3]=texture;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetPixelTexture");
    cpu.gpr[3]=vs;wiiu_gx2_handle_import(&cpu,"GX2SetVertexShader");
    cpu.gpr[3]=ps;wiiu_gx2_handle_import(&cpu,"GX2SetPixelShader");
    cpu.gpr[3]=fs;wiiu_gx2_handle_import(&cpu,"GX2SetFetchShader");
    write_float(&cpu,uniforms,0.01f);write_float(&cpu,uniforms+8,0.01f);write_float(&cpu,uniforms+12,0.01f);
    for(u32 i=0;i<4;++i)write_float(&cpu,uniforms+16+i*20,1);
    /* Match the real blur pass's near-plane position, Z=-W. */
    write_float(&cpu,uniforms+16+2*16+12,-1);
    cpu.gpr[3]=0;cpu.gpr[4]=64;cpu.gpr[5]=uniforms;
    wiiu_gx2_handle_import(&cpu,"GX2SetVertexUniformReg");
    const float xy[4][2]={{-0.5f,0.5f},{0.5f,0.5f},{-0.5f,-0.5f},{0.5f,-0.5f}};
    for(u32 i=0;i<4;++i) {
        write_float(&cpu,vertices+i*32,variant==36 && i==1?1.5f:xy[i][0]);
        write_float(&cpu,vertices+i*32+4,variant==36 && i==2?-1.5f:xy[i][1]);
        write_float(&cpu,vertices+i*32+20,1);
    }
    const u16 order[6]={0,1,2,2,1,3};
    for(u32 i=0;i<6;++i)mem_write16(&cpu,indices+i*2,order[i]);
    cpu.gpr[3]=0;cpu.gpr[4]=128;cpu.gpr[5]=32;cpu.gpr[6]=vertices;
    wiiu_gx2_handle_import(&cpu,"GX2SetAttribBuffer");
    draw_pass(&cpu,variant,indices);
    u8 pixels[8*8*4];CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
    unsigned at=(4*8+4)*4;
    CHECK(pixels[at]==32 && pixels[at+1]==64 && pixels[at+2]==128 && pixels[at+3]==255);
    if(variant==34 || variant==35)CHECK(pixels[0]==0 && pixels[1]==0 && pixels[2]==0);
    if(variant==36) {
        /* The game reuses one descriptor while ping-ponging between images.
           The source lives only on the GPU: its guest bytes remain zero. */
        const u32 source_formats[]={0x1Au,0x19u,0x816u,0x820u};
        for(unsigned ping=0;ping<4;++ping) {
            u32 source=base+0x10000+(ping&1?0x1000:0);
            u32 destination=base+0x10000+(ping&1?0:0x1000);
            /* Scene captures use the same exact copy shader with these
               host-rendered surface formats. Guest backing bytes are zero. */
            mem_write32(&cpu,texture+0x14,source_formats[ping]);
            mem_write32(&cpu,texture+4,8);mem_write32(&cpu,texture+8,8);
            mem_write32(&cpu,texture+0x20,256);mem_write32(&cpu,texture+0x3C,8);
            mem_write32(&cpu,texture+0x24,source);
            cpu.gpr[3]=texture;cpu.gpr[4]=0;
            wiiu_gx2_handle_import(&cpu,"GX2SetPixelTexture");
            mem_write32(&cpu,color+0x24,destination);
            cpu.gpr[3]=color;cpu.gpr[4]=0;
            wiiu_gx2_handle_import(&cpu,"GX2SetColorBuffer");
            cpu.gpr[3]=color;cpu.fpr[1]=cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
            wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
            draw_pass(&cpu,variant,indices);
            CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
            CHECK(pixels[at]==32 && pixels[at+1]==64 && pixels[at+2]==128);
        }
        printf("GPU-only postprocessing ping-pong preserved four source formats\n");
        /* Alter only TEX padding, bypassing the exact known-program hash.
           The general PS translator must execute the same real GX2 draw. */
        mem_write32(&cpu,pp+140,0x10203040);
        mem_write32(&cpu,texture,1);
        cpu.gpr[3]=texture;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetPixelTexture");
        cpu.gpr[3]=color;cpu.fpr[1]=cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
        wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
        draw_pass(&cpu,variant,indices);
        CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
        CHECK(pixels[at]==32 && pixels[at+1]==64 && pixels[at+2]==128);
        CHECK(load(&cpu,pp,directory,variant,"ps_program"));
    }
    if(variant==34) for(unsigned test=0;test<4;++test) {
        cpu.gpr[3]=color;cpu.fpr[1]=cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
        wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
        if(test==1) {
            cpu.gpr[3]=base+0xB000;cpu.gpr[4]=256;
            wiiu_gx2_handle_import(&cpu,"GX2BeginDisplayList");
        }
        cpu.gpr[3]=test==3?0:1;cpu.gpr[4]=test>=2?0:1;cpu.gpr[5]=1;
        wiiu_gx2_handle_import(&cpu,test==1?"GX2SetRasterizerClipControl":"GX2SetRasterizerClipControlEx");
        if(test==0) {
            /* Save HalfZ in A, initialize B (full Z), then restore A. */
            cpu.gpr[3]=base+0xD000;cpu.gpr[4]=0;
            wiiu_gx2_handle_import(&cpu,"GX2SetupContextStateEx");
            cpu.gpr[3]=base+0xC000;wiiu_gx2_handle_import(&cpu,"GX2SetContextState");
        }
        if(test==1) {
            cpu.gpr[3]=base+0xB000;wiiu_gx2_handle_import(&cpu,"GX2EndDisplayList");
            cpu.gpr[4]=cpu.gpr[3];cpu.gpr[3]=base+0xB000;
            wiiu_gx2_handle_import(&cpu,"GX2CallDisplayList");
        }
        draw_pass(&cpu,variant,indices);
        CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
        CHECK(pixels[at+2]==(test==0 || test==3?0:128));
    }
    cpu.gpr[3]=1;cpu.gpr[4]=1;wiiu_gx2_handle_import(&cpu,"GX2SetRasterizerClipControl");
    /* A known PS with unsupported vertex control flow must not trigger the
       legacy rectangle fallback (especially nonindexed GX2DrawEx). */
    mem_write32(&cpu,vp+4,0x00000085); /* little-endian CF JUMP */
    cpu.gpr[3]=color;cpu.fpr[1]=cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
    wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
    draw_pass(&cpu,variant,indices);
    CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
    CHECK(pixels[at]==0 && pixels[at+1]==0 && pixels[at+2]==0);
    if(variant==36) {
        /* Unknown nonindexed shaders must not blit a stale bound texture
           across the target (the yellow/black fullscreen fallback bug). */
        CHECK(load(&cpu,vp,directory,variant,"vs_program"));
        mem_write32(&cpu,pp,mem_read32(&cpu,pp)^0x00000001u);
        cpu.gpr[3]=color;cpu.fpr[1]=0.25;cpu.fpr[2]=0.5;cpu.fpr[3]=0.75;cpu.fpr[4]=1;
        wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
        draw_pass(&cpu,variant,indices);
        CHECK(wiiu_gx2_readback_color_buffer(color,pixels,8,8,32));
        CHECK(pixels[at]==191 && pixels[at+1]==128 && pixels[at+2]==64);
    }
    wiiu_memory_free(&memory);printf("captured GX2 pass %u rendered verified pixels\n",variant);return 0;
}
int main(int argc,char** argv) {
    if(argc!=2){fprintf(stderr,"usage: postprocess_capture_test capture-directory\n");return 2;}
    for(unsigned variant=33;variant<=36;++variant)CHECK(!run(argv[1],variant));
    return 0;
}
