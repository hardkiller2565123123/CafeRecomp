#include "wiiu_latte_execute.h"
#include <math.h>
#include <float.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static bool unsupported(unsigned line) {
    /* Opt-in offline diagnostics; ordinary draws remain silent. */
    const char* trace=getenv("BOTW_LATTE_TRACE");
    if(trace && *trace=='1') fprintf(stderr,"latte: rejected at executor line %u\n",line);
    return false;
}

typedef struct { uint32_t a, b; unsigned unit; } Instruction;
typedef struct {
    uint32_t r[128][4], pv[5];
    uint8_t valid[128], pv_valid;
    int32_t address[4];
    uint8_t address_valid;
    bool active;
    bool trace_alu;
    const WiiULatteVertexInputs* inputs;
} State;
static uint32_t le(const uint8_t* p) {
    return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static float as_float(uint32_t x) { float f; memcpy(&f,&x,4); return f; }
static uint32_t as_bits(float f) { uint32_t x; memcpy(&x,&f,4); return x; }
static bool op3(Instruction i) { return ((i.b>>13)&31)>=8; }
static unsigned opcode(Instruction i) { return op3(i)?(i.b>>13)&31:(i.b>>7)&2047; }
static bool predicate_op(Instruction i) {
    unsigned op=opcode(i);
    return !op3(i) && ((op>=0x20 && op<=0x23) || (op>=0x42 && op<=0x45));
}
static bool scalar_unit(Instruction i) {
    unsigned op=opcode(i);
    return !op3(i) && ((op>=0x61 && op<=0x6f && op!=0x64) ||
                      op==0x73 || op==0x75 || op==0x79);
}
static bool integer_source(Instruction i,unsigned n) {
    unsigned op=opcode(i);
    if(op3(i)) return op>=0x1c && op<=0x1e && n==0;
    return op==0x18 || (op>=0x30 && op<=0x3f) || (op>=0x42 && op<=0x45) || op==0x6c || op==0x6d ||
           (op>=0x70 && op<=0x75);
}
static bool integer_result(Instruction i) {
    unsigned op=opcode(i);
    return !op3(i) && ((op>=0xc && op<=0xf) || (op>=0x30 && op<=0x3f) ||
                      op==0x6b || (op>=0x70 && op<=0x75) || op==0x79);
}
static unsigned source_count(Instruction i) {
    if(op3(i)) return 3;
    switch(opcode(i)) {
    case 0x1a: return 0;
    case 0x10: case 0x11: case 0x13: case 0x14: case 0x16: case 0x18: case 0x19: case 0x33:
    case 0x61: case 0x62: case 0x63: case 0x66: case 0x69: case 0x6a:
    case 0x6b: case 0x6c: case 0x6d: case 0x6e: case 0x6f: case 0x79: return 1;
    default: return 2;
    }
}
static unsigned select_source(Instruction i,unsigned s) {
    return s==0?i.a&511:s==1?(i.a>>13)&511:i.b&511;
}
static unsigned source_channel(Instruction i,unsigned s) {
    return s==0?(i.a>>10)&3:s==1?(i.a>>23)&3:(i.b>>10)&3;
}
static bool source(State* s,Instruction i,unsigned n,uint32_t cf0,uint32_t cf1,
                   const uint32_t literals[4],uint32_t* result) {
    unsigned sel=select_source(i,n), c=source_channel(i,n);
    bool rel=n==0?(i.a>>9)&1:n==1?(i.a>>22)&1:(i.b>>9)&1;
    int32_t relative=0;
    if(rel) {
        unsigned mode=(i.a>>26)&7;
        if(mode>3 || !(s->address_valid&(1u<<mode)))return unsupported(__LINE__);
        relative=s->address[mode];
    }
    uint32_t bits;
    if(sel<128) {
        int reg=(int)sel+relative;
        if(reg<0 || reg>=128)return unsupported(__LINE__);
        sel=(unsigned)reg;
        if(!(s->valid[sel]&(1u<<c))) return unsupported(__LINE__);
        bits=s->r[sel][c];
    } else if(sel<192) {
        unsigned bank=sel<160?(cf0>>22)&15:(cf0>>26)&15;
        unsigned base=sel<160?(cf1>>2)&255:(cf1>>10)&255;
        unsigned mode=sel<160?(cf0>>30)&3:cf1&3;
        /* KCACHE_LOCK_1/2 are fixed banks; loop-relative addressing is not. */
        if(mode!=1 && mode!=2) return unsupported(__LINE__);
        int vector=(int)((sel&31)+base*16)+relative;
        if(vector<0)return unsupported(__LINE__);
        uint64_t offset=(uint64_t)vector*16+c*4;
        const uint8_t* p=s->inputs->blocks[bank];
        if(!p || offset+4>s->inputs->block_sizes[bank]) return unsupported(__LINE__);
        p+=offset;
        /* Uniform blocks are GPU-native words, unlike the CPU-endian
           GX2SetVertexUniformReg arguments. Cemu uploads these bytes as-is. */
        bits=le(p);
    } else if(sel>=256) {
        int vector=(int)sel-256+relative;
        if(vector<0)return unsupported(__LINE__);
        unsigned word=(unsigned)vector*4+c;
        if(word>=s->inputs->uniform_count || !s->inputs->uniforms ||
           !s->inputs->uniform_valid || !s->inputs->uniform_valid[word]) return unsupported(__LINE__);
        bits=s->inputs->uniforms[word];
    } else { if(rel)return unsupported(__LINE__);switch(sel) {
    case 248: bits=0; break;
    case 249: bits=0x3f800000; break;
    case 250: bits=1; break;
    case 251: bits=0xffffffff; break;
    case 252: bits=0x3f000000; break;
    case 253: bits=literals[c]; break;
    case 254: case 255: {
        unsigned lane=sel==254?c:4;
        if(!(s->pv_valid&(1u<<lane))) return unsupported(__LINE__);
        bits=s->pv[lane]; break;
    }
    default: return unsupported(__LINE__);
    }
    }
    bool absolute=!op3(i) && ((i.b>>n)&1);
    bool negative=n==0?(i.a>>12)&1:n==1?(i.a>>25)&1:(i.b>>12)&1;
    if(integer_source(i,n)) {
        if(absolute && (bits&0x80000000u)) bits=0u-bits;
        if(negative) bits=0u-bits;
    } else {
        if(absolute) bits&=0x7fffffffu;
        if(negative) bits^=0x80000000u;
    }
    *result=bits; return true;
}
static float multiply(float a,float b,bool ieee) {
    /* Latte MUL flushes a zero operand, including 0*Inf; MUL_IEEE does not. */
    return !ieee && (a==0 || b==0)?0:a*b;
}
static bool evaluate(Instruction i,const uint32_t v[3],uint32_t* out) {
    float a=as_float(v[0]),b=as_float(v[1]),c=as_float(v[2]),f;
    unsigned op=opcode(i);
    int32_t sa,sb;memcpy(&sa,&v[0],4);memcpy(&sb,&v[1],4);
    if(predicate_op(i)) {
        bool yes;
        if(op>=0x42) yes=op==0x42?sa==sb:op==0x43?sa>sb:op==0x44?sa>=sb:sa!=sb;
        else yes=op==0x20?a==b:op==0x21?a>b:op==0x22?a>=b:a!=b;
        *out=yes?UINT32_MAX:0;return true;
    }
    if(op3(i) && op>=0x18 && op<=0x1e && op!=0x1b) {
        bool choose=op==0x18?a==0:op==0x19?a>0:op==0x1a?a>=0:
                    op==0x1c?sa==0:op==0x1d?sa>0:sa>=0;
        *out=v[choose?1:2];return true;
    }
    if(!op3(i)) switch(op) {
    case 0x19: *out=v[0];return true; /* MOV preserves NaN/integer payloads. */
    case 0xc: *out=a==b?UINT32_MAX:0;return true;
    case 0xd: *out=a>b?UINT32_MAX:0;return true;
    case 0xe: *out=a>=b?UINT32_MAX:0;return true;
    case 0xf: *out=a!=b?UINT32_MAX:0;return true;
    case 0x30: *out=v[0]&v[1];return true;
    case 0x31: *out=v[0]|v[1];return true;
    case 0x32: *out=v[0]^v[1];return true;
    case 0x33: *out=~v[0];return true;
    case 0x34: *out=v[0]+v[1];return true;
    case 0x35: *out=v[0]-v[1];return true;
    case 0x36: *out=sa>sb?v[0]:v[1];return true;
    case 0x37: *out=sa<sb?v[0]:v[1];return true;
    case 0x38: *out=v[0]>v[1]?v[0]:v[1];return true;
    case 0x39: *out=v[0]<v[1]?v[0]:v[1];return true;
    case 0x3a: *out=v[0]==v[1]?UINT32_MAX:0;return true;
    case 0x3b: *out=sa>sb?UINT32_MAX:0;return true;
    case 0x3c: *out=sa>=sb?UINT32_MAX:0;return true;
    case 0x3d: *out=v[0]!=v[1]?UINT32_MAX:0;return true;
    case 0x3e: *out=v[0]>v[1]?UINT32_MAX:0;return true;
    case 0x3f: *out=v[0]>=v[1]?UINT32_MAX:0;return true;
    case 0x6b:
        /* Do not invoke undefined host casts for unsupported out-of-range input. */
        if(!isfinite(a) || a<-2147483648.0f || a>=2147483648.0f) {
            const char* trace=getenv("BOTW_LATTE_TRACE");
            if(trace && *trace=='1') fprintf(stderr,"latte: FLT_TO_INT input=%08X (%g)\n",v[0],a);
            return unsupported(__LINE__);
        }
        *out=(uint32_t)(int32_t)a;return true;
    case 0x79:
        if(!isfinite(a) || a<0 || a>=4294967296.0f) return unsupported(__LINE__);
        *out=(uint32_t)a;return true;
    case 0x70: {
        unsigned shift=v[1]&31;
        *out=v[0]>>shift;
        if(shift && (v[0]&0x80000000u)) *out|=UINT32_MAX<<(32-shift);
        return true;
    }
    case 0x71: *out=v[0]>>(v[1]&31);return true;
    case 0x72: *out=v[0]<<(v[1]&31);return true;
    case 0x73: case 0x75: *out=v[0]*v[1];return true;
    default: break;
    }
    if(op3(i)) {
        switch(op) {
        case 0x10: case 0x11: case 0x12: case 0x13: case 0x14:
            f=multiply(a,b,op==0x14)+c;
            if(op==0x11) f*=2; else if(op==0x12) f*=4; else if(op==0x13) f*=0.5f;
            break;
        case 0x18: f=a==0?b:c; break;
        case 0x19: f=a>0?b:c; break;
        case 0x1a: f=a>=0?b:c; break;
        default: return unsupported(__LINE__);
        }
    } else switch(op) {
    case 0: f=a+b; break;
    case 1: case 2: f=multiply(a,b,op==2); break;
    case 3: f=fmaxf(a,b); break;
    case 4: f=fminf(a,b); break;
    case 5: case 6:
        if(isnan(a) || isnan(b)) return unsupported(__LINE__); /* NaN-mode distinction not implemented. */
        f=op==5?fmaxf(a,b):fminf(a,b);break;
    case 8: f=a==b?1.0f:0.0f; break;
    case 9: f=a>b?1.0f:0.0f; break;
    case 10: f=a>=b?1.0f:0.0f; break;
    case 11: f=a!=b?1.0f:0.0f; break;
    case 0x10: f=a-floorf(a); break;
    case 0x11: f=truncf(a); break;
    case 0x13: {
        /* RNDNE is independent of the host floating-point rounding mode. */
        float low=floorf(a),fraction=a-low;
        f=fraction>.5f || (fraction==.5f && fmodf(low,2)!=0)?low+1:low;
        break;
    }
    case 0x14: f=floorf(a); break;
    case 0x16:
        if(isnan(a))return unsupported(__LINE__);
        f=fminf(255,fmaxf(-256,floorf(a)));break;
    case 0x18:
        *out=(uint32_t)(sa < -256 ? -256 : sa > 255 ? 255 : sa);return true;
    case 0x19: f=a; break;
    case 0x1a: f=0; break;
    case 0x61: f=exp2f(a);break;
    case 0x62: case 0x63:
        f=log2f(fmaxf(0,a));
        if(op==0x62 && isinf(f)) f=-FLT_MAX;
        break;
    case 0x66: f=1.0f/a;break;
    case 0x69: f=1.0f/sqrtf(a);break;
    case 0x6a: f=sqrtf(a);break;
    case 0x6c: f=(float)sa;break;
    case 0x6d: f=(float)v[0];break;
    case 0x6e: f=sinf(a/0.1591549367f);break;
    case 0x6f: f=cosf(a/0.1591549367f);break;
    default: return unsupported(__LINE__);
    }
    *out=as_bits(f); return true;
}
static bool clause(State* s,const uint8_t* p,size_t size,uint32_t a,uint32_t b) {
    unsigned at=a&0x3fffff,count=((b>>18)&127)+1;
    if((uint64_t)(at+count)*8>size) return unsupported(__LINE__);
    unsigned end=at+count;
    while(at<end) {
        Instruction ins[5]; uint32_t value[5],args[5][3]={{0}};
        unsigned n=0,units=0,literal_words=0;
        do {
            if(n==5 || at==end) return unsupported(__LINE__);
            Instruction i={le(p+at*8),le(p+at*8+4),0}; ++at;
            if((i.a>>29)&3 || (i.b>>28)&1) return unsupported(__LINE__); /* per-instruction predication/relative */
            if(predicate_op(i)) {
                /* Structured PUSH predicates have no GPR/OMOD/clamp output.
                   Standalone predicate-only execution is not implemented. */
                if(((b>>26)&15)!=9 || (i.b&0x80000070u) || (i.b&12)!=12) return unsupported(__LINE__);
            } else if(!op3(i) && (i.b&12)) return unsupported(__LINE__);
            if(!op3(i) && (opcode(i)==0x16 || opcode(i)==0x18) && (i.b&0x80000070u))
                return unsupported(__LINE__); /* MOVA has no GPR write or output modifiers. */
            i.unit=scalar_unit(i)?4:(i.b>>29)&3;
            if(units&(1u<<i.unit)) i.unit=4;
            if(units&(1u<<i.unit)) return unsupported(__LINE__);
            units|=1u<<i.unit;
            for(unsigned src=0;src<source_count(i);++src)
                if(select_source(i,src)==253 && source_channel(i,src)+1>literal_words)
                    literal_words=source_channel(i,src)+1;
            ins[n++]=i;
        } while(!(ins[n-1].a>>31));
        unsigned literal_slots=(literal_words+1)/2;
        if(literal_slots>end-at) return unsupported(__LINE__);
        uint32_t literals[4]={0};
        for(unsigned j=0;j<literal_slots*2;++j) literals[j]=le(p+at*8+j*4);
        at+=literal_slots;
        for(unsigned j=0;j<n;++j)
            for(unsigned src=0;src<source_count(ins[j]);++src)
                if(!source(s,ins[j],src,a,b,literals,&args[j][src])) return unsupported(__LINE__);
        for(unsigned j=0;j<n;++j) {
            unsigned op=opcode(ins[j]);
            if(!op3(ins[j]) && (op==0x50 || op==0x51)) {
                if(n<4 || ins[j].unit>=4) return unsupported(__LINE__);
                float dot=0;
                for(unsigned k=0;k<4;++k) {
                    if(op3(ins[k]) || opcode(ins[k])!=op || ins[k].unit!=k) return unsupported(__LINE__);
                    dot+=multiply(as_float(args[k][0]),as_float(args[k][1]),op==0x51);
                }
                value[j]=as_bits(dot);
            } else if(!evaluate(ins[j],args[j],&value[j])) return unsupported(__LINE__);
            if(s->trace_alu) fprintf(stderr,"latte: group=%u lane=%u op=%X src=%08X,%08X,%08X result=%08X\n",
                at,ins[j].unit,op,args[j][0],args[j][1],args[j][2],value[j]);
            unsigned omod=op3(ins[j])?0:(ins[j].b>>5)&3;
            bool clamp=(ins[j].b>>31)!=0;
            if(integer_result(ins[j]) && (omod || clamp)) return unsupported(__LINE__);
            if(omod || clamp) {
                float f=as_float(value[j]);
                if(omod==1) f*=2; else if(omod==2) f*=4; else if(omod==3) f*=0.5f;
                if(clamp) f=fminf(1,fmaxf(0,f));
                value[j]=as_bits(f);
            }
        }
        /* All sources, including PV/PS, see the previous group. Commit only
           after every lane has executed. The fifth ALU writes PS, not PV. */
        s->pv_valid=0;
        for(unsigned j=0;j<n;++j) {
            Instruction i=ins[j];
            if(!op3(i) && opcode(i)==0x1a) continue;
            uint32_t bits=value[j];
            if(predicate_op(i)) {
                s->active=s->active && bits!=0;
                continue;
            }
            s->pv[i.unit]=bits; s->pv_valid|=1u<<i.unit;
            if(!op3(i) && (opcode(i)==0x16 || opcode(i)==0x18)) {
                unsigned c=(i.b>>29)&3;
                s->address[c]=opcode(i)==0x16?(int32_t)as_float(bits):(int32_t)bits;
                s->address_valid|=1u<<c;
            }
            if(op3(i) || (i.b&16)) {
                unsigned reg=(i.b>>21)&127,c=(i.b>>29)&3;
                s->r[reg][c]=bits; s->valid[reg]|=1u<<c;
            }
        }
        if(!s->active) return true;
    }
    return true;
}
static bool fetch_uniform_clause(State* s,const uint8_t* program,size_t size,uint32_t a,uint32_t b) {
    unsigned count=((b>>10)&7)+(((b>>19)&1)<<3)+1;
    uint64_t start=(uint64_t)a*8;
    if((b&7) || ((b>>8)&3) || start+(uint64_t)count*16>size)
        return unsupported(__LINE__);
    if(!s->active) return true;
    for(unsigned i=0;i<count;++i) {
        const uint8_t* p=program+(size_t)start+i*16;
        uint32_t w0=le(p),w1=le(p+4),w2=le(p+8);
        unsigned tex_op=w0&31;
        if(tex_op==4) { /* GET_TEXTURE_RESINFO returns integer dimensions. */
            unsigned texture=(w0>>8)&255,src=(w0>>16)&127,dst=w1&127;
            unsigned component=(w2>>20)&7;
            uint32_t result[4];
            if((w0&0xFF8000E0u) || (w1&0x0FE00180u) || (w2&0xFFFFFu) ||
               component>=4 || texture>=32 || !(s->valid[src]&(1u<<component)) ||
               !s->inputs->texture_info || !s->inputs->texture_info(s->inputs->texture_user,
                   texture,s->r[src][component],result))return unsupported(__LINE__);
            for(unsigned c=0;c<4;++c) {
                unsigned sel=(w1>>(9+c*3))&7;
                if(sel==7)continue;
                if(sel>=4)return unsupported(__LINE__);
                s->r[dst][c]=result[sel];s->valid[dst]|=1u<<c;
            }
            continue;
        }
        if(tex_op==0x10 || tex_op==0x11 || tex_op==0x13) {
            unsigned texture=(w0>>8)&255,sampler=(w2>>15)&31,src=(w0>>16)&127,dst=w1&127;
            unsigned x=(w2>>20)&7,y=(w2>>23)&7,w=(w2>>29)&7;
            if((w0&0xFF8000E0u) || (w1&0x0FE00180u) || (w1&0x30000000u)!=0x30000000u ||
               (w2&32767) || x>=4 || y>=4 || texture>=32 ||
               !(s->valid[src]&(1u<<x)) || !(s->valid[src]&(1u<<y)) || !s->inputs->sample_texture)
                return unsupported(__LINE__);
            /* Implicit derivatives do not exist in a vertex shader: SAMPLE
               uses level zero, while SAMPLE_L reads the explicit W selector. */
            float lod=0,result[4];
            if(tex_op==0x11) {
                if(w>=4 || !(s->valid[src]&(1u<<w)))return unsupported(__LINE__);
                lod=as_float(s->r[src][w]);
            }
            if(!s->inputs->sample_texture(s->inputs->texture_user,texture,sampler,
                as_float(s->r[src][x]),as_float(s->r[src][y]),lod,result))return unsupported(__LINE__);
            for(unsigned c=0;c<4;++c) {
                unsigned sel=(w1>>(9+c*3))&7;
                if(sel==7)continue;if(sel==6)return unsupported(__LINE__);
                s->r[dst][c]=sel<4?as_bits(result[sel]):sel==5?0x3f800000u:0;
                s->valid[dst]|=1u<<c;
            }
            continue;
        }
        unsigned bank=(w0>>8)&255,src=(w0>>16)&127,component=(w0>>24)&3,dst=w1&127;
        /* VFETCH, fetch type NO_INDEX_OFFSET, 16-byte mega-fetch, constant
           resource format. The uniform resource is a packed LE float4 array.
           Reject other resource/format/address modes rather than guessing.
           The fourth instruction word is padding (often compiler garbage). */
        if((w0&0xfc8000ffu)!=0x3c000040u || bank<0x80 || bank>=0x90 ||
           (w1&0xffe00180u)!=0x00200000u || w2!=0x00080000u)
            return unsupported(__LINE__);
        if(!(s->valid[src]&(1u<<component))) return unsupported(__LINE__);
        uint64_t offset=(uint64_t)s->r[src][component]*16;
        const uint8_t* block=s->inputs->blocks[bank-0x80];
        if(!block || offset+16>s->inputs->block_sizes[bank-0x80]) return unsupported(__LINE__);
        uint32_t values[4];for(unsigned c=0;c<4;++c)values[c]=le(block+(size_t)offset+c*4);
        for(unsigned c=0;c<4;++c) {
            unsigned sel=(w1>>(9+c*3))&7;
            if(sel==7) continue;
            if(sel==6) return unsupported(__LINE__);
            s->r[dst][c]=sel<4?values[sel]:sel==5?0x3f800000u:0;
            s->valid[dst]|=1u<<c;
        }
    }
    return true;
}
bool wiiu_latte_execute_vertex(const uint8_t* program,size_t size,
    const WiiULatteVertexInputs* inputs,WiiULatteVertexOutputs* outputs) {
    if(!program || !inputs || !outputs || size<8 || size>1048576 || size%8) return unsupported(__LINE__);
    State s={0}; s.inputs=inputs;s.active=true;
    const char* trace=getenv("BOTW_LATTE_TRACE");s.trace_alu=trace && *trace=='2';
    bool stack[32];unsigned depth=0;
    memcpy(s.r,inputs->gpr,sizeof(s.r)); memcpy(s.valid,inputs->valid,sizeof(s.valid));
    WiiULatteVertexOutputs out={0};
    for(unsigned cf=0;cf<size/8 && cf<1024;++cf) {
        uint32_t a=le(program+cf*8),b=le(program+cf*8+4);
        unsigned op=(b>>23)&127;
        if(op>=64) {
            unsigned alu=(b>>26)&15;
            if(alu<8 || alu>11) return unsupported(__LINE__);
            if(alu==9) {
                if(depth==32) return unsupported(__LINE__);
                stack[depth++]=s.active;
            }
            if(s.active && !clause(&s,program,size,a,b)) return unsupported(__LINE__);
            unsigned pops=alu>=10?alu-9:0;
            if(pops>depth) return unsupported(__LINE__);
            if(pops) {depth-=pops;s.active=stack[depth];}
            continue;
        }
        if(op==0xa || op==0xd || op==0xe) {
            unsigned pops=b&7;
            if(((b>>8)&3) || (b&(1u<<21)) || !depth || pops>depth) return unsupported(__LINE__);
            if(op==0xe) {
                if(!pops) return unsupported(__LINE__);
                depth-=pops;s.active=stack[depth];continue;
            }
            /* Only forward structured branches. No arbitrary jumps/loops. */
            if(a<=cf || a>=size/8 || a>=1024) return unsupported(__LINE__);
            if(op==0xd) s.active=stack[depth-1] && !s.active;
            if(!s.active) {
                if(pops) {depth-=pops;s.active=stack[depth];}
                cf=a-1;
            }
            continue;
        }
        if(op==1) {
            if(!fetch_uniform_clause(&s,program,size,a,b)) return unsupported(__LINE__);
        } else if(op==0 || op==0x13) {
            if((b&7) || ((b>>8)&3)) return unsupported(__LINE__);
        } else if(op==0x27 || op==0x28) {
            if(!s.active) return unsupported(__LINE__);
            unsigned type=(a>>13)&3,base=a&8191,reg=(a>>15)&127;
            if(((a>>22)&255) || ((b>>17)&15)) return unsupported(__LINE__);
            float* dst; uint8_t* mask;
            if(type==1 && base==60) { dst=out.position;mask=&out.position_mask; }
            else if(type==2 && base<32) { dst=out.parameters[base];mask=&out.parameter_mask[base]; }
            else return unsupported(__LINE__);
            for(unsigned c=0;c<4;++c) {
                unsigned sel=(b>>(c*3))&7;
                if(sel==7) continue;
                if(sel<4) {
                    if(!(s.valid[reg]&(1u<<sel))) {
                        /* Compilers leave unused varying components undefined.
                           Preserve the missing-component mask; consumers must
                           validate only the components their PS actually reads. */
                        if(type==2) continue;
                        return unsupported(__LINE__);
                    }
                    dst[c]=as_float(s.r[reg][sel]);
                } else if(sel==4 || sel==5) dst[c]=sel==5?1.0f:0.0f;
                else return unsupported(__LINE__);
                if(!isfinite(dst[c])) {
                    if(type==2) {dst[c]=0;continue;}
                    return unsupported(__LINE__);
                }
                *mask|=1u<<c;
            }
        } else return unsupported(__LINE__);
        if(b&(1u<<21)) {
            if(depth || out.position_mask!=15) return unsupported(__LINE__);
            *outputs=out; return true;
        }
    }
    return unsupported(__LINE__); /* unterminated or unsupported control flow */
}
