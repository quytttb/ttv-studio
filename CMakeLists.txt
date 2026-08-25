cmake_minimum_required(VERSION 3.16)

project(ttv_studio VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Single source of truth for build-time versions ---------------------------
# Bump these in one place; everything else (find_package, qt_standard_project_setup,
# packaging scripts, QML module VERSION) reads from them.
set(QT_VERSION_REQUIRED "6.11" CACHE STRING "Minimum Qt version")
set(QML_MODULE_VERSION  "1.0"  CACHE STRING "QML module URI version for qt_add_qml_module")

# The patch version (6.11.x) and the install path must agree. See
# packaging/linux/{install_qt_aqt.sh,ci_configure.sh,resolve_qt_prefix.sh}
# and packaging/windows/{install_qt_aqt.ps1,build_installer.ps1} — update
# both sides together.
find_package(Qt6 ${QT_VERSION_REQUIRED} REQUIRED COMPONENTS
    Quick
    QuickControls2
    Qml
    Svg
    Network
    Test
)

qt_standard_project_setup(REQUIRES ${QT_VERSION_REQUIRED})

# Silence QTP0004 — generate a qmldir per QML source subdirectory so components
# resolve each other via the implicit import.
qt_policy(SET QTP0004 NEW)

# Generate src/utils/Version.h with kAppMajor/kAppMinor/kAppPatch from
# project(VERSION) above. Output goes to the build tree (see utils/CMakeLists.txt
# for include path).
configure_file(
    ${CMAKE_SOURCE_DIR}/src/utils/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/Version_generated.h
    @ONLY
)

# Shared CMake helpers (Qt Test boilerplate, etc.).
include(${CMAKE_SOURCE_DIR}/cmake/TtvStudioTest.cmake)

# Aggregate a `qmllint` target across every QML module so CI can lint every
# `.qml` file in one shot via `cmake --build build --target qmllint`.
add_custom_target(qmllint)
foreach(_module IN ITEMS ttv_studio_components ttv_studio_app)
    if(TARGET "${_module}_qmllint")
        add_dependencies(qmllint "${_module}_qmllint")
    endif()
endforeach()
# Qt 6.11 qmllint / Qt Creator expect QML module output under the URI target path
# (TtvStudio.Core → …/TtvStudio/Core). Static qt_add_qml_module targets otherwise
# default to CMAKE_CURRENT_BINARY_DIR only (e.g. build/.../src/core).
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}" CACHE PATH
    "QML build-tree root; qt_add_qml_module appends URI path segments")

include(GNUInstallDirs)

# Shared UI kit (git submodule) — provides LoggerKit.Theme + LoggerKit.Components.
# Built as STATIC QML modules; version tracks this app's QML module version.
set(LOGGERKIT_QML_MODULE_VERSION ${QML_MODULE_VERSION})
add_subdirectory(shared/logger-ui-kit)

# Qt recommended layout — see cmake-get-started.html
# Layers: utils → media (external binaries) → jobs (state machine, registry)
# → components (reusable QML) → app (shell). Provider REST clients and the
# pipeline engines plug in above jobs.
add_subdirectory(src/utils)
add_subdirectory(src/media)
add_subdirectory(src/providers)
add_subdirectory(src/jobs)
add_subdirectory(src/render)
add_subdirectory(src/core)
add_subdirectory(src/components)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)

include(${CMAKE_SOURCE_DIR}/cmake/CPackOptions.cmake)
