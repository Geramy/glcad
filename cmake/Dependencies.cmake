include(FetchContent)

set(GLCAD_VULKAN_TAG "v1.4.357" CACHE STRING "Vulkan-Headers tag to pin")
option(GLCAD_FETCH_DEPS "Fetch pinned third-party sources" ON)

if(GLCAD_FETCH_DEPS)
  FetchContent_Declare(vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        ${GLCAD_VULKAN_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(vulkan_headers)
endif()

set(GLCAD_SLANG_TAG "v2026.16.1" CACHE STRING "Slang release to pin")
option(GLCAD_FETCH_SLANG "Fetch the Slang compiler for the GPU mirror" ON)

if(GLCAD_FETCH_SLANG AND NOT GLCAD_SLANGC)
  set(GLCAD_SLANG_PLATFORM "")
  set(GLCAD_SLANG_SHA "")
  if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
    set(GLCAD_SLANG_PLATFORM "macos-aarch64")
    set(GLCAD_SLANG_SHA
        "31bb295d0ead64f5906ae140fb42067029412ca02330c11ff8ea63986560216a")
  elseif(APPLE)
    set(GLCAD_SLANG_PLATFORM "macos-x86_64")
  elseif(UNIX AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(GLCAD_SLANG_PLATFORM "linux-aarch64")
  elseif(UNIX)
    set(GLCAD_SLANG_PLATFORM "linux-x86_64")
  elseif(WIN32)
    set(GLCAD_SLANG_PLATFORM "windows-x86_64")
  endif()

  if(GLCAD_SLANG_PLATFORM)
    string(REGEX REPLACE "^v" "" GLCAD_SLANG_VERSION ${GLCAD_SLANG_TAG})
    set(GLCAD_SLANG_URL
        "https://github.com/shader-slang/slang/releases/download/${GLCAD_SLANG_TAG}/slang-${GLCAD_SLANG_VERSION}-${GLCAD_SLANG_PLATFORM}.tar.gz")
    if(GLCAD_SLANG_SHA)
      # The hash is recorded only for the platform it was verified on.
      FetchContent_Declare(slang_tool
        URL      ${GLCAD_SLANG_URL}
        URL_HASH SHA256=${GLCAD_SLANG_SHA}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    else()
      FetchContent_Declare(slang_tool
        URL ${GLCAD_SLANG_URL}
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
      message(STATUS
        "Slang: no pinned hash for ${GLCAD_SLANG_PLATFORM}; the download is "
        "not verified. Record one, or set -DGLCAD_FETCH_SLANG=OFF.")
    endif()

    FetchContent_MakeAvailable(slang_tool)
    find_program(GLCAD_SLANGC slangc
      HINTS ${slang_tool_SOURCE_DIR}/bin
      NO_DEFAULT_PATH)
  endif()
endif()

# volk: iPadOS ships no Vulkan loader.
if(GLCAD_FETCH_DEPS)
  set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        1.4.304
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(volk)
  target_link_libraries(volk PUBLIC Vulkan::Headers)
  if(APPLE)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_METAL_EXT)
  elseif(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
  elseif(UNIX)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
  endif()
endif()

add_library(glcad_vulkan_icd INTERFACE)

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  # iPadOS has no Vulkan loader: link MoltenVK statically.
  set(GLCAD_MOLTENVK_IOS_TAG "v1.4.0" CACHE STRING "MoltenVK release for iOS")
  if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    # The all-platform archive also has the simulator slices Xcode needs.
    FetchContent_Declare(moltenvk_ios_simulator
      URL https://github.com/KhronosGroup/MoltenVK/releases/download/${GLCAD_MOLTENVK_IOS_TAG}/MoltenVK-all.tar)
    FetchContent_MakeAvailable(moltenvk_ios_simulator)
    set(moltenvk_ios_SOURCE_DIR "${moltenvk_ios_simulator_SOURCE_DIR}")
  else()
    FetchContent_Declare(moltenvk_ios
      URL https://github.com/KhronosGroup/MoltenVK/releases/download/${GLCAD_MOLTENVK_IOS_TAG}/MoltenVK-ios.tar)
    FetchContent_MakeAvailable(moltenvk_ios)
  endif()
  set(GLCAD_MOLTENVK_XCFRAMEWORK
      "${moltenvk_ios_SOURCE_DIR}/MoltenVK/static/MoltenVK.xcframework")
  if(NOT EXISTS "${GLCAD_MOLTENVK_XCFRAMEWORK}/Info.plist")
    message(FATAL_ERROR
      "MoltenVK-ios.tar changed shape: no static xcframework at "
      "${GLCAD_MOLTENVK_XCFRAMEWORK}")
  endif()
  message(STATUS "MoltenVK (static, iOS): ${GLCAD_MOLTENVK_XCFRAMEWORK}")
  target_link_libraries(glcad_vulkan_icd INTERFACE
    "${GLCAD_MOLTENVK_XCFRAMEWORK}"
    "-framework Metal" "-framework Foundation" "-framework QuartzCore"
    "-framework IOSurface" "-framework CoreGraphics" "-framework CoreVideo")
  target_compile_definitions(glcad_vulkan_icd INTERFACE GLCAD_STATIC_MOLTENVK)
elseif(APPLE)
  find_library(GLCAD_MOLTENVK MoltenVK
    HINTS $ENV{VULKAN_SDK}/lib /opt/homebrew/lib /usr/local/lib)
  find_library(GLCAD_VULKAN_LOADER vulkan
    HINTS $ENV{VULKAN_SDK}/lib /opt/homebrew/lib /usr/local/lib)

  if(GLCAD_VULKAN_LOADER)
    target_link_libraries(glcad_vulkan_icd INTERFACE ${GLCAD_VULKAN_LOADER})
    message(STATUS "Vulkan ICD: loader at ${GLCAD_VULKAN_LOADER} (validation layers available)")
  elseif(GLCAD_MOLTENVK)
    target_link_libraries(glcad_vulkan_icd INTERFACE ${GLCAD_MOLTENVK})
    message(STATUS "Vulkan ICD: MoltenVK direct at ${GLCAD_MOLTENVK} (no validation layers)")
  else()
    message(WARNING
      "No Vulkan ICD found. Install with:\n"
      "  brew install molten-vk vulkan-loader vulkan-validationlayers\n"
      "or install the LunarG SDK and set VULKAN_SDK.")
  endif()

  target_link_libraries(glcad_vulkan_icd INTERFACE
    "-framework Metal" "-framework Foundation" "-framework QuartzCore"
    "-framework IOSurface")
else()
  find_library(GLCAD_VULKAN_LOADER vulkan HINTS $ENV{VULKAN_SDK}/lib)
  if(GLCAD_VULKAN_LOADER)
    target_link_libraries(glcad_vulkan_icd INTERFACE ${GLCAD_VULKAN_LOADER})
  endif()
endif()

find_program(GLCAD_GLSLANG glslangValidator
  HINTS $ENV{VULKAN_SDK}/bin /opt/homebrew/bin /usr/local/bin)
if(GLCAD_GLSLANG)
  message(STATUS "Shader compiler: ${GLCAD_GLSLANG}")
else()
  message(STATUS "Shader compiler: not found (brew install glslang)")
endif()

# Homebrew puts MoltenVK's ICD manifest where the loader does not search.
find_file(GLCAD_VK_ICD MoltenVK_icd.json
  PATHS
    $ENV{VULKAN_SDK}/share/vulkan/icd.d
    /opt/homebrew/etc/vulkan/icd.d
    /opt/homebrew/share/vulkan/icd.d
    /usr/local/etc/vulkan/icd.d
    /usr/local/share/vulkan/icd.d
  NO_DEFAULT_PATH)

find_path(GLCAD_VK_LAYER_PATH VkLayer_khronos_validation.json
  PATHS
    $ENV{VULKAN_SDK}/share/vulkan/explicit_layer.d
    /opt/homebrew/share/vulkan/explicit_layer.d
    /usr/local/share/vulkan/explicit_layer.d
  NO_DEFAULT_PATH)

set(GLCAD_VK_RUNTIME_ENV "")
if(GLCAD_VK_ICD)
  list(APPEND GLCAD_VK_RUNTIME_ENV
       "VK_ICD_FILENAMES=${GLCAD_VK_ICD}" "VK_DRIVER_FILES=${GLCAD_VK_ICD}")
  message(STATUS "Vulkan ICD manifest: ${GLCAD_VK_ICD}")
else()
  message(WARNING "No MoltenVK ICD manifest found -- vkCreateInstance will fail")
endif()
if(GLCAD_VK_LAYER_PATH)
  list(APPEND GLCAD_VK_RUNTIME_ENV "VK_LAYER_PATH=${GLCAD_VK_LAYER_PATH}")
  message(STATUS "Vulkan validation layers: ${GLCAD_VK_LAYER_PATH}")
endif()
# Never add Homebrew to DYLD_LIBRARY_PATH: it breaks ImageIO's libpng.

string(REPLACE ";" "\nexport " GLCAD_VK_RUNTIME_ENV_SH "${GLCAD_VK_RUNTIME_ENV}")
set(GLCAD_VK_RUNTIME_ENV_SH "export ${GLCAD_VK_RUNTIME_ENV_SH}")

string(REPLACE ";" "\n" GLCAD_VK_RUNTIME_ENV_DOTENV "${GLCAD_VK_RUNTIME_ENV}")

function(glcad_vulkan_runtime_env target)
  set_tests_properties(${target} PROPERTIES ENVIRONMENT "${GLCAD_VK_RUNTIME_ENV}")
endfunction()

if(GLCAD_FETCH_DEPS)
  FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG        f58f558c120e9b32c217290b80bad1a0729fbb2c
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(stb)
  add_library(stb INTERFACE)
  target_include_directories(stb INTERFACE ${stb_SOURCE_DIR})
endif()

if(GLCAD_FETCH_DEPS)
  FetchContent_Declare(cpp_httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.18.3
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE)
  FetchContent_MakeAvailable(cpp_httplib)
  add_library(glcad_httplib INTERFACE)
  target_include_directories(glcad_httplib INTERFACE ${cpp_httplib_SOURCE_DIR})
  if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Use macOS trust roots instead of OpenSSL's build-machine certificate path.
    target_compile_definitions(glcad_httplib INTERFACE CPPHTTPLIB_USE_CERTS_FROM_MACOSX_KEYCHAIN)
    target_link_libraries(glcad_httplib INTERFACE "-framework Security" "-framework CoreFoundation")
  endif()
endif()

if(GLCAD_FETCH_DEPS)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d)
  FetchContent_MakeAvailable(nlohmann_json)
endif()
