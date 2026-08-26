pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

import LoggerKit.Theme
import LoggerKit.Components
import TtvStudio.Components
import TtvStudio.Core

// Provider endpoints, models, tool locations and the render device.
Item {
    id: root

    function backendLabel(id) {
        for (let i = 0; i < RenderDeviceController.backends.length; ++i) {
            const b = RenderDeviceController.backends[i]
            if (b.id === id)
                return b.label
        }
        return id
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        SectionHeader {
            title: qsTr("Settings")
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 16
            Layout.fillWidth: true

            ElevatedPane {
                Layout.fillWidth: true
                implicitHeight: llmColumn.implicitHeight + 32

                ColumnLayout {
                    id: llmColumn

                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 10

                    Label {
                        text: qsTr("LLM (OpenAI-compatible)")
                        font: AppTypography.titleSmall
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Base URL")
                        text: SettingsStore.llmBaseUrl
                        onTextEdited: SettingsStore.llmBaseUrl = text
                    }

                    TextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("API key")
                        text: SettingsStore.llmApiKey
                        onTextEdited: SettingsStore.llmApiKey = text
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Model (vd. gpt-4o-mini)")
                        text: SettingsStore.llmModel
                        onTextEdited: SettingsStore.llmModel = text
                    }

                    Label {
                        text: qsTr("Local TTS (:3900)")
                        font: AppTypography.titleSmall
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Base URL")
                        text: SettingsStore.ttsBaseUrl
                        onTextEdited: SettingsStore.ttsBaseUrl = text
                    }
                }
            }

            ElevatedPane {
                Layout.fillWidth: true
                implicitHeight: gatewayColumn.implicitHeight + 32

                ColumnLayout {
                    id: gatewayColumn

                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 10

                    Label {
                        text: qsTr("Video gateway (:8765)")
                        font: AppTypography.titleSmall
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Base URL")
                        text: SettingsStore.videoGatewayBaseUrl
                        onTextEdited: SettingsStore.videoGatewayBaseUrl = text
                    }

                    TextField {
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: qsTr("X-API-Key (tuỳ chọn)")
                        text: SettingsStore.videoGatewayApiKey
                        onTextEdited: SettingsStore.videoGatewayApiKey = text
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Video model (bắt buộc để render)")
                        text: SettingsStore.videoModel
                        onTextEdited: SettingsStore.videoModel = text
                    }

                    Label {
                        text: qsTr("Whisper (STT local)")
                        font: AppTypography.titleSmall
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Đường dẫn binary whisper-cli")
                        text: SettingsStore.whisperBin
                        onTextEdited: SettingsStore.whisperBin = text
                    }

                    TextField {
                        Layout.fillWidth: true
                        placeholderText: qsTr("Đường dẫn model ggml .bin")
                        text: SettingsStore.whisperModel
                        onTextEdited: SettingsStore.whisperModel = text
                    }
                }
            }
        }

        // --- Render device (GPU / CPU) ------------------------------------
        // Hidden entirely on machines where no hardware encoder passes its
        // probe encode — CPU is then the only meaningful choice.
        ElevatedPane {
            visible: RenderDeviceController.gpuAvailable
                     || RenderDeviceController.scanning
            Layout.fillWidth: true
            implicitHeight: deviceColumn.implicitHeight + 32

            ColumnLayout {
                id: deviceColumn

                x: 16
                y: 16
                width: parent.width - 32
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Thiết bị render")
                        font: AppTypography.titleSmall
                        Layout.fillWidth: true
                    }

                    BusyIndicator {
                        visible: RenderDeviceController.scanning
                        implicitWidth: 22
                        implicitHeight: 22
                    }
                }

                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true

                    ComboBox {
                        id: backendCombo

                        readonly property int selectedIndex: {
                            const sel = RenderDeviceController.selectedBackend
                            for (let i = 0; i < RenderDeviceController.backends.length; ++i)
                                if (RenderDeviceController.backends[i].id === sel)
                                    return i
                            return 0
                        }

                        currentIndex: selectedIndex
                        textRole: "label"
                        model: RenderDeviceController.backends.filter(
                                   b => b.usable).map(
                                   b => ({ "id": b.id, "label": b.label }))
                        implicitWidth: 260
                        enabled: !RenderDeviceController.scanning
                                 && !RenderDeviceController.testRunning
                        onActivated: function (index) {
                            RenderDeviceController.selectedBackend =
                                model[index].id
                        }
                    }

                    AppButton {
                        text: RenderDeviceController.testRunning
                                  ? qsTr("Đang test…")
                                  : qsTr("Test render")
                        enabled: !RenderDeviceController.testRunning
                                 && !RenderDeviceController.scanning
                        onClicked: RenderDeviceController.testSelected()
                    }

                    Item { Layout.fillWidth: true }
                }

                InlineBanner {
                    visible: RenderDeviceController.testMessage !== ""
                             && !RenderDeviceController.testRunning
                    message: qsTr("Encoder %1 · %2")
                                 .arg(root.backendLabel(
                                          RenderDeviceController.selectedBackend))
                                 .arg(RenderDeviceController.testMessage)
                    semantic: RenderDeviceController.testMessage.startsWith("OK")
                                  ? "success" : "error"
                }

                Label {
                    text: qsTr("Hardware encoder được kiểm tra bằng một lượt encode thử ngắn; chỉ những backend pass mới xuất hiện ở danh sách.")
                    wrapMode: Label.Wrap
                    color: AppColors.textSecondary
                    font: AppTypography.bodySmall
                    Layout.fillWidth: true
                }
            }
        }

        // --- Application / updates ----------------------------------------
        ElevatedPane {
            Layout.fillWidth: true
            implicitHeight: updateColumn.implicitHeight + 32

            ColumnLayout {
                id: updateColumn

                x: 16
                y: 16
                width: parent.width - 32
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true

                    Label {
                        text: qsTr("Ứng dụng")
                        font: AppTypography.titleSmall
                        Layout.fillWidth: true
                    }

                    Label {
                        text: qsTr("Phiên bản %1").arg(UpdateController.currentVersion)
                        font: AppTypography.bodySmall
                        color: AppColors.onSurfaceVariant
                    }
                }

                RowLayout {
                    spacing: 12
                    Layout.fillWidth: true

                    AppButton {
                        text: UpdateController.busy && UpdateController.state === "checking"
                                  ? qsTr("Đang kiểm tra…")
                                  : qsTr("Kiểm tra cập nhật")
                        enabled: !UpdateController.busy
                        onClicked: UpdateController.checkForUpdates()
                    }

                    Label {
                        visible: UpdateController.state === "uptodate"
                        text: qsTr("Bạn đang dùng bản mới nhất (%1)")
                                  .arg(UpdateController.currentVersion)
                        font: AppTypography.bodyMedium
                        color: AppColors.success
                    }

                    Item { Layout.fillWidth: true }
                }

                InlineBanner {
                    visible: UpdateController.state === "available"
                    Layout.fillWidth: true
                    semantic: "info"
                    message: qsTr("Có phiên bản mới: %1")
                                 .arg(UpdateController.latestVersion)
                }

                ColumnLayout {
                    visible: UpdateController.state === "available"
                              || UpdateController.state === "downloading"
                    spacing: 8
                    Layout.fillWidth: true

                    TextArea {
                        visible: UpdateController.releaseNotes !== ""
                                 && UpdateController.state === "available"
                        text: UpdateController.releaseNotes
                        readOnly: true
                        wrapMode: TextArea.Wrap
                        implicitHeight: Math.min(contentHeight, 120)
                        font: AppTypography.bodySmall
                        color: AppColors.primaryText
                        background: Rectangle {
                            radius: AppTheme.listItemRadius
                            color: AppColors.surfaceContainerLow
                        }
                    }

                    ProgressBar {
                        visible: UpdateController.state === "downloading"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: UpdateController.downloadProgress < 0 ? 0
                                                                     : UpdateController.downloadProgress
                        indeterminate: UpdateController.downloadProgress < 0
                    }

                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true

                        Label {
                            visible: UpdateController.state === "downloading"
                            text: UpdateController.downloadProgress < 0
                                      ? qsTr("Đang tải…")
                                      : qsTr("Đang tải… %1%")
                                            .arg(Math.round(
                                                     UpdateController.downloadProgress * 100))
                            font: AppTypography.bodySmall
                            color: AppColors.onSurfaceVariant
                        }

                        Item { Layout.fillWidth: true }

                        AppButton {
                            visible: UpdateController.state === "available"
                            text: qsTr("Tải bản mới")
                            onClicked: UpdateController.downloadUpdate()
                        }

                        AppButton {
                            visible: UpdateController.state === "downloaded"
                                     || UpdateController.state === "installed"
                            text: UpdateController.state === "downloaded"
                                      ? qsTr("Cài đặt ngay")
                                      : qsTr("Đã cài — khởi động lại để dùng bản mới")
                            enabled: UpdateController.state === "downloaded"
                            onClicked: UpdateController.installUpdate()
                        }
                    }
                }

                InlineBanner {
                    visible: UpdateController.state === "error"
                              && UpdateController.errorMessage !== ""
                    Layout.fillWidth: true
                    semantic: "error"
                    message: UpdateController.errorMessage
                }

                Label {
                    text: qsTr("Linux: bản cài .deb được nâng cấp qua pkexec/apt. Windows: installer chạy sau khi tải xong và thay thế ứng dụng.")
                    wrapMode: Label.Wrap
                    font: AppTypography.bodySmall
                    color: AppColors.onSurfaceVariant
                    Layout.fillWidth: true
                }
            }
        }

        InlineBanner {
            Layout.fillWidth: true
            message: qsTr("Môi trường env (TTV_*) vẫn ưu tiên hơn giá trị lưu tại đây.")
            semantic: "info"
        }

        Item { Layout.fillHeight: true }
    }

    Component.onCompleted: RenderDeviceController.rescan()
}
