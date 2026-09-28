if(NOT DEFINED REPO_ROOT)
    message(FATAL_ERROR "REPO_ROOT is required")
endif()

file(GLOB llvm_sources
    "${REPO_ROOT}/src/backend/llvm/*.c"
    "${REPO_ROOT}/src/backend/llvm/*.cpp"
    "${REPO_ROOT}/src/backend/llvm/*.h"
)
list(APPEND llvm_sources
    "${REPO_ROOT}/src/backend/module_abi.c"
    "${REPO_ROOT}/src/backend/module_abi.h"
    "${REPO_ROOT}/src/backend/variant_output.c"
    "${REPO_ROOT}/src/backend/variant_output.h"
)

foreach(source IN LISTS llvm_sources)
    file(READ "${source}" contents)
    string(REGEX MATCHALL "\n" newlines "${contents}")
    list(LENGTH newlines line_count)
    file(STRINGS "${source}" lines)
    if(line_count GREATER 300)
        message(FATAL_ERROR "${source} has ${line_count} lines; limit is 300")
    endif()
    foreach(line IN LISTS lines)
        string(LENGTH "${line}" line_length)
        if(line_length GREATER 120 AND line MATCHES "//|/\\*")
            message(FATAL_ERROR "long comment in ${source}: ${line}")
        endif()
    endforeach()
endforeach()
