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
import Sailfish.Silica.private 1.0
import Sailfish.Pickers 1.0

// Settings in four tabs, in the tab view Jolla uses for its own Settings:
// Account, Privacy, App, About (pages/settings/). Each tab gets this page as
// `settings`: what more than one place shows, and what the core answers,
// lives here.
Page {
    id: page

    property string displayName
    property string ownAvatar
    property real ownAvatarVersion
    property var storage: ({})
    // the Encryption page's results, kept here: the new recovery key is
    // shown only once and must outlive that page
    property string recoveryResult
    property string shownKey
    property string resetApprovalUrl

    readonly property real tabBarHeight: tabs.tabBarHeight
    // the log out and the clearing; the Encryption page has its own
    property alias remorse: remorsePopup

    Component.onCompleted: {
        rustrx.command({ cmd: "profile" })
        rustrx.command({ cmd: "storage_info" })
    }

    function tabTitle(kind) {
        //% "Account"
        if (kind === "account") return qsTrId("rustrx-he-settings-account")
        //% "Privacy"
        if (kind === "privacy") return qsTrId("rustrx-he-settings-privacy")
        //% "App"
        if (kind === "app") return qsTrId("rustrx-he-settings-app")
        //% "About"
        return qsTrId("rustrx-he-settings-about")
    }

    // toEnd: that tab scrolls to its end, where its storage is (a note on
    // Account leads there); the tab takes it once it is loaded
    property string scrollToEnd
    function showTab(kind, toEnd) {
        if (toEnd) scrollToEnd = kind
        for (var i = 0; i < tabModel.count; i++)
            if (tabModel.get(i).kind === kind) tabs.currentIndex = i
    }

    function openEncryption() {
        pageStack.push(Qt.resolvedUrl("settings/EncryptionPage.qml"), { settings: page })
    }

    function pickPicture() { pageStack.push(ownPicturePicker) }

    // with the recovery key on this phone, a short undo; without it the
    // dialog says what is lost first, and offers to set the key up
    function logOut() {
        if (rustrx.recoveryState === "enabled") {
            //% "Logging out"
            remorsePopup.execute(qsTrId("rustrx-re-logging-out"), function () {
                rustrx.command({ cmd: "logout" })
            })
            return
        }
        var d = pageStack.push(Qt.resolvedUrl("settings/LogoutDialog.qml"), { account: rustrx.userId })
        d.accepted.connect(function () { rustrx.command({ cmd: "logout" }) })
        d.rejected.connect(function () { if (d.wantsRecovery) encryptionLater.start() })
    }

    // opened from the main page's line about verification or the recovery
    // key: Settings opens on Account, where the risks and their fixes are;
    // a missing recovery key goes on to the Encryption page, its only fix
    property bool showEncryption
    // a tab to open on, by its kind (the dev hook)
    property string startTab
    onStatusChanged: {
        if (status !== PageStatus.Active) return
        if (startTab.length) { showTab(startTab); startTab = "" }
        if (showEncryption) {
            showEncryption = false
            if (rustrx.recoveryState === "incomplete") encryptionLater.start()
        }
    }
    Timer {
        id: encryptionLater
        interval: 0
        onTriggered: pageStack.busy ? restart() : page.openEncryption()
    }

    TabView {
        id: tabs
        anchors.fill: parent
        model: tabModel
        currentIndex: 0
        // no wrap from Account round to About: at the first tab a swipe
        // back leaves Settings, as on any page (the author's find)
        wrapMode: PagedView.NoWrap

        header: TabBar {
            model: tabModel
            delegate: TabButton {
                title: page.tabTitle(model.kind)
                // four tabs: a size smaller, as on the main page
                titleFontSize: tabModel.count >= Tunables.crowdedTabCount ? Math.round(Theme.fontSizeMedium * Tunables.crowdedTabScale) : Theme.fontSizeLarge
            }
        }

        delegate: TabItem {
            flickable: loader.item
            Loader {
                id: loader
                anchors.fill: parent
                // with `settings` set before the tab's bindings first run
                Component.onCompleted: setSource(Qt.resolvedUrl("settings/" + model.file), { settings: page })
            }
        }
    }

    // after the TabView: children go in the order they were made, and the
    // TabView must go before its model (MainPage.qml says why)
    ListModel {
        id: tabModel
        ListElement { kind: "account"; file: "AccountTab.qml" }
        ListElement { kind: "privacy"; file: "PrivacyTab.qml" }
        ListElement { kind: "app"; file: "AppTab.qml" }
        ListElement { kind: "about"; file: "AboutTab.qml" }
    }

    Component {
        id: ownPicturePicker
        ImagePickerPage {
            //% "Your picture"
            title: qsTrId("rustrx-he-own-picture")
            onSelectedContentPropertiesChanged: {
                if (!selectedContentProperties.filePath) return
                rustrx.setOwnAvatar(selectedContentProperties.filePath)
                //% "Setting your picture"
                Notices.show(qsTrId("rustrx-no-own-picture"), Notice.Short)
            }
        }
    }

    RemorsePopup { id: remorsePopup }

    Connections {
        target: rustrx
        // another account shown from the Account tab's pulley: its settings
        onShownAccountChanged: {
            displayName = ""
            ownAvatar = ""
            recoveryResult = ""
            shownKey = ""
            resetApprovalUrl = ""
            rustrx.command({ cmd: "profile" })
            rustrx.command({ cmd: "storage_info" })
        }
        onStorageInfo: page.storage = info
        onProfile: {
            displayName = profile.display_name || ""
            ownAvatar = profile.avatar_path || ""
            ownAvatarVersion = profile.avatar_v || 0
        }
        //% "Encrypted messages restored. Rooms fill in as keys arrive."
        onRecoveryDone: recoveryResult = qsTrId("rustrx-la-restored")
        onIdentityResetApproval: {
            resetApprovalUrl = url
            //% "Approve the reset on your server's page in the browser (within two minutes). If it did not open, use the buttons above."
            recoveryResult = qsTrId("rustrx-la-reset-approve")
        }
        onRecoveryKey: {
            resetApprovalUrl = ""
            //% "Backup is on. Write this recovery key down and keep it safe; it is shown only now. Tap it to copy."
            recoveryResult = qsTrId("rustrx-la-key-shown")
            shownKey = key
        }
        onErrorOccurred: {
            if (cmd === "recovery_recover" || cmd === "recovery_enable" || cmd === "recovery_reset" || cmd === "identity_reset")
                recoveryResult = message
        }
    }
}
