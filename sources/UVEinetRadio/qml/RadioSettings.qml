import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Einstellungen: optionale Klick-Weitergabe an Radio Browser, Caches getrennt von Favoriten löschen
Item {
    id: root
    property var controller
    property var theme
    implicitWidth: 420
    implicitHeight: col.implicitHeight + 24
    RadioTexts { id: texts }

    readonly property color cText: theme ? theme.text : "#e6e8eb"
    readonly property color cMuted: theme ? theme.muted : "#8b93a1"

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 10

        Text { text: qsTr("Internetradio"); color: root.cText; font.pixelSize: 15; font.bold: true }
        Text {
            Layout.fillWidth: true
            text: qsTr("Senderkatalog: Radio Browser (frei, ohne Konto und API-Schlüssel). Wiedergabe, Lautstärke und Audioausgabe übernimmt der Player.")
            color: root.cMuted
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }

        CheckBox {
            id: clicks
            Layout.fillWidth: true
            checked: controller ? controller.sendClicks : false
            onToggled: if (controller) controller.sendClicks = checked
            text: qsTr("Senderklick an Radio Browser melden")
            contentItem: Text {
                leftPadding: clicks.indicator.width + 6
                text: clicks.text
                color: root.cText
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                wrapMode: Text.Wrap
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Aus: es werden keine Klicks gemeldet. An: beim Abspielen eines Senders aus der Suche wird dessen Beliebtheit im Katalog um einen Klick erhöht. Favoriten, Verlauf und eigene Adressen melden nie.")
            color: root.cMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: theme ? theme.line : "#262b34" }

        Text { text: qsTr("Zwischenspeicher"); color: root.cText; font.pixelSize: 13; font.bold: true }
        Text {
            Layout.fillWidth: true
            text: qsTr("Favoriten, eigene Sender und der Verlauf bleiben beim Leeren erhalten.")
            color: root.cMuted
            font.pixelSize: 11
            wrapMode: Text.Wrap
        }
        RowLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 220
                text: qsTr("Katalog-Cache: %1").arg(texts.bytes(controller ? controller.catalogCacheBytes : 0))
                color: root.cText
                font.pixelSize: 12
            }
            RButton { theme: root.theme; text: qsTr("Leeren"); onClicked: controller.clearCatalogCache() }
        }
        RowLayout {
            spacing: 8
            Text {
                Layout.preferredWidth: 220
                text: qsTr("Logo-Cache: %1").arg(texts.bytes(controller ? controller.logoCacheBytes : 0))
                color: root.cText
                font.pixelSize: 12
            }
            RButton { theme: root.theme; text: qsTr("Leeren"); onClicked: controller.clearLogoCache() }
        }
        Text {
            Layout.fillWidth: true
            text: "UVEinetRadio " + (controller ? controller.version : "")
            color: root.cMuted
            font.pixelSize: 10
        }
    }
}
