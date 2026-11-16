// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Nemo.Configuration 1.0
import "../names.js" as Names

// A debug switch for tests, off unless set by hand:
// dconf write /apps/harbour-rustrx/<name> <value>
ConfigurationValue {
    property string name
    key: Names.dconfPath + "/" + name
}
