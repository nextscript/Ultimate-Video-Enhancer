import QtQuick
import QtQuick.Controls

// Auswahlliste für einen Filter (Land, Sprache, Genre). entries: [{name, code, count}], erste Zeile = alle
ComboBox {
    id: control
    property var theme
    property var entries: []
    property string allText: qsTr("Alle")
    property string value: ""                 // gewählter Code, leer = alle
    signal picked(string code)

    implicitHeight: 30
    font.pixelSize: 13
    textRole: "label"
    valueRole: "code"
    model: {
        var m = [{ label: allText, code: "" }]
        for (var i = 0; i < entries.length; ++i)
            m.push({ label: entries[i].name + "  (" + entries[i].count + ")", code: entries[i].code })
        return m
    }
    currentIndex: Math.max(0, indexOfValue(value))
    onActivated: function(i) { value = model[i].code; picked(value) }

    contentItem: Text {
        leftPadding: 10
        rightPadding: control.indicator.width + 6
        text: control.currentIndex > 0 ? control.model[control.currentIndex].label.replace(/\s+\(\d+\)$/, "") : control.displayText
        font: control.font
        color: theme ? theme.text : "#e6e8eb"
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: 6
        color: theme ? theme.panel2 : "#1b1f26"
        border.width: control.visualFocus ? 2 : 1
        border.color: control.visualFocus ? (theme ? theme.focus : "#4da3ff")
                    : control.value !== "" ? (theme ? theme.accent : "#ff8a1f") : (theme ? theme.line : "#262b34")
    }
    delegate: ItemDelegate {
        width: ListView.view ? ListView.view.width : control.width
        height: 28
        highlighted: control.highlightedIndex === index
        contentItem: Text {
            text: modelData.label
            color: theme ? theme.text : "#e6e8eb"
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle { color: highlighted ? (theme ? theme.panel2 : "#1b1f26") : "transparent" }
    }
    popup: Popup {
        y: control.height + 2
        width: Math.max(control.width, 220)
        height: Math.min(contentItem.implicitHeight + 2, 360)
        padding: 1
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: ScrollBar {}
        }
        background: Rectangle {
            radius: 6
            color: theme ? theme.panel : "#14171c"
            border.color: theme ? theme.line : "#262b34"
        }
    }
}
