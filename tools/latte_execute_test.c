#include "wiiu_latte_execute.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
static uint32_t bits(float f) { uint32_t b;memcpy(&b,&f,4);return b; }
static void word(uint8_t* p,unsigned at,uint32_t b) {
    for(unsigned c=0;c<4;++c)p[at*4+c]=(uint8_t)(b>>(8*c));
}
static void pair(uint8_t* p,unsigned slot,uint32_t a,uint32_t b) {
    word(p,slot*2,a);word(p,slot*2+1,b);
}
static int address_tests(void) {
    uint8_t p[128]={0},block[64]={0},valid[16];uint32_t uniforms[16]={0};
    memset(valid,1,sizeof(valid));
    for(unsigned i=0;i<4;++i){uniforms[i*4]=bits((float)(10+i));word(block,i*4,uniforms[i*4]);}
    WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.valid[1]=in.valid[2]=15;in.gpr[1][3]=bits(1);
    in.uniforms=uniforms;in.uniform_valid=valid;in.uniform_count=16;
    in.blocks[0]=block;in.block_sizes[0]=sizeof(block);
    for(unsigned bank=0;bank<2;++bank)for(unsigned channel=0;channel<4;++channel)
        for(unsigned integer=0;integer<2;++integer) {
            pair(p,0,4|(bank?1u<<30:0),0xA0040000);
            pair(p,1,0xA03C,0x94200688);
            pair(p,4,0x80000002,((integer?0x18:0x16)<<7)|(channel<<29));
            pair(p,5,0x80000200|(bank?130:258)|(channel<<26),(1<<21)|(0x19<<7)|16);
            in.gpr[2][0]=integer?(uint32_t)-1:bits(-.25f);
            CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out) && out.position[0]==11);
            in.gpr[2][0]=integer?1:bits(1.75f);
            CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out) && out.position[0]==13);
            in.gpr[2][0]=integer?1000:bits(1000);
            CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
            pair(p,4,0x80000000,0x1a<<7); /* Uninitialized AR is not silently zero. */
            CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
        }
    return 0;
}
static int arithmetic_case(unsigned op,uint32_t a,uint32_t b,uint32_t expected,bool scalar) {
    uint8_t p[128]={0};WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.valid[1]=in.valid[2]=in.valid[3]=15;
    in.gpr[1][3]=bits(1);in.gpr[2][0]=a;in.gpr[2][1]=b;in.gpr[3][0]=expected;
    pair(p,0,4,0xA0080000); /* Three groups: calculate, compare bits, select float. */
    pair(p,1,0xA03C,0x94200688);
    pair(p,4,0x80000002|(2<<13)|(1<<23),(2<<21)|(op<<7)|(scalar?0:16));
    pair(p,5,0x80000000|(scalar?255:2)|(3<<13),(2<<21)|(0x3a<<7)|16);
    pair(p,6,0x80000002|(248<<13),(1<<21)|(0x1c<<13)|249);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    if(out.position[0]!=1) {
        fprintf(stderr,"Arithmetic opcode %x input %08x,%08x expected %08x scalar=%u\n",op,a,b,expected,scalar);
        return 1;
    }
    return 0;
}
static int arithmetic_tests(void) {
    CHECK(!arithmetic_case(0x13,bits(2.5f),0,bits(2),false));
    CHECK(!arithmetic_case(0x13,bits(3.5f),0,bits(4),false));
    CHECK(!arithmetic_case(0x13,bits(-2.5f),0,bits(-2),false));
    CHECK(!arithmetic_case(0x13,bits(-3.5f),0,bits(-4),false));
    CHECK(!arithmetic_case(0x19,0x7fc12345,0,0x7fc12345,false)); /* MOV NaN payload. */
    CHECK(!arithmetic_case(0x30,0xf1234567,0x00ff00ff,0x00230067,false));
    CHECK(!arithmetic_case(0x31,0xf0000000,0x11,0xf0000011,false));
    CHECK(!arithmetic_case(0x32,0xf0000000,0xff000011,0x0f000011,false));
    CHECK(!arithmetic_case(0x33,0x12345678,0,0xedcba987,false));
    CHECK(!arithmetic_case(0x34,UINT32_MAX,2,1,false));
    CHECK(!arithmetic_case(0x35,0,1,UINT32_MAX,false));
    CHECK(!arithmetic_case(0x36,0xfffffffe,1,1,false));
    CHECK(!arithmetic_case(0x37,0xfffffffe,1,0xfffffffe,false));
    CHECK(!arithmetic_case(0x38,0xfffffffe,1,0xfffffffe,false));
    CHECK(!arithmetic_case(0x39,0xfffffffe,1,1,false));
    for(unsigned op=0x3a;op<=0x3f;++op)
        CHECK(!arithmetic_case(op,0xfffffffe,1,op>=0x3d?UINT32_MAX:0,false));
    for(unsigned op=0xc;op<=0xf;++op)
        CHECK(!arithmetic_case(op,bits(2),bits(1),op==0xc?0:UINT32_MAX,false));
    CHECK(!arithmetic_case(0x70,0x80000000,33,0xc0000000,false));
    CHECK(!arithmetic_case(0x70,0x80000000,0,0x80000000,false));
    CHECK(!arithmetic_case(0x71,0x80000000,33,0x40000000,false));
    CHECK(!arithmetic_case(0x72,0x80000001,33,2,false));
    CHECK(!arithmetic_case(0x73,0x80000001,2,2,true));
    CHECK(!arithmetic_case(0x75,0x80000001,2,2,true));
    CHECK(!arithmetic_case(0x61,bits(3),0,bits(8),true));
    CHECK(!arithmetic_case(0x62,bits(0),0,0xff7fffff,true));
    CHECK(!arithmetic_case(0x63,bits(8),0,bits(3),true));
    CHECK(!arithmetic_case(0x66,bits(4),0,bits(.25f),true));
    CHECK(!arithmetic_case(0x69,bits(4),0,bits(.5f),true));
    CHECK(!arithmetic_case(0x6a,bits(4),0,bits(2),true));
    CHECK(!arithmetic_case(0x6b,bits(-3.9f),0,0xfffffffd,true));
    CHECK(!arithmetic_case(0x6c,0xfffffffd,0,bits(-3),true));
    CHECK(!arithmetic_case(0x6d,0xffffffff,0,bits(4294967296.0f),true));
    CHECK(!arithmetic_case(0x6e,bits(.25f),0,bits(1),true));
    CHECK(!arithmetic_case(0x6f,bits(.5f),0,bits(-1),true));
    CHECK(!arithmetic_case(0x79,bits(3.9f),0,3,true));
    return 0;
}
static int branch_tests(void) {
    uint8_t p[256]={0};WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.valid[1]=in.valid[2]=15;in.gpr[1][3]=bits(1);
    pair(p,0,16,0xA4000000); /* PUSH: R2.x == integer zero. */
    pair(p,1,3,0x85000000); /* inactive -> ELSE */
    pair(p,2,17,0xA0000000);
    pair(p,3,5,0x86800001); /* skip else and pop when true branch was taken */
    pair(p,4,18,0xA8000000);
    pair(p,5,0xA03C,0x94200688);
    pair(p,16,0x80000002|(248<<13),(0x42<<7)|12);
    pair(p,17,0x800000f9,0x00200C90);
    pair(p,18,0x800000fc,0x00200C90);
    for(unsigned which=0;which<2;++which) {
        in.gpr[2][0]=which;
        CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
        CHECK(out.position[0]==(which?.5f:1));
    }
    /* Nested masks, conditional pop counts, and ALU_POP2_AFTER. */
    pair(p,1,6,0x85000001);
    pair(p,2,19,0xA4000000);
    pair(p,3,6,0x85000002);
    pair(p,4,17,0xAC000000);
    pair(p,5,0,0);
    pair(p,6,0xA03C,0x94200688);
    pair(p,19,0x80000402|(248<<13),(0x42<<7)|12);
    for(unsigned which=0;which<4;++which) {
        in.gpr[1][0]=bits(.25f);
        in.gpr[2][0]=which&1;in.gpr[2][1]=which>>1;
        CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
        CHECK(out.position[0]==(which?.25f:1));
    }
    in.gpr[2][0]=0;in.gpr[2][1]=0;
    pair(p,3,1,0x85000002); /* backward branches fail even if not taken */
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,3,6,0x85000003); /* pop underflow */
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,3,100,0x85000002); /* target outside program */
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,0,16,0xA8000000); /* ALU pop underflow */
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    return 0;
}
static bool query_texture(void* user,unsigned texture,uint32_t lod,uint32_t result[4]) {
    if(user!=(void*)1 || texture!=1 || lod)return false;
    result[0]=1280;result[1]=720;result[2]=result[3]=1;return true;
}
static int texture_info_tests(void) {
    uint8_t p[128]={0};WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.valid[0]=15;in.gpr[0][2]=bits(2);in.gpr[0][3]=0;
    in.texture_user=(void*)1;in.texture_info=query_texture;
    pair(p,0,4,0x80800000);pair(p,1,0x203c,0x94200688); /* export R0 */
    /* Exact resource query pattern captured in scene VS 110. */
    pair(p,4,0x104,0xf01f9000);pair(p,5,0x71b00000,0xdeadfec);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    CHECK(bits(out.position[0])==1280 && bits(out.position[1])==720);
    CHECK(out.position[2]==2 && out.position[3]==0);
    in.gpr[0][3]=1;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.gpr[0][3]=0;in.valid[0]=7;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.valid[0]=15;in.texture_info=NULL;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    return 0;
}
static int uniform_fetch_tests(void) {
    uint8_t p[128]={0},block[32]={0};WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.blocks[7]=block;in.block_sizes[7]=sizeof(block);
    in.valid[1]=15;in.gpr[1][2]=1;in.gpr[1][3]=bits(1);
    for(unsigned c=0;c<4;++c)word(block,4+c,bits((float)c+.25f));
    pair(p,0,4,0x80800000);pair(p,1,0xa03c,0x94200688);
    pair(p,4,0x3e018740,0x003d1001);pair(p,5,0x80000,0xdeadbeef);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    CHECK(out.position[0]==.25f && out.position[1]==1.25f && out.position[2]==2.25f && out.position[3]==1);
    in.gpr[1][2]=2;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.gpr[1][2]=UINT32_MAX;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.gpr[1][2]=1;in.block_sizes[7]=31;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.block_sizes[7]=32;in.valid[1]=11;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.valid[1]=15;
    pair(p,4,0x3e818740,0x003d1001);CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,4,0x3e018740,0x003d1081);CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,4,0x3e018740,0x003d1001);pair(p,5,0x90000,0);
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    pair(p,5,0x80000,0);pair(p,0,15,0x80800000);
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    return 0;
}
static int captured(const char* directory) {
    uint32_t uniforms[1024]={0}; uint8_t valid[1024];memset(valid,1,sizeof(valid));
    uniforms[0]=bits(0.01f);uniforms[2]=uniforms[3]=bits(0.01f);
    for(unsigned c=0;c<4;++c) uniforms[4+c*4+c]=bits(1);
    WiiULatteVertexInputs in={0};
    in.uniforms=uniforms;in.uniform_valid=valid;in.uniform_count=1024;
    in.gpr[1][0]=bits(-0.5f);in.gpr[1][1]=bits(0.5f);in.gpr[1][3]=bits(1);in.valid[1]=15;
    for(unsigned v=33;v<=36;++v) {
        char path[2048];snprintf(path,sizeof(path),"%s/botw_guide_variant_%u_vs_program.bin",directory,v);
        FILE* file=fopen(path,"rb");CHECK(file);
        uint8_t p[8192];size_t size=fread(p,1,sizeof(p),file);fclose(file);
        WiiULatteVertexOutputs out;CHECK(wiiu_latte_execute_vertex(p,size,&in,&out));
        float scale=v==33 || v==36?2.0f:1.0f;
        CHECK(fabsf(out.position[0]+0.5f*scale)<0.00001f);
        CHECK(fabsf(out.position[1]-0.5f*scale)<0.00001f);
        CHECK(out.position[2]==0 && out.position[3]==1);
        if(v==36) CHECK(out.parameters[0][0]==0 && out.parameters[0][1]==0);
        printf("captured VS %u: position (%g,%g,%g,%g), UV (%g,%g,%g,%g)\n",v,
            out.position[0],out.position[1],out.position[2],out.position[3],
            out.parameters[0][0],out.parameters[0][1],out.parameters[0][2],out.parameters[0][3]);
    }
    return 0;
}
int main(int argc,char** argv) {
    CHECK(!texture_info_tests());
    CHECK(!address_tests());
    CHECK(!arithmetic_tests());
    CHECK(!branch_tests());
    CHECK(!uniform_fetch_tests());
    uint8_t p[128]={0}; WiiULatteVertexInputs in={0};WiiULatteVertexOutputs out;
    in.gpr[1][0]=bits(2);in.gpr[1][1]=bits(3);in.gpr[1][2]=bits(4);in.gpr[1][3]=bits(1);
    in.valid[1]=15;
    pair(p,0,4,0xA0040000); /* two ALU instructions in one group */
    pair(p,1,0xA03C,0x94200688); /* export R1 */
    pair(p,4,249,0x00200C90); /* R1.x=1 */
    pair(p,5,0x80000001,0x20200C90); /* R1.y=old R1.x, not newly written 1 */
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    CHECK(out.position[0]==1 && out.position[1]==2 && out.position[2]==4 && out.position[3]==1);
    /* Z-only literals still occupy XY and ZW slots. OMOD *2 then clamp. */
    pair(p,0,4,0xA0080000);
    pair(p,4,0x800008fd,0x80200CB0);
    pair(p,5,0,0);pair(p,6,bits(0.75f),0);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));CHECK(out.position[0]==1);
    CHECK(!wiiu_latte_execute_vertex(p,48,&in,&out)); /* truncated literal */
    /* Kcache 0 -> bank 2, first vec4; GPU-native little endian. */
    uint8_t block[16]={0,0,0x80,0x3e};in.blocks[2]=block;in.block_sizes[2]=16;
    pair(p,0,0x40800004,0xA0000000);pair(p,4,0x80000080,0x00200C90);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));CHECK(out.position[0]==0.25f);
    in.block_sizes[2]=3;CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    /* An unused parameter lane may be undefined; positions may not. */
    pair(p,0,0x00014000,0x13800688); /* param 0 = R2.xyzw, no end */
    in.valid[2]=1;in.gpr[2][0]=bits(.25f);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    CHECK(out.parameter_mask[0]==1 && out.parameters[0][0]==.25f);
    in.valid[2]=0;
    /* Unsupported branches and missing inputs cannot produce partial output. */
    memset(&out,0x5a,sizeof(out));WiiULatteVertexOutputs sentinel=out;
    pair(p,0,0,0x85000000);
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));CHECK(!memcmp(&out,&sentinel,sizeof(out)));
    pair(p,0,4,0xA0000000);pair(p,4,0x80000002,0x00200C90);
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    /* DOT4 broadcasts to PV.xyzw while a fifth same-group instruction
       writes PS. Only the designated write mask updates the destination. */
    pair(p,0,4,0xA0180000); /* seven ALU slots */
    for(unsigned c=0;c<4;++c)
        pair(p,4+c,1|(c<<10)|(1<<13)|(c<<23),
             (1<<21)|(c<<29)|(0x50<<7)|(c==0?16:0));
    pair(p,8,0x800000f9,0x00000C80); /* PS=1; no GPR write */
    pair(p,9,254,0x20200C90); /* R1.y=PV.x (30) */
    pair(p,10,0x800000ff,0x40200C90); /* R1.z=PS (1) */
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    CHECK(out.position[0]==30 && out.position[1]==30 && out.position[2]==1 && out.position[3]==1);
    /* GPU MUL's zero suppression differs from IEEE multiplication. */
    in.gpr[1][0]=bits(INFINITY);
    pair(p,0,4,0xA0000000);pair(p,4,0x80000001|(248<<13),0x00200090);
    CHECK(wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));CHECK(out.position[0]==0);
    pair(p,4,0x80000001|(248<<13),0x00200110);
    CHECK(!wiiu_latte_execute_vertex(p,sizeof(p),&in,&out));
    in.gpr[1][0]=bits(2);
    /* Deterministic malformed-program corpus; also run under ASan. */
    uint32_t seed=0x94200688;
    for(unsigned trial=0;trial<20000;++trial) {
        for(unsigned i=0;i<sizeof(p);++i) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;p[i]=(uint8_t)seed;
        }
        memset(&out,0x5a,sizeof(out));sentinel=out;
        bool ok=wiiu_latte_execute_vertex(p,8*(1+trial%16),&in,&out);
        if(!ok)CHECK(!memcmp(&out,&sentinel,sizeof(out)));
        else CHECK(out.position_mask==15);
    }
    if(argc>1) CHECK(captured(argv[1])==0);
    puts("Latte ALU execution checks passed");return 0;
}
