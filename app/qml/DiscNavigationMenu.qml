import QtQuick
import QtQuick.Controls

// Wiedergabe → Disc-Navigation (auch im Rechtsklickmenü des Bildes). Navigation nur für DVD/Blu-ray und nur,
// wenn die geladene libmpv "discnav" kennt; Titel (edition-list) und Kapitel (chapter-list) kommen von libmpv.
// Die ID darf nicht "menu" heißen (siehe VideoContextMenu.qml).
Menu {
    id: discMenu
    property var theme
    readonly property var disc: app.disc
    title: qsTr("Disc-Navigation")
    enabled: disc.active

    MenuItem { text: qsTr("Disc-Menü aufrufen"); enabled: discMenu.disc.navigationAvailable; onTriggered: discMenu.disc.navigate("menu") }
    MenuItem { text: qsTr("Titelmenü"); enabled: discMenu.disc.navigationAvailable && discMenu.disc.activeKind === "dvd"; onTriggered: discMenu.disc.navigate("title-menu") }
    MenuItem { text: qsTr("Popup-Menü"); enabled: discMenu.disc.navigationAvailable; onTriggered: discMenu.disc.navigate("popup") }
    MenuSeparator {}
    MenuItem { text: qsTr("Hoch"); enabled: discMenu.disc.menuActive; onTriggered: discMenu.disc.navigate("up") }
    MenuItem { text: qsTr("Runter"); enabled: discMenu.disc.menuActive; onTriggered: discMenu.disc.navigate("down") }
    MenuItem { text: qsTr("Links"); enabled: discMenu.disc.menuActive; onTriggered: discMenu.disc.navigate("left") }
    MenuItem { text: qsTr("Rechts"); enabled: discMenu.disc.menuActive; onTriggered: discMenu.disc.navigate("right") }
    MenuItem { text: qsTr("Auswählen"); enabled: discMenu.disc.menuActive; onTriggered: discMenu.disc.navigate("select") }
    MenuItem { text: qsTr("Zurück"); enabled: discMenu.disc.navigationAvailable; onTriggered: discMenu.disc.navigate("prev") }
    MenuSeparator {}
    MenuItem {
        text: qsTr("Vorheriges Kapitel")
        enabled: discMenu.disc.chapter > 0
        onTriggered: discMenu.disc.selectChapter(discMenu.disc.chapter - 1)
    }
    MenuItem {
        text: qsTr("Nächstes Kapitel")
        enabled: discMenu.disc.chapter >= 0 && discMenu.disc.chapter + 1 < discMenu.disc.chapters.length
        onTriggered: discMenu.disc.selectChapter(discMenu.disc.chapter + 1)
    }
    Menu {
        id: chapterMenu
        title: qsTr("Kapitel (%1)").arg(discMenu.disc.chapters.length)
        enabled: discMenu.disc.chapters.length > 0
        Instantiator {
            model: discMenu.disc.chapters
            delegate: MenuItem {
                required property var modelData
                text: (modelData.title ? modelData.title : qsTr("Kapitel %1").arg(modelData.index + 1)) + "   " + app.formatTime(modelData.time)
                checkable: true
                checked: discMenu.disc.chapter === modelData.index
                onTriggered: discMenu.disc.selectChapter(modelData.index)
            }
            onObjectAdded: (i, o) => chapterMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => chapterMenu.removeItem(o)
        }
    }
    Menu {
        id: titleMenu
        title: qsTr("Titel (%1)").arg(discMenu.disc.titles.length)
        enabled: discMenu.disc.titles.length > 0
        Instantiator {
            model: discMenu.disc.titles
            delegate: MenuItem {
                required property var modelData
                text: modelData.title ? modelData.title : qsTr("Titel %1").arg(modelData.id + 1)
                checkable: true
                checked: modelData.current === true
                onTriggered: discMenu.disc.selectTitle(modelData.id)
            }
            onObjectAdded: (i, o) => titleMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => titleMenu.removeItem(o)
        }
    }
}
