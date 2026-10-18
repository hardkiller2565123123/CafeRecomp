#include "graphics/surface_bridge.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"surface check failed line %d: %s\n",__LINE__,#x); return 1; } } while(0)
int main(void) {
    uint32_t s[29]={1,1280,720,1,1,0x1A,0,2};
    s[12]=16; s[9]=0x20000000; s[11]=0x30000000;
    CHECK(cafe_surface_calculate(s));
    CHECK(s[8]==1280*720*4 && s[15]==1280 && s[14]==1 && s[10]==0);
    CHECK(s[9]==0x20000000 && s[11]==0x30000000);
    memset(s,0,sizeof(s)); s[0]=1;s[1]=64;s[2]=32;s[3]=1;s[4]=7;s[5]=0x31;s[12]=16;
    CHECK(cafe_surface_calculate(s));
    CHECK(s[8]==16*8*8 && s[15]==16 && s[4]==7 && s[10]>0 && s[16]>=s[8]);
    for(unsigned i=2;i<6;++i) CHECK(s[16+i]>s[15+i]);
    memset(s,0,sizeof(s));s[0]=2;s[1]=8;s[2]=8;s[3]=8;s[4]=4;s[5]=0x1A;s[12]=0;
    CHECK(cafe_surface_calculate(s));
    CHECK(s[8]>=8*8*8*4 && s[14]>=256 && s[4]==4 && s[10]>0);
    uint32_t before[29];s[1]=0;memcpy(before,s,sizeof(s));
    CHECK(!cafe_surface_calculate(s) && !memcmp(before,s,sizeof(s)));
    CHECK(!cafe_surface_calculate(NULL));
    puts("Cafe surface layouts: linear, BC1 mip chain, 3D and invalid descriptors passed");
    return 0;
}
