# Runs as a build step so the stamp is current after every rebuild.
execute_process(COMMAND git -C ${SRC} rev-parse --short HEAD
                OUTPUT_VARIABLE HASH OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git -C ${SRC} status --porcelain --untracked-files=no
                OUTPUT_VARIABLE DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT HASH)
  set(HASH "unknown")
elseif(NOT DIRTY STREQUAL "")
  set(HASH "${HASH}+")     # uncommitted changes are in this binary
endif()
string(TIMESTAMP WHEN "%Y-%m-%d %H:%M:%S")

file(WRITE ${OUT}.tmp
"#pragma once\n"
"// Generated each build by cmake/BuildStamp.cmake. Do not edit.\n"
"#define GLCAD_BUILD_HASH \"${HASH}\"\n"
"#define GLCAD_BUILD_TIME \"${WHEN}\"\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OUT}.tmp ${OUT})
file(REMOVE ${OUT}.tmp)
