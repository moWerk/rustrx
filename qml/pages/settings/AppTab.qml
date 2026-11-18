/*
 * Copyright (C) 2026 - Timo Könnecke <github.com/moWerk>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
import QtQuick 2.6
import Rustrx 1.0
import Sailfish.Silica 1.0
import "../../components"

// Settings, App: notifications, how Rustrx looks, and the rest of how it
// behaves on this phone, its storage with it.
SilicaFlickable {
    id: tab

    property Item settings

    contentHeight: column.height

    // asked from Account's note on kept pictures: down to the storage, once
    // the tab has laid out
    function takeScroll() {
        if (!settings || settings.scrollToEnd !== "app") return
        settings.scrollToEnd = ""
        settle.restart()
    }
    Component.onCompleted: takeScroll()
    Connections { target: settings; onScrollToEndChanged: tab.takeScroll() }
    Timer {
        id: settle
        interval: Tunables.tabSettleMs
        onTriggered: {
            toEnd.to = Math.max(tab.originY, tab.contentHeight - tab.height + tab.originY)
            toEnd.restart()
        }
    }
    NumberAnimation { id: toEnd; target: tab; property: "contentY"; duration: Tunables.sectionScrollMs; easing.type: Easing.InOutQuad }

    Column {
        id: column
        width: parent.width
        // room between the buttons and the texts around them
        spacing: Theme.paddingMedium

        // the tab bar is drawn over a tab with a pulley menu; content starts below it
        Item { width: 1; height: tab.pullDownMenu ? settings.tabBarHeight : 0 }

        //% "Notifications"
        SectionHeader { text: qsTrId("rustrx-se-notifications") }

        // When Rustrx is closed, one of three: push through a UnifiedPush
        // distributor (Foghorn; nothing kept running), the background
        // stand-in (kept running and syncing), or nothing. Until chosen:
        // push where a distributor is installed, else the stand-in
        // (app.applyClosedMode). Push is only offered with a distributor.
        HoldComboBox {
            id: closedBox
            width: parent.width
            readonly property var modes: rustrx.push.available ? ["push", "background", "off"] : ["background", "off"]
            readonly property string mode: rustrx.push.available && rustrx.push.enabled ? "push"
                                           : rustrx.background ? "background" : "off"
            //% "When Rustrx is closed"
            label: qsTrId("rustrx-la-closed-mode")
            currentIndex: Math.max(0, modes.indexOf(mode))
            menu: ContextMenu {
                Repeater {
                    model: closedBox.modes
                    MenuItem {
                        text: modelData === "push"
                              //% "Push notifications"
                              ? qsTrId("rustrx-la-push")
                              : modelData === "background"
                              //% "Keep running in the background"
                              ? qsTrId("rustrx-la-background")
                              //% "No notifications"
                              : qsTrId("rustrx-la-closed-off")
                    }
                }
            }
            onCurrentIndexChanged: {
                var m = modes[currentIndex]
                if (m === mode) return
                app.closedMode = m
                app.applyClosedMode()
            }
            description: mode === "push"
                         ? (rustrx.push.active
                            //: %1 the push gateway's host
                            //% "Through Foghorn and %1: nothing of Rustrx keeps running. The gateway and the push server see when a message comes and for which room, never what it says."
                            ? qsTrId("rustrx-la-push-active").arg(rustrx.push.gatewayHost)
                            //: %1 what push is doing (registering, waiting for an address, an error)
                            //% "Setting up: %1"
                            : qsTrId("rustrx-la-push-setting-up").arg(rustrx.push.status || "…"))
                         : mode === "background"
                         //% "Rustrx starts with the phone and stays connected without a window, shown by a line in the events view: tap it to open Rustrx, or quit it there. While the phone sleeps it checks for messages every five minutes."
                         ? qsTrId("rustrx-la-background-hint") + (rustrx.push.available ? ""
                         //% " Push needs no running Rustrx: install a UnifiedPush distributor such as Foghorn (OpenRepos)."
                                                                                        : qsTrId("rustrx-la-push-none-short"))
                         //% "Notifications only while Rustrx is open."
                         : qsTrId("rustrx-la-closed-off-hint")
        }

        TextSwitch {
            //% "Reply in notifications"
            text: qsTrId("rustrx-la-notify-reply")
            //% "A Reply button that opens a text field in the notification."
            description: qsTrId("rustrx-la-notify-reply-hint")
            checked: service.notifyReply
            automaticCheck: false
            onClicked: service.notifyReply = !service.notifyReply
        }

        TextSwitch {
            //% "Emoji replies in notifications"
            text: qsTrId("rustrx-la-notify-reactions")
            //% "Three emoji as buttons, to react to the latest message."
            description: qsTrId("rustrx-la-notify-reactions-hint")
            checked: service.notifyReactions
            automaticCheck: false
            onClicked: service.notifyReactions = !service.notifyReactions
        }

        TextField {
            visible: service.notifyReactions
            width: parent.width
            // in line with the switches' texts above (Silica TextSwitch:
            // its toggle, then the label)
            textLeftMargin: Theme.horizontalPageMargin - Theme.paddingLarge + Theme.itemSizeExtraSmall
                            + (palette.colorScheme === Theme.DarkOnLight ? Theme.paddingMedium : 0)
            //% "Up to three emoji, with spaces between"
            label: qsTrId("rustrx-la-notify-emoji")
            placeholderText: label
            text: service.notifyEmoji
            inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
            EnterKey.iconSource: "image://theme/icon-m-enter-close"
            EnterKey.onClicked: focus = false
            onActiveFocusChanged: if (!activeFocus) service.notifyEmoji = text.trim()
        }

        // only with the background stand-in, the line's subject
        TextSwitch {
            visible: rustrx.background
            //% "Show the background line"
            text: qsTrId("rustrx-la-background-line")
            //% "The line in the events view while Rustrx runs in the background. Without it, Rustrx runs unseen: open it from its icon, and switch background mode off to end it."
            description: qsTrId("rustrx-la-background-line-hint")
            checked: service.showBackgroundLine
            automaticCheck: false
            onClicked: service.showBackgroundLine = !service.showBackgroundLine
        }

        //% "Appearance"
        SectionHeader { text: qsTrId("rustrx-se-appearance") }

        // the avatars' corners, from square to a circle, with one's own
        // picture beside it as it will look
        Item {
            width: parent.width
            height: Math.max(cornerSlider.height, cornerPreview.height)
            Slider {
                id: cornerSlider
                anchors { left: parent.left; right: cornerPreview.left; verticalCenter: parent.verticalCenter }
                //% "Avatar corners"
                label: qsTrId("rustrx-la-avatar-corners")
                minimumValue: 0
                maximumValue: 50
                stepSize: 5
                valueText: value >= maximumValue
                           //% "Round"
                           ? qsTrId("rustrx-la-avatar-round")
                           : value <= minimumValue
                             //% "Square"
                             ? qsTrId("rustrx-la-avatar-square")
                             : Math.round(value * 2) + " %"
                Component.onCompleted: value = app.avatarCorners
                onValueChanged: if (Math.round(value) !== app.avatarCorners) app.avatarCorners = Math.round(value)
            }
            OwnAvatar {
                id: cornerPreview
                anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin; verticalCenter: parent.verticalCenter }
                width: Theme.iconSizeLarge
                height: width
                path: settings.ownAvatar
                version: settings.ownAvatarVersion
                name: settings.displayName
            }
        }

        TextSwitch {
            //% "Highlight Favs channels with new messages"
            text: qsTrId("rustrx-la-highlight-favs")
            //% "A favourite with something unread glows calmly in the lists, its star with it."
            description: qsTrId("rustrx-la-highlight-favs-hint")
            checked: app.highlightFavs
            automaticCheck: false
            onClicked: app.highlightFavs = !app.highlightFavs
        }

        HoldComboBox {
            width: parent.width
            //% "Message preview"
            label: qsTrId("rustrx-la-preview-lines")
            //% "Lines of the last message under a room's name."
            description: qsTrId("rustrx-la-preview-lines-hint")
            currentIndex: Math.max(0, Math.min(2, app.previewLines - 1))
            menu: ContextMenu {
                //% "1 line"
                MenuItem { text: qsTrId("rustrx-me-lines-1") }
                //% "2 lines"
                MenuItem { text: qsTrId("rustrx-me-lines-2") }
                //% "3 lines"
                MenuItem { text: qsTrId("rustrx-me-lines-3") }
            }
            onCurrentIndexChanged: app.previewLines = currentIndex + 1
        }

        TextSwitch {
            //% "Separate People tab"
            text: qsTrId("rustrx-la-separate-lists")
            //% "Rooms and people as two tabs. When off, they share one list; the filter menu sorts and searches it."
            description: qsTrId("rustrx-la-separate-lists-hint")
            checked: app.separateLists
            automaticCheck: false
            onClicked: app.separateLists = !app.separateLists
        }

        TextSwitch {
            //% "Favourites tab"
            text: qsTrId("rustrx-la-separate-favs")
            //% "The favourites as a tab of their own. When off, “Favourites on top” in the filter menu keeps them at hand."
            description: qsTrId("rustrx-la-separate-favs-hint")
            checked: app.separateFavs
            automaticCheck: false
            onClicked: app.separateFavs = !app.separateFavs
        }

        TextSwitch {
            //% "Spaces tab"
            text: qsTrId("rustrx-la-separate-spaces")
            //% "The spaces you are in (groups of rooms) as a tab: their rooms, joining them, asking to join, and requests to join them."
            description: qsTrId("rustrx-la-separate-spaces-hint")
            checked: app.separateSpaces
            automaticCheck: false
            onClicked: app.separateSpaces = !app.separateSpaces
        }

        //% "General"
        SectionHeader { text: qsTrId("rustrx-se-general") }

        TextSwitch {
            //% "Reopen the last chat"
            text: qsTrId("rustrx-la-reopen-last")
            //% "Rustrx starts in the room or chat that was open when it was closed."
            description: qsTrId("rustrx-la-reopen-last-hint")
            checked: app.reopenLast
            automaticCheck: false
            onClicked: app.reopenLast = !app.reopenLast
        }

        TextSwitch {
            //% "Lock to portrait"
            text: qsTrId("rustrx-la-portrait-only")
            //% "Rustrx stays upright when the phone turns. Pictures and videos still turn with it, and so does everything while a slide-out keyboard is open."
            description: qsTrId("rustrx-la-portrait-only-hint2")
            checked: app.portraitOnly
            automaticCheck: false
            onClicked: app.portraitOnly = !app.portraitOnly
        }

        TextSwitch {
            //% "Send smaller pictures"
            text: qsTrId("rustrx-la-downscale")
            //% "Photos larger than 2048 pixels are scaled down before sending."
            description: qsTrId("rustrx-la-downscale-hint")
            checked: rustrx.downscaleImages
            automaticCheck: false
            onClicked: rustrx.downscaleImages = !rustrx.downscaleImages
        }

        // what Rustrx keeps on this phone
        DetailItem {
            //% "Messages and keys"
            label: qsTrId("rustrx-la-storage-store")
            value: settings.storage.store !== undefined ? Format.formatFileSize(settings.storage.store) : "…"
        }
        DetailItem {
            //% "Pictures and files"
            label: qsTrId("rustrx-la-storage-media")
            value: settings.storage.media_files !== undefined
                   ? Format.formatFileSize(settings.storage.media_files + (settings.storage.media_cache || 0)) : "…"
        }
        PageButton {
            //% "Clear pictures and files"
            text: qsTrId("rustrx-bt-clear-media")
            onClicked: {
                var core = rustrx
                //% "Clearing the cached pictures and files"
                settings.remorse.execute(qsTrId("rustrx-re-clear-media"), function () { core.command({ cmd: "clear_media" }) })
            }
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * x
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            //% "They are fetched again when a room shows them."
            text: qsTrId("rustrx-la-clear-media-hint")
        }

        Item { width: 1; height: Theme.paddingLarge }
    }

    VerticalScrollDecorator { }
}
