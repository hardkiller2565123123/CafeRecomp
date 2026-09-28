if(NOT DEFINED OBJDUMP OR NOT DEFINED X86_OBJECT OR NOT DEFINED ARM_OBJECT)
    message(FATAL_ERROR "missing MC contract input")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -d "${X86_OBJECT}"
    RESULT_VARIABLE x86_status
    OUTPUT_VARIABLE x86_disassembly
    ERROR_VARIABLE x86_error
)
if(NOT x86_status EQUAL 0)
    message(FATAL_ERROR "x86 disassembly failed: ${x86_error}")
endif()
if(NOT x86_disassembly MATCHES "(bswap|movbe)")
    message(FATAL_ERROR "x86 object has no target endian instruction")
endif()
if(NOT x86_disassembly MATCHES "adc[qwl]?")
    message(FATAL_ERROR "x86 object has no native carry-chain instruction")
endif()
if(NOT x86_disassembly MATCHES "v(add|sub|mul)p[sd]")
    message(FATAL_ERROR "x86 object has no packed paired-single arithmetic")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -t "${X86_OBJECT}"
    RESULT_VARIABLE symbol_status
    OUTPUT_VARIABLE symbols
    ERROR_VARIABLE symbol_error
)
if(NOT symbol_status EQUAL 0)
    message(FATAL_ERROR "x86 symbol inspection failed: ${symbol_error}")
endif()
foreach(width f32 f64)
    if(NOT symbols MATCHES
       "\\.text\\.unlikely\\..*fix_pair_nan_${width}")
        message(FATAL_ERROR
            "paired ${width} NaN fixup is not in the unlikely section")
    endif()
endforeach()
if(NOT symbols MATCHES
   "\\.text\\.unlikely\\..*classify_fprf_unusual")
    message(FATAL_ERROR
        "unusual FPRF classifier is not in the unlikely section")
endif()
foreach(fixup fix_pair_fma_nan_f32 fix_pair_fma_nan_f64
              fix_pair_fma_rounding)
    if(NOT symbols MATCHES "\\.text\\.unlikely\\..*${fixup}")
        message(FATAL_ERROR "${fixup} is not in the unlikely section")
    endif()
endforeach()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_800039C0_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE paired_status
    OUTPUT_VARIABLE paired_disassembly
    ERROR_VARIABLE paired_error
)
if(NOT paired_status EQUAL 0)
    message(FATAL_ERROR "paired-chain disassembly failed: ${paired_error}")
endif()
if(paired_disassembly MATCHES "ppc_ps_.*")
    message(FATAL_ERROR "paired SSA chain still calls a paired-single helper")
endif()
foreach(operation add mul sub)
    if(NOT paired_disassembly MATCHES "v?${operation}ps")
        message(FATAL_ERROR
            "paired SSA chain has no packed-single ${operation}")
    endif()
endforeach()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003980_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE fma_status
    OUTPUT_VARIABLE fma_disassembly
    ERROR_VARIABLE fma_error
)
if(NOT fma_status EQUAL 0)
    message(FATAL_ERROR "raw paired-FMA disassembly failed: ${fma_error}")
endif()
if(fma_disassembly MATCHES "ppc_ps_madd")
    message(FATAL_ERROR "raw paired FMA still calls an architectural helper")
endif()
if(NOT fma_disassembly MATCHES "vfm(add|sub)[0-9]*pd")
    message(FATAL_ERROR "raw paired FMA has no packed-double fused operation")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003A20_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE fused_chain_status
    OUTPUT_VARIABLE fused_chain_disassembly
    ERROR_VARIABLE fused_chain_error
)
if(NOT fused_chain_status EQUAL 0)
    message(FATAL_ERROR
        "paired-FMA chain disassembly failed: ${fused_chain_error}")
endif()
if(fused_chain_disassembly MATCHES "ppc_ps_madd")
    message(FATAL_ERROR "paired-FMA chain still calls an architectural helper")
endif()
if(NOT fused_chain_disassembly MATCHES "vfm(add|sub)[0-9]*ps")
    message(FATAL_ERROR "paired-FMA chain has no packed-single fused operation")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003A60_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE psq_fma_status
    OUTPUT_VARIABLE psq_fma_disassembly
    ERROR_VARIABLE psq_fma_error
)
if(NOT psq_fma_status EQUAL 0)
    message(FATAL_ERROR "PSQ-FMA disassembly failed: ${psq_fma_error}")
endif()
if(psq_fma_disassembly MATCHES "ppc_ps_madd")
    message(FATAL_ERROR "PSQ-FMA path still calls an architectural helper")
endif()
if(NOT psq_fma_disassembly MATCHES "vfm(add|sub)[0-9]*ps" OR
   NOT psq_fma_disassembly MATCHES "vfm(add|sub)[0-9]*pd")
    message(FATAL_ERROR "PSQ-FMA is missing its NI-specialized fused paths")
endif()
if(psq_fma_disassembly MATCHES "vcvtsd2ss")
    message(FATAL_ERROR
        "PSQ-FMA widens a packed-single result before an unquantized store")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003AA0_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE fixed_gqr_status
    OUTPUT_VARIABLE fixed_gqr_disassembly
    ERROR_VARIABLE fixed_gqr_error
)
if(NOT fixed_gqr_status EQUAL 0)
    message(FATAL_ERROR "fixed-GQR disassembly failed: ${fixed_gqr_error}")
endif()
if(fixed_gqr_disassembly MATCHES "ppc_ps_madd" OR
   fixed_gqr_disassembly MATCHES "vfm(add|sub)[0-9]*pd")
    message(FATAL_ERROR "fixed-GQR FMA left its packed-single fast path")
endif()
if(NOT fixed_gqr_disassembly MATCHES "vfm(add|sub)[0-9]*ps")
    message(FATAL_ERROR "fixed-GQR FMA has no packed-single fused operation")
endif()
if(fixed_gqr_disassembly MATCHES "jmpq[^\n]*\\*" OR
   fixed_gqr_disassembly MATCHES "vcvtsd2ss")
    message(FATAL_ERROR
        "fixed-GQR path retained dynamic format dispatch or a store round-trip")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003500_budget__x86_64_v3
            "${X86_OBJECT}"
    RESULT_VARIABLE call_status
    OUTPUT_VARIABLE call_disassembly
    ERROR_VARIABLE call_error
)
if(NOT call_status EQUAL 0)
    message(FATAL_ERROR "direct-call disassembly failed: ${call_error}")
endif()
if(call_disassembly MATCHES "ppc_host_call" OR
   call_disassembly MATCHES "dolrecomp_indirect_dispatch")
    message(FATAL_ERROR "known call uses generic guest dispatch")
endif()
if(NOT call_disassembly MATCHES "ppc_native_region_available" OR
   NOT call_disassembly MATCHES "80003600" OR
   NOT call_disassembly MATCHES "80003608")
    message(FATAL_ERROR "known call has no callee-region interception gate")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -d "${ARM_OBJECT}"
    RESULT_VARIABLE arm_status
    OUTPUT_VARIABLE arm_disassembly
    ERROR_VARIABLE arm_error
)
if(NOT arm_status EQUAL 0)
    message(FATAL_ERROR "AArch64 disassembly failed: ${arm_error}")
endif()
if(NOT arm_disassembly MATCHES "rev")
    message(FATAL_ERROR "AArch64 object has no REV endian instruction")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003500__aarch64_a57
            "${ARM_OBJECT}"
    RESULT_VARIABLE arm_abi_status
    OUTPUT_VARIABLE arm_abi_disassembly
    ERROR_VARIABLE arm_abi_error
)
if(NOT arm_abi_status EQUAL 0)
    message(FATAL_ERROR "AArch64 ABI disassembly failed: ${arm_abi_error}")
endif()
if(NOT arm_abi_disassembly MATCHES "setjmp" OR
   NOT arm_abi_disassembly MATCHES "func_80003500_budget__aarch64_a57")
    message(FATAL_ERROR "AArch64 wrapper has no cold escape boundary")
endif()

execute_process(
    COMMAND "${OBJDUMP}" -dr
            --disassemble-symbols=func_80003500_budget__aarch64_a57
            "${ARM_OBJECT}"
    RESULT_VARIABLE arm_call_status
    OUTPUT_VARIABLE arm_call_disassembly
    ERROR_VARIABLE arm_call_error
)
if(NOT arm_call_status EQUAL 0)
    message(FATAL_ERROR "AArch64 call disassembly failed: ${arm_call_error}")
endif()
if(NOT arm_call_disassembly MATCHES "longjmp")
    message(FATAL_ERROR "AArch64 native body has no cold escape")
endif()
