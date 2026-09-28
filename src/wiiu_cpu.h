#ifndef BOTW_WIIU_CPU_H
#define BOTW_WIIU_CPU_H
#include "cpu/cpu.h"
void wiiu_instruction_fallback(CPUState* cpu, u32 raw, u32 cia);
#endif
