cmake_minimum_required(VERSION 3.20)
project(ModernGekkoBuildCheck LANGUAGES C CXX)

find_package(Git REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(NINJA_EXECUTABLE ninja REQUIRED)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR
    "A 32-bit compiler was selected (${CMAKE_C_COMPILER}). "
    "ModernGekko requires a 64-bit compiler and 8-byte pointers.")
elseif(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR
    "Unsupported pointer size: ${CMAKE_SIZEOF_VOID_P} bytes. "
    "ModernGekko requires 8-byte pointers.")
endif()

message(STATUS
  "C compiler: ${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
message(STATUS
  "C++ compiler: ${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION})")
message(STATUS "Pointer size: ${CMAKE_SIZEOF_VOID_P} bytes")
message(STATUS "CMake: ${CMAKE_VERSION}")
message(STATUS "Ninja: ${NINJA_EXECUTABLE}")
message(STATUS "Git: ${GIT_EXECUTABLE} (${GIT_VERSION_STRING})")
message(STATUS "Python: ${Python3_EXECUTABLE} (${Python3_VERSION})")

option(MODERNGEKKO_CHECK_LLVM "Check the optional LLVM backend dependency" OFF)
if(MODERNGEKKO_CHECK_LLVM)
  find_package(LLVM CONFIG REQUIRED)
  if(LLVM_PACKAGE_VERSION VERSION_LESS 19 OR
     LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 21)
    message(FATAL_ERROR
      "DolRecomp requires LLVM 19 or 20 (found ${LLVM_PACKAGE_VERSION})")
  endif()
  message(STATUS "LLVM: ${LLVM_PACKAGE_VERSION} (${LLVM_DIR})")
endif()

add_executable(moderngekko_compiler_check_c compiler_check.c)
set_target_properties(moderngekko_compiler_check_c PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

add_executable(moderngekko_compiler_check_cxx compiler_check.cpp)
set_target_properties(moderngekko_compiler_check_cxx PROPERTIES
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)
