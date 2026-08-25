pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

import LoggerKit.Theme
import LoggerKit.Components
import TtvStudio.Components
import TtvStudio.Core

// Render pipeline page — create text→video jobs and follow their progress.
Item {
    id: root

    // RenderController singleton (TtvStudio.Core).
    required property RenderController controller

    readonly property var stateColors: ({
        "CREATED": AppColors.textSecondary,
        "VALIDATING": AppColors.accentColor,
        "TTS_RUNNING": AppColors.accentColor,
        "TTS_READY": AppColors.success,
        "PLANNING": AppColors.accentColor,
        "SCENES_READY": AppColors.success,
        "VIDEO_RUNNING": AppColors.accentColor,
        "CLIPS_READY": AppColors.success,
        "POST_PROCESSING": AppColors.accentColor,
        "VERIFYING": AppColors.accentColor,
        "COMPLETED": AppColors.success,
        "FAILED": AppColors.error,
        "CANCELLED": AppColors.error,
        "WAITING_FOR_PROVIDER": Material.color(Material.Orange),
        "UNKNOWN_PROVIDER_STATE": Material.color(Material.Orange)
    })

    function stateColor(state) {
        return root.stateColors[state] || AppColors.textSecondary
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        SectionHeader {
            title: qsTr("Render — text → video")
            Layout.fillWidth: true
        }

        InlineBanner {
            Layout.fillWidth: true
            visible: root.controller.lastError !== ""
            message: root.controller.lastError
            semantic: "error"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            // --- New job form --------------------------------------------
            ElevatedPane {
                Layout.fillWidth: true
                Layout.preferredHeight: formColumn.implicitHeight + 32

                ColumnLayout {
                    id: formColumn

                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 12

                    Label {
                        text: qsTr("New render job")
                        font: AppTypography.titleSmall
                    }

                    TextArea {
                        id: scriptInput

                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        placeholderText: qsTr("Nhập kịch bản thuyết minh…")
                        wrapMode: TextArea.Wrap
                        readOnly: root.controller.runActive
                    }

                    RowLayout {
                        spacing: 12

                        TextField {
                            id: languageInput

                            text: "vi"
                            implicitWidth: 72
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Narration language")
                        }

                        ComboBox {
                            id: aspectInput

                            model: ["16:9", "9:16", "1:1"]
                            implicitWidth: 110
                        }

                        ComboBox {
                            id: resolutionInput

                            model: ["720p", "1080p"]
                            implicitWidth: 110
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            text: qsTr("Create & run")
                            enabled: !root.controller.runActive
                                     && scriptInput.length > 0
                            onClicked: {
                                const jobId = root.controller.createRenderJob(
                                    scriptInput.text, languageInput.text,
                                    aspectInput.currentText, resolutionInput.currentText)
                                if (jobId !== "") {
                                    root.controller.runJob(jobId)
                                    scriptInput.clear()
                                }
                            }
                        }
                    }
                }
            }
        }

        // --- Active run progress ------------------------------------------
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

        // --- Job history ---------------------------------------------------
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
                        chipText: jobCard.state
                        chipFill: Qt.rgba(root.stateColor(jobCard.state).r,
                                          root.stateColor(jobCard.state).g,
                                          root.stateColor(jobCard.state).b, 0.18)
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
                message: qsTr("Chưa có job nào — nhập kịch bản phía trên để bắt đầu.")
            }
        }
    }
}
