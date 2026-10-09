set(CPACK_PACKAGE_DIRECTORY "${CMAKE_SOURCE_DIR}/build/installers")

set(CPACK_PACKAGE_NAME "glcad")
# Architectures share the output folder but must not share CPack staging.
set(CPACK_TOPLEVEL_TAG "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_PACKAGE_VENDOR "Geramy Loveless")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "GLCAD -- precision 2D/3D CAD")
set(CPACK_PACKAGE_CONTACT "gloveless@jqluv.com")

if(APPLE)
  set(CPACK_GENERATOR "DragNDrop;ZIP")
  set(CPACK_DMG_VOLUME_NAME "GLCAD")
  set(CPACK_DMG_FORMAT "UDZO")
  # hdiutil wrapper: create -srcfolder fails on a wedged diskimages client.
  set(CPACK_COMMAND_HDIUTIL "${CMAKE_SOURCE_DIR}/cmake/cpack-hdiutil.sh")
elseif(WIN32)
  set(CPACK_GENERATOR "NSIS;ZIP")
  set(CPACK_NSIS_PACKAGE_NAME "GLCAD")
  set(CPACK_PACKAGE_EXECUTABLES "glcad" "GLCAD")
  set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
  set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/assets/icon/glcad.ico")
  set(CPACK_NSIS_DISPLAY_NAME "GLCAD")
  set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
else()
  set(CPACK_GENERATOR "DEB;RPM;TGZ")
  set(CPACK_DEBIAN_PACKAGE_MAINTAINER "gloveless@jqluv.com")
  set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "fonts-dejavu-core, libvulkan1")
  set(CPACK_RPM_PACKAGE_REQUIRES "dejavu-sans-fonts, vulkan-loader")
  # Arch in the name: both architectures share build/installers/.
  set(CPACK_PACKAGE_FILE_NAME "glcad-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_RPM_PACKAGE_AUTOREQ ON)
  set(CPACK_RPM_PACKAGE_LICENSE "LicenseRef-GLCAD-Noncommercial-1.0")
  set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
endif()

include(CPack)

if(APPLE)
  add_custom_target(package-all
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/package-all.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    USES_TERMINAL)
endif()

