pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LoggerKit.Components
import LoggerKit.Theme

// Horizontal pipeline stepper. Feed it an array of
//   { label: string, status: "done" | "active" | "pending" | "error" }
// entries; the connector fills up to the last non-pending stage.
Item {
    id: root

    property var stages: []
    readonly property int circleSize: 22

    TextMetrics {
        id: labelMetrics
        font: AppTypography.labelMedium
    }

    implicitHeight: circleSize + labelMetrics.height + 10

    RowLayout {
        id: row

        anchors.fill: parent
        spacing: 0

        Repeater {
            model: root.stages

            delegate: Item {
                id: step

                required property var modelData
                required property int index

                readonly property bool isLast: index === root.stages.length - 1
                readonly property string status: modelData.status

                Layout.fillWidth: !isLast
                Layout.preferredWidth: isLast ? stepLabel.implicitWidth + root.circleSize : 0
                implicitHeight: root.implicitHeight

                RowLayout {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8

                    Rectangle {
                        id: badge

                        width: root.circleSize
                        height: root.circleSize
                        radius: width / 2
                        color: step.status === "done"
                                   ? AppColors.success
                                   : step.status === "active"
                                       ? AppColors.accentColor
                                       : step.status === "error"
                                             ? AppColors.error
                                             : "transparent"
                        border.width: step.status === "pending" ? 2 : 1
                        border.color: step.status === "pending"
                                          ? AppColors.dividerLine
                                          : badge.color

                        Label {
                            anchors.centerIn: parent
                            font.pixelSize: 11
                            font.bold: true
                            color: step.status === "pending"
                                       ? AppColors.onSurfaceVariant
                                       : AppColors.onPrimary
                            text: step.status === "done"
                                      ? "\u2713"
                                      : step.status === "error"
                                            ? "!"
                                            : String(step.index + 1)
                        }
                    }

                    Label {
                        id: stepLabel

                        text: step.modelData.label
                        font: AppTypography.labelMedium
                        color: step.status === "active"
                                   ? AppColors.primaryText
                                   : AppColors.onSurfaceVariant
                    }

                    UiIcon {
                        visible: !step.isLast
                        name: "chevronRight"
                        size: 14
                        iconColor: AppColors.dividerLine
                    }
                }
            }
        }
    }
}
