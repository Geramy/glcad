add_library(gsnap STATIC snap.cpp snap3d.cpp)
target_include_directories(gsnap PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gsnap PUBLIC gcm gtopo)
target_compile_features(gsnap PUBLIC cxx_std_20)

add_executable(gsnap_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_snap.cpp
  tests/test_snap3d.cpp)
target_link_libraries(gsnap_tests PRIVATE gsnap)
target_include_directories(gsnap_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gsnap_tests)

add_library(gsolve STATIC system.cpp infer.cpp)
target_include_directories(gsolve PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gsolve PUBLIC gcm)
target_compile_features(gsolve PUBLIC cxx_std_20)

add_executable(gsolve_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_solve.cpp
  tests/test_infer.cpp)
target_link_libraries(gsolve_tests PRIVATE gsolve)
target_include_directories(gsolve_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gsolve_tests)

add_library(gedit STATIC edit.cpp)
target_include_directories(gedit PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gedit PUBLIC gsnap)
target_compile_features(gedit PUBLIC cxx_std_20)

add_executable(gedit_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_edit.cpp
  tests/test_modify.cpp
  tests/test_corner.cpp)
target_link_libraries(gedit_tests PRIVATE gedit)
target_include_directories(gedit_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gedit_tests)

add_library(gdim STATIC dimension.cpp annotation.cpp table.cpp)
target_include_directories(gdim PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gdim PUBLIC gsnap gunit)
target_compile_features(gdim PUBLIC cxx_std_20)

add_executable(gdim_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_dimension.cpp
  tests/test_annotation.cpp
  tests/test_table.cpp)
target_link_libraries(gdim_tests PRIVATE gdim)
target_include_directories(gdim_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gdim_tests)

add_library(ggeo INTERFACE)
target_include_directories(ggeo INTERFACE ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(ggeo INTERFACE gcm)
target_compile_features(ggeo INTERFACE cxx_std_20)

add_executable(ggeo_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_d2.cpp
  tests/test_d3.cpp
  tests/test_footprint.cpp
  tests/test_nurbs.cpp)
target_link_libraries(ggeo_tests PRIVATE ggeo)
target_include_directories(ggeo_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(ggeo_tests)

add_library(gtopo STATIC merge.cpp
  body.cpp
  sketch.cpp
  pmi.cpp
  boolcurved.cpp
  profile.cpp region.cpp clip.cpp
  extrude.cpp
  triangulate.cpp
  boolean.cpp solid_edit.cpp circular_edge.cpp mate.cpp surface.cpp ssi.cpp measure.cpp recognise.cpp live.cpp
  revolve.cpp
  loft.cpp)
target_include_directories(gtopo PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gtopo PUBLIC gcm ggeo gsnap gsolve)
# Apple ld warns on a link cycle's repeated members; GNU ld needs them.
if(APPLE)
  set_target_properties(gsnap gtopo PROPERTIES LINK_INTERFACE_MULTIPLICITY 1)
endif()
target_compile_features(gtopo PUBLIC cxx_std_20)

add_executable(gtopo_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_extrude.cpp
  tests/test_boolean.cpp
  tests/test_region.cpp tests/test_clip.cpp
  tests/test_revolve.cpp
  tests/test_loft.cpp
  tests/test_solid_edit.cpp
  tests/test_sketch.cpp
  tests/test_pmi.cpp
  tests/test_boolcurved.cpp
  tests/test_mate.cpp
  tests/test_ssi.cpp
  tests/test_measure.cpp
  tests/test_recognise.cpp
  tests/test_live.cpp)
target_link_libraries(gtopo_tests PRIVATE gtopo)
target_include_directories(gtopo_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gtopo_tests)

glcad_xcode_source_groups()
