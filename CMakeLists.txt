cmake_minimum_required(VERSION 3.8)
project(legwork)

set(
  SRCS
  src/legwork.h
  src/legwork.cpp
)

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

set(
  SRCS
  ${SRCS}
  src/fcontext.h
  vendor/boost_context/src/asm/make_${FCONTEXT_PLATFORM}_${FCONTEXT_OS}_${FCONTEXT_ABI}_${FCONTEXT_ASM}
  vendor/boost_context/src/asm/jump_${FCONTEXT_PLATFORM}_${FCONTEXT_OS}_${FCONTEXT_ABI}_${FCONTEXT_ASM}
)

# max out the warning settings for the compilers (why isn't there a generic way to do this?)
if(MSVC)
  string(REPLACE "/W3" "" CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
  string(REPLACE "/W3" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
endif()

add_library(legwork STATIC ${SRCS})
target_compile_features(legwork PRIVATE cxx_std_11 cxx_thread_local)
target_compile_definitions(legwork PRIVATE BOOST_CONTEXT_EXPORT=) # needed to compile x64 ASM files (don't export the symbol)
target_compile_features(legwork PUBLIC cxx_variadic_macros)
target_include_directories(
  legwork
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:>
)
if(MSVC)
  target_compile_options(legwork PRIVATE /W4 /WX /wd4100)
elseif(APPLE)
  target_compile_options(legwork PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
endif()

set(
  TEST_SRCS
  spec/catch.hpp
  spec/main.cpp
  spec/legwork_spec.cpp
)

add_executable(test_runner ${TEST_SRCS})
target_compile_features(test_runner PRIVATE cxx_std_11)
target_link_libraries(test_runner legwork)
if(MSVC)
  target_compile_options(test_runner PRIVATE /W4 /WX /wd4100)
elseif(APPLE)
  target_compile_options(test_runner PRIVATE -Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter)
endif()

enable_testing()
add_test(NAME spec COMMAND test_runner)
