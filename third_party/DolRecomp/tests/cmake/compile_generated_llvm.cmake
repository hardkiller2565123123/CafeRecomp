if(NOT DEFINED C_COMPILER OR NOT DEFINED GENERATED_C OR NOT DEFINED REPO_SRC)
    message(FATAL_ERROR "missing generated LLVM compile input")
endif()

if(C_COMPILER_ID STREQUAL "MSVC")
    set(compile_args /std:c11 "/I${REPO_SRC}" /c "${GENERATED_C}"
        "/Fo${GENERATED_C}.obj")
else()
    set(compile_args -std=c11 -I "${REPO_SRC}" -c "${GENERATED_C}"
        -o "${GENERATED_C}.o")
endif()

execute_process(
    COMMAND "${C_COMPILER}" ${compile_args}
    RESULT_VARIABLE status
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "generated LLVM header does not compile:\n${output}${error}")
endif()
