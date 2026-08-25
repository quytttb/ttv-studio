pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import QtQuick.Window

import LoggerKit.Theme

// Fixed 80px Material-style navigation rail (icon above label per destination).
Item {
    id: rail

    property string currentView: "render"

    signal navigate(string view)

    implicitWidth: AppTheme.railWidth

    Rectangle {
        anchors.fill: parent
        color: AppColors.navRail

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 1
            color: AppColors.dividerLine
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 76

            Image {
                id: brandImage

                anchors.centerIn: parent
                source: "qrc:/qt/qml/TtvStudio/Components/resources/icons/studio.svg"
                sourceSize: Qt.size(60, 60)
                fillMode: Image.PreserveAspectFit
                layer.enabled: true
                // Source artwork is solid black — tint it for the active theme.
                layer.effect: MultiEffect {
                    colorization: 1
                    colorizationColor: AppColors.isLight ? "#1C1B1F" : "#E3E2E6"
                }
            }

            MouseArea {
                anchors.fill: parent
                onPressed: mouse => {
                    if (mouse.button === Qt.LeftButton && Window.window)
                        Window.window.startSystemMove()
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: AppTheme.navItemSpacing

            NavItem {
                viewName: "render"
                label: qsTr("Render")
                iconName: "playArrow"
                active: rail.currentView === "render"
                onNavigate: view => rail.navigate(view)
            }
            NavItem {
                viewName: "redub"
                label: qsTr("Redub")
                iconName: "restartAlt"
                active: rail.currentView === "redub"
                onNavigate: view => rail.navigate(view)
            }
            NavItem {
                viewName: "settings"
                label: qsTr("Settings")
                iconName: "cog"
                active: rail.currentView === "settings"
                onNavigate: view => rail.navigate(view)
            }
        }

        Item { Layout.fillHeight: true }

        WindowControls {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 16
        }
    }
}
