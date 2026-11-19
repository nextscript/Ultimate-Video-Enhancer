import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var controller
    property var theme
    implicitHeight: content.implicitHeight + 24

    readonly property bool de: !controller || controller.language.indexOf("de") === 0
    function t(deText, enText) { return root.de ? deText : enText }
    readonly property color textColor: theme ? theme.text : "white"
    readonly property color mutedColor: theme ? theme.muted : "#999"
    readonly property color accent: "#1db954"
    readonly property color cardColor: theme && theme.panel ? Qt.lighter(theme.panel, 1.08) : "#1f1f1f"
    readonly property color cardHover: theme && theme.panel ? Qt.lighter(theme.panel, 1.25) : "#2a2a2a"
    readonly property color cardBorder: theme && theme.line ? theme.line : "#333333"

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Label { text: "UVESpotify"; font.bold: true; color: root.textColor }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.mutedColor
            text: root.t("Öffne einen Spotify-Link (Titel, Album, Playlist, Künstler). Spotify-Ton ist kopiergeschützt, deshalb wird der passende Titel von YouTube gespielt – ohne Werbung, nichts wird gespeichert.",
                         "Open a Spotify link (track, album, playlist, artist). Spotify audio is copy-protected, so the matching track is played from YouTube – no ads, nothing is saved.")
        }

        RowLayout {
            Layout.fillWidth: true
            Label { text: root.t("Wiedergabe", "Playback"); color: root.textColor }
            ComboBox {
                Layout.fillWidth: true
                model: [root.t("Nur Ton (passende Länge)", "Audio only (matching length)"), root.t("Musikvideo", "Music video")]
                currentIndex: controller && controller.mode === "video" ? 1 : 0
                onActivated: (i) => controller.mode = i === 1 ? "video" : "audio"
            }
        }

        // ------------------------------------------------ Konto
        Rectangle { Layout.fillWidth: true; height: 1; color: root.mutedColor; opacity: 0.3 }
        Label { text: root.t("Spotify-Konto", "Spotify account"); font.bold: true; color: root.textColor }
        Label {
            visible: !!controller && !controller.loggedIn
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.mutedColor
            text: root.t("Melde dich an, um deine Playlists und Lieblingssongs hier zu sehen und abzuspielen. Die Anmeldung läuft im Browser direkt bei Spotify.",
                         "Sign in to see and play your playlists and liked songs here. Sign-in happens in the browser, directly at Spotify.")
        }

        Label {
            visible: !!controller && controller.loggedIn
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.textColor
            text: controller ? root.t("Angemeldet als %1", "Signed in as %1").arg(controller.userName || "…")
                               + (controller.product ? " (" + controller.product + ")" : "") : ""
        }

        RowLayout {
            Layout.fillWidth: true
            visible: !!controller
            Button {
                visible: !!controller && !controller.loggedIn
                enabled: !!controller && !controller.loginPending
                text: controller && controller.loginPending ? root.t("Warte auf Browser …", "Waiting for browser …") : root.t("Anmelden", "Sign in")
                onClicked: controller.login()
            }
            Button {
                visible: !!controller && controller.loggedIn
                text: root.t("Playlists neu laden", "Reload playlists")
                enabled: !!controller && !controller.playlistsLoading
                onClicked: controller.refreshPlaylists()
            }
            Button {
                visible: !!controller && controller.loggedIn
                text: root.t("Abmelden", "Sign out")
                onClicked: controller.logout()
            }
        }

        // ------------------------------------------------ Playlists
        Label {
            visible: !!controller && controller.loggedIn
            text: root.t("Meine Playlists", "My playlists")
            font.bold: true
            color: root.textColor
        }
        BusyIndicator {
            visible: !!controller && controller.playlistsLoading
            running: visible
            Layout.alignment: Qt.AlignHCenter
        }
        ListView {
            id: list
            visible: !!controller && controller.loggedIn
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight + topMargin + bottomMargin, 440)
            clip: true
            spacing: 6
            topMargin: 3
            bottomMargin: 3
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: list.contentHeight > list.height ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff }
            model: controller && controller.loggedIn
                   ? [{ name: root.t("Lieblingssongs", "Liked Songs"), owner: "", count: -1, image: "", liked: true,
                        url: "https://open.spotify.com/collection/tracks" }].concat(controller.playlists)
                   : []

            delegate: Rectangle {
                id: card
                required property var modelData
                readonly property bool hovered: mouse.containsMouse
                // 3 px Rand, damit die leichte Vergrößerung beim Darüberfahren nicht abgeschnitten wird
                x: 3
                width: ListView.view.width - 6 - (list.ScrollBar.vertical.visible ? 10 : 0)
                height: 62
                radius: 8
                color: hovered ? root.cardHover : root.cardColor
                border.width: 1
                border.color: hovered ? root.accent : root.cardBorder
                scale: mouse.pressed ? 0.98 : (hovered ? 1.01 : 1.0)
                Behavior on color { ColorAnimation { duration: 140 } }
                Behavior on border.color { ColorAnimation { duration: 140 } }
                Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 7
                    spacing: 10

                    // Cover (Lieblingssongs: Herz auf Verlauf wie bei Spotify)
                    Rectangle {
                        Layout.preferredWidth: 48
                        Layout.preferredHeight: 48
                        radius: 5
                        clip: true
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: card.modelData.liked ? "#4a1fd6" : "#2a2a2a" }
                            GradientStop { position: 1.0; color: card.modelData.liked ? "#8fd5c9" : "#3d3d3d" }
                        }
                        Label {
                            anchors.centerIn: parent
                            visible: cover.status !== Image.Ready
                            text: card.modelData.liked ? "♥" : "♪"
                            font.pixelSize: 22
                            color: "white"
                        }
                        Image {
                            id: cover
                            anchors.fill: parent
                            source: card.modelData.image || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize: Qt.size(96, 96)
                            visible: status === Image.Ready
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: card.modelData.name
                            elide: Text.ElideRight
                            font.bold: true
                            color: card.hovered ? root.accent : root.textColor
                            Behavior on color { ColorAnimation { duration: 140 } }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: card.modelData.liked
                                  ? root.t("Deine gespeicherten Songs", "Your saved songs")
                                  : root.t("%1 Titel", "%1 tracks").arg(card.modelData.count)
                                    + (card.modelData.owner ? " · " + card.modelData.owner : "")
                            elide: Text.ElideRight
                            font.pixelSize: 11
                            color: root.mutedColor
                        }
                    }

                    // Abspielknopf erscheint beim Darüberfahren
                    Rectangle {
                        Layout.preferredWidth: 32
                        Layout.preferredHeight: 32
                        radius: 16
                        color: root.accent
                        opacity: card.hovered ? 1 : 0
                        scale: card.hovered ? 1 : 0.7
                        Behavior on opacity { NumberAnimation { duration: 140 } }
                        Behavior on scale { NumberAnimation { duration: 140; easing.type: Easing.OutBack } }
                        Canvas {
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: 1
                            width: 12; height: 14
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "black"
                                ctx.beginPath()
                                ctx.moveTo(0, 0); ctx.lineTo(width, height / 2); ctx.lineTo(0, height)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: controller.play(card.modelData.url)
                }
            }
        }

        Label {
            visible: !!controller && controller.status !== ""
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: root.textColor
            text: controller ? controller.status : ""
        }
        Item { Layout.fillHeight: true }
    }
}
