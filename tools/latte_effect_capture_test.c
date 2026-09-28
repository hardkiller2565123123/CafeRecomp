#include "wiiu_latte_execute.h"
#include "wiiu_latte_fetch.h"
#include "wiiu_memory.h"
#include "wiiu_gx2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Offline replay of locally captured effect VS input, never packaged game data. */
#define CHECK(x) do { if(!(x)) {fprintf(stderr,"variant %u line %d: %s\n",variant,__LINE__,#x);return 1;} } while(0)
static unsigned load(const char* dir,const char* name,u8* bytes,unsigned capacity) {
    char path[2048];snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE* f=fopen(path,"rb");if(!f)return 0;
    size_t size=fread(bytes,1,capacity,f);int tail=fgetc(f);fclose(f);
    return tail==EOF?(unsigned)size:0;
}
static u32 be(const u8* p) {return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static int replay(const char* dir,const char* log,unsigned variant) {
    WiiUMemory memory;CPUState cpu={0};wiiu_memory_init(&memory);
    CHECK(wiiu_memory_add_segment(&memory,"effect capture",0x11000000,0x200000,NULL,true));
    wiiu_memory_bind_cpu(&memory,&cpu);
    u8 descriptor[512],fetch_desc[32],program[16384],fetch[512];char name[128];
    snprintf(name,sizeof(name),"botw_guide_variant_%u_vs_descriptor.bin",variant);
    CHECK(load(dir,name,descriptor,sizeof(descriptor))>=0xd8);
    snprintf(name,sizeof(name),"botw_guide_variant_%u_vs_program.bin",variant);
    unsigned size=load(dir,name,program,sizeof(program));CHECK(size);
    snprintf(name,sizeof(name),"botw_guide_variant_%u_fetch_descriptor.bin",variant);
    CHECK(load(dir,name,fetch_desc,sizeof(fetch_desc))==32);
    snprintf(name,sizeof(name),"botw_guide_variant_%u_fetch_program.bin",variant);
    unsigned fs=load(dir,name,fetch,sizeof(fetch));CHECK(fs);
    for(unsigned i=0;i<fs;++i)mem_write8(&cpu,0x11000000+i,fetch[i]);
    u32 divisors[2]={be(fetch_desc+24),be(fetch_desc+28)},count=0;
    WiiULatteFetchStream streams[16];
    CHECK(wiiu_latte_fetch_parse_program(&cpu,0x11000000,fs,divisors,be(fetch_desc+20),streams,16,&count));
    u32 registers[1024]={0};u8 valid[1024]={0};
    unsigned strides[16]={0};strides[0]=16;
    FILE* f=fopen(log,"rb");CHECK(f);char line[4096];
    while(fgets(line,sizeof(line),f)) {
        unsigned id,at,a,b,c,d,slot,address,bytes,stride;
        if(sscanf(line,"gx2: guide variant_%u_vs c[%u]=%x,%x,%x,%x",&id,&at,&a,&b,&c,&d)==6 && id==variant && at<=1020) {
            registers[at]=a;registers[at+1]=b;registers[at+2]=c;registers[at+3]=d;
            memset(valid+at,1,4);
        }
        if(sscanf(line,"gx2: variant=%u attrib=%u data=%x size=%u stride=%u",&id,&slot,&address,&bytes,&stride)==5 && id==variant && slot<16)
            strides[slot]=stride;
    }
    fclose(f);
    WiiULatteVertexInputs in={0};in.uniforms=registers;in.uniform_valid=valid;in.uniform_count=1024;
    WiiULatteFetchBuffer buffers[16]={0};
    u8* data=malloc(65536);CHECK(data);
    for(unsigned slot=0;slot<16;++slot) {
        snprintf(name,sizeof(name),slot?"botw_variant_%u_attrib_%u.bin":"botw_variant_%u_vertices.bin",variant,slot);
        unsigned bytes=load(dir,name,data,65536);
        if(bytes && strides[slot]) {
            u32 addr=0x11010000+slot*65536;
            for(unsigned i=0;i<bytes;++i)mem_write8(&cpu,addr+i,data[i]);
            buffers[slot]=(WiiULatteFetchBuffer){true,bytes,strides[slot],addr};
        }
        snprintf(name,sizeof(name),"botw_variant_%u_vs_block_%u.bin",variant,slot);
        bytes=load(dir,name,data,65536);
        if(bytes) {
            u8* block=malloc(bytes);CHECK(block);memcpy(block,data,bytes);
            in.blocks[slot]=block;in.block_sizes[slot]=bytes;
        }
    }
    free(data);
    unsigned vertex_count=variant==29?buffers[0].size/buffers[0].stride:4;
    if(buffers[0].stride && buffers[0].size/buffers[0].stride<vertex_count)
        vertex_count=buffers[0].size/buffers[0].stride;
    CHECK(vertex_count && vertex_count<=4096);
    for(unsigned index=0;index<vertex_count;++index) {
        WiiULatteFetchedVertex fetched;
        CHECK(wiiu_latte_fetch_vertex(&cpu,streams,count,buffers,16,index,0,&fetched));
        CHECK(!fetched.failed_count);memset(in.valid,0,sizeof(in.valid));
        in.gpr[0][0]=index;in.gpr[0][1]=in.gpr[0][2]=in.gpr[0][3]=0;in.valid[0]=15;
        for(unsigned j=0;j<32;++j) {
            u32 semantic=be(descriptor+0x44+j*4);
            if(semantic<128 && fetched.valid[semantic]) {
                memcpy(in.gpr[j+1],fetched.gpr[semantic],16);in.valid[j+1]=15;
            }
        }
        WiiULatteVertexOutputs out;
        const char* trace=getenv("BOTW_LATTE_TRACE");
        if(index==0 && trace && *trace) for(unsigned reg=1;reg<33;++reg)
            if(in.valid[reg]) printf("R%u=%08X,%08X,%08X,%08X\n",reg,in.gpr[reg][0],in.gpr[reg][1],in.gpr[reg][2],in.gpr[reg][3]);
        CHECK(wiiu_latte_execute_vertex(program,size,&in,&out));
        if(index<4)printf("effect VS %u vertex %u clip=(%g,%g,%g,%g) masks=%x,%x,%x,%x\n",variant,index,
            out.position[0],out.position[1],out.position[2],out.position[3],
            out.parameter_mask[0],out.parameter_mask[1],out.parameter_mask[2],out.parameter_mask[3]);
    }
    printf("effect VS %u executed all %u captured vertices\n",variant,vertex_count);
    if(variant>=25 && variant<=30) {
        /* Reuse the captured VS/fetch and actual uniform blocks through GX2.
           White test textures isolate geometry/material execution from texture
           decoding; clearing magenta makes an accidentally skipped draw fail. */
        const u32 target=0x11000100,tex=0x11001000,vs=0x11000400,ps=0x11000800,fs_desc=0x11000a00;
        u8 pixel_desc[256],pixel_program[1024];
        snprintf(name,sizeof(name),"botw_guide_variant_%u_ps_descriptor.bin",variant);
        CHECK(load(dir,name,pixel_desc,sizeof(pixel_desc))>=0xac);
        snprintf(name,sizeof(name),"botw_guide_variant_%u_ps_program.bin",variant);
        unsigned ps_size=load(dir,name,pixel_program,sizeof(pixel_program));CHECK(ps_size);
        for(unsigned i=0;i<0x134;++i)mem_write8(&cpu,vs+i,descriptor[i]);
        for(unsigned i=0;i<0xec;++i)mem_write8(&cpu,ps+i,pixel_desc[i]);
        for(unsigned i=0;i<32;++i)mem_write8(&cpu,fs_desc+i,fetch_desc[i]);
        for(unsigned i=0;i<size;++i)mem_write8(&cpu,0x11002000+i,program[i]);
        for(unsigned i=0;i<ps_size;++i)mem_write8(&cpu,0x11007000+i,pixel_program[i]);
        mem_write32(&cpu,vs+0xd4,0x11002000);mem_write32(&cpu,ps+0xa8,0x11007000);
        mem_write32(&cpu,fs_desc+12,0x11000000);
        wiiu_gx2_reset();
        for(unsigned slot=0;slot<16;++slot) {
            if(buffers[slot].valid) {
                cpu.gpr[3]=slot;cpu.gpr[4]=buffers[slot].size;
                cpu.gpr[5]=buffers[slot].stride;cpu.gpr[6]=buffers[slot].data;
                wiiu_gx2_handle_import(&cpu,"GX2SetAttribBuffer");
            }
            if(in.blocks[slot]) {
                u32 address=0x11110000+slot*0x1000;
                CHECK(in.block_sizes[slot]<=0x1000);
                for(unsigned i=0;i<in.block_sizes[slot];++i)mem_write8(&cpu,address+i,in.blocks[slot][i]);
                cpu.gpr[3]=slot;cpu.gpr[4]=in.block_sizes[slot];cpu.gpr[5]=address;
                wiiu_gx2_handle_import(&cpu,"GX2SetVertexUniformBlock");
            }
        }
        for(unsigned i=0;i<1024;++i)mem_write32(&cpu,0x1100a000+i*4,registers[i]);
        cpu.gpr[3]=0;cpu.gpr[4]=1024;cpu.gpr[5]=0x1100a000;
        wiiu_gx2_handle_import(&cpu,"GX2SetVertexUniformReg");
        cpu.gpr[3]=vs;wiiu_gx2_handle_import(&cpu,"GX2SetVertexShader");
        cpu.gpr[3]=ps;wiiu_gx2_handle_import(&cpu,"GX2SetPixelShader");
        cpu.gpr[3]=fs_desc;wiiu_gx2_handle_import(&cpu,"GX2SetFetchShader");
        for(unsigned i=0;i<4;++i) {
            u32 d=i==0?target:tex+(i-1)*256,w=i==0?64:1;
            mem_write32(&cpu,d+4,w);mem_write32(&cpu,d+8,w);mem_write32(&cpu,d+12,1);
            mem_write32(&cpu,d+0x14,0x1a);mem_write32(&cpu,d+0x20,w*w*4);
            mem_write32(&cpu,d+0x24,0x111e0000+i*0x4000);mem_write32(&cpu,d+0x30,16);
            mem_write32(&cpu,d+0x3c,w);mem_write32(&cpu,d+0x84,0x00010203);
            if(i) {
                mem_write32(&cpu,0x111e0000+i*0x4000,UINT32_MAX);
                cpu.gpr[3]=d;cpu.gpr[4]=i-1;wiiu_gx2_handle_import(&cpu,"GX2SetPixelTexture");
            }
        }
        cpu.gpr[3]=target;cpu.gpr[4]=0;wiiu_gx2_handle_import(&cpu,"GX2SetColorBuffer");
        cpu.fpr[1]=cpu.fpr[3]=cpu.fpr[4]=1;cpu.fpr[2]=0;
        cpu.gpr[3]=target;wiiu_gx2_handle_import(&cpu,"GX2ClearColor");
        /* Sub-pixel geometry may cover no pixel. VS 27 and 30 are large enough
           to check rasterization without altering the captured vertex data. */
        cpu.gpr[3]=variant==29?4:19;cpu.gpr[4]=variant==29?3:4;cpu.gpr[5]=0;cpu.gpr[6]=0;
        wiiu_gx2_handle_import(&cpu,"GX2DrawEx");
        u8 pixels[64*64*4];CHECK(wiiu_gx2_readback_color_buffer(target,pixels,64,64,256));
        for(unsigned i=0;i<64*64;++i)CHECK(pixels[i*4]==255 && !pixels[i*4+1] && pixels[i*4+2]==255);
        if(variant==30) {
            /* Only one instance was captured. Instance 1 must fail its buffer
               bounds checks, not reuse instance 0's cached full-screen quad.
               Exercise display-list replay as well as immediate draws. */
            for(unsigned recorded=0;recorded<2;++recorded) {
                if(recorded) {
                    cpu.gpr[3]=0x1100d000;cpu.gpr[4]=256;
                    wiiu_gx2_handle_import(&cpu,"GX2BeginDisplayList");
                }
                cpu.gpr[3]=19;cpu.gpr[4]=4;cpu.gpr[5]=0;cpu.gpr[6]=2;
                wiiu_gx2_handle_import(&cpu,"GX2DrawEx");
                if(recorded) {
                    cpu.gpr[3]=0x1100d000;wiiu_gx2_handle_import(&cpu,"GX2EndDisplayList");
                    cpu.gpr[4]=cpu.gpr[3];cpu.gpr[3]=0x1100d000;
                    wiiu_gx2_handle_import(&cpu,"GX2CallDisplayList");
                }
                CHECK(wiiu_gx2_readback_color_buffer(target,pixels,64,64,256));
                for(unsigned i=0;i<64*64;++i)CHECK(pixels[i*4]==255 && !pixels[i*4+1] && pixels[i*4+2]==255);
            }
        }
        cpu.gpr[3]=variant==29?4:19;cpu.gpr[4]=variant==29?3:4;cpu.gpr[5]=0;cpu.gpr[6]=1;
        if(variant==29) {
            u8 indices[2304];snprintf(name,sizeof(name),"botw_variant_%u_indices.bin",variant);
            CHECK(load(dir,name,indices,sizeof(indices))==sizeof(indices));
            for(unsigned i=0;i<sizeof(indices);++i)mem_write8(&cpu,0x1100c000+i,indices[i]);
            for(unsigned i=0;i<576;++i)CHECK(be(indices+i*4)<vertex_count);
            cpu.gpr[3]=4;cpu.gpr[4]=576;cpu.gpr[5]=9;cpu.gpr[6]=0x1100c000;cpu.gpr[7]=0;cpu.gpr[8]=1;
            wiiu_gx2_handle_import(&cpu,"GX2DrawIndexedEx");
        } else wiiu_gx2_handle_import(&cpu,"GX2DrawEx");
        CHECK(wiiu_gx2_readback_color_buffer(target,pixels,64,64,256));
        if(variant==30 || variant==27) {
            unsigned center=(32*64+32)*4;
            CHECK(pixels[center]!=255 || pixels[center+1]!=0 || pixels[center+2]!=255);
            printf("effect material GX2 draw changed captured geometry pixels: %u,%u,%u,%u\n",
                pixels[center],pixels[center+1],pixels[center+2],pixels[center+3]);
        }
        wiiu_gx2_reset();
    }
    for(unsigned i=0;i<16;++i)free((void*)in.blocks[i]);
    wiiu_memory_free(&memory);return 0;
}
int main(int argc,char** argv) {
    if(argc!=4){fprintf(stderr,"usage: latte_effect_capture_test capture-dir log variant\n");return 2;}
    return replay(argv[1],argv[2],(unsigned)strtoul(argv[3],NULL,10));
}
