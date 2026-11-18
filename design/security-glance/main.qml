// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import QtQuick.Window 2.2
import Sailfish.Silica 1.0

// Rustrx's Account glance on its own: a phone-sized frame with the tab,
// and controls beside it for the states and the look. Run:
//     qmlscene -I . main.qml        (Qt 5; Qt 6: qml -I . main.qml)
// --shot=out.png renders, saves and quits; --design=rail|halo|horizon|
// orbits|0.4.9|all picks the direction.
Window {
    id: win
    visible: true
    width: phone.width + panel.width + 3 * 16
    height: phone.height + 2 * 16
    color: "#2b2b2b"
    title: "Rustrx: Account glance mockup"

    MockAccount { id: account }

    // the directions: a file suffix (AccountGlance<suffix>.qml) and a name
    readonly property var designs: [["", "0.4.9 (now)"], ["Rail", "Rail"], ["Halo", "Halo"], ["Horizon", "Horizon"], ["Orbits", "Orbits"]]
    // the one shown, or -1 for all side by side
    property int design: 1

    // the Jolla Phone 2026 is 720 x 1600 px at pixel ratio 1.5: the same
    // layout at ratio 1.0 is 480 x 1067
    Row {
        id: phone
        x: 16; y: 16
        spacing: 16
        Repeater {
            model: win.design < 0 ? win.designs : [win.designs[win.design]]
            Rectangle {
                width: Math.round(480 * Theme.pixelRatio)
                height: Math.round(1067 * Theme.pixelRatio)
                color: Theme.backgroundColor
                clip: true
                // the ambience's picture shows faintly behind every page
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.backgroundColor }
                    GradientStop { position: 1; color: Qt.tint(Theme.backgroundColor, Theme.rgba(Theme.highlightColor, 0.12)) }
                }
                Loader {
                    anchors.fill: parent
                    source: "AccountGlance" + modelData[0] + ".qml"
                    onLoaded: item.account = account
                }
            }
        }
    }

    Column {
        id: panel
        x: phone.x + phone.width + 16; y: 16
        width: 220
        spacing: 8
        Text { color: "white"; text: "Direction"; font.bold: true }
        Repeater {
            model: win.designs
            Choice { text: (win.design === index ? "● " : "") + modelData[1]; onClicked: win.design = index }
        }
        Choice { text: (win.design < 0 ? "● " : "") + "All side by side"; onClicked: win.design = -1 }
        Text { color: "white"; text: "State"; font.bold: true }
        Repeater {
            model: [["good", "All in place"], ["gaps", "Backup off, 2 devices"], ["bad", "Most missing"], ["checking", "Checking"]]
            Choice { text: modelData[1]; onClicked: account.scenario(modelData[0]) }
        }
        Text { color: "white"; text: "Ambience"; font.bold: true }
        Choice { text: Theme.lightOnDark ? "Dark (tap: light)" : "Light (tap: dark)"; onClicked: Theme.lightOnDark = !Theme.lightOnDark }
        Repeater {
            model: [["#fe9a80", "Ruska (the author's)"], ["#87c7ff", "Blue"], ["#b5e86c", "Green-ish"], ["#c98aff", "Purple"]]
            Choice { text: modelData[1]; swatch: modelData[0]; onClicked: Theme.highlightColor = modelData[0] }
        }
        Text { color: "white"; text: "Size"; font.bold: true }
        Choice { text: "Pixel ratio " + Theme.pixelRatio + " (tap)"; onClicked: Theme.pixelRatio = Theme.pixelRatio === 1.0 ? 1.5 : 1.0 }
        Choice { text: "Font scale " + Theme.fontScale + " (tap)"; onClicked: Theme.fontScale = Theme.fontScale === 1.0 ? 1.25 : 1.0 }
        Text { color: "white"; text: "Notes"; font.bold: true }
        Choice { text: "Debug log " + (account.debugLog ? "on" : "off"); onClicked: account.debugLog = !account.debugLog }
        Choice { text: "Receipts " + (account.sendReceipts ? "on" : "off"); onClicked: account.sendReceipts = !account.sendReceipts }
    }

    // --shot: render once, save the phone frame, quit
    Timer {
        id: shooter
        interval: 600
        running: false
        onTriggered: {
            phone.grabToImage(function (result) {
                result.saveToFile(shotPath)
                Qt.quit()
            })
        }
    }
    property string shotPath: ""
    Component.onCompleted: {
        var args = Qt.application.arguments
        for (var i = 0; i < args.length; i++) {
            if (args[i].indexOf("--shot=") === 0) shotPath = args[i].slice(7)
            if (args[i].indexOf("--state=") === 0) account.scenario(args[i].slice(8))
            if (args[i] === "--light") Theme.lightOnDark = false
            if (args[i] === "--huge") Theme.fontScale = 1.25
            if (args[i].indexOf("--highlight=") === 0) Theme.highlightColor = args[i].slice(12)
            if (args[i].indexOf("--design=") === 0) {
                var want = args[i].slice(9).toLowerCase()
                design = -1
                for (var d = 0; d < designs.length; d++)
                    if (designs[d][1].toLowerCase().indexOf(want) === 0) design = d
            }
        }
        if (shotPath.length) shooter.start()
    }
}
