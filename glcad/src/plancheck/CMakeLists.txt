add_library(gplan STATIC plancheck.cpp)
target_include_directories(gplan PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gplan PUBLIC gcm gsnap)
target_compile_features(gplan PUBLIC cxx_std_20)

add_executable(gplan_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_plancheck.cpp)
target_link_libraries(gplan_tests PRIVATE gplan)
target_include_directories(gplan_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gplan_tests)
