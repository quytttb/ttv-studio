# Desktop shell — QML module URI TtvStudio.App.
# Binary "ttv-studio", target "app" (so the OUTPUT_NAME does not collide
# with the QML module folder).

qt_add_executable(app
    main.cpp
)

# ThemeSetup.h ships with the shared kit (LoggerKit.Theme).
target_include_directories(app PRIVATE
    ${CMAKE_SOURCE_DIR}/shared/logger-ui-kit/src/theme
)

qt_add_qml_module(app
    URI TtvStudio.App
    VERSION ${QML_MODULE_VERSION}
    QML_FILES
        qml/Main.qml
        qml/RenderPage.qml
        qml/RedubPage.qml
        qml/SettingsPage.qml
    DEPENDENCIES
        LoggerKit.Theme
        LoggerKit.Components
        TtvStudio.Core
        ttv_studio_components
)

set_target_properties(app PROPERTIES
    OUTPUT_NAME ttv_studio
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

target_link_libraries(app
    PRIVATE
        Qt6::Quick
        Qt6::QuickControls2
        ttv_studio_componentsplugin
        ttv_studio_coreplugin
        logger_ui_kit_themeplugin
        logger_ui_kit_componentsplugin
        LoggerKit::theme_setup
        utils
)

# Linux/Windows packaging: Qt CMake Deployment API + CPack DEB (see cmake/CPackOptions.cmake)
# qt_generate_deploy_app_script (Qt6Core) does NOT deploy QML imports on Linux — it only
# deploys shared libs and C++ plugins.  qt_generate_deploy_qml_app_script (Qt6Qml) is the
# correct function for QML apps; it deploys runtime deps + QML modules in one shot.
qt_generate_deploy_qml_app_script(
    TARGET app
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(TARGETS app
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Deploy Qt runtime (libs, plugins, QML modules) into the private prefix.
install(SCRIPT ${deploy_script})

# System-integration files use absolute destinations so they land under
# /usr/share and /usr/bin regardless of CPACK_PACKAGING_INSTALL_PREFIX.
install(PROGRAMS ${CMAKE_SOURCE_DIR}/packaging/linux/ttv-studio
    DESTINATION /usr/bin
)

install(FILES ${CMAKE_SOURCE_DIR}/packaging/linux/ttv-studio.desktop
    DESTINATION /usr/share/applications
)

install(FILES ${CMAKE_SOURCE_DIR}/resources/icons/studio.svg
    DESTINATION /usr/share/icons/hicolor/scalable/apps
    RENAME ttv-studio.svg
)
