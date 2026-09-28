#include <bit>
#include <cstdint>
#include <span>

static_assert(sizeof(void*) == 8, "ModernGekko requires 8-byte pointers");
static_assert(std::endian::native == std::endian::little ||
              std::endian::native == std::endian::big);

int main()
{
    std::uint32_t values[]{1, 2, 3};
    return std::span{values}.size() == 3 ? 0 : 1;
}
