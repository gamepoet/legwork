cmake_minimum_required(VERSION 3.8)
project(legwork LANGUAGES C CXX)

option(LEGWORK_BUILD_TESTS "Build tests" OFF)

# max out the warning settings for the compilers (why isn't there a generic way to do this?)
if (MSVC)
  string(REPLACE "/W3" "" CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
  string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
endif()

if(WIN32)
  enable_language(ASM_MASM)
else()
  enable_language(ASM)
endif()

if(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
  set(FCONTEXT_PLATFORM "x86_64")
endif()
if(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
  set(FCONTEXT_PLATFORM "x86_64")
endif()
if(APPLE)
  set(FCONTEXT_OS "sysv")
  set(FCONTEXT_ABI "macho")
  set(FCONTEXT_ASM "gas.S")
endif()
if(WIN32)
  set(FCONTEXT_OS "ms")
  set(FCONTEXT_ABI "pe")
  set(FCONTEXT_ASM "masm.asm")
endif()

add_library(
  legwork
  STATIC
  src/fcontext.h
  src/legwork.cpp
  src/legwork.h
  vendor/boost_context/src/asm/jump_${FCONTEXT_PLATFORM}_${FCONTEXT_OS}_${FCONTEXT_ABI}_${FCONTEXT_ASM}
  vendor/boost_context/src/asm/make_${FCONTEXT_PLATFORM}_${FCONTEXT_OS}_${FCONTEXT_ABI}_${FCONTEXT_ASM}
)
target_compile_features(legwork PRIVATE cxx_std_11 cxx_thread_local)
target_compile_definitions(legwork PRIVATE BOOST_CONTEXT_EXPORT=) # needed to compile x64 ASM files (don't export the symbol)
target_compile_features(legwork PUBLIC cxx_variadic_macros)
target_include_directories(
  legwork
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:>
)
target_compile_options(
  legwork
  PRIVATE
  $<$<CXX_COMPILER_ID:AppleClang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
  $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100>
)

# test app
if (LEGWORK_BUILD_TESTS)
  add_executable(
    test_runner
    spec/catch.hpp
    spec/main.cpp
    spec/legwork_spec.cpp
  )
  target_compile_features(test_runner PRIVATE cxx_std_11)
  target_link_libraries(test_runner legwork)
  target_compile_options(
    test_runner
    PRIVATE
    $<$<CXX_COMPILER_ID:AppleClang>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100>
  )

  enable_testing()
  add_test(NAME spec COMMAND test_runner)
endif()
