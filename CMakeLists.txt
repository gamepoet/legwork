cmake_minimum_required(VERSION 3.8)
project(legwork)

set(
  SRCS
  src/legwork.h
  src/legwork.cpp
)

# TODO: max out the warning settings for the compilers (why isn't there a generic way to do this?)
#if(MSVC)
#  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
#endif()

add_library(legwork STATIC ${SRCS})
target_compile_features(legwork PRIVATE cxx_std_11 cxx_thread_local)
target_compile_features(legwork PUBLIC cxx_variadic_macros)
target_include_directories(
  legwork
  PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:>
)

set(
  TEST_SRCS
  spec/lest.hpp
  spec/legwork_spec.cpp
)

add_executable(test_runner ${TEST_SRCS})
target_compile_features(test_runner PRIVATE cxx_std_11)
target_link_libraries(test_runner legwork)

enable_testing()
add_test(NAME spec COMMAND test_runner)
