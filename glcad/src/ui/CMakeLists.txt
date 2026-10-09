add_library(gui STATIC context.cpp widgets.cpp icons.cpp icon_glyphs.cpp dock.cpp containers.cpp ring.cpp panels.cpp access.cpp)
target_include_directories(gui PUBLIC ${CMAKE_SOURCE_DIR}/glcad/include)
target_link_libraries(gui PUBLIC gcm gtheme)
target_compile_features(gui PUBLIC cxx_std_20)

add_executable(gui_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  tests/test_gui.cpp
  tests/test_containers.cpp)
target_link_libraries(gui_tests PRIVATE gui)
target_include_directories(gui_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
glcad_add_tiered_test(gui_tests)

if(GLCAD_BUILD_VULKAN AND TARGET volk)

if(APPLE)
  enable_language(OBJCXX)
  if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(GLCAD_PLATFORM_SOURCE main_ios.mm)
  else()
    set(GLCAD_PLATFORM_SOURCE main_macos.mm)
  endif()
  set(GLCAD_SURFACE GLCAD_SURFACE_METAL)
elseif(UNIX)
  find_package(X11)
  if(NOT X11_FOUND)
    message(STATUS "gcapp: no X11; the Linux shell needs it")
    return()
  endif()
  set(GLCAD_PLATFORM_SOURCE main_linux.cpp)
  set(GLCAD_SURFACE GLCAD_SURFACE_XLIB)
elseif(WIN32)
  set(GLCAD_PLATFORM_SOURCE main_windows.cpp)
  set(GLCAD_SURFACE GLCAD_SURFACE_WIN32)
else()
  message(STATUS "gcapp: no shell for this platform")
  return()
endif()

set(GLCAD_ICON_FONT_DEF
    GLCAD_ICON_FONT="${CMAKE_SOURCE_DIR}/assets/fonts/MaterialSymbols-glcad.ttf")

set(SHADER_OUT ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT})
set(SPV_FILES "")
foreach(S canvas.vert canvas.frag geom.vert geom.frag line.vert arc.vert arc_thin.vert ui.vert ui.frag
            text.vert text.frag image.frag mesh.vert mesh.frag flat.frag depth_only.frag
            bool_preview.frag)
  add_custom_command(
    OUTPUT ${SHADER_OUT}/${S}.spv
    COMMAND ${GLCAD_GLSLANG} -V -I${CMAKE_CURRENT_SOURCE_DIR}/shaders
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${S}
            -o ${SHADER_OUT}/${S}.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${S}
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/frame.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/stroke.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/mesh_shading.glsl
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/view_ray.glsl
    COMMENT "glslang ${S}")
  list(APPEND SPV_FILES ${SHADER_OUT}/${S}.spv)
endforeach()
add_custom_target(gcapp_shaders DEPENDS ${SPV_FILES})

set(GLCAD_SHELL_SOURCES
  shell.cpp
  subscription.cpp
  product_ui.cpp
  product_workspace.cpp
  app_config.cpp
  assistant.cpp
  tabs.cpp
  journal.cpp
  menu_bar.cpp
  document_io.cpp
  recovery.cpp
  layouts.cpp
  shell_panels.cpp
  text_style_bar.cpp
  hatch_style_bar.cpp
  shell_commands.cpp
  command_line.cpp
  drafting_features.cpp
  drafting_features_ui.cpp
  object_library.cpp
  block_library.cpp
  library_commands.cpp
  wall_sections.cpp
  symbols.cpp
  commands.cpp
  commands_draw.cpp
  commands_modify.cpp
  commands_annotate.cpp
  commands_parametric.cpp
  commands_inquire.cpp
  commands_tools.cpp
  commands_organise.cpp
  ai_tools.cpp
  mcp.cpp
  mcp_http.cpp
  snapshot.cpp
  scenario.cpp)

add_executable(glcad MACOSX_BUNDLE
  ${GLCAD_PLATFORM_SOURCE}
  canvas.cpp selection.cpp revision_cloud.cpp publishing.cpp regions.cpp
  touch.cpp
  ${GLCAD_SHELL_SOURCES}
  tools.cpp
  menus.cpp
  solids_file.cpp
  doc_entities.cpp document_state.cpp document_sync.cpp document_lock.cpp drafting_settings.cpp underlays.cpp xrefs.cpp
  build_stamp.cpp
  modeller.cpp
  renderer.cpp
  boolean_gpu.cpp)

if(APPLE)
  target_sources(glcad PRIVATE accessibility_apple.mm)
elseif(WIN32)
  target_sources(glcad PRIVATE accessibility_windows.cpp)
elseif(UNIX)
  target_sources(glcad PRIVATE accessibility_atspi.cpp)
endif()
set_source_files_properties(accessibility_apple.mm main_ios.mm PROPERTIES
  COMPILE_OPTIONS "-fobjc-arc")
set(GLCAD_STAMP_H ${CMAKE_CURRENT_BINARY_DIR}/build_stamp.h)
add_custom_target(glcad_stamp ALL
  COMMAND ${CMAKE_COMMAND} -DOUT=${GLCAD_STAMP_H} -DSRC=${CMAKE_SOURCE_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/BuildStamp.cmake
  BYPRODUCTS ${GLCAD_STAMP_H}
  COMMENT "build stamp")
add_dependencies(glcad gcapp_shaders glcad_stamp)

target_include_directories(glcad PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/glcad/include
  ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(glcad PRIVATE gcm ggeo grender gci gcs gcmd
                                   gtext gui gview
                                   gedit gplot gdxf gdwg gstep gmesh gxt gdraft gfile gai gplan grag
                                   volk glcad_vulkan_icd)
target_compile_definitions(glcad PRIVATE GLCAD_SHADER_DIR="${SHADER_OUT}"
                                        ${GLCAD_ICON_FONT_DEF}
                                        ${GLCAD_SURFACE})
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(GLCAD_IOS_BUNDLE_ID "com.glcad.app" CACHE STRING "Apple bundle identifier")
  option(GLCAD_PAID_BUILD "Require a verified subscription to edit in the iOS app" OFF)
  set(GLCAD_PURCHASE_MODEL "" CACHE STRING "iOS purchase model: community, upfront or subscription")
  set_property(CACHE GLCAD_PURCHASE_MODEL PROPERTY STRINGS "" community upfront subscription)
  if(GLCAD_PURCHASE_MODEL STREQUAL "")
    set(GLCAD_PURCHASE_MODEL_EFFECTIVE community)
    if(GLCAD_PAID_BUILD)
      set(GLCAD_PURCHASE_MODEL_EFFECTIVE subscription)
    endif()
  elseif(GLCAD_PURCHASE_MODEL MATCHES "^(community|upfront|subscription)$")
    set(GLCAD_PURCHASE_MODEL_EFFECTIVE ${GLCAD_PURCHASE_MODEL})
  else()
    message(FATAL_ERROR "GLCAD_PURCHASE_MODEL must be community, upfront or subscription")
  endif()
  if(GLCAD_PAID_BUILD AND NOT GLCAD_PURCHASE_MODEL_EFFECTIVE STREQUAL "subscription")
    message(FATAL_ERROR "GLCAD_PAID_BUILD=ON conflicts with GLCAD_PURCHASE_MODEL=${GLCAD_PURCHASE_MODEL}")
  endif()
  set(GLCAD_PAID_BUILD_PLIST false)
  if(GLCAD_PURCHASE_MODEL_EFFECTIVE STREQUAL "subscription")
    set(GLCAD_PAID_BUILD_PLIST true)
  endif()
  set(GLCAD_IOS_BUILD_NUMBER "" CACHE STRING "CFBundleVersion; defaults to the project version")
  if(NOT GLCAD_IOS_BUILD_NUMBER)
    set(GLCAD_IOS_BUILD_NUMBER_EFFECTIVE ${PROJECT_VERSION})
  else()
    set(GLCAD_IOS_BUILD_NUMBER_EFFECTIVE ${GLCAD_IOS_BUILD_NUMBER})
  endif()
  set(GLCAD_IOS_DEVICE_FAMILY "1,2" CACHE STRING "TARGETED_DEVICE_FAMILY: 1,2 for iPhone and iPad, 2 for iPad only")
  foreach(KEY SUBSCRIPTION_PRODUCT_ID SUPPORT_URL PRIVACY_URL TERMS_URL)
    set(GLCAD_${KEY} "" CACHE STRING "App Store product configuration: ${KEY}")
    set(GLCAD_${KEY}_PLIST "${GLCAD_${KEY}}")
    string(REPLACE "&" "&amp;" GLCAD_${KEY}_PLIST "${GLCAD_${KEY}_PLIST}")
    string(REPLACE "<" "&lt;" GLCAD_${KEY}_PLIST "${GLCAD_${KEY}_PLIST}")
    string(REPLACE ">" "&gt;" GLCAD_${KEY}_PLIST "${GLCAD_${KEY}_PLIST}")
  endforeach()
  target_link_libraries(glcad PRIVATE
    "-framework UIKit" "-framework QuartzCore" "-framework Metal" "-framework UniformTypeIdentifiers" "-framework StoreKit")
  enable_language(Swift)
  target_sources(glcad PRIVATE subscription_storekit.swift iosproduct.mm)
  set_source_files_properties(iosproduct.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
    execute_process(COMMAND xcrun --sdk iphonesimulator --show-sdk-path
      OUTPUT_VARIABLE GLCAD_SIM_SDK OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
    set(GLCAD_STOREKIT_TEST_FRAMEWORKS "${GLCAD_SIM_SDK}/Developer/Library/Frameworks")
    # Xcode requires a configuration-independent source list.
    target_sources(glcad PRIVATE tests/storekit_smoke.swift tests/GLCADLocal.storekit)
    set_source_files_properties(tests/GLCADLocal.storekit PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_compile_options(glcad PRIVATE
      "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:Swift>>:-DDEBUG>"
      "$<$<AND:$<CONFIG:Debug>,$<COMPILE_LANGUAGE:Swift>>:-F${GLCAD_STOREKIT_TEST_FRAMEWORKS}>")
    target_link_options(glcad PRIVATE "$<$<CONFIG:Debug>:-F${GLCAD_STOREKIT_TEST_FRAMEWORKS}>")
    target_link_libraries(glcad PRIVATE "$<$<CONFIG:Debug>:-framework StoreKitTest>")
    get_filename_component(GLCAD_SIM_PLATFORM_FRAMEWORKS
      "${GLCAD_SIM_SDK}/../../Library/Frameworks" ABSOLUTE)
    add_library(gcapp_storekit_tests MODULE EXCLUDE_FROM_ALL tests/test_storekit.mm)
    set_target_properties(gcapp_storekit_tests PROPERTIES
      BUNDLE TRUE BUNDLE_EXTENSION xctest XCTEST TRUE XCTEST_TESTEE glcad
      XCODE_GENERATE_SCHEME TRUE
      XCODE_PRODUCT_TYPE "com.apple.product-type.bundle.unit-test"
      XCODE_ATTRIBUTE_GENERATE_INFOPLIST_FILE YES
      XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.glcad.storekit-tests"
      XCODE_ATTRIBUTE_TEST_HOST "$<TARGET_FILE:glcad>"
      XCODE_ATTRIBUTE_BUNDLE_LOADER "$(TEST_HOST)"
      XCODE_SCHEME_ENVIRONMENT "GLCAD_IOS_STOREKIT_TEST_HOST=1")
    target_compile_options(gcapp_storekit_tests PRIVATE -fobjc-arc "-F${GLCAD_SIM_PLATFORM_FRAMEWORKS}")
    target_link_options(gcapp_storekit_tests PRIVATE "-F${GLCAD_SIM_PLATFORM_FRAMEWORKS}")
    target_link_libraries(gcapp_storekit_tests PRIVATE "-framework XCTest" "-framework Foundation" "-framework UIKit")
    add_dependencies(gcapp_storekit_tests glcad)
  endif()
  set_target_properties(glcad PROPERTIES
    Swift_LANGUAGE_VERSION 5
    XCODE_ATTRIBUTE_SWIFT_VERSION 5.0
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_INSTALL_PATH "/Applications")
  set_target_properties(glcad PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "GLCAD"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${GLCAD_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "${GLCAD_IOS_DEVICE_FAMILY}"
    MACOSX_BUNDLE_BUNDLE_VERSION ${GLCAD_IOS_BUILD_NUMBER_EFFECTIVE}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/ios-Info.plist.in)
  # Named directly: GLCAD_FONT_FILES is not defined yet at this point.
  set(GLCAD_IOS_FONTS
      ${CMAKE_SOURCE_DIR}/assets/fonts/MaterialSymbols-glcad.ttf
      ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf
      ${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-DejaVu.txt
      ${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-MaterialSymbols.txt)
  target_sources(glcad PRIVATE ${SPV_FILES} ${GLCAD_IOS_FONTS}
                 ${CMAKE_SOURCE_DIR}/assets/icon/AppIcon.xcassets)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/icon/AppIcon.xcassets
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(glcad PROPERTIES
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
  set_source_files_properties(${SPV_FILES} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/shaders GENERATED TRUE)
  set_source_files_properties(${GLCAD_IOS_FONTS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources/fonts)
  set(GLCAD_IOS_SAMPLE "${CMAKE_SOURCE_DIR}/assets/samples/GettingStarted.dxf")
  set(GLCAD_IOS_PRIVACY "${CMAKE_SOURCE_DIR}/cmake/PrivacyInfo.xcprivacy")
  find_program(GLCAD_PYTHON3 python3 REQUIRED)
  set(GLCAD_IOS_NOTICES "${CMAKE_CURRENT_BINARY_DIR}/licenses/THIRD_PARTY_NOTICES.txt")
  add_custom_command(OUTPUT "${GLCAD_IOS_NOTICES}"
    COMMAND "${GLCAD_PYTHON3}" "${CMAKE_SOURCE_DIR}/scripts/bundle-license-notices.py"
      --build "${CMAKE_BINARY_DIR}" --output "${CMAKE_CURRENT_BINARY_DIR}/licenses" --allow-incomplete
    DEPENDS "${CMAKE_SOURCE_DIR}/scripts/bundle-license-notices.py" "${CMAKE_BINARY_DIR}/CMakeCache.txt"
      "${CMAKE_SOURCE_DIR}/LICENSE"
    COMMENT "Bundle dependency license texts")
  target_sources(glcad PRIVATE "${GLCAD_IOS_SAMPLE}" "${GLCAD_IOS_PRIVACY}" "${GLCAD_IOS_NOTICES}")
  set_source_files_properties("${GLCAD_IOS_SAMPLE}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources/samples)
  set_source_files_properties("${GLCAD_IOS_PRIVACY}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties("${GLCAD_IOS_NOTICES}" PROPERTIES GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources/licenses)
  target_sources(glcad PRIVATE "${CMAKE_SOURCE_DIR}/COMMERCIAL-LICENSE.md")
  set_source_files_properties("${CMAKE_SOURCE_DIR}/COMMERCIAL-LICENSE.md" PROPERTIES MACOSX_PACKAGE_LOCATION Resources/licenses)
  if(GLCAD_WITH_PGLITE)
    # chdir, not WORKING_DIRECTORY: Xcode escapes $(...) in WORKING_DIRECTORY.
    add_custom_command(TARGET glcad POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
              "$<TARGET_BUNDLE_CONTENT_DIR:glcad>/pglite"
      COMMAND ${CMAKE_COMMAND} -E chdir
              "$<TARGET_BUNDLE_CONTENT_DIR:glcad>/pglite"
              ${CMAKE_COMMAND} -E tar x ${GLCAD_PGLITE_OUT}/pglite-runtime.tar.gz)
  endif()
elseif(APPLE)
  target_link_libraries(glcad PRIVATE
    "-framework Cocoa" "-framework QuartzCore" "-framework Metal"
    "-framework AVFoundation" "-framework CoreAudio" "-framework PDFKit" "-framework ApplicationServices")
  set_target_properties(glcad PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "GLCAD"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.glcad.app"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE "glcad.icns"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/cmake/macos-Info.plist.in)
  target_sources(glcad PRIVATE ${CMAKE_SOURCE_DIR}/assets/icon/glcad.icns)
  set_source_files_properties(${CMAKE_SOURCE_DIR}/assets/icon/glcad.icns
    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(UNIX)
  target_link_libraries(glcad PRIVATE ${X11_LIBRARIES})
  target_include_directories(glcad PRIVATE ${X11_INCLUDE_DIR})
elseif(WIN32)
  target_link_libraries(glcad PRIVATE uiautomationcore oleaut32 ole32 comdlg32 shell32)
  target_sources(glcad PRIVATE ${CMAKE_SOURCE_DIR}/cmake/glcad.rc)
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gcapp_gpu_tests tests/test_boolean_gpu.cpp boolean_gpu.cpp)
  target_include_directories(gcapp_gpu_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/include)
  target_link_libraries(gcapp_gpu_tests PRIVATE volk glcad_vulkan_icd gcm gtopo gview)
  target_compile_definitions(gcapp_gpu_tests PRIVATE GLCAD_SHADER_DIR="${SHADER_OUT}")
  add_dependencies(gcapp_gpu_tests gcapp_shaders)
endif()

add_executable(gcapp_uishot
  tools/uishot.cpp
  canvas.cpp selection.cpp revision_cloud.cpp publishing.cpp regions.cpp
  touch.cpp
  ${GLCAD_SHELL_SOURCES}
  tools.cpp
  menus.cpp
  solids_file.cpp
  doc_entities.cpp document_state.cpp document_sync.cpp document_lock.cpp drafting_settings.cpp underlays.cpp xrefs.cpp
  build_stamp.cpp
  modeller.cpp)
add_dependencies(gcapp_uishot glcad_stamp)
target_include_directories(gcapp_uishot PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/glcad/include ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(gcapp_uishot PRIVATE ${GLCAD_ICON_FONT_DEF})
target_link_libraries(gcapp_uishot PRIVATE gcm ggeo grender gci gcs gcmd
                                           gtext gui gview
                                           gedit gplot
                                           gdxf gdwg gstep gmesh gxt gdraft gfile gai gplan grag)

# A broad Homebrew DYLD_LIBRARY_PATH makes ImageIO crash in libpng.
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  add_executable(gcapp_print_tests
    ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp tests/test_printing_macos.mm)
  target_include_directories(gcapp_print_tests PRIVATE ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
  target_compile_options(gcapp_print_tests PRIVATE -fobjc-arc)
  target_link_libraries(gcapp_print_tests PRIVATE "-framework Cocoa")
  glcad_add_tiered_test(gcapp_print_tests)
  foreach(_tier IN ITEMS fast full soak)
    set_property(TEST gcapp_print_tests_${_tier} APPEND PROPERTY ENVIRONMENT ${GLCAD_VK_RUNTIME_ENV})
  endforeach()
endif()

add_executable(gcapp_tests
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests/main.cpp
  canvas.cpp selection.cpp revision_cloud.cpp publishing.cpp regions.cpp
  touch.cpp
  modeller.cpp
  tools.cpp
  menus.cpp
  solids_file.cpp
  doc_entities.cpp document_state.cpp document_sync.cpp document_lock.cpp drafting_settings.cpp underlays.cpp xrefs.cpp
  build_stamp.cpp
  scenario.cpp
  ${GLCAD_SHELL_SOURCES}
  tests/test_view.cpp
  tests/test_tools.cpp
  tests/test_touch.cpp
  tests/test_modeller.cpp
  tests/test_workflow.cpp
  tests/test_drafting_features.cpp
  tests/test_block_library.cpp
  tests/test_door_bulk_edit.cpp
  tests/test_symbols.cpp
  tests/test_mcp_http.cpp
  tests/test_release_workflow.cpp
  tests/test_platform_workflow.cpp
  tests/test_subscription.cpp
  tests/test_actions.cpp
  tests/test_bench.cpp
  tests/test_text_style_bar.cpp
  tests/test_door_opening_bounds.cpp
  tests/test_hatch_style_bar.cpp
  tests/test_document_events.cpp
  tests/test_scenario.cpp)
add_dependencies(gcapp_tests glcad_stamp)
target_include_directories(gcapp_tests PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/glcad/include
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/glcad/src/math/tests)
target_link_libraries(gcapp_tests PRIVATE gcm ggeo grender gci gcs gcmd
                                          gtext gui gview
                                          gedit gplot
                                          gdxf gdwg gstep gmesh gxt gdraft gfile gai gplan grag gdgn_test_support)
target_compile_definitions(gcapp_tests PRIVATE ${GLCAD_ICON_FONT_DEF})
glcad_add_tiered_test(gcapp_tests)

set(_glcad_app_env "${GLCAD_VK_RUNTIME_ENV_DOTENV}")
set(_glcad_app_env_sh "${GLCAD_VK_RUNTIME_ENV_SH}")
if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(_glcad_app_env "# The app uses its bundled Vulkan runtime.")
  set(_glcad_app_env_sh "# The app uses its bundled Vulkan runtime.")
endif()
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/glcad.env
     CONTENT "${_glcad_app_env}\n")

if(NOT CMAKE_CONFIGURATION_TYPES)
  file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/run-glcad.sh CONTENT
  "#!/bin/sh\n# generated by cmake\n${_glcad_app_env_sh}\nexec \"$<TARGET_FILE:glcad>\" \"$@\"\n")
endif()

set(GLCAD_FONT_FILES
    ${CMAKE_SOURCE_DIR}/assets/fonts/DejaVuSans.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/MaterialSymbols-glcad.ttf
    ${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-MaterialSymbols.txt
    ${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-DejaVu.txt)

if(APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set_target_properties(glcad PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

  install(FILES ${CMAKE_SOURCE_DIR}/third_party/dgnlib/LICENSE.txt DESTINATION glcad.app/Contents/Resources/licenses/dgnlib)
  install(TARGETS glcad BUNDLE DESTINATION .)
  install(FILES ${SPV_FILES}
          DESTINATION glcad.app/Contents/Resources/shaders)
  install(FILES ${GLCAD_FONT_FILES}
          DESTINATION glcad.app/Contents/Resources/fonts)

  find_program(GLCAD_PYTHON3 python3 REQUIRED)
  if(NOT GLCAD_MOLTENVK)
    message(FATAL_ERROR "macOS app builds require MoltenVK to bundle with the app")
  endif()
  set(_glcad_bundle_script "${CMAKE_SOURCE_DIR}/scripts/bundle-macos.py")
  set(_glcad_bundle_args --moltenvk "${GLCAD_MOLTENVK}"
      --shaders "${SHADER_OUT}" --fonts "${CMAKE_SOURCE_DIR}/assets/fonts")
  if(GLCAD_VULKAN_LOADER)
    list(APPEND _glcad_bundle_args --vulkan-loader "${GLCAD_VULKAN_LOADER}")
  endif()
  if(GLCAD_WITH_PGLITE)
    list(APPEND _glcad_bundle_args --pglite-runtime "${GLCAD_PGLITE_OUT}/install")
  endif()
  set_property(TARGET glcad APPEND PROPERTY LINK_DEPENDS "${_glcad_bundle_script}")
  add_custom_command(TARGET glcad POST_BUILD
    COMMAND "${GLCAD_PYTHON3}" "${_glcad_bundle_script}"
      --app "$<TARGET_BUNDLE_DIR:glcad>" ${_glcad_bundle_args} --identity -
    COMMENT "Bundle and verify macOS runtime libraries"
    VERBATIM)
  add_test(NAME glcad_macos_bundle
    COMMAND "${GLCAD_PYTHON3}" "${_glcad_bundle_script}"
      --app "$<TARGET_BUNDLE_DIR:glcad>" --check)
  add_test(NAME glcad_macos_bundle_regressions
    COMMAND "${GLCAD_PYTHON3}" -m unittest discover
      -s "${CMAKE_SOURCE_DIR}/scripts/tests" -p test_macos_bundle.py)
  set_tests_properties(glcad_macos_bundle glcad_macos_bundle_regressions
    PROPERTIES LABELS "packaging")
elseif(WIN32)
  install(FILES ${CMAKE_SOURCE_DIR}/third_party/dgnlib/LICENSE.txt DESTINATION licenses/dgnlib)
  install(TARGETS glcad RUNTIME DESTINATION .)
  install(FILES ${SPV_FILES} DESTINATION shaders)
  install(FILES ${GLCAD_FONT_FILES} DESTINATION fonts)
  if(GLCAD_PORTABLE_UNDERLAYS AND DEFINED VCPKG_INSTALLED_DIR)
    install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/"
            DESTINATION licenses FILES_MATCHING PATTERN "copyright")
  endif()
elseif(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
  install(FILES ${CMAKE_SOURCE_DIR}/third_party/dgnlib/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/glcad/licenses/dgnlib)
  install(TARGETS glcad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(FILES ${SPV_FILES}
          DESTINATION ${CMAKE_INSTALL_DATADIR}/glcad/shaders)
  install(FILES ${GLCAD_FONT_FILES}
          DESTINATION ${CMAKE_INSTALL_DATADIR}/glcad/fonts)
  install(FILES ${CMAKE_SOURCE_DIR}/cmake/glcad.desktop
          DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/assets/icon/glcad-256.png
          DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
          RENAME glcad.png)
endif()

if(APPLE)
  set(_glcad_package_resources glcad.app/Contents/Resources)
elseif(WIN32)
  set(_glcad_package_resources .)
else()
  set(_glcad_package_resources ${CMAKE_INSTALL_DATADIR}/glcad)
endif()
install(FILES ${CMAKE_SOURCE_DIR}/LICENSE ${CMAKE_SOURCE_DIR}/COMMERCIAL-LICENSE.md
        DESTINATION ${_glcad_package_resources}/licenses)
install(FILES ${CMAKE_SOURCE_DIR}/assets/samples/GettingStarted.dxf
        DESTINATION ${_glcad_package_resources}/samples)
foreach(_notice IN ITEMS
    "volk-src/LICENSE.md" "vulkan_headers-src/LICENSE.md"
    "vulkan_headers-src/LICENSES/Apache-2.0.txt" "vulkan_headers-src/LICENSES/MIT.txt"
    "stb-src/LICENSE" "cpp_httplib-src/LICENSE" "nlohmann_json-src/LICENSE.MIT")
  get_filename_component(_notice_dir "${_notice}" DIRECTORY)
  install(FILES "${FETCHCONTENT_BASE_DIR}/${_notice}"
          DESTINATION "${_glcad_package_resources}/licenses/${_notice_dir}")
endforeach()
if(GLCAD_WITH_PGLITE)
  if(NOT APPLE)
  install(DIRECTORY "${GLCAD_PGLITE_OUT}/install/bin" "${GLCAD_PGLITE_OUT}/install/lib"
                    "${GLCAD_PGLITE_OUT}/install/share"
          DESTINATION "${_glcad_package_resources}/pglite" USE_SOURCE_PERMISSIONS)
  install(DIRECTORY "${GLCAD_PGLITE_OUT}/install/pgdata-template"
          DESTINATION "${_glcad_package_resources}/pglite" USE_SOURCE_PERMISSIONS OPTIONAL)
  endif()
  install(FILES "${CMAKE_BINARY_DIR}/pglite/src/pglite_engine/LICENSE"
          DESTINATION "${_glcad_package_resources}/licenses/pglite-rs")
  install(FILES "${CMAKE_BINARY_DIR}/pglite/src/pglite_engine/postgres-pglite/COPYRIGHT"
          DESTINATION "${_glcad_package_resources}/licenses/postgresql")
  install(FILES "${CMAKE_BINARY_DIR}/pglite/src/pglite_engine/postgres-pglite/pglite/other_extensions/vector/LICENSE"
          DESTINATION "${_glcad_package_resources}/licenses/pgvector")
endif()
if(APPLE)
  foreach(_library IN ITEMS GLCAD_MOLTENVK GLCAD_VULKAN_LOADER OPENSSL_SSL_LIBRARY)
    if(${_library})
      get_filename_component(_library_real "${${_library}}" REALPATH)
      get_filename_component(_library_dir "${_library_real}" DIRECTORY)
      get_filename_component(_library_prefix "${_library_dir}" DIRECTORY)
      if(EXISTS "${_library_prefix}/LICENSE")
        install(FILES "${_library_prefix}/LICENSE"
                DESTINATION "${_glcad_package_resources}/licenses/${_library}")
      elseif(EXISTS "${_library_prefix}/LICENSE.txt")
        install(FILES "${_library_prefix}/LICENSE.txt"
                DESTINATION "${_glcad_package_resources}/licenses/${_library}")
      else()
        message(FATAL_ERROR "Missing license text for bundled ${_library}: ${_library_prefix}")
      endif()
    endif()
  endforeach()
  install(CODE "execute_process(COMMAND \"${GLCAD_PYTHON3}\"
    \"${_glcad_bundle_script}\" --app \"\${CMAKE_INSTALL_PREFIX}/glcad.app\"
    COMMAND_ERROR_IS_FATAL ANY)")

endif()

endif()


glcad_xcode_source_groups()
