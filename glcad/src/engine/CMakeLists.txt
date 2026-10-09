add_library(gio STATIC
  stream.cpp
  frame.cpp
  transport.cpp
  tcp.cpp
  session.cpp
  tls.cpp)
target_include_directories(gio PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
find_package(Threads REQUIRED)
target_link_libraries(gio PUBLIC gcm Threads::Threads)
if(WIN32)
  target_link_libraries(gio PUBLIC ws2_32)
endif()

find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
  target_compile_definitions(gio PRIVATE GLCAD_HAVE_OPENSSL)
  target_link_libraries(gio PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  message(STATUS "gio: TLS enabled (${OPENSSL_VERSION})")
else()
  message(STATUS "gio: no TLS library found; tls:// will report that it is unavailable")
endif()
target_compile_features(gio PUBLIC cxx_std_20)

add_executable(gio_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_io.cpp
  tests/test_tcp.cpp
  tests/test_session.cpp
  tests/test_tls.cpp)
target_link_libraries(gio_tests PRIVATE gio)
target_include_directories(gio_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gio_tests)
# Loopback TLS handshakes flake when run in parallel; run them serially.
set_tests_properties(gio_tests_fast gio_tests_full gio_tests_soak
                     PROPERTIES RESOURCE_LOCK gio_loopback)

add_library(gunit STATIC units.cpp parameter.cpp)
target_include_directories(gunit PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gunit PUBLIC gcm)
target_compile_features(gunit PUBLIC cxx_std_20)

add_executable(gunit_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_units.cpp
  tests/test_parameter.cpp)
target_link_libraries(gunit_tests PRIVATE gunit)
target_include_directories(gunit_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gunit_tests)

add_library(gcmd STATIC
  command.cpp
  standard.cpp
  sysvar.cpp
  commandline.cpp coordinate.cpp script.cpp lisp.cpp)
target_include_directories(gcmd PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gcmd PUBLIC gcm gunit)
target_compile_features(gcmd PUBLIC cxx_std_20)

add_executable(gcmd_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_command.cpp
  tests/test_coordinate.cpp
  tests/test_script.cpp
  tests/test_lisp.cpp)
target_link_libraries(gcmd_tests PRIVATE gcmd)
target_include_directories(gcmd_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gcmd_tests)

add_library(gdoc STATIC layer.cpp linetype.cpp hatch.cpp entity.cpp block.cpp underlay.cpp)
target_include_directories(gdoc PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gdoc PUBLIC gcm ggeo gsnap)
target_compile_features(gdoc PUBLIC cxx_std_20)

add_executable(gdoc_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_layer.cpp
  tests/test_hatch.cpp
  tests/test_entity.cpp
  tests/test_block.cpp
  tests/test_hatch_assoc.cpp
  tests/test_dynamic.cpp)
target_link_libraries(gdoc_tests PRIVATE gdoc)
target_include_directories(gdoc_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gdoc_tests)

find_program(FLATC_EXECUTABLE flatc)
find_path(FLATBUFFERS_INCLUDE_DIR flatbuffers/flatbuffers.h)

if(NOT FLATC_EXECUTABLE OR NOT FLATBUFFERS_INCLUDE_DIR)
  message(FATAL_ERROR
    "gfile needs the FlatBuffers compiler and headers.\n"
    "  macOS:  brew install flatbuffers\n"
    "  Linux:  apt install flatbuffers-compiler libflatbuffers-dev")
endif()

set(GLCAD_FBS ${CMAKE_CURRENT_SOURCE_DIR}/schema/glcad.fbs)
set(GLCAD_FB_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/glcad_generated.h)

add_custom_command(
  OUTPUT ${GLCAD_FB_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND ${FLATC_EXECUTABLE} --cpp --scoped-enums
          -o ${CMAKE_CURRENT_BINARY_DIR}/generated ${GLCAD_FBS}
  DEPENDS ${GLCAD_FBS}
  COMMENT "flatc: glcad.fbs")
set(GLCAD_CFG_FBS ${CMAKE_CURRENT_SOURCE_DIR}/schema/config.fbs)
set(GLCAD_CFG_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/config_generated.h)
add_custom_command(
  OUTPUT ${GLCAD_CFG_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/generated
  COMMAND ${FLATC_EXECUTABLE} --cpp --scoped-enums
          -o ${CMAKE_CURRENT_BINARY_DIR}/generated ${GLCAD_CFG_FBS}
  DEPENDS ${GLCAD_CFG_FBS}
  COMMENT "flatc: config.fbs")
add_custom_target(gfile_schema DEPENDS ${GLCAD_FB_HEADER} ${GLCAD_CFG_HEADER})

add_library(gfile STATIC document_file.cpp incremental.cpp appconfig.cpp
  ${GLCAD_FB_HEADER} ${GLCAD_CFG_HEADER})
add_dependencies(gfile gfile_schema)
target_include_directories(gfile
  PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include
  PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated ${FLATBUFFERS_INCLUDE_DIR})
target_link_libraries(gfile PUBLIC gdoc gsnap gio)
target_compile_features(gfile PUBLIC cxx_std_20)

add_executable(gfile_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_file.cpp)
target_link_libraries(gfile_tests PRIVATE gfile)
target_include_directories(gfile_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gfile_tests)

add_library(gci INTERFACE)
target_include_directories(gci INTERFACE ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gci INTERFACE gcm)
target_compile_features(gci INTERFACE cxx_std_20)

add_executable(gci_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_swap.cpp)
target_link_libraries(gci_tests PRIVATE gci)
target_include_directories(gci_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gci_tests)

add_library(gcs INTERFACE)
target_include_directories(gcs INTERFACE ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gcs INTERFACE gcm)
target_compile_features(gcs INTERFACE cxx_std_20)

add_executable(gcs_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_scheduler.cpp
  tests/test_tile.cpp)
target_link_libraries(gcs_tests PRIVATE gcs)
target_include_directories(gcs_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gcs_tests)

glcad_xcode_source_groups()
