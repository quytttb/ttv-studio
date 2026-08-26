# Reusable QML components — module URI TtvStudio.Components.
# App-specific components only. Generic shell/layout/table/chart/status chrome
# lives in the shared kit (LoggerKit.Components).
#
# Subfolders (filesystem only; types stay on TtvStudio.Components):
#   shell/   — app chrome (nav rail, top bar, window controls)
#   layout/  — page layout (stat cards, banners, section headers, dialogs)

qt_add_qml_module(ttv_studio_components
    URI TtvStudio.Components
    VERSION ${QML_MODULE_VERSION}
    STATIC
    QML_FILES
        # shell
        shell/NavItem.qml
        shell/AppNavigationRail.qml
        shell/RailCircleButton.qml
        shell/WindowControls.qml
        # layout
        layout/SectionHeader.qml
        layout/InlineBanner.qml
        layout/StageStepper.qml
    DEPENDENCIES
        LoggerKit.Theme
        LoggerKit.Components
    RESOURCES
        ../../resources/icons/studio.svg
)

add_library(ttv_studio::components ALIAS ttv_studio_components)
