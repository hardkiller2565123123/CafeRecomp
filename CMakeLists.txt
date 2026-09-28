cmake_minimum_required(VERSION 3.22)
project(CafeRecomp C)

include(CTest)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(FetchContent)

set(BOTW_OUTPUT_ROOT "${CMAKE_CURRENT_LIST_DIR}/build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${BOTW_OUTPUT_ROOT}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${BOTW_OUTPUT_ROOT}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${BOTW_OUTPUT_ROOT}/lib")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${BOTW_OUTPUT_ROOT}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${BOTW_OUTPUT_ROOT}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${BOTW_OUTPUT_ROOT}/lib")
endforeach()

set(DOLRECOMP_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/DolRecomp" CACHE PATH
    "DolRecomp source directory")
set(BOTW_BASE_TITLE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/game/base"
    CACHE PATH "Extracted BOTW base title directory")
set(BOTW_UPDATE_TITLE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/game/update"
    CACHE PATH "Extracted BOTW update title directory")
set(BOTW_DLC_TITLE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/game/dlc"
    CACHE PATH "Extracted BOTW DLC title directory")
set(GENERATED_DIR "${CMAKE_CURRENT_LIST_DIR}/recomp/generated" CACHE PATH
    "DolRecomp-generated BOTW C output")

if(NOT EXISTS "${GENERATED_DIR}/generated.h")
    message(FATAL_ERROR
        "Missing generated BOTW output. Run .\\generate.ps1 before building.")
endif()
if(NOT EXISTS "${BOTW_UPDATE_TITLE_DIR}/code/U-King.rpx")
    message(FATAL_ERROR
        "Missing updated U-King.rpx under BOTW_UPDATE_TITLE_DIR.")
endif()

set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(zlib
    URL https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz
    URL_HASH SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
)
FetchContent_MakeAvailable(zlib)

add_library(dolrecomp_cpu STATIC
    "${DOLRECOMP_DIR}/src/cpu/cpu.c"
)
target_include_directories(dolrecomp_cpu PUBLIC "${DOLRECOMP_DIR}/src")

add_library(dolrecomp_rpx STATIC
    src/rpx_runtime.c
)
target_compile_definitions(dolrecomp_rpx PUBLIC DOLRECOMP_HAVE_ZLIB)
target_include_directories(dolrecomp_rpx PUBLIC "${DOLRECOMP_DIR}/src")
target_link_libraries(dolrecomp_rpx PUBLIC ZLIB::ZLIBSTATIC)

file(GLOB BOTW_GENERATED_CHUNKS CONFIGURE_DEPENDS
    "${GENERATED_DIR}/chunks/*.c"
)
add_library(botw_generated SHARED
    "${GENERATED_DIR}/generated.c"
    ${BOTW_GENERATED_CHUNKS}
)

# Keep the generated recomp payload separate from the small host/launcher binary.
# The generated declarations predate the DLL split, so let CMake produce the
# export table used by the launcher import library on Windows.
set_target_properties(botw_generated PROPERTIES
    OUTPUT_NAME "botw_runtime"
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)
target_include_directories(botw_generated PUBLIC
    "${GENERATED_DIR}"
    "${DOLRECOMP_DIR}/src"
)
target_link_libraries(botw_generated PUBLIC dolrecomp_cpu)
if(MSVC)
    target_compile_options(botw_generated PRIVATE /MP8 /bigobj)
endif()

add_library(botw_audio SHARED
    src/wiiu_audio.c
    src/wiiu_audio_buffer.c
)
set_target_properties(botw_audio PROPERTIES
    OUTPUT_NAME "botw_audio"
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)
target_include_directories(botw_audio PUBLIC
    src
    "${DOLRECOMP_DIR}/src"
)
if(WIN32)
    target_link_libraries(botw_audio PRIVATE winmm)
endif()

add_library(botw_graphics SHARED
    src/wiiu_latte_pixel.c
    src/wiiu_gx2.c
    src/wiiu_gx2_cemu_layout.c
    src/wiiu_gx2_software.c
    src/wiiu_latte_fetch.c
    src/wiiu_latte_vertex.c
    src/wiiu_latte_execute.c
    src/wiiu_memory.c
    src/wiiu_window.c
)
set_target_properties(botw_graphics PROPERTIES
    OUTPUT_NAME "botw_graphics"
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)
target_include_directories(botw_graphics PUBLIC
    src
    "${DOLRECOMP_DIR}/src"
)
target_link_libraries(botw_graphics PRIVATE dolrecomp_cpu)
if(WIN32)
    target_link_libraries(botw_graphics PRIVATE
        user32 gdi32 d3d11 dxgi d3dcompiler dxguid uuid
    )
endif()

add_library(botw_system SHARED
    src/wiiu_cpu.c
    src/wiiu_filesystem.c
    src/wiiu_guest_string.c
    src/wiiu_imports.c
)
set_target_properties(botw_system PROPERTIES
    OUTPUT_NAME "botw_system"
    WINDOWS_EXPORT_ALL_SYMBOLS TRUE
)
target_include_directories(botw_system PUBLIC
    src
    "${GENERATED_DIR}"
    "${DOLRECOMP_DIR}/src"
)
target_link_libraries(botw_system PUBLIC
    botw_audio
    botw_graphics
    botw_generated
    dolrecomp_cpu
    dolrecomp_rpx
)

add_executable(botw_recomp
    src/main.c
    src/rpx_runtime.c
)
set_target_properties(botw_recomp PROPERTIES
    OUTPUT_NAME "botw_recomp"
)
target_compile_definitions(botw_recomp PRIVATE
    BOTW_BASE_TITLE_DIR="${BOTW_BASE_TITLE_DIR}"
    BOTW_UPDATE_TITLE_DIR="${BOTW_UPDATE_TITLE_DIR}"
    BOTW_DLC_TITLE_DIR="${BOTW_DLC_TITLE_DIR}"
)
target_include_directories(botw_recomp PRIVATE
    src
    "${GENERATED_DIR}"
    "${DOLRECOMP_DIR}/src"
)
target_link_libraries(botw_recomp PRIVATE
    botw_generated
    botw_system
    dolrecomp_cpu
    dolrecomp_rpx
)

add_executable(botw_rpx_peek tools/rpx_peek.c)
target_include_directories(botw_rpx_peek PRIVATE src "${DOLRECOMP_DIR}/src")
target_link_libraries(botw_rpx_peek PRIVATE dolrecomp_rpx)

# The launch directory contains only the game launcher and its four DLLs.
# Keep diagnostic executables outside it, including multi-config builds.
function(botw_development_tool target)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${BOTW_OUTPUT_ROOT}/tools")
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER "${config}" config_upper)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${BOTW_OUTPUT_ROOT}/tools")
    endforeach()
endfunction()
botw_development_tool(botw_rpx_peek)
if(WIN32)
    add_executable(botw_guest_sample tools/guest_sample.c)
    target_include_directories(botw_guest_sample PRIVATE src "${DOLRECOMP_DIR}/src")
    botw_development_tool(botw_guest_sample)
endif()

install(TARGETS botw_recomp botw_generated botw_system botw_graphics botw_audio
    RUNTIME DESTINATION . LIBRARY DESTINATION .)

if(BUILD_TESTING)
    add_executable(botw_vertex_texture_test tools/vertex_texture_test.c)
    target_link_libraries(botw_vertex_texture_test PRIVATE botw_graphics dolrecomp_cpu)
    botw_development_tool(botw_vertex_texture_test)
    add_test(NAME botw_vertex_texture COMMAND botw_vertex_texture_test)
    set_tests_properties(botw_vertex_texture PROPERTIES ENVIRONMENT "BOTW_HEADLESS=1;BOTW_MUTE=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    add_executable(botw_latte_pixel_test tools/latte_pixel_test.c)
    target_link_libraries(botw_latte_pixel_test PRIVATE botw_graphics)
    botw_development_tool(botw_latte_pixel_test)
    add_test(NAME botw_latte_pixel COMMAND botw_latte_pixel_test)
    set_tests_properties(botw_latte_pixel PROPERTIES ENVIRONMENT "BOTW_HEADLESS=1;BOTW_MUTE=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    add_executable(botw_cpu_fallback_test tools/cpu_fallback_test.c)
    target_link_libraries(botw_cpu_fallback_test PRIVATE botw_system)
    add_test(NAME botw_cpu_fallback COMMAND botw_cpu_fallback_test)
    botw_development_tool(botw_cpu_fallback_test)
    set_tests_properties(botw_cpu_fallback PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    add_executable(botw_audio_buffer_test tools/audio_buffer_test.c src/wiiu_audio_buffer.c)
    target_include_directories(botw_audio_buffer_test PRIVATE src)
    add_test(NAME botw_audio_buffer COMMAND botw_audio_buffer_test)
    botw_development_tool(botw_audio_buffer_test)
    add_executable(botw_guest_string_test tools/guest_string_test.c)
    target_link_libraries(botw_guest_string_test PRIVATE botw_system)
    add_test(NAME botw_guest_string COMMAND botw_guest_string_test)
    botw_development_tool(botw_guest_string_test)
    set_tests_properties(botw_guest_string PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    add_executable(botw_latte_execute_test tools/latte_execute_test.c)
    target_link_libraries(botw_latte_execute_test PRIVATE botw_graphics)
    add_test(NAME botw_latte_execute COMMAND botw_latte_execute_test)
    botw_development_tool(botw_latte_execute_test)
    add_executable(botw_postprocess_test tools/postprocess_test.c)
    target_link_libraries(botw_postprocess_test PRIVATE botw_graphics)
    add_test(NAME botw_postprocess COMMAND botw_postprocess_test)
    botw_development_tool(botw_postprocess_test)
    add_executable(botw_postprocess_capture_test tools/postprocess_capture_test.c)
    target_link_libraries(botw_postprocess_capture_test PRIVATE botw_graphics dolrecomp_cpu)
    botw_development_tool(botw_postprocess_capture_test)
    add_executable(botw_latte_effect_capture_test tools/latte_effect_capture_test.c)
    target_link_libraries(botw_latte_effect_capture_test PRIVATE botw_graphics dolrecomp_cpu)
    botw_development_tool(botw_latte_effect_capture_test)
    set_tests_properties(botw_latte_execute botw_postprocess PROPERTIES
        ENVIRONMENT "BOTW_HEADLESS=1;BOTW_MUTE=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    add_executable(botw_audio_mix_test tools/audio_mix_test.c)
    target_link_libraries(botw_audio_mix_test PRIVATE botw_system)
    add_test(NAME botw_audio_mix COMMAND botw_audio_mix_test)
    set_tests_properties(botw_audio_mix PROPERTIES ENVIRONMENT "BOTW_MUTE=1"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
    botw_development_tool(botw_audio_mix_test)
    add_executable(botw_animation_tick_test tools/animation_tick_test.c)
    target_link_libraries(botw_animation_tick_test PRIVATE botw_graphics botw_generated)
    add_test(NAME botw_animation_tick COMMAND botw_animation_tick_test)
    add_executable(botw_audio_muted_test tools/audio_muted_test.c)
    target_link_libraries(botw_audio_muted_test PRIVATE botw_audio)
    add_test(NAME botw_audio_muted COMMAND botw_audio_muted_test)
    set_tests_properties(botw_audio_muted PROPERTIES ENVIRONMENT "BOTW_MUTE=1"
        PASS_REGULAR_EXPRESSION "host output muted"
        FAIL_REGULAR_EXPRESSION "native Windows output opened")
    add_executable(botw_present_without_clear_test tools/present_without_clear_test.c)
    target_link_libraries(botw_present_without_clear_test PRIVATE botw_graphics dolrecomp_cpu)
    add_test(NAME botw_present_without_clear COMMAND botw_present_without_clear_test)
    set_tests_properties(botw_present_without_clear PROPERTIES ENVIRONMENT "BOTW_HEADLESS=1")
    add_executable(botw_job_lock_test tools/job_lock_test.c)
    target_link_libraries(botw_job_lock_test PRIVATE botw_graphics botw_generated)
    add_test(NAME botw_job_lock COMMAND botw_job_lock_test)
    add_executable(botw_memory_layout_test tools/memory_layout_test.c)
    target_link_libraries(botw_memory_layout_test PRIVATE botw_graphics)
    add_test(NAME botw_memory_layout COMMAND botw_memory_layout_test)
    add_executable(botw_filesystem_read_test tools/filesystem_read_test.c)
    target_link_libraries(botw_filesystem_read_test PRIVATE botw_system)
    add_test(NAME botw_large_asset_read COMMAND botw_filesystem_read_test
        "${BOTW_UPDATE_TITLE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/filesystem_fixture")
    add_executable(botw_rendezvous_test tools/rendezvous_test.c)
    target_link_libraries(botw_rendezvous_test PRIVATE botw_system)
    add_test(NAME botw_rendezvous COMMAND botw_rendezvous_test)
    add_executable(botw_import_lookup_test tools/import_lookup_test.c)
    target_link_libraries(botw_import_lookup_test PRIVATE botw_system)
    add_test(NAME botw_import_lookup COMMAND botw_import_lookup_test)
    add_executable(botw_cemu_layout_test
        tools/cemu_layout_test.c
        src/wiiu_gx2_cemu_layout.c
    )
    target_include_directories(botw_cemu_layout_test PRIVATE
        src "${DOLRECOMP_DIR}/src")
add_test(NAME botw_cemu_layout COMMAND botw_cemu_layout_test)
add_test(
    NAME botw_rpx_load
    COMMAND botw_rpx_peek
            "${BOTW_UPDATE_TITLE_DIR}/code/U-King.rpx"
            0x02000020)
add_test(NAME botw_cli_help COMMAND botw_recomp --help)
add_test(NAME botw_boot_10000 COMMAND botw_recomp --blocks 10000)
    foreach(target botw_present_without_clear_test botw_job_lock_test botw_memory_layout_test botw_filesystem_read_test
                   botw_rendezvous_test botw_import_lookup_test botw_cemu_layout_test botw_audio_muted_test botw_animation_tick_test)
        botw_development_tool(${target})
    endforeach()
    set_tests_properties(botw_present_without_clear botw_job_lock botw_memory_layout botw_large_asset_read botw_rendezvous
        botw_import_lookup botw_cemu_layout botw_rpx_load botw_audio_muted botw_animation_tick PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:botw_recomp>")
endif()
