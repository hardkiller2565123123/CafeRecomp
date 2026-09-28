#include "wiiu_window.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x);return 1; } } while(0)
int main(void) {
    wiiu_window_show("postprocess regression");
    /* The wake-up scene binds over 100 distinct targets. Earlier attachments
       must survive until their consumers; the former 64-entry cache lost them. */
    for(unsigned i=0;i<128;++i)CHECK(wiiu_window_gpu_clear(0x20000+i,2,2,(uint8_t)i,0,0,255));
    for(unsigned i=0;i<128;++i) {
        uint8_t retained[16];CHECK(wiiu_window_gpu_readback(0x20000+i,retained,2,2,8));
        CHECK(retained[2]==i && retained[3]==255);
        wiiu_window_gpu_invalidate(0x20000+i);
    }
    const uint32_t target=0x10001,source=0x10002;
    uint8_t texture[4*4*4],pixels[8*8*4];
    const uint8_t red[4]={20,40,80,160};
    for(unsigned y=0;y<4;++y)for(unsigned x=0;x<4;++x) {
        unsigned at=(y*4+x)*4;texture[at]=0;texture[at+1]=(uint8_t)(y*40);
        texture[at+2]=red[x];texture[at+3]=128;
    }
    CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_upload_bgra(source,4,4,texture,16));
    WiiUWindowPostVertex v[3]={0};
    const float xy[3][2]={{-1,1},{3,1},{-1,-3}};
    for(unsigned i=0;i<3;++i) {
        v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[3]=1;
    }
    float viewport[4]={0,0,8,8};
    WiiUWindowGpuBlendControl blend={true,1,0,0,true,1,0,0};
    /* HDR values must survive a real PS sample/export between render targets,
       not just a CPU round trip or a UNORM screenshot conversion. */
    const uint32_t formats[]={0x806,0x810,0x820,0x80e,0x81e,0x823,0x816};
    const unsigned channels[]={1,2,4,1,2,4,3};
    const float hdr[4]={16,-2,.125f,.5f};float readback[8*8*4];
    for(unsigned f=0;f<7;++f) {
        CHECK(wiiu_window_gpu_configure(source,4,4,formats[f]));
        CHECK(wiiu_window_gpu_configure(target,8,8,formats[f]));
        CHECK(wiiu_window_gpu_clear_float(source,4,4,hdr));
        CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,0));
        for(unsigned i=0;i<3;++i)v[i].parameters[0][0]=v[i].parameters[0][1]=.5f;
        CHECK(wiiu_window_gpu_draw_postprocess(target,8,8,source,NULL,4,4,0,v,3,
            0,0x00010203,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback_float(target,readback,8,8));
        for(unsigned p=0;p<64;++p)for(unsigned c=0;c<4;++c)
            CHECK(readback[p*4+c]==(f==6 && c==1?0:c<channels[f]?hdr[c]:c==3?1:0));
        CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
        CHECK(pixels[2]==255 && pixels[1]==0);
        CHECK(wiiu_window_gpu_upload_bgra(source,4,4,texture,16));
        CHECK(wiiu_window_gpu_readback_float(source,readback,4,4));
        CHECK(fabsf(readback[0]-20/255.0f)<(f==6?.001f:.0001f));
    }
    CHECK(wiiu_window_gpu_configure(source,4,4,0x1a));
    CHECK(wiiu_window_gpu_configure(target,8,8,0x1a));
    CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_upload_bgra(source,4,4,texture,16));
    for(unsigned mode=0;mode<4;++mode) {
        for(unsigned i=0;i<3;++i) {
            float* p=v[i].parameters[0];float* q=v[i].parameters[1];
            if(mode==0){p[0]=0.125f;p[1]=0.125f;}
            if(mode==1){p[0]=0.125f;p[2]=0.875f;q[1]=0.125f;q[3]=0.875f;}
            if(mode==2){p[0]=0.125f;p[1]=0.125f;p[2]=0.875f;p[3]=0.625f;}
            if(mode==3){p[0]=0.125f;p[1]=0.125f;p[2]=0.875f;p[3]=0.625f;}
        }
        CHECK(wiiu_window_gpu_draw_postprocess(target,8,8,source,NULL,4,4,0,v,3,
            mode,0x00010203,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
        unsigned at=(4*8+4)*4;
        float expected_red=mode==0 || mode==3?20:mode==1?90:20*0.352941185f+80*0.294117659f+160*0.352941185f;
        float expected_green=mode==1?60:mode==3?80*0.294117659f+120*0.352941185f:0;
        CHECK(fabsf(pixels[at+2]-expected_red)<1.1f);
        CHECK(fabsf(pixels[at+1]-expected_green)<1.1f);
        CHECK(pixels[at]==0 && pixels[at+3]==128);
    }
    /* Source alpha swizzle, scissor preservation, and feedback rejection. */
    CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_draw_postprocess(target,8,8,source,NULL,4,4,0,v,3,
        0,0x03050505,viewport,&blend,true,2,2,4,4));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[0]==0 && pixels[(4*8+4)*4+2]==128 && pixels[(4*8+4)*4+1]==255);
    CHECK(!wiiu_window_gpu_draw_postprocess(target,8,8,target,NULL,4,4,0,v,3,
        0,0x00010203,viewport,&blend,false,0,0,0,0));
    /* A small triangle with W=2 must cover only its actual projected area.
       Dropping W or replacing geometry with a rect makes these checks fail. */
    const float small[3][2]={{-2,2},{0,2},{-2,0}};
    for(unsigned i=0;i<3;++i) {
        v[i].position[0]=small[i][0];v[i].position[1]=small[i][1];v[i].position[3]=2;
        v[i].parameters[0][0]=v[i].parameters[0][1]=0.125f;
    }
    CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_draw_postprocess(target,8,8,source,NULL,4,4,0,v,3,
        0,0x00010203,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[(1*8+1)*4+2]==20 && pixels[(3*8+3)*4+2]==0 && pixels[(5*8+5)*4+2]==0);
    /* Captured two-texture effect material: independent color endpoints,
       texture multiplication, alpha saturation and per-particle opacity. */
    const uint8_t tex_a[4]={192,64,128,128},tex_b[4]={64,255,128,128},tex_c[4]={192,128,64,192};
    WiiUWindowEffectTexture effect[3]={{tex_a,1,1,1001},{tex_b,1,1,1002},{tex_c,1,1,1003}};
    const float start[4]={.8f,.6f,.4f,.5f},end[4]={.2f,.1f,.05f,.75f};
    for(unsigned i=0;i<3;++i) {
        v[i].position[0]=xy[i][0];v[i].position[1]=xy[i][1];v[i].position[3]=1;
        memcpy(v[i].parameters[0],start,16);memcpy(v[i].parameters[1],end,16);
        v[i].parameters[2][0]=.75f;
        for(unsigned c=0;c<4;++c)v[i].parameters[3][c]=.5f;
    }
    for(unsigned mode=4;mode<=8;++mode) {
    if(mode>=7)for(unsigned i=0;i<3;++i) {
        v[i].parameters[0][3]=.1f;v[i].parameters[4][0]=v[i].parameters[4][1]=.5f;
    }
    CHECK(wiiu_window_gpu_clear(target,8,8,0,0,0,255));
    CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,mode,viewport,&blend,true,2,2,4,4));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[0]==0 && pixels[3]==255);
    for(unsigned c=0;c<4;++c) {
        unsigned rgba=c==0?2:c==2?0:c;
        float product=tex_a[c]/255.0f;
        if(mode==4) product*=tex_b[c]/255.0f;
        if(mode==5 && c!=3) product*=product;
        float expected=c==3?.75f*.5f*product*(mode==4?1:end[3]):(start[rgba]-end[rgba])*product+end[rgba];
        if(mode>=7) {
            product=(tex_a[c]/255.0f)*(tex_b[c]/255.0f);
            if(c==3) {
                if(mode==7)product*=tex_c[c]/255.0f;
                expected=.75f*fminf(1,end[3]*fminf(1,fmaxf(0,4*(product-.1f))));
            } else {
                float mask=tex_c[c]/255.0f;
                product=mode==7?product*product*mask:product*product+mask;
                expected=fminf(1,(start[rgba]-end[rgba])*product+end[rgba]);
            }
        }
        CHECK(fabsf(pixels[(4*8+4)*4+c]-expected*255)<1.1f);
    }
    }
    for(unsigned edge=0;edge<2;++edge) {
        for(unsigned i=0;i<3;++i) {
            v[i].parameters[0][3]=edge?-1.0f:1.0f;
            v[i].parameters[1][3]=2.0f;
        }
        CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,7,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
        CHECK(pixels[(4*8+4)*4+3]==(edge?191:0));
    }
    effect[2].pixels=NULL;
    CHECK(!wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,7,viewport,&blend,false,0,0,0,0));
    CHECK(!wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,9,viewport,&blend,false,0,0,0,0));
    /* A real two-texel gradient distinguishes wrapping from edge clamping,
       point from bilinear, and independent texture-one sampler bindings. */
    const uint8_t gradient[8]={0,0,0,255,255,255,255,255},white[4]={255,255,255,255};
    effect[0]=(WiiUWindowEffectTexture){gradient,2,1,2001,true,0};
    effect[1]=(WiiUWindowEffectTexture){white,1,1,2002,true,0};
    for(unsigned i=0;i<3;++i) {
        for(unsigned c=0;c<4;++c){v[i].parameters[0][c]=1;v[i].parameters[1][c]=0;}
        v[i].parameters[1][3]=1;v[i].parameters[2][0]=1;
        v[i].parameters[3][0]=1.25f;v[i].parameters[3][1]=.5f;
        v[i].parameters[3][2]=1.25f;v[i].parameters[3][3]=.5f;
    }
    const unsigned clamp[]={0,1,2,3,6,6,6};
    const unsigned borders[]={0,0,0,0,0,1,2};
    const unsigned expected[]={0,255,255,255,0,0,255};
    for(unsigned i=0;i<7;++i) {
        effect[0].sampler_word0=clamp[i]|(2u<<3)|(borders[i]<<22);
        CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,6,viewport,&blend,false,0,0,0,0));
        CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
        CHECK(pixels[(4*8+4)*4]==expected[i]);
        CHECK(pixels[(4*8+4)*4+3]==(i==4?0:255));
    }
    for(unsigned i=0;i<3;++i)v[i].parameters[3][0]=.5f;
    effect[0].sampler_word0=2|(2<<3)|(1<<9)|(1<<12);
    CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,6,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(abs((int)pixels[(4*8+4)*4]-128)<=1);
    effect[0].sampler_word0=2|(2<<3);
    CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,6,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[(4*8+4)*4]==255);
    effect[0]=(WiiUWindowEffectTexture){white,1,1,2002,true,2|(2<<3)};
    effect[1]=(WiiUWindowEffectTexture){gradient,2,1,2001,true,0};
    CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,4,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[(4*8+4)*4]==0);
    effect[1].sampler_word0=2|(2<<3);
    CHECK(wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,4,viewport,&blend,false,0,0,0,0));
    CHECK(wiiu_window_gpu_readback(target,pixels,8,8,32));
    CHECK(pixels[(4*8+4)*4]==255);
    effect[0].sampler_word0=4; /* Half-border has no exact D3D11 equivalent. */
    CHECK(!wiiu_window_gpu_draw_effect(target,8,8,effect,v,3,6,viewport,&blend,false,0,0,0,0));
    puts("GPU postprocess and effect sampler readback checks passed");return 0;
}
