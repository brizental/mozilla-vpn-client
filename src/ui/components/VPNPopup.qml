/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

import QtQuick 2.5
import QtQuick.Controls 2.14
import QtGraphicalEffects 1.14
import "../themes/themes.js" as Theme

Popup {
    id: popup

    anchors.centerIn: parent
    closePolicy: Popup.CloseOnEscape
    enabled: true
    focus: true
    modal: true
    width: parent.width
    leftInset: Theme.windowMargin
    rightInset: Theme.windowMargin
    horizontalPadding: Theme.popupMargin + popup.leftInset
    verticalPadding: Theme.popupMargin

    background: Rectangle {
        id: popupBackground

        anchors.margins: 0
        color: Theme.bgColor
        radius: 8

        DropShadow {
            id: popupShadow

            anchors.fill: popupBackground
            cached: true
            color: "black"
            opacity: 0.2
            radius: 16
            samples: 33
            source: popupBackground
            spread: 0.1
            transparentBorder: true
            verticalOffset: 4
            z: -1
        }

        // Close button
        VPNIconButton {
            id: closeButton

            anchors {
                right: popupBackground.right
                top: popupBackground.top
//                rightMargin: Theme.popupMargin
//                topMargin: Theme.popupMargin
            }

            accessibleName: "close button"
            onClicked: {
                popup.close();
            }

            Image {
                id: closeImage

                anchors.centerIn: closeButton
                fillMode: Image.PreserveAspectFit
                source: "../resources/close-big-dark.svg"
                sourceSize.width: Theme.iconSize
            }
        }
    }

    enter: Transition {
        NumberAnimation {
            property: "opacity"
            duration: 120
            from: 0.0
            to: 1.0
            easing.type: Easing.InOutQuad
        }
    }

    exit: Transition {
        NumberAnimation {
            property: "opacity"
            duration: 120
            from: 1.0
            to: 0.0
            easing.type: Easing.InOutQuad
        }
    }

    onClosed: {
    }

    Overlay.modal: Rectangle {
        id: overlayBackground

        color: "#4D000000"

        Behavior on opacity {
            NumberAnimation {
                duration: 175
            }
        }
    }
}
