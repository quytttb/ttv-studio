pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import LoggerKit.Theme

// Section title row — M3 headline-sized heading + optional actions slot.
RowLayout {
    id: root

    property string title: ""

    default property alias actions: actionsHost.data

    spacing: AppTheme.toolbarGap

    Label {
        text: root.title
        font: AppTypography.headlineSmall
        Layout.fillWidth: true
    }

    RowLayout {
        id: actionsHost
        spacing: AppTheme.toolbarGap
    }
}
