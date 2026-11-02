import QtQuick

// Farbschema (dunkel = Standard, hell optional)
QtObject {
    property string mode: "dark"
    readonly property bool dark: mode !== "light"
    readonly property color bg: dark ? "#0b0d10" : "#f3f4f6"
    readonly property color panel: dark ? "#14171c" : "#ffffff"
    readonly property color panel2: dark ? "#1b1f26" : "#eceef1"
    readonly property color line: dark ? "#262b34" : "#d6d9de"
    readonly property color text: dark ? "#e6e8eb" : "#1d2026"
    readonly property color muted: dark ? "#8b93a1" : "#5d6573"
    readonly property color accent: "#ff8a1f"
    readonly property color accent2: "#ffb15c"
    readonly property color focus: "#4da3ff"
    readonly property color error: dark ? "#ff6b6b" : "#c62828"
    readonly property color overlay: dark ? "#cc000000" : "#cc000000"
}
