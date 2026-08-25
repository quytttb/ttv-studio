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

    function relTime(ms) {
        const delta = Date.now() - ms
        if (delta < 60_000)
            return qsTr("vừa xong")
        if (delta < 3_600_000)
            return qsTr("%1 phút trước").arg(Math.floor(delta / 60_000))
        if (delta < 86_400_000)
            return qsTr("%1 giờ trước").arg(Math.floor(delta / 3_600_000))
        return new Date(ms).toLocaleDateString()
    }

    // Map the raw pipeline state onto the display stepper groups.
    function stageGroups() {
        return [
                    { label: qsTr("Kịch bản"), states: ["CREATED", "VALIDATING"] },
                    { label: qsTr("TTS"), states: ["TTS_RUNNING", "TTS_READY"] },
                    { label: qsTr("Cảnh"), states: ["PLANNING", "SCENES_READY"] },
                    { label: qsTr("Video"), states: ["VIDEO_RUNNING", "CLIPS_READY"] },
                    { label: qsTr("Ghép"), states: ["POST_PROCESSING", "VERIFYING"] }
                ]
    }

    function stepperModel(state) {
        const groups = root.stageGroups()
        let current = -1
        for (let i = 0; i < groups.length; ++i)
            if (groups[i].states.indexOf(state) >= 0)
                current = i
        const finished = state === "COMPLETED"
        const failed = state === "FAILED" || state === "CANCELLED"
        return groups.map(function (g, i) {
            let status = "pending"
            if (finished || i < current)
                status = "done"
            else if (i === current)
                status = failed ? "error" : "active"
            return { "label": g.label, "status": status }
        })
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
            Layout.fillHeight: true
            spacing: 16

            // --- Left column: form + history --------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.maximumWidth: root.width * 0.58
                spacing: 16

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
                            text: qsTr("Kịch bản mới")
                            font: AppTypography.titleSmall
                        }

                        TextArea {
                            id: scriptInput

                            Layout.fillWidth: true
                            implicitHeight: 132
                            placeholderText: qsTr(
                                "Nhập kịch bản thuyết minh — văn bản tự do, " +
                                "pipeline sẽ tự chia cảnh theo ngữ nghĩa…")
                            wrapMode: TextArea.Wrap
                            readOnly: root.controller.runActive
                            background: Rectangle {
                                radius: AppTheme.listItemRadius
                                color: AppColors.surfaceContainerLow
                                border.width: parent.activeFocus ? 1 : 0
                                border.color: AppColors.accentColor
                            }
                        }

                        RowLayout {
                            spacing: 12
                            Layout.fillWidth: true

                            TextField {
                                id: languageInput

                                text: "vi"
                                implicitWidth: 72
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Ngôn ngữ thuyết minh")
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

                            Label {
                                text: scriptInput.length
                                      ? qsTr("%1 ký tự").arg(scriptInput.length)
                                      : ""
                                font: AppTypography.bodySmall
                                color: AppColors.onSurfaceVariant
                            }

                            AppButton {
                                text: qsTr("Tạo & chạy")
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

                Label {
                    text: qsTr("Lịch sử job")
                    font: AppTypography.titleMedium
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
                        height: 60
                        radius: AppTheme.cardRadius
                        color: AppColors.surfaceContainerLow

                        Rectangle {
                            anchors.left: parent.left
                            anchors.topMargin: 10
                            anchors.bottomMargin: 10
                            anchors.leftMargin: 0
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            radius: 2
                            color: root.stateColor(jobCard.state)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            anchors.leftMargin: 16
                            spacing: 12

                            Rectangle {
                                width: 30
                                height: 30
                                radius: 15
                                color: AppColors.withAlpha(AppColors.accentColor, 0.14)

                                Label {
                                    anchors.centerIn: parent
                                    text: "R"
                                    font: AppTypography.labelLarge
                                    color: AppColors.accentColor
                                }
                            }

                            ColumnLayout {
                                spacing: 2
                                Layout.fillWidth: true

                                Label {
                                    text: jobCard.jobId
                                    font: AppTypography.bodyMedium
                                    elide: Text.ElideMiddle
                                    Layout.maximumWidth: 220
                                }

                                Label {
                                    text: root.relTime(jobCard.createdAtMs)
                                    font: AppTypography.bodySmall
                                    color: AppColors.onSurfaceVariant
                                }
                            }

                            StatusChip {
                                chipText: jobCard.state
                                chipFill: Qt.rgba(root.stateColor(jobCard.state).r,
                                                  root.stateColor(jobCard.state).g,
                                                  root.stateColor(jobCard.state).b, 0.18)
                            }

                            AppButton {
                                text: qsTr("Chạy")
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

            // --- Right column: live run / guidance --------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                ElevatedPane {
                    visible: root.controller.runActive
                    Layout.fillWidth: true
                    implicitHeight: progressColumn.implicitHeight + 32

                    ColumnLayout {
                        id: progressColumn

                        x: 16
                        y: 16
                        width: parent.width - 32
                        spacing: 14

                        RowLayout {
                            Layout.fillWidth: true

                            Label {
                                text: qsTr("Đang chạy")
                                font: AppTypography.titleSmall
                                Layout.fillWidth: true
                            }

                            AppButton {
                                text: qsTr("Huỷ")
                                kind: AppButton.Secondary
                                onClicked: root.controller.cancelRun()
                            }
                        }

                        Label {
                            text: root.controller.activeJobId
                            font: AppTypography.labelLarge
                            color: AppColors.onSurfaceVariant
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }

                        StageStepper {
                            Layout.fillWidth: true
                            stages: root.stepperModel(root.controller.activeStage)
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            from: 0
                            to: root.controller.scenesTotal > 0
                                    ? root.controller.scenesTotal : 1
                            value: root.controller.scenesDone
                            indeterminate: root.controller.scenesTotal === 0
                        }

                        Label {
                            text: qsTr("Clip %1/%2")
                                      .arg(root.controller.scenesDone)
                                      .arg(root.controller.scenesTotal)
                            font: AppTypography.bodySmall
                            color: AppColors.onSurfaceVariant
                        }

                        Flow {
                            visible: root.controller.scenesTotal > 0
                            spacing: 6
                            Layout.fillWidth: true

                            Repeater {
                                model: root.controller.scenesTotal

                                delegate: Rectangle {
                                    required property int index

                                    width: 28
                                    height: 22
                                    radius: 6
                                    color: index < root.controller.scenesDone
                                               ? AppColors.withAlpha(AppColors.success, 0.20)
                                               : AppColors.surfaceContainer

                                    Label {
                                        anchors.centerIn: parent
                                        text: parent.index + 1
                                        font.pixelSize: 10
                                        color: index < root.controller.scenesDone
                                                   ? AppColors.success
                                                   : AppColors.onSurfaceVariant
                                    }
                                }
                            }
                        }
                    }
                }

                ElevatedPane {
                    visible: !root.controller.runActive
                    Layout.fillWidth: true
                    implicitHeight: guideColumn.implicitHeight + 32

                    ColumnLayout {
                        id: guideColumn

                        x: 16
                        y: 16
                        width: parent.width - 32
                        spacing: 10

                        Label {
                            text: qsTr("Quy trình")
                            font: AppTypography.titleSmall
                        }

                        StageStepper {
                            Layout.fillWidth: true
                            stages: [
                                { "label": qsTr("Kịch bản"), "status": "pending" },
                                { "label": qsTr("TTS"), "status": "pending" },
                                { "label": qsTr("Cảnh"), "status": "pending" },
                                { "label": qsTr("Video"), "status": "pending" },
                                { "label": qsTr("Ghép"), "status": "pending" }
                            ]
                        }

                        Label {
                            text: qsTr("Mỗi cảnh được tạo prompt hình ảnh riêng, sinh clip qua video gateway rồi chuẩn hoá về cùng fps/độ phân giải trước khi ghép. Job lưu trạng thái liên tục — ngắt giữa chừng có thể tiếp tục mà không mất clip đã trả phí.")
                            wrapMode: Label.Wrap
                            font: AppTypography.bodySmall
                            color: AppColors.onSurfaceVariant
                            Layout.fillWidth: true
                        }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }
    }
}
