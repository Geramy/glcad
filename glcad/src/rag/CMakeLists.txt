add_library(grag STATIC library.cpp)
target_include_directories(grag PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(grag PUBLIC gcm)
target_compile_features(grag PUBLIC cxx_std_20)

if(GLCAD_WITH_PGLITE)
  include(ExternalProject)
  set(GLCAD_PGLITE_SRC ${CMAKE_BINARY_DIR}/pglite/src/pglite_engine)
  set(GLCAD_PGLITE_OUT ${GLCAD_PGLITE_SRC}/native/out)
  # ui embeds the runtime payload in the iOS app bundle, and rag runs first.
  set(GLCAD_PGLITE_OUT ${GLCAD_PGLITE_OUT} PARENT_SCOPE)
  set(GLCAD_PGLITE_BUILD_COMMAND
      bash ${CMAKE_CURRENT_SOURCE_DIR}/pglite/build-engine.sh ${GLCAD_PGLITE_SRC})
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(_pglite_arch arm64)
    if(CMAKE_OSX_ARCHITECTURES)
      list(GET CMAKE_OSX_ARCHITECTURES 0 _pglite_arch)
    endif()
    if(_pglite_arch STREQUAL "arm64")
      set(_pglite_triple aarch64-apple-darwin)
    else()
      set(_pglite_triple ${_pglite_arch}-apple-darwin)
    endif()
    execute_process(COMMAND xcrun --sdk iphoneos -f clang
      OUTPUT_VARIABLE _pglite_cc OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND xcrun --sdk iphoneos --show-sdk-path
      OUTPUT_VARIABLE _pglite_sdk OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY)
    # -D__IOS_PROHIBITED=: keeps the system() shim rename from erroring.
    set(_pglite_cc
        "${_pglite_cc} -arch ${_pglite_arch} -isysroot ${_pglite_sdk} -D__IOS_PROHIBITED=")
    if(CMAKE_OSX_DEPLOYMENT_TARGET)
      string(APPEND _pglite_cc
             " -miphoneos-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
    endif()
    # PG_SYSROOT: else Postgres appends xcrun's macOS -isysroot after ours.
    set(GLCAD_PGLITE_BUILD_COMMAND
        ${CMAKE_COMMAND} -E env
        "GLCAD_PGLITE_HOST_TRIPLE=${_pglite_triple}"
        "GLCAD_PGLITE_CC=${_pglite_cc}"
        "PG_SYSROOT=${_pglite_sdk}"
        ${GLCAD_PGLITE_BUILD_COMMAND})
  elseif(CMAKE_CROSSCOMPILING)
    get_filename_component(_pglite_cc_name "${CMAKE_C_COMPILER}" NAME)
    string(REGEX REPLACE "-(gcc|cc|clang)(-[0-9.]+)?$" "" _pglite_triple
           "${_pglite_cc_name}")
    set(GLCAD_PGLITE_BUILD_COMMAND
        ${CMAKE_COMMAND} -E env
        "GLCAD_PGLITE_HOST_TRIPLE=${_pglite_triple}"
        ${GLCAD_PGLITE_BUILD_COMMAND})
  endif()
  ExternalProject_Add(pglite_engine
    GIT_REPOSITORY https://github.com/Midwess/pglite-rs.git
    GIT_TAG f1bf157a857fdbf623439da804139d9c6ea37361
    GIT_SUBMODULES ""
    PREFIX ${CMAKE_BINARY_DIR}/pglite
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE TRUE
    BUILD_COMMAND ${GLCAD_PGLITE_BUILD_COMMAND}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${GLCAD_PGLITE_OUT}/libpglite.a
    LOG_BUILD TRUE
    LOG_OUTPUT_ON_FAILURE TRUE)

  target_sources(grag PRIVATE pglite_store.cpp)
  # PUBLIC: library.hpp's #ifdef must agree in every consumer.
  target_compile_definitions(grag PUBLIC GLCAD_WITH_PGLITE)
  target_compile_definitions(grag PRIVATE
    GLCAD_PGLITE_RUNTIME_DIR="${GLCAD_PGLITE_OUT}/install")
  target_include_directories(grag PRIVATE ${GLCAD_PGLITE_SRC}/native)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    target_compile_definitions(grag PRIVATE GLCAD_PGLITE_STATIC_EXTS)
  endif()
  add_dependencies(grag pglite_engine)
  if(WIN32)
    # The mingw engine has no zlib; the win32 backend links ws2_32 and secur32.
    target_link_libraries(grag PUBLIC ${GLCAD_PGLITE_OUT}/libpglite.a ws2_32 secur32)
  else()
    target_link_libraries(grag PUBLIC ${GLCAD_PGLITE_OUT}/libpglite.a z)
  endif()
  # ELF needs --export-dynamic so extension modules resolve Postgres symbols.
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(grag PUBLIC -Wl,--export-dynamic)
  endif()
endif()

add_executable(grag_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_rag.cpp)
target_link_libraries(grag_tests PRIVATE grag)
target_include_directories(grag_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(grag_tests)
