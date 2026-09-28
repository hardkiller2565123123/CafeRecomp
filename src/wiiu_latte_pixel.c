#include "wiiu_latte_pixel.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct { uint32_t a,b; unsigned unit,op; bool three; unsigned offset; } Ins;
typedef struct {
    uint8_t valid[128],origin[128][4],pv;
    uint8_t then_valid[128],then_origin[128][4],then_pv;
    unsigned jump,join,jump_pops;
    bool has_jump,has_else;
} Branch;
typedef struct {
    WiiULattePixelProgram* out;
    size_t used;
    bool ok;
    uint8_t valid[128], origin[128][4], pv;
    Branch branches[16];unsigned depth;
} Emit;
static bool predicate(Ins i){return !i.three && ((i.op>=0x20 && i.op<=0x23) || (i.op>=0x42 && i.op<=0x45));}
static uint32_t le(const uint8_t* p) {
    return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void emit(Emit* e,const char* format,...) {
    if(!e->ok)return;
    va_list ap;va_start(ap,format);
    int n=vsnprintf(e->out->hlsl+e->used,sizeof(e->out->hlsl)-e->used,format,ap);
    va_end(ap);
    if(n<0 || (size_t)n>=sizeof(e->out->hlsl)-e->used)e->ok=false;
    else e->used+=(size_t)n;
}
static bool read_lane(Emit* e,unsigned r,unsigned c) {
    if(!(e->valid[r]&(1u<<c)))return false;
    if(e->origin[r][c])e->out->input_mask[e->origin[r][c]-1]|=1u<<c;
    return true;
}
static unsigned argc_ins(Ins i) {
    if(i.three)return 3;
    switch(i.op) {
    case 0x1a:return 0;
    case 0x10:case 0x11:case 0x13:case 0x14:case 0x19:case 0x33:case 0x61:case 0x62:
    case 0x63:case 0x66:case 0x69:case 0x6a:case 0x6b:case 0x6c:case 0x6d:
    case 0x6e:case 0x6f:case 0x79:return 1;
    default:return 2;
    }
}
static unsigned sel(Ins i,unsigned n) {return n==0?i.a&511:n==1?(i.a>>13)&511:i.b&511;}
static unsigned chan(Ins i,unsigned n) {return n==0?(i.a>>10)&3:n==1?(i.a>>23)&3:(i.b>>10)&3;}
static bool source(Emit* e,Ins i,unsigned n,uint32_t a,uint32_t b,const uint32_t lit[4],char dst[96]) {
    unsigned s=sel(i,n),c=chan(i,n);
    if(n==0?(i.a>>9)&1:n==1?(i.a>>22)&1:(i.b>>9)&1)return false;
    char raw[64];
    if(s<128) {
        if(!read_lane(e,s,c))return false;
        snprintf(raw,sizeof(raw),"r[%u][%u]",s,c);
    } else if((s>=128 && s<192) || s>=256) {
        unsigned bank=16,vector=s-256;
        if(s<192) {
            bank=s<160?(a>>22)&15:(a>>26)&15;
            unsigned mode=s<160?(a>>30)&3:b&3;
            if(mode!=1 && mode!=2)return false;
            vector=(s&31)+(s<160?(b>>2)&255:(b>>10)&255)*16;
        }
        unsigned at=0;
        for(;at<e->out->constant_count;++at)
            if(e->out->constants[at].bank==bank && e->out->constants[at].vector==vector)break;
        if(at==e->out->constant_count) {
            if(at==WIIU_PIXEL_CONSTANTS)return false;
            e->out->constants[at]=(WiiULattePixelConstant){(uint16_t)bank,(uint16_t)vector};
            ++e->out->constant_count;
        }
        snprintf(raw,sizeof(raw),"k[%u][%u]",at,c);
    } else if(s==254 || s==255) {
        unsigned u=s==255?4:c;
        if(!(e->pv&(1u<<u)))return false;
        snprintf(raw,sizeof(raw),"pv[%u]",u);
    } else {
        uint32_t value;
        switch(s) {
        case 248:value=0;break;case 249:value=0x3f800000;break;
        case 250:value=1;break;case 251:value=UINT32_MAX;break;
        case 252:value=0x3f000000;break;case 253:value=lit[c];break;
        default:return false;
        }
        snprintf(raw,sizeof(raw),"0x%08Xu",value);
    }
    bool abs=!i.three && ((i.b>>n)&1);
    bool neg=n==0?(i.a>>12)&1:n==1?(i.a>>25)&1:(i.b>>12)&1;
    bool integer=i.three?(i.op>=0x1c && i.op<=0x1e && n==0):
        (i.op>=0x30 && i.op<=0x45) || i.op==0x6c || i.op==0x6d || (i.op>=0x70 && i.op<=0x75);
    if(integer) {
        if(abs)return false; /* no integer absolute modifier in this subset */
        snprintf(dst,96,"asfloat(%s(%s))",neg?"0u-":"",raw);
    } else snprintf(dst,96,"asfloat((%s%s)%s)",raw,abs?" & 0x7FFFFFFFu":"",neg?" ^ 0x80000000u":"");
    return true;
}
static bool expression(Ins i,char* dst,size_t size) {
    const char* expression=NULL;
    if(i.three) switch(i.op) {
    case 0x10:expression="lm(a,b)+c";break;
    case 0x11:expression="(lm(a,b)+c)*2";break;
    case 0x12:expression="(lm(a,b)+c)*4";break;
    case 0x13:expression="(lm(a,b)+c)*0.5";break;
    case 0x14:expression="a*b+c";break;
    case 0x18:expression="a==0?b:c";break;
    case 0x19:expression="a>0?b:c";break;
    case 0x1a:expression="a>=0?b:c";break;
    case 0x1c:expression="asint(a)==0?b:c";break;
    case 0x1d:expression="asint(a)>0?b:c";break;
    case 0x1e:expression="asint(a)>=0?b:c";break;
    default:return false;
    } else switch(i.op) {
    case 0:expression="a+b";break;
    case 1:expression="lm(a,b)";break;
    case 2:expression="a*b";break;
    case 3:case 5:expression="max(a,b)";break;
    case 4:case 6:expression="min(a,b)";break;
    case 8:expression="a==b?1.0:0.0";break;
    case 9:expression="a>b?1.0:0.0";break;
    case 10:expression="a>=b?1.0:0.0";break;
    case 11:expression="a!=b?1.0:0.0";break;
    case 0xc:expression="asfloat(a==b?0xFFFFFFFFu:0u)";break;
    case 0xd:expression="asfloat(a>b?0xFFFFFFFFu:0u)";break;
    case 0xe:expression="asfloat(a>=b?0xFFFFFFFFu:0u)";break;
    case 0xf:expression="asfloat(a!=b?0xFFFFFFFFu:0u)";break;
    case 0x10:expression="frac(a)";break;
    case 0x11:expression="trunc(a)";break;
    case 0x13:expression="round(a)";break;
    case 0x14:expression="floor(a)";break;
    case 0x19:expression="a";break;
    case 0x1a:expression="0.0";break;
    case 0x20:expression="a==b?1.0:0.0";break;
    case 0x21:expression="a>b?1.0:0.0";break;
    case 0x22:expression="a>=b?1.0:0.0";break;
    case 0x23:expression="a!=b?1.0:0.0";break;
    case 0x42:expression="asint(a)==asint(b)?1.0:0.0";break;
    case 0x43:expression="asint(a)>asint(b)?1.0:0.0";break;
    case 0x44:expression="asint(a)>=asint(b)?1.0:0.0";break;
    case 0x45:expression="asint(a)!=asint(b)?1.0:0.0";break;
    case 0x30:expression="asfloat(asuint(a)&asuint(b))";break;
    case 0x31:expression="asfloat(asuint(a)|asuint(b))";break;
    case 0x32:expression="asfloat(asuint(a)^asuint(b))";break;
    case 0x33:expression="asfloat(~asuint(a))";break;
    case 0x34:expression="asfloat(asuint(a)+asuint(b))";break;
    case 0x35:expression="asfloat(asuint(a)-asuint(b))";break;
    case 0x36:expression="asfloat(max(asint(a),asint(b)))";break;
    case 0x37:expression="asfloat(min(asint(a),asint(b)))";break;
    case 0x38:expression="asfloat(max(asuint(a),asuint(b)))";break;
    case 0x39:expression="asfloat(min(asuint(a),asuint(b)))";break;
    case 0x3a:expression="asfloat(asuint(a)==asuint(b)?0xFFFFFFFFu:0u)";break;
    case 0x3b:expression="asfloat(asint(a)>asint(b)?0xFFFFFFFFu:0u)";break;
    case 0x3c:expression="asfloat(asint(a)>=asint(b)?0xFFFFFFFFu:0u)";break;
    case 0x3d:expression="asfloat(asuint(a)!=asuint(b)?0xFFFFFFFFu:0u)";break;
    case 0x3e:expression="asfloat(asuint(a)>asuint(b)?0xFFFFFFFFu:0u)";break;
    case 0x3f:expression="asfloat(asuint(a)>=asuint(b)?0xFFFFFFFFu:0u)";break;
    case 0x61:expression="exp2(a)";break;
    case 0x62:expression="max(log2(max(0,a)),-3.402823466e+38)";break;
    case 0x63:expression="log2(max(0,a))";break;
    case 0x66:expression="1.0/a";break;
    case 0x69:expression="rsqrt(a)";break;
    case 0x6a:expression="sqrt(a)";break;
    case 0x6b:expression="asfloat((int)a)";break;
    case 0x6c:expression="(float)asint(a)";break;
    case 0x6d:expression="(float)asuint(a)";break;
    case 0x6e:expression="sin(a/0.1591549367)";break;
    case 0x6f:expression="cos(a/0.1591549367)";break;
    case 0x70:expression="asfloat(asint(a)>>(asuint(b)&31u))";break;
    case 0x71:expression="asfloat(asuint(a)>>(asuint(b)&31u))";break;
    case 0x72:expression="asfloat(asuint(a)<<(asuint(b)&31u))";break;
    case 0x73:case 0x75:expression="asfloat(asuint(a)*asuint(b))";break;
    case 0x79:expression="asfloat((uint)a)";break;
    default:return false;
    }
    snprintf(dst,size,"%s",expression);return true;
}
static bool alu(Emit* e,const uint8_t* p,size_t size,uint32_t a,uint32_t b) {
    unsigned at=a&0x3fffff,end=at+((b>>18)&127)+1;
    bool push=((b>>26)&15)==9,has_predicate=false;
    if((uint64_t)end*8>size)return false;
    while(at<end) {
        Ins ins[5];unsigned n=0,units=0,words=0;
        do {
            if(n==5 || at==end)return false;
            e->out->instruction_offset=at*8;
            Ins i={le(p+at*8),le(p+at*8+4),0,0,false,at*8};++at;
            i.three=((i.b>>13)&31)>=8;i.op=i.three?(i.b>>13)&31:(i.b>>7)&2047;
            if(((i.a>>29)&3) || ((i.b>>28)&1))return false;
            if(predicate(i)) {
                if(!push || has_predicate || (i.b&0x80000070u) || (i.b&12)!=12)return false;
                has_predicate=true;
            } else if(!i.three && (i.b&12))return false;
            i.unit=!i.three && ((i.op>=0x61 && i.op<=0x6f) || i.op==0x73 || i.op==0x75 || i.op==0x79)?4:(i.b>>29)&3;
            if(units&(1u<<i.unit))i.unit=4;
            if(units&(1u<<i.unit))return false;
            units|=1u<<i.unit;
            for(unsigned s=0;s<argc_ins(i);++s)
                if(sel(i,s)==253 && words<=chan(i,s))words=chan(i,s)+1;
            ins[n++]=i;
        } while(!(ins[n-1].a>>31));
        unsigned slots=(words+1)/2;
        if(slots>end-at)return false;
        uint32_t lit[4]={0};for(unsigned j=0;j<slots*2;++j)lit[j]=le(p+at*8+j*4);
        at+=slots;
        if(has_predicate && at!=end)return false;
        /* Evaluate every lane before writing any GPR/PV. */
        for(unsigned j=0;j<n;++j) {
            Ins i=ins[j];char args[3][96]={"0","0","0"},expr[128];
            e->out->instruction_offset=i.offset;
            if(!i.three && (i.op==0x50 || i.op==0x51)) {
                if(n<4 || i.unit>=4)return false;
                emit(e,"{float v=0;");
                for(unsigned k=0;k<4;++k) {
                    if(ins[k].three || ins[k].op!=i.op || ins[k].unit!=k)return false;
                    for(unsigned s=0;s<2;++s)if(!source(e,ins[k],s,a,b,lit,args[s]))return false;
                    emit(e,i.op==0x50?"v+=lm(%s,%s);":"v+=(%s)*(%s);",args[0],args[1]);
                }
            } else {
                if(!expression(i,expr,sizeof(expr)))return false;
            for(unsigned k=0;k<argc_ins(i);++k)if(!source(e,i,k,a,b,lit,args[k]))return false;
            emit(e,"{float a=%s,b=%s,c=%s; float v=(%s);",args[0],args[1],args[2],expr);
            }
            unsigned omod=i.three?0:(i.b>>5)&3;
            bool integer_result=!i.three && ((i.op>=0xc && i.op<=0xf) || (i.op>=0x30 && i.op<=0x3f) ||
                i.op==0x6b || (i.op>=0x70 && i.op<=0x75) || i.op==0x79);
            if(integer_result && (omod || (i.b>>31)))return false;
            if(omod)emit(e,"v*=%s;",omod==1?"2.0":omod==2?"4.0":"0.5");
            if(i.b>>31)emit(e,"v=saturate(v);");
            if(predicate(i))emit(e,"pred=v!=0;");
            emit(e,"t[%u]=asuint(v);}\n",j);
        }
        e->pv=0;
        for(unsigned j=0;j<n;++j) {
            Ins i=ins[j];if(!i.three && i.op==0x1a)continue;
            if(predicate(i))continue;
            emit(e,"pv[%u]=t[%u];\n",i.unit,j);e->pv|=1u<<i.unit;
            if(i.three || (i.b&16)) {
                unsigned r=(i.b>>21)&127,c=(i.b>>29)&3;
                emit(e,"r[%u][%u]=t[%u];\n",r,c,j);
                e->valid[r]|=1u<<c;e->origin[r][c]=0;
            }
        }
    }
    return e->ok && (!push || has_predicate);
}
static bool close_branch(Emit* e,unsigned cf) {
    if(!e->depth)return false;
    Branch* branch=&e->branches[e->depth-1];
    if(!branch->has_jump || (branch->has_else?branch->join!=cf+1:
        branch->jump!=cf+branch->jump_pops))return false;
    const uint8_t* valid=branch->has_else?branch->then_valid:branch->valid;
    const uint8_t (*origin)[4]=branch->has_else?branch->then_origin:branch->origin;
    for(unsigned r=0;r<128;++r) {
        e->valid[r]&=valid[r];
        for(unsigned c=0;c<4;++c)if(origin[r][c])e->origin[r][c]=origin[r][c];
    }
    e->pv&=branch->has_else?branch->then_pv:branch->pv;
    --e->depth;emit(e,"}\n");return true;
}
static bool tex(Emit* e,const uint8_t* p,size_t size,uint32_t a,uint32_t b) {
    unsigned count=((b>>10)&7)+(((b>>19)&1)<<3)+1;
    uint64_t at=(uint64_t)a*8;
    if((b&7) || ((b>>8)&3) || at+count*16>size)return false;
    for(unsigned n=0;n<count;++n) {
        e->out->instruction_offset=(unsigned)at+n*16;
        uint32_t x=le(p+at+n*16),y=le(p+at+n*16+4),z=le(p+at+n*16+8);
        unsigned texture=(x>>8)&255,sampler=(z>>15)&31,r=(x>>16)&127,d=y&127;
        unsigned op=x&31;
        if((x&0xfc8000ffu)==0x3c000040u && texture>=0x80 && texture<0x90 &&
           (y&0xffe00180u)==0x00200000u && z==0x00080000u) {
            /* Packed uniform VFETCH: an integer vector index, not a texture
               coordinate. Keep the entire referenced block GPU-visible. */
            unsigned bank=texture-0x80,component=(x>>24)&3;
            if(!read_lane(e,r,component))return false;
            e->out->block_mask|=1u<<bank;
            emit(e,"{uint n,stride;B%u.GetDimensions(n,stride);uint ix=r[%u][%u];uint4 q=0;if(ix<n)q=B%u[ix];\n",
                bank,r,component,bank);
            for(unsigned c=0;c<4;++c) {
                unsigned s=(y>>(9+c*3))&7;
                if(s==7)continue;if(s==6)return false;
                if(s<4)emit(e,"r[%u][%u]=q[%u];\n",d,c,s);
                else emit(e,"r[%u][%u]=0x%08Xu;\n",d,c,s==5?0x3f800000u:0);
                e->valid[d]|=1u<<c;e->origin[d][c]=0;
            }
            emit(e,"}\n");continue;
        }
        /* Normalized 2D sampling, including whole-texel XY offsets. */
        if((op<0x10 || op>0x13) || (x&0xFF8000E0u) ||
           (y&0x0FE00180u) || (y&0x30000000u)!=0x30000000u ||
           (z&0x7c21u) || texture>=16 || sampler>=16)return false;
        int ox=(int)(z&31),oy=(int)((z>>5)&31);
        if(ox&16)ox-=32;if(oy&16)oy-=32;ox/=2;oy/=2;
        unsigned sx=(z>>20)&7,sy=(z>>23)&7;
        if(sx>=4 || sy>=4 || !read_lane(e,r,sx) || !read_lane(e,r,sy))return false;
        e->out->texture_mask|=1u<<texture;e->out->sampler_mask|=1u<<sampler;
        if(op==0x10)emit(e,"{float4 q=T%u.Sample(S%u,float2(asfloat(r[%u][%u]),asfloat(r[%u][%u])),int2(%d,%d));\n",texture,sampler,r,sx,r,sy,ox,oy);
        else {
            unsigned lod=(z>>29)&7;
            if(op!=0x13 && (lod>=4 || !read_lane(e,r,lod)))return false;
            emit(e,"{float4 q=T%u.%s(S%u,float2(asfloat(r[%u][%u]),asfloat(r[%u][%u])),",texture,op==0x12?"SampleBias":"SampleLevel",sampler,r,sx,r,sy);
            if(op!=0x13)emit(e,"asfloat(r[%u][%u])",r,lod);else emit(e,"0");
            emit(e,",int2(%d,%d));\n",ox,oy);
        }
        emit(e,"q=float4(ch(q,m[%u].x),ch(q,m[%u].y),ch(q,m[%u].z),ch(q,m[%u].w));\n",texture,texture,texture,texture);
        for(unsigned c=0;c<4;++c) {
            unsigned s=(y>>(9+c*3))&7;
            if(s==7)continue;if(s==6)return false;
            if(s<4)emit(e,"r[%u][%u]=asuint(q[%u]);\n",d,c,s);
            else emit(e,"r[%u][%u]=0x%08Xu;\n",d,c,s==5?0x3f800000u:0);
            e->valid[d]|=1u<<c;e->origin[d][c]=0;
        }
        emit(e,"}\n");
    }
    return e->ok;
}
bool wiiu_latte_translate_pixel(const uint8_t* p,size_t size,unsigned inputs,WiiULattePixelProgram* out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!p || size<8 || size>16384 || size%8 || inputs>WIIU_PIXEL_INPUTS)return false;
    out->key=2166136261u^inputs;
    for(size_t i=0;i<size;++i)out->key=(out->key^p[i])*16777619u;
    Emit e={0};e.out=out;e.ok=true;
    emit(&e,"struct P {float4 pos:SV_POSITION;");
    for(unsigned i=0;i<WIIU_PIXEL_INPUTS;++i)emit(&e,"float4 p%u:TEXCOORD%u;",i,i);
    emit(&e,"};\ncbuffer C:register(b1){uint4 m[16];uint4 k[256];};\n");
    for(unsigned i=0;i<16;++i)emit(&e,"Texture2D T%u:register(t%u);SamplerState S%u:register(s%u);\n",i,i,i,i);
    for(unsigned i=0;i<16;++i)emit(&e,"StructuredBuffer<uint4> B%u:register(t%u);\n",i,16+i);
    emit(&e,"struct O {");
    for(unsigned i=0;i<8;++i)emit(&e,"float4 c%u:SV_TARGET%u;",i,i);
    emit(&e,"};\nfloat lm(float a,float b){return a==0||b==0?0:a*b;}\nfloat ch(float4 v,uint s){return s<4?v[s]:s==5?1:0;}\nO main(P v) {uint4 r[128];uint pv[5],t[5];bool pred=false;O color=(O)0;\n");
    for(unsigned i=0;i<inputs;++i) {
        e.valid[i]=15;
        for(unsigned c=0;c<4;++c)e.origin[i][c]=(uint8_t)(i+1);
        emit(&e,"r[%u]=asuint(v.p%u);\n",i,i);
    }
    for(unsigned cf=0;cf<size/8 && cf<1024;++cf) {
        out->instruction_offset=cf*8;
        uint32_t a=le(p+cf*8),b=le(p+cf*8+4);unsigned op=(b>>23)&127;
        if(e.depth && !e.branches[e.depth-1].has_jump && op!=0xa)return false;
        if(op>=64) {
            unsigned kind=(b>>26)&15;
            if(kind<8 || kind>10 || !alu(&e,p,size,a,b))return false;
            if(kind==9) {
                if(e.depth==16)return false;
                Branch* branch=&e.branches[e.depth++];memset(branch,0,sizeof(*branch));
                memcpy(branch->valid,e.valid,sizeof(e.valid));memcpy(branch->origin,e.origin,sizeof(e.origin));branch->pv=e.pv;
                emit(&e,"if(pred){\n");
            } else if(kind==10 && !close_branch(&e,cf))return false;
            continue;
        }
        if(op==0xa || op==0xd || op==0xe) {
            if(!e.depth || ((b>>8)&3) || (b&(1u<<21)))return false;
            Branch* branch=&e.branches[e.depth-1];
            if(op==0xa) {
                /* JUMP POP_COUNT=1 restores the enclosing mask on the
                   skipped arm of a simple if. The taken arm ends in POP. */
                if((b&7)>1 || branch->has_jump || a<=cf || a>=size/8 || a>=1024)return false;
                branch->has_jump=true;branch->jump=a;branch->jump_pops=b&7;
            } else if(op==0xd) {
                if((b&7)!=1 || !branch->has_jump || branch->jump_pops || branch->has_else || branch->jump!=cf || a<=cf || a>=size/8 || a>=1024)return false;
                branch->has_else=true;branch->join=a;
                memcpy(branch->then_valid,e.valid,sizeof(e.valid));memcpy(branch->then_origin,e.origin,sizeof(e.origin));branch->then_pv=e.pv;
                memcpy(e.valid,branch->valid,sizeof(e.valid));memcpy(e.origin,branch->origin,sizeof(e.origin));e.pv=branch->pv;
                emit(&e,"}else{\n");
            } else if((b&7)!=1 || !close_branch(&e,cf))return false;
            continue;
        }
        if(op==1) {if(!tex(&e,p,size,a,b))return false;}
        else if(op==0) {if((b&7) || ((b>>8)&3))return false;}
        else if(op==0x27 || op==0x28) {
            if(e.depth)return false;
            unsigned base=a&8191,burst=((b>>17)&15)+1,reg=(a>>15)&127;
            if((a&0x6000) || (a&0xFFC00000u) || base+burst>8 || reg+burst>128)return false;
            for(unsigned t=0;t<burst;++t) {
                unsigned r=reg+t,target=base+t;
                if(out->target_mask&(1u<<target))return false;
                for(unsigned c=0;c<4;++c) {
                unsigned s=(b>>(c*3))&7;
                if(s<4) {if(!read_lane(&e,r,s))return false;emit(&e,"color.c%u[%u]=asfloat(r[%u][%u]);\n",target,c,r,s);}
                else if(s==4 || s==5)emit(&e,"color.c%u[%u]=%u;\n",target,c,s-4);
                else return false;
                }
                out->target_mask|=1u<<target;
            }
        } else return false;
        if(b&(1u<<21)) {emit(&e,"return color;}\n");return !e.depth && out->target_mask && e.ok;}
    }
    return false;
}
