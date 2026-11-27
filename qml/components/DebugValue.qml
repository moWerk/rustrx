// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Nemo.Configuration 1.0
import "../names.js" as Names

// A debug switch for tests, off unless set by hand:
// dconf write /apps/harbour-rustrx/<name> <value>
// dconf is not private to Rustrx, so a switch that acts
// (opens, sends, records, closes) holds its value only while the
// dev-commands file is in the data folder (rustrx.devMode, as the dev
// hook); elsewhere it stays at its default. `acts: false` for one that
// only writes the log.
QtObject {
    id: debugValue
    property string name
    property var defaultValue
    property bool acts: true
    readonly property var value: !acts || rustrx.devMode ? conf.value : defaultValue

    property ConfigurationValue conf: ConfigurationValue {
        key: Names.dconfPath + "/" + debugValue.name
        defaultValue: debugValue.defaultValue
    }
}
