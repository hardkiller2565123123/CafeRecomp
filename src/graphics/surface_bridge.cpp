// CafeRecomp adapter for the MPL-2.0 surface implementation imported through
// ZeldaWWHDRecomp. This boundary needs neither its game runtime nor Vulkan/SDL.
#include "graphics/surface_bridge.h"
#include "gx2_surface_calc.h"

void cemu_shim_log(const std::string& message) {
    std::fprintf(stderr, "latte: %s\n", message.c_str());
}

extern "C" bool cafe_surface_calculate(uint32_t words[29]) {
    if (!words || words[0]>7 || !words[1] || !words[2] ||
        words[1]>16384 || words[2]>16384 || words[3]>2048 ||
        words[4]>14 || words[6]>3 ||
        (words[12]>16 && words[12]!=32)) return false;
    uint32_t bits=Latte::GetFormatBits(static_cast<Latte::E_HWFMT>(words[5]&63));
    if (!bits || uint64_t(words[1])*words[2]*std::max(words[3],1u)*bits*(1u<<words[6])/8>0xFFFFFFFFull)
        return false;
    GX2Surface surface{};
    uint32_t encoded[29];
    for (unsigned i=0;i<29;++i) encoded[i]=_swapEndianU32(words[i]);
    std::memcpy(&surface,encoded,sizeof(surface));
    if (!surface.depth) surface.depth=1;
    gx2calc::CalcSurfaceSizeAndAlignment(&surface);
    std::memcpy(encoded,&surface,sizeof(surface));
    for (unsigned i=0;i<29;++i) words[i]=_swapEndianU32(encoded[i]);
    return true;
}
