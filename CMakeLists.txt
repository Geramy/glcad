cmake_minimum_required(VERSION 3.24)
project(glcad VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Strict IEEE-754, no contraction: gcm::eft breaks under fast-math.
add_library(gcm_fp_flags INTERFACE)
if(MSVC)
  target_compile_options(gcm_fp_flags INTERFACE $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:/fp:strict>)
else()
  target_compile_options(gcm_fp_flags INTERFACE
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffp-contract=off>
    $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-fno-fast-math>)
endif()

option(GLCAD_BUILD_VULKAN "Build the Vulkan RHI layer" ON)

option(GLCAD_WITH_PGLITE "Build the embedded-Postgres RAG backend" ON)

# Static OpenSSL, set before any find_package caches the answer.
set(OPENSSL_USE_STATIC_LIBS TRUE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
if(GLCAD_BUILD_VULKAN)
  include(Dependencies)
endif()

enable_testing()

# Test tiers: ctest -L fast (default), -L full, -L soak.
function(glcad_add_tiered_test target)
  add_test(NAME ${target}_fast COMMAND ${target})
  set_tests_properties(${target}_fast PROPERTIES
    LABELS "fast" ENVIRONMENT "GLCAD_TEST_SCALE=0.02")

  add_test(NAME ${target}_full COMMAND ${target})
  set_tests_properties(${target}_full PROPERTIES
    LABELS "full" ENVIRONMENT "GLCAD_TEST_SCALE=1")

  add_test(NAME ${target}_soak COMMAND ${target})
  set_tests_properties(${target}_soak PROPERTIES
    LABELS "soak" ENVIRONMENT "GLCAD_TEST_SCALE=20" TIMEOUT 3600)

  # The tiers share on-disk files: one lock per target stops ctest -j races.
  set_tests_properties(${target}_fast ${target}_full ${target}_soak
                       PROPERTIES RESOURCE_LOCK ${target}_files)
endfunction()
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
function(glcad_xcode_source_groups)
  get_property(_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
  get_filename_component(_group ${CMAKE_CURRENT_SOURCE_DIR} NAME)
  foreach(_t IN LISTS _targets)
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "INTERFACE_LIBRARY" OR _type STREQUAL "UTILITY")
      continue()
    endif()
    if(_t MATCHES "_tests$|_bench$")
      set_target_properties(${_t} PROPERTIES FOLDER "${_group}/tests")
    else()
      set_target_properties(${_t} PROPERTIES FOLDER "${_group}")
    endif()
    get_target_property(_srcs ${_t} SOURCES)
    if(NOT _srcs)
      continue()
    endif()
    set(_tree "")
    foreach(_f IN LISTS _srcs)
      get_filename_component(_abs ${_f} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
      string(FIND "${_abs}" "${CMAKE_SOURCE_DIR}/" _at)
      string(FIND "${_abs}" "${CMAKE_BINARY_DIR}/" _in_build)
      if(_at EQUAL 0 AND NOT _in_build EQUAL 0)
        list(APPEND _tree ${_abs})
      endif()
    endforeach()
    if(_tree)
      source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${_tree})
    endif()
  endforeach()
endfunction()

add_subdirectory(glcad/src/math)
add_subdirectory(glcad/src/engine)

add_subdirectory(glcad/src/render)
add_subdirectory(glcad/src/geometry)
add_subdirectory(glcad/src/plancheck)
add_subdirectory(glcad/src/rag)

add_subdirectory(glcad/src/exchange)
add_subdirectory(glcad/src/ai)

add_subdirectory(glcad/src/ui)

# Packaging last, after every install() rule exists.
include(Packaging)
