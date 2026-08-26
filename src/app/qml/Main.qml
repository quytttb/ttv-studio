pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

import TtvStudio.Components
import TtvStudio.Core
import LoggerKit.Theme
import LoggerKit.Components

ApplicationWindow {
    id: root

    RenderController {
        id: renderController
    }


    readonly property var targetScreen: root.screen || Screen
    readonly property real windowScreenFraction: 0.8

    width: targetScreen
           ? Math.round(targetScreen.availableWidth * windowScreenFraction)
           : Math.round(Screen.desktopAvailableWidth * windowScreenFraction)
    height: targetScreen
            ? Math.round(targetScreen.availableHeight * windowScreenFraction)
            : Math.round(Screen.desktopAvailableHeight * windowScreenFraction)
    minimumWidth: 1024
    minimumHeight: 700
    visible: true
    visibility: Window.Maximized
    title: qsTr("TTV Studio")

    flags: Qt.Window | (Qt.platform.os === "windows" ? 0 : Qt.FramelessWindowHint) | Qt.WindowSystemMenuHint

    Material.theme:   AppTheme.materialTheme
    Material.accent:  AppTheme.accent
    Material.primary: AppTheme.primary
    color: AppColors.surface

    property string currentView: "render"

    function centerOnTargetScreen() {
        const scr = root.screen || Screen
        if (!scr)
            return
        x = scr.virtualX + Math.round((scr.availableWidth - width) / 2)
        y = scr.virtualY + Math.round((scr.availableHeight - height) / 2)
    }

    Component.onCompleted: {
        centerOnTargetScreen()
        UpdateController.scheduleStartupCheck()
    }

    RowLayout {
        id: shellRow

        anchors.fill: parent
        spacing: 0

        AppNavigationRail {
            id: navigationRail

            Layout.fillHeight: true
            currentView: root.currentView

            onNavigate: function (view) {
                root.currentView = view
            }
        }

        ColumnLayout {
            id: contentColumn

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                id: viewStack

                Layout.fillWidth: true
                Layout.fillHeight: true

                currentIndex: root.currentView === "redub"    ? 1
                            : root.currentView === "settings" ? 2
                            : 0

                // Render pipeline (text -> new video).
                RenderPage {
                    controller: renderController
                }

                // Redub pipeline (existing video -> new narration).
                RedubPage {
                    controller: renderController
                }

                // Provider endpoints, tools and render device configuration.
                SettingsPage {
                }
            }
        }
    }
}
