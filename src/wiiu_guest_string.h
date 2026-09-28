#ifndef BOTW_GUEST_STRING_H
#define BOTW_GUEST_STRING_H
#include "cpu/cpu.h"
/* Checked fast path for the two verified v208 SafeString contains routines.
   Returns false without side effects when a virtual callback or memory layout
   requires executing the original guest implementation. */
bool wiiu_guest_string_contains(CPUState* cpu, u32 address);
#endif
