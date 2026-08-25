pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

import LoggerKit.Theme
import LoggerKit.Components
import TtvStudio.Components
import TtvStudio.Core

// Provider endpoints, models and tool locations.
Item {
    id: root

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
                }
            }

            ElevatedPane {
                Layout.fillWidth: true
                implicitHeight: ttsColumn.implicitHeight + 32

                ColumnLayout {
                    id: ttsColumn

                    x: 16
                    y: 16
                    width: parent.width - 32
                    spacing: 10

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

        InlineBanner {
            Layout.fillWidth: true
            message: qsTr("Môi trường env (TTV_*) vẫn ưu tiên hơn giá trị lưu tại đây.")
            semantic: "info"
        }

        Item { Layout.fillHeight: true }
    }
}
