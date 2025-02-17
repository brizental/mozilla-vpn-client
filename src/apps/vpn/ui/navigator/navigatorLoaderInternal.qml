/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.5

import Mozilla.VPN 1.0

Loader {
    id: loader
    asynchronous: true

    // Let's use `onCompleted` to take the current value of
    // VPNNavigator.component without creating a property binding.
    Component.onCompleted: () => { loader.sourceComponent = VPNNavigator.component }
}
