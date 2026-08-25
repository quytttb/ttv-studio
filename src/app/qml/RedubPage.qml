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

    readonly property color waitingColor: Material.color(Material.Orange)

    function stateColor(state) {
        if (state === "COMPLETED")
            return AppColors.success
        if (state === "FAILED" || state === "CANCELLED")
            return AppColors.error
        if (state === "WAITING_FOR_PROVIDER" || state === "UNKNOWN_PROVIDER_STATE")
            return root.waitingColor
        if (state === "CREATED")
            return AppColors.textSecondary
        return AppColors.accentColor
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

    function stepperModel(state) {
        const groups = [
                    { label: qsTr("Nguồn"), states: ["CREATED", "VALIDATING", "INGESTING", "SOURCE_READY"] },
                    { label: qsTr("STT"), states: ["TRANSCRIBING", "TRANSCRIPT_READY"] },
                    { label: qsTr("Dịch"), states: ["TRANSLATING", "TRANSLATION_READY"] },
                    { label: qsTr("TTS"), states: ["TTS_RUNNING", "TTS_READY"] },
                    { label: qsTr("Ghép"), states: ["PLANNING", "SCENES_READY", "VIDEO_RUNNING",
                                                    "CLIPS_READY", "POST_PROCESSING", "VERIFYING"] }
                ]
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
            title: qsTr("Redub — video → lồng tiếng mới")
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
                            text: qsTr("Job redub mới")
                            font: AppTypography.titleSmall
                        }

                        TextField {
                            id: sourceInput

                            Layout.fillWidth: true
                            placeholderText: qsTr(
                                "URL (Douyin/XHS/TikTok/YT) hoặc đường dẫn file MP4…")
                            readOnly: root.controller.runActive
                        }

                        RowLayout {
                            spacing: 12
                            Layout.fillWidth: true

                            TextField {
                                id: languageInput

                                text: "vi"
                                implicitWidth: 72
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Ngôn ngữ lồng tiếng")
                            }

                            Item { Layout.fillWidth: true }

                            Label {
                                visible: sourceInput.length > 0
                                         && !root.controller.runActive
                                text: qsTr("Xoá")
                                font: AppTypography.labelLarge
                                color: AppColors.accentColor
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: sourceInput.clear()
                                }
                            }

                            AppButton {
                                text: qsTr("Tạo & chạy")
                                enabled: !root.controller.runActive
                                         && sourceInput.length > 0
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

                        readonly property bool isRedub: kind === "redub"

                        width: jobList.width
                        height: 60
                        radius: AppTheme.cardRadius
                        color: jobCard.isRedub ? AppColors.surfaceContainerLow
                                               : AppColors.surfaceContainer
                        opacity: jobCard.isRedub ? 1.0 : 0.45

                        Rectangle {
                            anchors.left: parent.left
                            anchors.topMargin: 10
                            anchors.bottomMargin: 10
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
                                color: AppColors.withAlpha(AppColors.primaryColor, 0.14)

                                Label {
                                    anchors.centerIn: parent
                                    text: "D"
                                    font: AppTypography.labelLarge
                                    color: AppColors.primaryColor
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
                                         && jobCard.isRedub
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
                            text: qsTr("Phân đoạn %1/%2")
                                      .arg(root.controller.scenesDone)
                                      .arg(root.controller.scenesTotal)
                            font: AppTypography.bodySmall
                            color: AppColors.onSurfaceVariant
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
                                { "label": qsTr("Nguồn"), "status": "pending" },
                                { "label": qsTr("STT"), "status": "pending" },
                                { "label": qsTr("Dịch"), "status": "pending" },
                                { "label": qsTr("TTS"), "status": "pending" },
                                { "label": qsTr("Ghép"), "status": "pending" }
                            ]
                        }

                        Label {
                            text: qsTr("Whisper local chép lời từ audio gốc, LLM dịch theo độ dài từng phân đoạn để giữ nhịp, rồi TTS lồng lại trên đúng timeline gốc.")
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
