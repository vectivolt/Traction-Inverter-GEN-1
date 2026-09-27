# Traction Tool — bench/service desktop application for the 220 kW / 800 V traction inverter (Qt 6 Widgets, C++17).
# Qt is used under LGPL-3.0 with dynamic linking and Qwt under LGPL-2.1 + the Qwt exception: see NOTICES.md.
cmake_minimum_required(VERSION 3.25)
project(TractionTool VERSION 0.1.0 LANGUAGES CXX
        DESCRIPTION "Traction Tool: bench/service application for the traction inverter")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

set(TT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Repository root (firmware/ and tool/ below it)")
set(TT_SIM_BRIDGE "${TT_REPO_ROOT}/tool/bridge/sim_bridge" CACHE FILEPATH
    "Simulator bridge executable used by the tests and bundled by packaging (built by tool/bridge/Makefile)")
option(TT_BUILD_TESTS "Build the QtTest suites" ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets SerialBus Svg Test)

# ---- Qwt (no CMake package upstream): a framework on macOS/Homebrew, a plain library elsewhere ----
find_library(QWT_LIBRARY NAMES qwt qwt-qt6 qwt6-qt6 qwt6 REQUIRED)
set(_qwt_hints "")
if(QWT_LIBRARY MATCHES "\\.framework$")
  list(APPEND _qwt_hints "${QWT_LIBRARY}/Headers")
endif()
find_path(QWT_INCLUDE_DIR NAMES qwt_plot.h HINTS ${_qwt_hints} PATH_SUFFIXES qwt qwt-qt6 qwt6 REQUIRED)
message(STATUS "Qwt: ${QWT_LIBRARY} (headers ${QWT_INCLUDE_DIR})")

# ---- the protocol exports (the single source of truth; tool/protocol/generate.mjs writes them) ----
set(TT_PROTOCOL_DIR "${TT_REPO_ROOT}/tool/protocol" CACHE PATH "params.json, dtcs.json, can-frames.json")
set(TT_PROTOCOL_FILES "")
foreach(f params.json dtcs.json can-frames.json)
  if(NOT EXISTS "${TT_PROTOCOL_DIR}/${f}")
    message(FATAL_ERROR "${TT_PROTOCOL_DIR}/${f} is missing: run  node tool/protocol/generate.mjs")
  endif()
  set_source_files_properties("${TT_PROTOCOL_DIR}/${f}" PROPERTIES QT_RESOURCE_ALIAS "${f}")
  list(APPEND TT_PROTOCOL_FILES "${TT_PROTOCOL_DIR}/${f}")
endforeach()

# ---- warnings are errors on everything we compile ----
# (-Wpedantic everywhere Qt allows it: QtTest's QSKIP/QTRY_* macros are not pedantic-clean in C++17, so the test
#  executables use tt_warnings_tests: the same set without -Wpedantic)
add_library(tt_warnings INTERFACE)
add_library(tt_warnings_tests INTERFACE)
if(MSVC)
  target_compile_options(tt_warnings INTERFACE /W4 /WX /permissive- /Zc:__cplusplus)
  target_compile_options(tt_warnings_tests INTERFACE /W4 /WX /permissive- /Zc:__cplusplus)
else()
  target_compile_options(tt_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(tt_warnings_tests INTERFACE -Wall -Wextra -Werror)
endif()

# ---- the application library (everything but main.cpp: the tests link it) ----
file(GLOB_RECURSE TT_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(FILTER TT_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
add_library(ttlib STATIC ${TT_SOURCES})
target_include_directories(ttlib PUBLIC src src/core src/transport src/ui src/ui/widgets src/ui/pages)
target_include_directories(ttlib SYSTEM PUBLIC "${QWT_INCLUDE_DIR}")
target_compile_definitions(ttlib PUBLIC QWT_DLL QT_NO_KEYWORDS
  TT_VERSION="${PROJECT_VERSION}" TT_DEV_SIM_BRIDGE="${TT_SIM_BRIDGE}" TT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ttlib PUBLIC Qt6::Widgets Qt6::SerialBus Qt6::Svg "${QWT_LIBRARY}" PRIVATE tt_warnings)
file(GLOB TT_ICONS CONFIGURE_DEPENDS RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" resources/icons/*.svg)
qt_add_resources(ttlib "tt_resources" PREFIX "/" BASE resources FILES resources/theme.qss resources/app-icon.svg ${TT_ICONS})
qt_add_resources(ttlib "tt_protocol" PREFIX "/protocol" FILES ${TT_PROTOCOL_FILES})

# ---- the application ----
set(TT_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/resources/TractionTool.icns")
set_source_files_properties("${TT_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
qt_add_executable(TractionTool WIN32 MACOSX_BUNDLE src/main.cpp "${TT_ICNS}")
target_link_libraries(TractionTool PRIVATE ttlib tt_warnings)
set_target_properties(TractionTool PROPERTIES
  OUTPUT_NAME "TractionTool"
  MACOSX_BUNDLE_BUNDLE_NAME "Traction Tool"
  MACOSX_BUNDLE_ICON_FILE TractionTool.icns
  MACOSX_BUNDLE_COPYRIGHT "Proprietary. Qt (LGPL-3.0) and Qwt (LGPL-2.1 + Qwt exception): see NOTICES.md"
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.joulepoint.tractiontool"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

if(TT_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---- packaging (macOS): a self-contained .app (Qt, Qwt and the simulator bridge inside) and a .dmg ----
#   cmake --build --preset package            (= the package_dmg target of the macos-homebrew preset)
# The bundle is staged in <build>/package so the build tree's own .app stays a development build.
# Windows (windeployqt) and Linux (linuxdeploy + its Qt plugin) are documented in README.md; not run here.
if(APPLE)
  get_target_property(_tt_qmake Qt6::qmake IMPORTED_LOCATION)
  get_filename_component(_tt_qtbin "${_tt_qmake}" DIRECTORY)
  find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${_tt_qtbin}")
  set(TT_STAGE "${CMAKE_CURRENT_BINARY_DIR}/package")
  set(TT_DMG "${CMAKE_CURRENT_BINARY_DIR}/TractionTool-${PROJECT_VERSION}-macos-${CMAKE_SYSTEM_PROCESSOR}.dmg")
  set(TT_SRC_ZIP "${CMAKE_CURRENT_BINARY_DIR}/TractionTool-${PROJECT_VERSION}-corresponding-source.zip") # LGPL: shipped beside the .dmg
  # a dylib's own id still naming its Homebrew path is harmless for loading (the executable's rpath finds the bundled
  # copy) but wrong: point it into the bundle
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/fix-install-ids.cmake" [=[
file(GLOB libs "${APP}/Contents/Frameworks/*.dylib" "${APP}/Contents/Frameworks/*.framework/Versions/*/*")
foreach(l IN LISTS libs)
  if(IS_DIRECTORY "${l}" OR IS_SYMLINK "${l}")
    continue()
  endif()
  execute_process(COMMAND otool -D "${l}" OUTPUT_VARIABLE out ERROR_QUIET)
  string(REGEX MATCH "\n(/[^\n]+)" m "${out}")
  if(CMAKE_MATCH_1 MATCHES "^/(opt|usr/local)/")
    file(RELATIVE_PATH rel "${APP}/Contents/Frameworks" "${l}")
    execute_process(COMMAND install_name_tool -id "@executable_path/../Frameworks/${rel}" "${l}")
    message(STATUS "install id fixed: ${rel}")
  endif()
endforeach()
]=])
  if(MACDEPLOYQT_EXECUTABLE)
    add_custom_target(package_dmg
      COMMAND "${CMAKE_COMMAND}" -E rm -rf "${TT_STAGE}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${TT_STAGE}"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "$<TARGET_BUNDLE_DIR:TractionTool>" "${TT_STAGE}/Traction Tool.app"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${TT_SIM_BRIDGE}" "${TT_STAGE}/Traction Tool.app/Contents/MacOS/sim_bridge"
      COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${TT_STAGE}/Traction Tool.app" -always-overwrite
      # the LGPL release items (NOTICES.md): corresponding source + Qt attributions, then the bundled-library table from
      # what macdeployqt actually put into Contents/Frameworks — a library the script cannot attribute fails the package
      COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/licences.mjs" sources --out "${CMAKE_CURRENT_BINARY_DIR}/corresponding-source"
      COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/scripts/licences.mjs" table --app "${TT_STAGE}/Traction Tool.app"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/NOTICES.md" "${TT_STAGE}/Traction Tool.app/Contents/Resources/NOTICES.md"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${TT_STAGE}/Traction Tool.app/Contents/Resources/licenses"
      COMMAND "${CMAKE_COMMAND}" "-DAPP=${TT_STAGE}/Traction Tool.app" -P "${CMAKE_CURRENT_BINARY_DIR}/fix-install-ids.cmake"
      COMMAND codesign --force --deep --sign - "${TT_STAGE}/Traction Tool.app"
      COMMAND "${CMAKE_COMMAND}" -E create_symlink /Applications "${TT_STAGE}/Applications"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/NOTICES.md" "${TT_STAGE}/NOTICES.md"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/licenses" "${TT_STAGE}/Licenses"
      COMMAND "${CMAKE_COMMAND}" -E rm -f "${TT_DMG}" "${TT_SRC_ZIP}"
      COMMAND hdiutil create -volname "Traction Tool ${PROJECT_VERSION}" -srcfolder "${TT_STAGE}" -ov -format UDZO "${TT_DMG}"
      COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_COMMAND}" -E tar cf "${TT_SRC_ZIP}" --format=zip corresponding-source
      DEPENDS TractionTool
      COMMENT "Deploying Qt and Qwt into Traction Tool.app, attributing the bundle, writing ${TT_DMG} and the corresponding-source zip"
      VERBATIM)
  else()
    message(STATUS "macdeployqt not found: the package_dmg target is not available")
  endif()
endif()
