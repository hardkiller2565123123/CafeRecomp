#include <stdint.h>

_Static_assert(sizeof(void*) == 8, "ModernGekko requires 8-byte pointers");

int main(void)
{
    uint64_t value = UINT64_C(0x4d6f6465726e474b);
    return value == 0;
}
