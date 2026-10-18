# Sources integrated into the CafeRecomp build; no nested project/configure or downloads.
set(_zlib "${PROJECT_SOURCE_DIR}/src/compression/zlib")
add_library(cafe_zlib STATIC
    ${_zlib}/adler32.c ${_zlib}/compress.c ${_zlib}/crc32.c
    ${_zlib}/deflate.c ${_zlib}/gzclose.c ${_zlib}/gzlib.c
    ${_zlib}/gzread.c ${_zlib}/gzwrite.c ${_zlib}/infback.c
    ${_zlib}/inffast.c ${_zlib}/inflate.c ${_zlib}/inftrees.c
    ${_zlib}/trees.c ${_zlib}/uncompr.c ${_zlib}/zutil.c)
target_include_directories(cafe_zlib PUBLIC ${_zlib})
add_library(ZLIB::ZLIBSTATIC ALIAS cafe_zlib)

# The C backend and its Espresso runtime come from the locally modified DolRecomp
# sources. Keep their names and GPL notices; consolidation is not a relicensing.
file(GLOB _compiler_sources CONFIGURE_DEPENDS
    "${DOLRECOMP_DIR}/app/*.c" "${DOLRECOMP_DIR}/platform/*.c"
    "${DOLRECOMP_DIR}/frontend/*.c" "${DOLRECOMP_DIR}/frontend/container/*.c"
    "${DOLRECOMP_DIR}/analysis/*.c" "${DOLRECOMP_DIR}/backend/*.c"
    "${DOLRECOMP_DIR}/ir/*.c" "${DOLRECOMP_DIR}/cpu/*.c")
# The optional LLVM variant emitter is not used by the C-only generator.
list(FILTER _compiler_sources EXCLUDE REGEX "/variant_output\\.c$")
file(GLOB_RECURSE _compiler_hash_sources CONFIGURE_DEPENDS "${DOLRECOMP_DIR}/*.c" "${DOLRECOMP_DIR}/*.h")
list(SORT _compiler_hash_sources)
set(_compiler_digest "")
foreach(_source IN LISTS _compiler_hash_sources)
    file(SHA256 "${_source}" _hash)
    string(APPEND _compiler_digest "${_hash}")
endforeach()
string(SHA256 _compiler_digest "${_compiler_digest}")
add_executable(cafe_codegen ${_compiler_sources})
target_include_directories(cafe_codegen PRIVATE ${DOLRECOMP_DIR})
target_compile_definitions(cafe_codegen PRIVATE DOLRECOMP_HAVE_ZLIB
    DOLRECOMP_CODEGEN_SOURCE_HASH="${_compiler_digest}")
target_link_libraries(cafe_codegen PRIVATE cafe_zlib)
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(cafe_codegen PRIVATE Threads::Threads m)
endif()
set_target_properties(cafe_codegen PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${BOTW_OUTPUT_ROOT}/tools")
foreach(_config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_config}" _upper)
    set_target_properties(cafe_codegen PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${_upper} "${BOTW_OUTPUT_ROOT}/tools")
endforeach()

if(NOT CAFERECOMP_COMPILER_ONLY)
    set(_latte "${PROJECT_SOURCE_DIR}/src/graphics/latte")
    add_library(cafe_surface STATIC
        ${_latte}/gx2_surface_calc.cpp
        ${_latte}/Cafe/HW/Latte/LatteAddrLib/LatteAddrLib.cpp
        ${_latte}/Cafe/HW/Latte/LatteAddrLib/LatteAddrLib_Coord.cpp
        src/graphics/surface_bridge.cpp)
    target_include_directories(cafe_surface PRIVATE ${_latte}
        "${PROJECT_SOURCE_DIR}/src/libraries/fmt/include")
    target_include_directories(cafe_surface PUBLIC "${PROJECT_SOURCE_DIR}/src")
    if(MSVC)
        target_compile_options(cafe_surface PRIVATE /FIcemu_shim.h /Zc:preprocessor /utf-8)
    else()
        target_compile_options(cafe_surface PRIVATE -include cemu_shim.h)
    endif()
endif()
