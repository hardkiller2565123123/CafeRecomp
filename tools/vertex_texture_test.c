#include "wiiu_gx2.h"
#include "wiiu_gx2_software.h"
#include "wiiu_latte_execute.h"
#include "wiiu_memory.h"
#include "wiiu_window.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1;}}while(0)
static void le(CPUState* cpu,u32 a,u32 v){for(u32 c=0;c<4;++c)mem_write8(cpu,a+c,(u8)(v>>(c*8)));}
static u32 bits(float f){u32 b;memcpy(&b,&f,4);return b;}
static void pair(u8* p,u32 index,u32 a,u32 b){for(u32 c=0;c<4;++c){p[index*8+c]=(u8)(a>>(c*8));p[index*8+4+c]=(u8)(b>>(c*8));}}
int main(void) {
    WiiUMemory memory;CPUState cpu={0};wiiu_memory_init(&memory);
    const u32 base=0x11000000,texture=base,sampler=base+256,data=base+4096;
    CHECK(wiiu_memory_add_segment(&memory,"vertex texture fixture",base,65536,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);wiiu_gx2_reset();
    const u32 point=2|(2<<3),linear=point|(1<<9)|(1<<12);
    le(&cpu,data,bits(-2));le(&cpu,data+4,bits(4));
    WiiUGX2TextureView view={data,8,2,1,1,0x80e,16,0,2,0x00040505,0};float out[4];
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,point,.25f,.5f,out) && out[0]==-2);
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,point,.75f,.5f,out) && out[0]==4);
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,linear,.5f,.5f,out) && out[0]==1);
    CHECK(out[1]==0 && out[2]==1 && out[3]==1);
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,0,1.25f,.5f,out) && out[0]==-2);
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,1,1.25f,.5f,out) && out[0]==4);
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,6,-1,.5f,out) && out[0]==0 && out[3]==0);
    CHECK(!wiiu_gx2_software_sample_float(&cpu,&view,4,.5f,.5f,out));
    CHECK(!wiiu_gx2_software_sample_float(&cpu,&view,point,NAN,.5f,out));
    view.image_size=4;CHECK(!wiiu_gx2_software_sample_float(&cpu,&view,point,.75f,.5f,out));view.image_size=8;
    /* Half floats preserve HDR, negative and subnormal values. */
    le(&cpu,data,0x4C00C000);le(&cpu,data+4,0x3C000001);
    view.width=view.pitch=1;view.format=0x820;view.comp_map=0x00010203;
    CHECK(wiiu_gx2_software_sample_float(&cpu,&view,point,.5f,.5f,out));
    CHECK(out[0]==-2 && out[1]==16 && out[2]==ldexpf(1,-24) && out[3]==1);
    /* GPU readback sampling applies the view swizzle exactly once, preserves
       RG defaults, and uses the same address/filter rules as guest textures. */
    const u8 bgra[8]={200,64,0,17,100,128,255,33};
    view.width=2;view.format=7;view.comp_map=0x00010203;
    CHECK(wiiu_gx2_software_sample_bgra(bgra,&view,linear,.5f,.5f,out));
    CHECK(out[0]==.5f && fabsf(out[1]-96/255.0f)<.00001f && out[2]==0 && out[3]==1);
    view.comp_map=0x01000504;
    CHECK(wiiu_gx2_software_sample_bgra(bgra,&view,point,.25f,.5f,out));
    CHECK(out[0]==64/255.0f && out[1]==0 && out[2]==1 && out[3]==0);
    CHECK(wiiu_gx2_software_sample_bgra(bgra,&view,6,-1,.5f,out) && out[3]==0);
    view.format=0x820;CHECK(!wiiu_gx2_software_sample_bgra(bgra,&view,point,0,0,out));
    /* Real GX2 texture/sampler binding, recording and context restoration. */
    cpu.gpr[3]=base+0x2000;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetupContextStateEx");
    le(&cpu,data,bits(-2));le(&cpu,data+4,bits(4));
    mem_write32(&cpu,texture,1);mem_write32(&cpu,texture+4,2);mem_write32(&cpu,texture+8,1);
    mem_write32(&cpu,texture+12,1);mem_write32(&cpu,texture+0x14,0x80e);
    mem_write32(&cpu,texture+0x20,8);mem_write32(&cpu,texture+0x24,data);
    mem_write32(&cpu,texture+0x30,16);mem_write32(&cpu,texture+0x3c,2);
    mem_write32(&cpu,texture+0x84,0x00040505);
    cpu.gpr[3]=texture;cpu.gpr[4]=3;wiiu_gx2_handle_import(&cpu,"GX2SetVertexTexture");
    mem_write32(&cpu,sampler,linear);mem_write32(&cpu,sampler+4,14u<<16);
    cpu.gpr[3]=sampler;cpu.gpr[4]=5;wiiu_gx2_handle_import(&cpu,"GX2SetVertexSampler");
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out) && out[0]==1);
    CHECK(!wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,1,out));
    CHECK(!wiiu_gx2_sample_vertex_texture(&cpu,3,6,.5f,.5f,0,out));
    /* Captured scene opcode pattern: SAMPLE_L with separate texture/sampler. */
    u8 program[144]={0};pair(program,0,16,0x80800000);pair(program,1,0xA03C,0x94200688);
    pair(program,16,0x00000311,0xF00D1001);pair(program,17,0x68828000,0xDEADFEC);
    WiiULatteVertexInputs in={0};WiiULatteVertexOutputs vertex;
    in.valid[0]=15;in.gpr[0][0]=in.gpr[0][1]=bits(.5f);
    in.sample_texture=wiiu_gx2_sample_vertex_texture;in.texture_user=&cpu;
    CHECK(wiiu_latte_execute_vertex(program,sizeof(program),&in,&vertex));
    CHECK(vertex.position[0]==1 && vertex.position[1]==0 && vertex.position[2]==1 && vertex.position[3]==1);
    pair(program,16,0x00000310,0xF00D1001);
    CHECK(wiiu_latte_execute_vertex(program,sizeof(program),&in,&vertex) && vertex.position[0]==1);
    pair(program,16,0x00000311,0xF00D1001);
    in.sample_texture=NULL;CHECK(!wiiu_latte_execute_vertex(program,sizeof(program),&in,&vertex));
    in.sample_texture=wiiu_gx2_sample_vertex_texture;in.gpr[0][3]=bits(1);
    CHECK(!wiiu_latte_execute_vertex(program,sizeof(program),&in,&vertex));
    in.gpr[0][3]=0;
    cpu.gpr[3]=base+0x5000;cpu.gpr[4]=256;wiiu_gx2_handle_import(&cpu,"GX2BeginDisplayList");
    mem_write32(&cpu,sampler,point);cpu.gpr[3]=sampler;cpu.gpr[4]=5;
    wiiu_gx2_handle_import(&cpu,"GX2SetVertexSampler");
    cpu.gpr[3]=base+0x5000;wiiu_gx2_handle_import(&cpu,"GX2EndDisplayList");u32 used=cpu.gpr[3];
    mem_write32(&cpu,sampler,linear);
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out) && out[0]==1);
    cpu.gpr[3]=base+0x5000;cpu.gpr[4]=used;wiiu_gx2_handle_import(&cpu,"GX2CallDisplayList");
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out) && out[0]==4);
    cpu.gpr[3]=base+0x3000;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetupContextStateEx");
    CHECK(!wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out));
    cpu.gpr[3]=base+0x2000;wiiu_gx2_handle_import(&cpu,"GX2SetContextState");
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out) && out[0]==4);
    /* Sample an actual GPU render target, then change it to check that the
       CPU readback cache never returns the previous render pass. */
    mem_write32(&cpu,texture+0x14,7);
    mem_write32(&cpu,texture+0x84,0x00010203);
    cpu.gpr[3]=texture;cpu.gpr[4]=3;wiiu_gx2_handle_import(&cpu,"GX2SetVertexTexture");
    for(unsigned pass=0;pass<2;++pass) {
        cpu.gpr[3]=texture;cpu.fpr[1]=pass;cpu.fpr[2]=1-pass;
        cpu.fpr[3]=.75;cpu.fpr[4]=.25;
        wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
        for(unsigned repeat=0;repeat<2;++repeat) {
            CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out));
            CHECK(out[0]==pass && out[1]==1-pass && out[2]==0 && out[3]==1);
        }
    }
    /* GX2 descriptor/clear routing must retain float values too. */
    mem_write32(&cpu,texture+0x14,0x820);
    cpu.gpr[3]=texture;cpu.fpr[1]=16;cpu.fpr[2]=-2;cpu.fpr[3]=.125;cpu.fpr[4]=.5;
    wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
    float hdr[8];CHECK(wiiu_window_gpu_readback_float(data,hdr,2,1));
    for(unsigned p=0;p<2;++p)CHECK(hdr[p*4]==16 && hdr[p*4+1]==-2 && hdr[p*4+2]==.125f && hdr[p*4+3]==.5f);
    cpu.gpr[3]=texture;cpu.gpr[4]=3;wiiu_gx2_handle_import(&cpu,"GX2SetVertexTexture");
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out));
    CHECK(out[0]==16 && out[1]==-2 && out[2]==.125f && out[3]==.5f);
    cpu.gpr[3]=texture;cpu.fpr[1]=32;wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
    CHECK(wiiu_gx2_sample_vertex_texture(&cpu,3,5,.5f,.5f,0,out) && out[0]==32);
    /* Exercise host GX2 resource retention as well as the native GPU cache. */
    mem_write32(&cpu,texture+4,1);mem_write32(&cpu,texture+0x14,0x1a);
    mem_write32(&cpu,texture+0x3c,1);
    for(unsigned i=0;i<128;++i) {
        mem_write32(&cpu,texture+0x24,base+0x6000+i*16);
        cpu.gpr[3]=texture;cpu.fpr[1]=i/255.0;cpu.fpr[2]=cpu.fpr[3]=0;cpu.fpr[4]=1;
        wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
    }
    for(unsigned i=0;i<128;++i) {
        u8 pixel[4];CHECK(wiiu_window_gpu_readback(base+0x6000+i*16,pixel,1,1,4));
        CHECK(pixel[2]==i && pixel[3]==255);
    }
    wiiu_gx2_reset();wiiu_memory_free(&memory);
    puts("Vertex TEX: float precision, filtering, bindings, replay and context verified");return 0;
}
