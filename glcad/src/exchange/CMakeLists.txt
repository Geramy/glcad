set(_dgn_root "${CMAKE_SOURCE_DIR}/third_party/dgnlib")
add_library(gdgn STATIC dgn.cpp
  ${_dgn_root}/upstream/dgnopen.cpp ${_dgn_root}/upstream/dgnread.cpp
  ${_dgn_root}/upstream/dgnhelp.cpp ${_dgn_root}/upstream/dgnstroke.cpp
  ${_dgn_root}/upstream/cpl_vax.cpp ${_dgn_root}/port/port.cpp)
target_include_directories(gdgn PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include
  PRIVATE ${_dgn_root}/upstream ${_dgn_root}/port)
target_compile_features(gdgn PUBLIC cxx_std_20)

# The upstream writer is test support only; applications link the reader.
add_library(gdgn_test_support STATIC tests/dgn_fixture.cpp ${_dgn_root}/upstream/dgnwrite.cpp)
target_include_directories(gdgn_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests
  PRIVATE ${_dgn_root}/upstream ${_dgn_root}/port)
target_link_libraries(gdgn_test_support PUBLIC gdgn)
target_compile_definitions(gdgn_test_support PRIVATE GLCAD_DGN_SEED="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/dgn/seed_2d.dgn")
add_executable(gdgn_tests ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp tests/test_dgn.cpp)
target_include_directories(gdgn_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests
  ${_dgn_root}/upstream ${_dgn_root}/port)
target_link_libraries(gdgn_tests PRIVATE gdgn_test_support gplot)
target_compile_definitions(gdgn_tests PRIVATE GLCAD_DGN_SEED="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/dgn/seed_2d.dgn")
glcad_add_tiered_test(gdgn_tests)

find_package(ZLIB REQUIRED)
add_library(gplot STATIC page.cpp pdf.cpp dwf.cpp plot_style.cpp underlay_image.cpp underlay_dgn.cpp)
set(_dwf_font "${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dwf_font}")
file(READ "${_dwf_font}" _dwf_font_bytes HEX)
string(REGEX REPLACE "(..)" "0x\\1," _dwf_font_bytes "${_dwf_font_bytes}")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/dwf_default_font.inc"
     CONTENT "constexpr unsigned char kDwfDefaultFont[] = {@_dwf_font_bytes@};\n" @ONLY)
unset(_dwf_font_bytes)
target_include_directories(gplot PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(gplot PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gplot PUBLIC gdoc gsnap gio gdgn ZLIB::ZLIB)
target_compile_features(gplot PUBLIC cxx_std_20)
if(APPLE)
  set(_portable_underlay_default OFF)
else()
  set(_portable_underlay_default ON)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_xps_underlay_default OFF)
else()
  set(_xps_underlay_default ON)
endif()
option(GLCAD_XPS_UNDERLAYS "Decode DWFx/XPS pages using libgxps" ${_xps_underlay_default})
if(GLCAD_XPS_UNDERLAYS)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(UNDERLAY_XPS REQUIRED IMPORTED_TARGET libgxps)
  target_sources(gplot PRIVATE underlay_xps.cpp)
  target_compile_definitions(gplot PUBLIC GLCAD_XPS_UNDERLAYS=1)
  target_link_libraries(gplot PUBLIC PkgConfig::UNDERLAY_XPS)
endif()
option(GLCAD_PORTABLE_UNDERLAYS "Decode PDF/PNG/JPEG/TIFF with portable libraries" ${_portable_underlay_default})
if(GLCAD_PORTABLE_UNDERLAYS)
  find_package(PNG REQUIRED)
  find_package(JPEG REQUIRED)
  find_package(TIFF REQUIRED)
  # MANDATORY: Poppler is GPL and cannot ship under the GLCAD license. Replace it
  # with PDFium before releasing any Windows or Linux binary.
  find_package(unofficial-poppler CONFIG QUIET)
  if(TARGET unofficial::poppler::poppler-cpp)
    set(_underlay_poppler unofficial::poppler::poppler-cpp)
    find_path(UNDERLAY_POPPLER_INCLUDE_DIR poppler-document.h PATH_SUFFIXES poppler/cpp REQUIRED)
    target_include_directories(gplot PRIVATE ${UNDERLAY_POPPLER_INCLUDE_DIR})
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(UNDERLAY_POPPLER REQUIRED IMPORTED_TARGET poppler-cpp)
    set(_underlay_poppler PkgConfig::UNDERLAY_POPPLER)
  endif()
  target_sources(gplot PRIVATE underlay_portable.cpp)
  target_compile_definitions(gplot PUBLIC GLCAD_PORTABLE_UNDERLAYS=1)
  target_link_libraries(gplot PUBLIC PNG::PNG JPEG::JPEG TIFF::TIFF ${_underlay_poppler})
endif()
if(APPLE)
  target_link_libraries(gplot PUBLIC "-framework CoreGraphics" "-framework ImageIO" "-framework CoreFoundation")
endif()

add_executable(gplot_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_plot.cpp
  tests/test_plot_style.cpp
  tests/test_dwf.cpp
  tests/test_underlay.cpp)
target_link_libraries(gplot_tests PRIVATE gplot)
target_include_directories(gplot_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gplot_tests)

add_library(gdraft STATIC views.cpp)
target_include_directories(gdraft PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gdraft PUBLIC gcm ggeo gsnap gtopo gview)
target_compile_features(gdraft PUBLIC cxx_std_20)

add_executable(gdraft_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_views.cpp)
target_link_libraries(gdraft_tests PRIVATE gdraft)
target_include_directories(gdraft_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gdraft_tests)

add_library(gstep STATIC step.cpp brep.cpp)
target_include_directories(gstep PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gstep PUBLIC gcm gtopo)
target_compile_features(gstep PUBLIC cxx_std_20)

add_executable(gstep_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_step.cpp)
target_link_libraries(gstep_tests PRIVATE gstep)
target_include_directories(gstep_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gstep_tests)

add_library(gmesh STATIC mesh_export.cpp)
target_include_directories(gmesh PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gmesh PUBLIC gcm gtopo gplot)
target_compile_features(gmesh PUBLIC cxx_std_20)

add_executable(gmesh_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_mesh_export.cpp)
target_link_libraries(gmesh_tests PRIVATE gmesh)
target_include_directories(gmesh_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gmesh_tests)

add_library(gdxf STATIC dxf.cpp entities.cpp region_export.cpp)
target_include_directories(gdxf PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gdxf PUBLIC gcm gdoc gsnap gdim ggeo gtopo)
target_compile_features(gdxf PUBLIC cxx_std_20)

add_executable(gdxf_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_dxf.cpp)
target_link_libraries(gdxf_tests PRIVATE gdxf)
target_include_directories(gdxf_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gdxf_tests)

add_library(gdwg STATIC bitcode.cpp dwg.cpp dwg_r2004.cpp dwg_r2007.cpp dwg_write.cpp)
target_include_directories(gdwg PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gdwg PUBLIC gcm gdoc gsnap gdim gdxf)
target_compile_features(gdwg PUBLIC cxx_std_20)

add_executable(gdwg_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_bitcode.cpp
  tests/test_dwg.cpp
  tests/test_dwg_write.cpp
  tests/test_region_export.cpp)
target_link_libraries(gdwg_tests PRIVATE gdwg)
target_include_directories(gdwg_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
target_compile_definitions(gdwg_tests PRIVATE
  GLCAD_DWG_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/r2000.dwg"
  GLCAD_DWG_LAYERS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/r2000_layers.dwg"
  GLCAD_DWG_DIMS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/r2000_dims.dwg")
glcad_add_tiered_test(gdwg_tests)

add_library(gxt STATIC xt.cpp)
target_include_directories(gxt PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gxt PUBLIC gcm ggeo gtopo)
target_compile_features(gxt PUBLIC cxx_std_20)

add_executable(gxt_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_xt.cpp)
target_link_libraries(gxt_tests PRIVATE gxt)
target_include_directories(gxt_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
target_compile_definitions(gxt_tests PRIVATE
  GLCAD_XT_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/data/brick.x_t")
glcad_add_tiered_test(gxt_tests)

glcad_xcode_source_groups()

# An independent reader is a development check, never a shipped dependency.
find_program(GLCAD_DWGREAD dwgread)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(GLCAD_DWGREAD AND Python3_EXECUTABLE)
  add_executable(gdwg_fixture tests/dwg_fixture.cpp)
  target_link_libraries(gdwg_fixture PRIVATE gdwg)
  add_test(NAME gdwg_independent_reader
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_dwg_reader.py
            $<TARGET_FILE:gdwg_fixture> ${GLCAD_DWGREAD})
  set_tests_properties(gdwg_independent_reader PROPERTIES LABELS fast)
endif()
