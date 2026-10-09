add_library(gcm INTERFACE)
target_include_directories(gcm INTERFACE ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gcm INTERFACE gcm_fp_flags)
target_compile_features(gcm INTERFACE cxx_std_20)

if(NOT GLCAD_SLANGC)
  find_program(GLCAD_SLANGC slangc
    HINTS ENV SLANG_ROOT
    PATH_SUFFIXES bin
    DOC "The Slang compiler, for the GPU predicate mirror")
endif()

if(GLCAD_SLANGC)
  set(SLANG_DIR ${CMAKE_CURRENT_BINARY_DIR}/slang)
  file(MAKE_DIRECTORY ${SLANG_DIR})

  add_custom_command(
    OUTPUT ${SLANG_DIR}/slang_generated.cpp
    COMMAND ${GLCAD_SLANGC} -target cpp
            -o ${SLANG_DIR}/slang_generated.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/slang/predicates.slang
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/slang/predicates.slang
    COMMENT "slangc: the predicate mirror to C++")

  add_custom_command(
    OUTPUT ${SLANG_DIR}/predicates.spv
    COMMAND ${GLCAD_SLANGC} -target spirv
            -o ${SLANG_DIR}/predicates.spv
            ${CMAKE_CURRENT_SOURCE_DIR}/slang/predicates.slang
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/slang/predicates.slang
    COMMENT "slangc: the predicate mirror to SPIR-V")

  add_custom_target(gcm_slang DEPENDS
    ${SLANG_DIR}/slang_generated.cpp ${SLANG_DIR}/predicates.spv)

  # The prelude the generated code includes lives beside slangc.
  get_filename_component(GLCAD_SLANG_BIN ${GLCAD_SLANGC} DIRECTORY)
  get_filename_component(GLCAD_SLANG_ROOT ${GLCAD_SLANG_BIN} DIRECTORY)
  message(STATUS "gcm: Slang mirror enabled (${GLCAD_SLANGC})")
else()
  message(STATUS "gcm: no slangc; the GPU predicate mirror will not be checked")
endif()

add_executable(gcm_tests
  tests/main.cpp
  tests/test_eft.cpp
  tests/test_scalar.cpp
  tests/test_bigint.cpp
  tests/test_fixed.cpp
  tests/test_predicate.cpp
  tests/test_soft_f64.cpp
  tests/test_interval.cpp
  tests/test_slang.cpp
  tests/test_thread.cpp
)
target_link_libraries(gcm_tests PRIVATE gcm)
target_include_directories(gcm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)

if(GLCAD_SLANGC)
  target_sources(gcm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/slang/slang_shim.cpp)
  add_dependencies(gcm_tests gcm_slang)
  target_include_directories(gcm_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/slang ${SLANG_DIR} ${GLCAD_SLANG_ROOT}/include)
  target_compile_definitions(gcm_tests PRIVATE GLCAD_HAVE_SLANG)
  # OBJECT_DEPENDS: dependency scanning misses the generated file.
  set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/slang/slang_shim.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-unused-variable;-Wno-unused-parameter"
               OBJECT_DEPENDS ${SLANG_DIR}/slang_generated.cpp)
endif()
glcad_add_tiered_test(gcm_tests)

add_executable(gcm_demo demo/gcm_demo.cpp)
target_link_libraries(gcm_demo PRIVATE gcm)

add_executable(gcm_accel_bench demo/gcm_accel_bench.cpp)
target_link_libraries(gcm_accel_bench PRIVATE gcm)
if(APPLE)
  target_link_libraries(gcm_accel_bench PRIVATE "-framework Accelerate")
endif()

add_executable(gcm_precision_bench demo/gcm_precision_bench.cpp)
target_link_libraries(gcm_precision_bench PRIVATE gcm)

glcad_xcode_source_groups()
