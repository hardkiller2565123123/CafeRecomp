if(NOT DEFINED C_COMPILER OR NOT DEFINED GENERATED_DIR OR
   NOT DEFINED CPU_LIBRARY OR NOT DEFINED BACKEND_LIBRARY OR
   NOT DEFINED REPO_SRC)
    message(FATAL_ERROR "missing generated variant link input")
endif()

set(test_source "${GENERATED_DIR}/variant_runtime_test.c")
set(test_binary "${GENERATED_DIR}/variant_runtime_test")
file(WRITE "${test_source}" [=[
#include "backend/module_abi.h"
#include "generated.h"
#include <string.h>
int main(void) {
    CPUState state;
    if (!cpu_init(&state)) return 1;
    DolRecompLoadedModule loaded;
    if (!dolrecomp_select_module(&dolrecomp_module_v4,
            DOLRECOMP_FEATURE_X86_V3, DOLRECOMP_SEMANTICS_EXACT,
            &loaded, stderr)) return 2;
    if (strcmp(loaded.variant_name, "x86-64-v3-exact") != 0) return 3;
    state.pc = 0x80003100u;
    state.lr = 0x81234567u;
    state.ctr = 5;
    state.downcount = 1000;
    if (!dolrecomp_run_loaded(&loaded, &state, 1)) return 4;
    if (state.gpr[3] != 5 || state.pc != 0x81234564u) return 5;
    cpu_free(&state);
    return 0;
}
]=])

set(chunks "${GENERATED_DIR}/chunks")
separate_arguments(extra_c_flags NATIVE_COMMAND "${C_FLAGS}")
set(objects
    "${chunks}/chunk_0000_text0_80003100.o"
    "${chunks}/chunk_0000_text0_80003100_x86_64_v3.o"
    "${chunks}/chunk_0001_text0_80003900.o"
    "${chunks}/chunk_0001_text0_80003900_x86_64_v3.o"
)
execute_process(
    COMMAND "${C_COMPILER}" ${extra_c_flags} -std=c11 -I "${REPO_SRC}"
            -I "${GENERATED_DIR}" "${test_source}" ${objects}
            "${BACKEND_LIBRARY}" "${CPU_LIBRARY}" -lm -o "${test_binary}"
    RESULT_VARIABLE compile_status
    OUTPUT_VARIABLE compile_output
    ERROR_VARIABLE compile_error
)
if(NOT compile_status EQUAL 0)
    message(FATAL_ERROR "variant runtime link failed:\n${compile_output}${compile_error}")
endif()
execute_process(COMMAND "${test_binary}" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 0)
    message(FATAL_ERROR "variant runtime failed with status ${run_status}")
endif()
