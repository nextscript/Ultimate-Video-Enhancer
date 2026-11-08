import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Shown as its own entry in Settings. implicitHeight lets the player scroll long pages.
ColumnLayout {
    property var theme
    property var plugin
    spacing: 8
    CheckBox {
        text: "Example option"
        checked: plugin ? plugin.settings().exampleOption === true : false
        onToggled: {
            var s = plugin.settings()
            s.exampleOption = checked
            plugin.saveSettings(s)
        }
    }
    Item { Layout.fillHeight: true }
}
