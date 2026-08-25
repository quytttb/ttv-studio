pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

import LoggerKit.Theme
import LoggerKit.Components
import TtvStudio.Components
import TtvStudio.Core

// Redub pipeline page — existing video → new-language dub.
Item {
    id: root

    required property RenderController controller

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        SectionHeader {
            title: qsTr("Redub — video → lồng tiếng mới")
            Layout.fillWidth: true
        }

        InlineBanner {
            Layout.fillWidth: true
            visible: root.controller.lastError !== ""
            message: root.controller.lastError
            semantic: "error"
        }

        ElevatedPane {
            Layout.fillWidth: true
            implicitHeight: formColumn.implicitHeight + 32

            ColumnLayout {
                id: formColumn

                x: 16
                y: 16
                width: parent.width - 32
                spacing: 12

                Label {
                    text: qsTr("New redub job")
                    font: AppTypography.titleSmall
                }

                TextField {
                    id: sourceInput

                    Layout.fillWidth: true
                    placeholderText: qsTr("URL (Douyin/XHS/TikTok/YT) hoặc đường dẫn file MP4…")
                    readOnly: root.controller.runActive
                }

                RowLayout {
                    spacing: 12

                    TextField {
                        id: languageInput

                        text: "vi"
                        implicitWidth: 72
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Ngôn ngữ lồng tiếng")
                    }

                    Item { Layout.fillWidth: true }

                    AppButton {
                        text: qsTr("Create & run")
                        enabled: !root.controller.runActive && sourceInput.length > 0
                        onClicked: {
                            const jobId = root.controller.createRedubJob(
                                sourceInput.text, languageInput.text)
                            if (jobId !== "") {
                                root.controller.runJob(jobId)
                                sourceInput.clear()
                            }
                        }
                    }
                }
            }
        }

        ElevatedPane {
            Layout.fillWidth: true
            visible: root.controller.runActive
            implicitHeight: progressColumn.implicitHeight + 32

            ColumnLayout {
                id: progressColumn

                x: 16
                y: 16
                width: parent.width - 32
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Running %1").arg(root.controller.activeJobId)
                        font: AppTypography.titleSmall
                        Layout.fillWidth: true
                    }

                    Label {
                        text: root.controller.activeStage
                        font: AppTypography.labelLarge
                        color: AppColors.textSecondary
                    }

                    AppButton {
                        text: qsTr("Cancel")
                        onClicked: root.controller.cancelRun()
                    }
                }

                ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: root.controller.scenesTotal > 0 ? root.controller.scenesTotal : 1
                    value: root.controller.scenesDone
                    indeterminate: root.controller.scenesTotal === 0
                }
            }
        }

        SectionHeader {
            title: qsTr("Jobs")
            Layout.fillWidth: true
        }

        ListView {
            id: jobList

            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 8
            model: root.controller.jobs

            delegate: Rectangle {
                id: jobCard

                required property string jobId
                required property string kind
                required property string state
                required property real createdAtMs

                width: jobList.width
                height: 56
                radius: 12
                color: AppColors.surfaceContainer
                opacity: jobCard.kind === "redub" ? 1.0 : 0.35

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label {
                        text: jobCard.jobId
                        font: AppTypography.bodyMedium
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: 220
                    }

                    StatusChip {
                        chipText: jobCard.kind
                    }

                    StatusChip {
                        chipText: jobCard.state
                        chipFill: Qt.rgba(AppColors.accentColor.r, AppColors.accentColor.g,
                                          AppColors.accentColor.b, 0.18)
                    }

                    Item { Layout.fillWidth: true }

                    Label {
                        text: new Date(jobCard.createdAtMs).toLocaleString()
                        font: AppTypography.bodySmall
                        color: AppColors.textSecondary
                    }

                    AppButton {
                        text: qsTr("Run")
                        enabled: !root.controller.runActive
                                 && jobCard.kind === "redub"
                                 && (jobCard.state === "CREATED"
                                     || jobCard.state === "FAILED"
                                     || jobCard.state === "WAITING_FOR_PROVIDER")
                        visible: enabled
                        onClicked: root.controller.runJob(jobCard.jobId)
                    }
                }
            }

            EmptyStatePlaceholder {
                anchors.fill: parent
                visible: jobList.count === 0
                message: qsTr("Chưa có job nào — dán URL hoặc chọn file phía trên.")
            }
        }
    }
}
