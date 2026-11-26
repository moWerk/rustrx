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
import QtSensors 5.2

// The proximity sensor during a call on the earpiece, loaded only then
// (harbour-rustrx.qml): made at start, it connected to sensorfw while the
// window loaded, for every start (0.8.2 audit, ST7). "Near" counts only
// after the sensor has once said "far" in this call: the C2's reports
// "near" at once with nothing over it, which kept the call page dark.
Item {
    property bool seenFar: false
    ProximitySensor {
        active: true
        alwaysOn: true
        onReadingChanged: {
            var near = reading !== null && reading.near
            if (reading !== null && !near && !seenFar) {
                seenFar = true
                console.log("call screen: proximity sensor said far")
            }
            rustrx.setCallNear(near && seenFar)
        }
    }
}
